// Mod framework phase 1b: 3D model dump and replacement (Windows fork).
//
//   DC_DUMP_MODELS=1 (run_win.ps1 -DumpModels)   writes mods/_dump/models/<texture>__<N>v.obj for every model built
//   mods/<mod>/models/<texture>.obj|.glb          replaces every model whose first texture is <texture>
//   mods/<mod>/models/<texture>__<N>v.obj         replaces only the model that has N vertices (what the dump writes)
//   mods/<mod>/models/<same name>.json            optional: {"scale":1,"rotate_deg":[0,0,0],"offset":[0,0,0],"flip_v":false}
//
// The replacement keeps the first material of the model it replaces (so the PNG texture mods repaint it) and is drawn
// with one strip. OBJ files are read in the dump's own axes and units, so a dump edited in Blender and exported with the
// same axis settings round-trips exactly. A .glb (glTF binary: Unity, Unreal, Blender) is used as it is, with the
// sidecar .json to scale/rotate/offset it into the game's units.

#include "modmodel.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "draw3d.hpp"
#include "platform/mods.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

namespace {

struct ModelFile {
    fs::path path;
    int      verts = -1; // -1: any model with the texture
};

struct Model {
    bool                       ok = false;
    std::vector<gfx::Vertex3D> vertices;
    std::vector<uint32_t>      indices;
    // OBJ `usemtl strip<N>` groups, as the dump writes them: which original strip's material each run of indices uses.
    struct Group {
        int      strip;
        uint32_t first_index;
        uint32_t index_count;
    };
    std::vector<Group> groups; // empty: the whole mesh takes the original's first material
};

struct State {
    bool                                     inited = false;
    bool                                     dump = false;
    fs::path                                 root;
    std::map<std::string, std::vector<ModelFile>> by_name; // later mods last
    std::map<std::string, Model>             cache;
    std::set<std::string>                    dumped;
    int                                      replaced = 0;
    std::map<const void *, std::string>      skin_name; // DC_DEBUG_SKIN=1: which replaced models the game re-skins
    std::set<const void *>                   skin_seen;
    std::map<const void *, std::vector<const void *>> skin_blocks; // an original model's vertex array -> blocks of its replaced visuals
};

State g;

std::string Fold(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string Utf8(const fs::path &p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string StripExtension(std::string name) {
    size_t dot = name.find('.');
    if (dot != std::string::npos) {
        name.erase(dot);
    }
    return name;
}

bool ModEnabled(const fs::path &dir) {
    std::ifstream in(dir / "mod.json");
    if (!in) {
        return true;
    }
    auto json = nlohmann::json::parse(in, nullptr, false, true);
    return json.is_discarded() || !json.is_object() || json.value("enabled", true);
}

// "name__123v" -> name, 123; otherwise the stem and -1.
void SplitStem(const std::string &stem, std::string &name, int &verts) {
    name = stem;
    verts = -1;
    size_t at = stem.rfind("__");
    if (at == std::string::npos || stem.back() != 'v') {
        return;
    }
    std::string digits = stem.substr(at + 2, stem.size() - at - 3);
    if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return;
    }
    name = stem.substr(0, at);
    verts = std::atoi(digits.c_str());
}

void Init() {
    g.inited = true;
    const char *dump = std::getenv("DC_DUMP_MODELS");
    g.dump = dump != nullptr && dump[0] != '0';
    g.root = PathsSaveRoot() / "mods";
    std::error_code error;
    if (!fs::is_directory(g.root, error)) {
        return;
    }
    std::vector<fs::path> mods = ModsLoadOrder(g.root);
    for (const fs::path &mod : mods) {
        if (!ModEnabled(mod)) {
            continue;
        }
        fs::path models = mod / "models";
        if (!fs::is_directory(models, error)) {
            continue;
        }
        int count = 0;
        for (const auto &entry : fs::directory_iterator(models, error)) {
            std::string ext = Fold(Utf8(entry.path().extension()));
            if (ext != ".obj" && ext != ".glb") {
                continue;
            }
            ModelFile file;
            file.path = entry.path();
            std::string name;
            SplitStem(Fold(Utf8(entry.path().stem())), name, file.verts);
            g.by_name[name].push_back(file);
            count++;
        }
        std::fprintf(stderr, "mods: %s: %d model override(s)\n", Utf8(mod.filename()).c_str(), count);
    }
}

// ---- Transform sidecar -----------------------------------------------------------------------------------------------

struct Transform {
    float scale = 1.0f;
    float rotate[3] = {0, 0, 0}; // degrees
    float offset[3] = {0, 0, 0};
    bool  flip_v = false;
};

Transform ReadTransform(const fs::path &model) {
    Transform t;
    fs::path sidecar = model;
    sidecar.replace_extension(".json");
    std::ifstream in(sidecar);
    if (!in) {
        return t;
    }
    auto json = nlohmann::json::parse(in, nullptr, false, true);
    if (json.is_discarded() || !json.is_object()) {
        std::fprintf(stderr, "mods: %s is not valid JSON; ignored\n", Utf8(sidecar).c_str());
        return t;
    }
    t.scale = json.value("scale", 1.0f);
    t.flip_v = json.value("flip_v", false);
    for (const char *key : {"rotate_deg", "offset"}) {
        if (json.contains(key) && json[key].is_array() && json[key].size() == 3) {
            float *target = std::strcmp(key, "offset") == 0 ? t.offset : t.rotate;
            for (int i = 0; i < 3; i++) {
                target[i] = json[key][i].get<float>();
            }
        }
    }
    return t;
}

void Rotate(float v[3], const float deg[3]) {
    const float k = 3.14159265358979f / 180.0f;
    float       x = v[0], y = v[1], z = v[2];
    float       c = std::cos(deg[0] * k), s = std::sin(deg[0] * k);
    float       y1 = y * c - z * s, z1 = y * s + z * c;
    y = y1;
    z = z1;
    c = std::cos(deg[1] * k);
    s = std::sin(deg[1] * k);
    float x2 = x * c + z * s, z2 = -x * s + z * c;
    x = x2;
    z = z2;
    c = std::cos(deg[2] * k);
    s = std::sin(deg[2] * k);
    float x3 = x * c - y * s, y3 = x * s + y * c;
    v[0] = x3;
    v[1] = y3;
    v[2] = z;
}

void Normalize(float v[3]) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-8f) {
        for (int i = 0; i < 3; i++) {
            v[i] /= len;
        }
    } else {
        v[0] = 0;
        v[1] = 1;
        v[2] = 0;
    }
}

void ApplyTransform(Model &m, const Transform &t) {
    for (gfx::Vertex3D &v : m.vertices) {
        Rotate(v.position, t.rotate);
        Rotate(v.normal, t.rotate);
        for (int i = 0; i < 3; i++) {
            v.position[i] = v.position[i] * t.scale + t.offset[i];
        }
        if (t.flip_v) {
            v.uv[1] = 1.0f - v.uv[1];
        }
    }
}

void ComputeNormals(Model &m) {
    for (gfx::Vertex3D &v : m.vertices) {
        v.normal[0] = v.normal[1] = v.normal[2] = 0;
    }
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        gfx::Vertex3D &a = m.vertices[m.indices[i]];
        gfx::Vertex3D &b = m.vertices[m.indices[i + 1]];
        gfx::Vertex3D &c = m.vertices[m.indices[i + 2]];
        float e1[3], e2[3], n[3];
        for (int k = 0; k < 3; k++) {
            e1[k] = b.position[k] - a.position[k];
            e2[k] = c.position[k] - a.position[k];
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for (int k = 0; k < 3; k++) {
            a.normal[k] += n[k];
            b.normal[k] += n[k];
            c.normal[k] += n[k];
        }
    }
    for (gfx::Vertex3D &v : m.vertices) {
        Normalize(v.normal);
    }
}

// ---- OBJ ---------------------------------------------------------------------------------------------------------------

Model LoadObj(const fs::path &file) {
    Model         m;
    std::ifstream in(file);
    if (!in) {
        return m;
    }
    std::vector<std::array<float, 3>> positions, normals;
    std::vector<std::array<float, 2>> uvs;
    std::map<std::tuple<int, int, int>, uint32_t> seen;
    bool        any_normal = false;
    int         group = -1;
    bool        any_group = false;
    std::map<int, std::vector<uint32_t>> grouped; // strip number -> indices (-1: before any usemtl)
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string        tag;
        s >> tag;
        if (tag == "usemtl") {
            std::string name;
            s >> name;
            if (name.rfind("strip", 0) == 0 && name.size() > 5 &&
                std::all_of(name.begin() + 5, name.end(), [](unsigned char c) { return std::isdigit(c); })) {
                group = std::atoi(name.c_str() + 5);
                any_group = true;
            }
        } else if (tag == "v") {
            std::array<float, 3> p{};
            s >> p[0] >> p[1] >> p[2];
            positions.push_back(p);
        } else if (tag == "vn") {
            std::array<float, 3> n{};
            s >> n[0] >> n[1] >> n[2];
            normals.push_back(n);
        } else if (tag == "vt") {
            std::array<float, 2> t{};
            s >> t[0] >> t[1];
            uvs.push_back(t);
        } else if (tag == "f") {
            std::vector<uint32_t> face;
            std::string           token;
            while (s >> token) {
                int  idx[3] = {0, 0, 0};
                int  part = 0;
                std::string number;
                for (size_t i = 0; i <= token.size(); i++) {
                    if (i == token.size() || token[i] == '/') {
                        if (!number.empty() && part < 3) {
                            idx[part] = std::atoi(number.c_str());
                        }
                        number.clear();
                        part++;
                    } else {
                        number += token[i];
                    }
                }
                auto fix = [](int i, size_t n) { return i > 0 ? i - 1 : (i < 0 ? static_cast<int>(n) + i : -1); };
                int  pi = fix(idx[0], positions.size());
                int  ti = fix(idx[1], uvs.size());
                int  ni = fix(idx[2], normals.size());
                if (pi < 0 || pi >= static_cast<int>(positions.size())) {
                    continue;
                }
                auto key = std::make_tuple(pi, ti, ni);
                auto it = seen.find(key);
                if (it == seen.end()) {
                    gfx::Vertex3D v = {};
                    std::memcpy(v.position, positions[pi].data(), sizeof(v.position));
                    if (ti >= 0 && ti < static_cast<int>(uvs.size())) {
                        v.uv[0] = uvs[ti][0];
                        v.uv[1] = 1.0f - uvs[ti][1]; // OBJ v points up, the game's down
                    }
                    if (ni >= 0 && ni < static_cast<int>(normals.size())) {
                        std::memcpy(v.normal, normals[ni].data(), sizeof(v.normal));
                        any_normal = true;
                    }
                    std::memset(v.color, 0x80, sizeof(v.color));
                    it = seen.emplace(key, static_cast<uint32_t>(m.vertices.size())).first;
                    m.vertices.push_back(v);
                }
                face.push_back(it->second);
            }
            for (size_t i = 1; i + 1 < face.size(); i++) { // fan
                auto &target = grouped[group];
                target.insert(target.end(), {face[0], face[i], face[i + 1]});
            }
        }
    }
    if (!any_group) {
        m.indices = grouped[-1];
    } else {
        std::map<int, std::vector<uint32_t>> merged; // faces before the first usemtl count as strip 0
        for (auto &[key, list] : grouped) {
            auto &dst = merged[key < 0 ? 0 : key];
            dst.insert(dst.end(), list.begin(), list.end());
        }
        for (auto &[key, list] : merged) {
            m.groups.push_back({key, static_cast<uint32_t>(m.indices.size()), static_cast<uint32_t>(list.size())});
            m.indices.insert(m.indices.end(), list.begin(), list.end());
        }
    }
    if (m.vertices.empty() || m.indices.empty()) {
        return m;
    }
    if (!any_normal) {
        ComputeNormals(m);
    }
    m.ok = true;
    return m;
}

// ---- GLB ---------------------------------------------------------------------------------------------------------------

bool ReadAccessor(const nlohmann::json &doc, const std::vector<uint8_t> &bin, int index, int want_components, std::vector<float> &out) {
    if (!doc.contains("accessors") || index < 0 || index >= static_cast<int>(doc["accessors"].size())) {
        return false;
    }
    const auto &acc = doc["accessors"][index];
    if (!acc.contains("bufferView")) {
        return false;
    }
    const auto &view = doc["bufferViews"][acc["bufferView"].get<int>()];
    size_t      offset = static_cast<size_t>(view.value("byteOffset", 0)) + static_cast<size_t>(acc.value("byteOffset", 0));
    size_t      count = acc["count"].get<size_t>();
    int         type = acc["componentType"].get<int>();
    bool        normalized = acc.value("normalized", false);
    int         size = type == 5126 ? 4 : (type == 5123 ? 2 : (type == 5121 ? 1 : 0));
    if (size == 0) {
        return false;
    }
    size_t stride = static_cast<size_t>(view.value("byteStride", 0));
    if (stride == 0) {
        stride = static_cast<size_t>(size) * want_components;
    }
    out.resize(count * want_components);
    for (size_t i = 0; i < count; i++) {
        for (int c = 0; c < want_components; c++) {
            size_t at = offset + i * stride + static_cast<size_t>(c) * size;
            if (at + size > bin.size()) {
                return false;
            }
            float value;
            if (type == 5126) {
                std::memcpy(&value, &bin[at], 4);
            } else if (type == 5123) {
                uint16_t v;
                std::memcpy(&v, &bin[at], 2);
                value = normalized ? v / 65535.0f : static_cast<float>(v);
            } else {
                value = normalized ? bin[at] / 255.0f : static_cast<float>(bin[at]);
            }
            out[i * want_components + c] = value;
        }
    }
    return true;
}

bool ReadIndexAccessor(const nlohmann::json &doc, const std::vector<uint8_t> &bin, int index, std::vector<uint32_t> &out) {
    const auto &acc = doc["accessors"][index];
    const auto &view = doc["bufferViews"][acc["bufferView"].get<int>()];
    size_t      offset = static_cast<size_t>(view.value("byteOffset", 0)) + static_cast<size_t>(acc.value("byteOffset", 0));
    size_t      count = acc["count"].get<size_t>();
    int         type = acc["componentType"].get<int>();
    int         size = type == 5125 ? 4 : (type == 5123 ? 2 : (type == 5121 ? 1 : 0));
    if (size == 0 || offset + count * size > bin.size()) {
        return false;
    }
    out.resize(count);
    for (size_t i = 0; i < count; i++) {
        uint32_t v = 0;
        std::memcpy(&v, &bin[offset + i * size], static_cast<size_t>(size));
        out[i] = v;
    }
    return true;
}

Model LoadGlb(const fs::path &file) {
    Model         m;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return m;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 20 || std::memcmp(data.data(), "glTF", 4) != 0) {
        std::fprintf(stderr, "mods: %s is not a .glb file (export glTF Binary)\n", Utf8(file).c_str());
        return m;
    }
    std::string          json_text;
    std::vector<uint8_t> bin;
    size_t               at = 12;
    while (at + 8 <= data.size()) {
        uint32_t length, type;
        std::memcpy(&length, &data[at], 4);
        std::memcpy(&type, &data[at + 4], 4);
        at += 8;
        if (at + length > data.size()) {
            break;
        }
        if (type == 0x4E4F534A) {
            json_text.assign(reinterpret_cast<const char *>(&data[at]), length);
        } else if (type == 0x004E4942 && bin.empty()) {
            bin.assign(data.begin() + at, data.begin() + at + length);
        }
        at += (length + 3u) & ~3u;
    }
    auto doc = nlohmann::json::parse(json_text, nullptr, false);
    if (doc.is_discarded() || !doc.contains("meshes") || doc["meshes"].empty()) {
        std::fprintf(stderr, "mods: %s has no mesh\n", Utf8(file).c_str());
        return m;
    }
    for (const auto &prim : doc["meshes"][0]["primitives"]) {
        if (prim.value("mode", 4) != 4 || !prim.contains("attributes") || !prim["attributes"].contains("POSITION")) {
            continue;
        }
        std::vector<float> pos, nor, uv;
        if (!ReadAccessor(doc, bin, prim["attributes"]["POSITION"].get<int>(), 3, pos)) {
            continue;
        }
        bool has_n = prim["attributes"].contains("NORMAL") &&
                     ReadAccessor(doc, bin, prim["attributes"]["NORMAL"].get<int>(), 3, nor);
        bool has_uv = prim["attributes"].contains("TEXCOORD_0") &&
                      ReadAccessor(doc, bin, prim["attributes"]["TEXCOORD_0"].get<int>(), 2, uv);
        uint32_t base = static_cast<uint32_t>(m.vertices.size());
        for (size_t i = 0; i < pos.size() / 3; i++) {
            gfx::Vertex3D v = {};
            std::memcpy(v.position, &pos[i * 3], sizeof(v.position));
            if (has_n) {
                std::memcpy(v.normal, &nor[i * 3], sizeof(v.normal));
            }
            if (has_uv) {
                std::memcpy(v.uv, &uv[i * 2], sizeof(v.uv));
            }
            std::memset(v.color, 0x80, sizeof(v.color));
            m.vertices.push_back(v);
        }
        std::vector<uint32_t> idx;
        if (prim.contains("indices")) {
            if (!ReadIndexAccessor(doc, bin, prim["indices"].get<int>(), idx)) {
                continue;
            }
        } else {
            for (uint32_t i = 0; i < pos.size() / 3; i++) {
                idx.push_back(i);
            }
        }
        for (uint32_t i : idx) {
            m.indices.push_back(base + i);
        }
        if (!has_n) {
            // Normals are computed over the whole model below when any primitive lacks them.
        }
    }
    if (m.vertices.empty() || m.indices.empty()) {
        return m;
    }
    bool all_zero = true;
    for (const gfx::Vertex3D &v : m.vertices) {
        if (v.normal[0] != 0 || v.normal[1] != 0 || v.normal[2] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        ComputeNormals(m);
    }
    m.ok = true;
    return m;
}

const Model &Load(const ModelFile &file) {
    std::string key = Utf8(file.path);
    auto        it = g.cache.find(key);
    if (it != g.cache.end()) {
        return it->second;
    }
    Model m = Fold(Utf8(file.path.extension())) == ".glb" ? LoadGlb(file.path) : LoadObj(file.path);
    if (m.ok) {
        ApplyTransform(m, ReadTransform(file.path));
    } else {
        std::fprintf(stderr, "mods: cannot read model %s\n", key.c_str());
    }
    return g.cache.emplace(key, std::move(m)).first->second;
}

void Dump(const Draw3DVisual &visual, const std::string &name) {
    if (visual.indices.empty() || visual.vertices.empty()) {
        return;
    }
    std::string stem = (name.empty() ? std::string("untextured") : name) + "__" + std::to_string(visual.vertices.size()) + "v";
    if (!g.dumped.insert(stem).second) {
        return;
    }
    fs::path    dir = g.root / "_dump" / "models";
    std::error_code error;
    fs::create_directories(dir, error);
    std::ofstream out(dir / (stem + ".obj"));
    out << "# dumped by Dark Cloud (Windows fork): texture " << name << ", " << visual.vertices.size() << " vertices\n";
    char buffer[160];
    for (const gfx::Vertex3D &v : visual.vertices) {
        std::snprintf(buffer, sizeof(buffer), "v %.6f %.6f %.6f\n", v.position[0], v.position[1], v.position[2]);
        out << buffer;
    }
    for (const gfx::Vertex3D &v : visual.vertices) {
        std::snprintf(buffer, sizeof(buffer), "vt %.6f %.6f\n", v.uv[0], 1.0f - v.uv[1]);
        out << buffer;
    }
    for (const gfx::Vertex3D &v : visual.vertices) {
        std::snprintf(buffer, sizeof(buffer), "vn %.6f %.6f %.6f\n", v.normal[0], v.normal[1], v.normal[2]);
        out << buffer;
    }
    // One group per original strip (`usemtl strip<N>`): a replacement keeps strip N's material (texture, colours) for the faces
    // under that group, so a character with several materials (body, boots, hair) stays correctly painted. Keep the lines.
    size_t strips = std::max<size_t>(visual.strips.size(), 1);
    for (size_t k = 0; k < strips; k++) {
        size_t first = visual.strips.empty() ? 0 : visual.strips[k].first_index;
        size_t count = visual.strips.empty() ? visual.indices.size() : visual.strips[k].index_count;
        if (count == 0 || first >= visual.indices.size()) {
            continue;
        }
        out << "usemtl strip" << k << "\n";
        for (size_t i = first; i + 2 < first + count && i + 2 < visual.indices.size(); i += 3) {
            uint32_t a = visual.indices[i] + 1, b = visual.indices[i + 1] + 1, c = visual.indices[i + 2] + 1;
            std::snprintf(buffer, sizeof(buffer), "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
            out << buffer;
        }
    }
}

} // namespace

namespace {

constexpr int kSkinNeighbours = 4;

// For every replacement vertex, the kSkinNeighbours nearest vertices of the original model (by bind pose) and weights that
// sum to 1 (inverse distance). The original is skinned by the game each tick; the replacement just follows its neighbours.
void BuildSkin(Draw3DVisual &visual, const float (*original)[4], int count) {
    visual.skin_bind.clear();
    visual.skin_base.clear();
    visual.skin_src.clear();
    visual.skin_weight.clear();
    if (original == nullptr || count <= 0) {
        return;
    }
    visual.skin_bind.resize(static_cast<size_t>(count) * 3);
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < 3; k++) {
            visual.skin_bind[static_cast<size_t>(i) * 3 + k] = original[i][k];
        }
    }
    const size_t n = visual.vertices.size();
    visual.skin_base.resize(n * 3);
    visual.skin_src.assign(n * kSkinNeighbours, 0);
    visual.skin_weight.assign(n * kSkinNeighbours, 0.0f);
    for (size_t j = 0; j < n; j++) {
        const float *p = visual.vertices[j].position;
        std::copy(p, p + 3, &visual.skin_base[j * 3]);
        float best_d[kSkinNeighbours];
        int   best_i[kSkinNeighbours];
        for (int k = 0; k < kSkinNeighbours; k++) {
            best_d[k] = 1e30f;
            best_i[k] = 0;
        }
        for (int i = 0; i < count; i++) {
            float dx = p[0] - original[i][0], dy = p[1] - original[i][1], dz = p[2] - original[i][2];
            float d = dx * dx + dy * dy + dz * dz;
            if (d >= best_d[kSkinNeighbours - 1]) {
                continue;
            }
            int at = kSkinNeighbours - 1;
            while (at > 0 && best_d[at - 1] > d) {
                best_d[at] = best_d[at - 1];
                best_i[at] = best_i[at - 1];
                at--;
            }
            best_d[at] = d;
            best_i[at] = i;
        }
        float sum = 0.0f;
        for (int k = 0; k < kSkinNeighbours; k++) {
            float d = std::sqrt(best_d[k]) + 0.01f; // cubed: a vertex that sits on an original one follows it exactly
            float w = 1.0f / (d * d * d);
            visual.skin_src[j * kSkinNeighbours + k] = best_i[k];
            visual.skin_weight[j * kSkinNeighbours + k] = w;
            sum += w;
        }
        for (int k = 0; k < kSkinNeighbours; k++) {
            visual.skin_weight[j * kSkinNeighbours + k] /= sum;
        }
    }
    visual.skin_by_orig.assign(static_cast<size_t>(count), {});
    for (size_t j = 0; j < n; j++) {
        visual.skin_by_orig[static_cast<size_t>(visual.skin_src[j * kSkinNeighbours])].push_back(static_cast<int>(j)); // nearest first
    }
}

} // namespace

namespace {

void Apply4(float out[4], const float m[4][4], const float v[4]) {
    float r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = m[0][i] * v[0];
        r[i] = r[i] + m[1][i] * v[1];
        r[i] = r[i] + m[2][i] * v[2];
        r[i] = r[i] + m[3][i] * v[3];
    }
    for (int i = 0; i < 4; i++) {
        out[i] = r[i];
    }
}

// the game's SkinVertex (port/src/gameutil_motion.cpp) on one replacement vertex
void SkinPoint(float *out, float *deformed, const float *source, const float base[4][4], const float bone[4][4], const float inverse[4][4],
               float weight) {
    float local[4];
    for (int i = 0; i < 3; i++) {
        local[i] = source[i] - base[3][i];
    }
    local[3] = 1.0f;
    float accumulated[4];
    Apply4(accumulated, base, local);
    for (int i = 0; i < 3; i++) {
        local[i] = accumulated[i] - base[3][i];
    }
    Apply4(accumulated, bone, local);
    float moved[3];
    for (int i = 0; i < 3; i++) {
        moved[i] = accumulated[i] - source[i];
    }
    for (int i = 0; i < 3; i++) {
        deformed[i] = deformed[i] + moved[i] * weight;
    }
    float result[4];
    Apply4(result, inverse, deformed);
    for (int i = 0; i < 4; i++) {
        out[i] = result[i];
    }
}

} // namespace

void ModsSkinBegin(const float (*mdt)[4]) {
    static int debug_calls = 0;
    if (std::getenv("DC_DEBUG_SKIN") != nullptr && debug_calls < 6 && !g.skin_blocks.empty()) {
        debug_calls++;
        std::fprintf(stderr, "skin: MotionProc2 begins a pose for %p; %zu replaced model(s) registered, match %d\n", static_cast<const void *>(mdt),
                     g.skin_blocks.size(), g.skin_blocks.count(mdt) ? 1 : 0);
    }
    auto it = g.skin_blocks.find(mdt);
    if (it == g.skin_blocks.end()) {
        return;
    }
    for (const void *block : it->second) {
        Draw3DVisual *v = Draw3DFindVisual(block);
        if (v == nullptr || !v->replaced || v->skin_by_orig.empty()) {
            continue;
        }
        const size_t n = v->vertices.size();
        v->skin_def.assign(n * 4, 0.0f);
        v->skin_out.assign(n * 4, 0.0f);
        for (size_t j = 0; j < n; j++) {
            for (int k = 0; k < 3; k++) {
                v->skin_def[j * 4 + k] = v->skin_base[j * 3 + k];
                v->skin_out[j * 4 + k] = v->skin_base[j * 3 + k];
            }
            v->skin_def[j * 4 + 3] = 1.0f;
            v->skin_out[j * 4 + 3] = 1.0f;
        }
        v->skin_fresh = true;
    }
}

void ModsSkinVertex(const float (*mdt)[4], unsigned vertex, const float base[4][4], const float bone[4][4], const float inverse[4][4],
                    float weight) {
    auto it = g.skin_blocks.find(mdt);
    if (it == g.skin_blocks.end()) {
        return;
    }
    for (const void *block : it->second) {
        Draw3DVisual *v = Draw3DFindVisual(block);
        if (v == nullptr || !v->replaced || !v->skin_fresh || vertex >= v->skin_by_orig.size()) {
            continue;
        }
        for (int j : v->skin_by_orig[vertex]) {
            SkinPoint(&v->skin_out[static_cast<size_t>(j) * 4], &v->skin_def[static_cast<size_t>(j) * 4],
                      &v->skin_base[static_cast<size_t>(j) * 3], base, bone, inverse, weight);
        }
    }
}

void ModsModelSkin(Draw3DVisual &visual, const float (*vertex)[4], const void *block) {
    if (visual.skin_src.empty() || vertex == nullptr) {
        return;
    }
    if (block != nullptr) { // the live vertex array the game poses (not the one the visual was built from): MotionProc2 finds us by it
        auto &blocks = g.skin_blocks[vertex];
        if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
            blocks.push_back(block);
        }
    }
    if (visual.skin_fresh) { // exact: posed by the game's own bone matrices in MotionProc2
        visual.skin_fresh = false;
        if (std::getenv("DC_DEBUG_SKIN") != nullptr) {
            static int shown = 0;
            size_t     posed = 0, covered = 0;
            for (size_t j = 0; j < visual.vertices.size(); j++) {
                if (visual.skin_out[j * 4] != visual.skin_base[j * 3] || visual.skin_out[j * 4 + 1] != visual.skin_base[j * 3 + 1] ||
                    visual.skin_out[j * 4 + 2] != visual.skin_base[j * 3 + 2]) {
                    posed++;
                }
                covered += 0;
            }
            if (shown++ < 4) {
                std::fprintf(stderr, "skin: exact pose: %zu of %zu replacement vertices moved from bind\n", posed, visual.vertices.size());
            }
        }
        bool changed = false;
        for (size_t j = 0; j < visual.vertices.size(); j++) {
            float *out = visual.vertices[j].position;
            for (int a = 0; a < 3; a++) {
                if (out[a] != visual.skin_out[j * 4 + a]) {
                    out[a] = visual.skin_out[j * 4 + a];
                    changed = true;
                }
            }
        }
        if (changed && visual.mesh != gfx::kNullMesh) {
            gfx::UpdateMeshVertices(visual.mesh, 0, visual.vertices);
        }
        return;
    }
    if (std::getenv("DC_DEBUG_SKIN") != nullptr && g.skin_seen.insert(&visual).second) {
        std::fprintf(stderr, "skin: %s re-skinned by the game (%zu vertices)\n",
                     g.skin_name.count(&visual) ? g.skin_name[&visual].c_str() : "?", visual.vertices.size());
    }
    const size_t n = visual.vertices.size();
    bool         changed = false;
    for (size_t j = 0; j < n; j++) {
        float moved[3] = {0, 0, 0};
        for (int k = 0; k < kSkinNeighbours; k++) {
            int          s = visual.skin_src[j * kSkinNeighbours + k];
            float        w = visual.skin_weight[j * kSkinNeighbours + k];
            const float *bind = &visual.skin_bind[static_cast<size_t>(s) * 3];
            for (int a = 0; a < 3; a++) {
                moved[a] += w * (vertex[s][a] - bind[a]);
            }
        }
        float *out = visual.vertices[j].position;
        for (int a = 0; a < 3; a++) {
            float v = visual.skin_base[j * 3 + a] + moved[a];
            if (out[a] != v) {
                out[a] = v;
                changed = true;
            }
        }
    }
    if (changed && visual.mesh != gfx::kNullMesh) {
        gfx::UpdateMeshVertices(visual.mesh, 0, visual.vertices);
    }
}

void ModsModelHook(Draw3DVisual &visual, const char *name_c, const float (*original)[4], int original_count, const void *block) {
    if (!g.inited) {
        Init();
    }
    std::string name = Fold(StripExtension(name_c != nullptr ? name_c : ""));
    if (g.dump) {
        Dump(visual, name);
    }
    auto it = g.by_name.find(name);
    if (it == g.by_name.end() || visual.indices.empty()) {
        return;
    }
    const int verts = static_cast<int>(visual.vertices.size());
    const ModelFile *chosen = nullptr;
    for (auto f = it->second.rbegin(); f != it->second.rend(); ++f) { // last mod wins; an exact vertex count beats "any"
        if (f->verts == verts) {
            chosen = &*f;
            break;
        }
        if (f->verts < 0 && chosen == nullptr) {
            chosen = &*f;
        }
    }
    if (chosen == nullptr) {
        return;
    }
    const Model &model = Load(*chosen);
    if (!model.ok) {
        return;
    }
    const std::vector<Draw3DStrip> old_strips = visual.strips;
    auto material_of = [&](int k) { // the original strip's material, else its first
        if (old_strips.empty()) {
            return Draw3DStrip{};
        }
        return old_strips[k >= 0 && k < static_cast<int>(old_strips.size()) ? k : 0];
    };
    visual.vertices = model.vertices;
    visual.indices = model.indices;
    visual.strips.clear();
    if (model.groups.empty()) {
        Draw3DStrip strip = material_of(0);
        strip.first_index = 0;
        strip.index_count = static_cast<uint32_t>(model.indices.size());
        visual.strips.push_back(strip);
    } else {
        for (const Model::Group &gr : model.groups) {
            Draw3DStrip strip = material_of(gr.strip);
            strip.first_index = gr.first_index;
            strip.index_count = gr.index_count;
            visual.strips.push_back(strip);
        }
    }
    visual.replaced = true;
    BuildSkin(visual, original, original_count);
    g.skin_name[&visual] = name;
    if (original != nullptr && block != nullptr) {
        auto &blocks = g.skin_blocks[original];
        if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
            blocks.push_back(block);
        }
    }
    if (g.replaced++ < 50) {
        std::fprintf(stderr, "mods: replaced model %s (%d -> %zu vertices)\n", name.c_str(), verts, model.vertices.size());
    }
}
