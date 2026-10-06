// Mod framework, phase 1a: PNG texture overrides and a texture dump so modders can find what to replace.
//
// Layout (next to config.json, in the save folder):
//   mods/<mod>/textures/<name>.png                      replaces every texture the game names <name>
//   mods/<mod>/textures/<game file>/<name>.png          replaces <name> only when it came from <game file>
//   mods/<mod>/mod.json                                 optional: {"enabled": false} switches the mod off
//   mods/_dump/...                                      written when DC_DUMP_TEXTURES=1 (see run_win.ps1 -DumpTextures)
// Mods load in alphabetical order; a later mod wins. Folders starting with "_" are skipped.
// A replacement is scaled to the size of the texture it replaces, so the game's UV coordinates keep working.

#include "mods.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "input.hpp"
#include "overlay.hpp"
#include "paths.hpp"

namespace fs = std::filesystem;

namespace {

struct State {
    bool                               inited = false;
    bool                               dump = false;
    fs::path                           root;
    std::map<std::string, std::string> by_stem;   // folded texture name -> png path (utf-8)
    std::map<std::string, std::string> by_scoped; // folded "<game file>/<name>" -> png path
    std::set<std::string>              dumped;
    std::set<std::string>              dumped_names;
    std::string                        last_file;
    int                                replaced = 0;
};

State g;

std::string Fold(std::string s) {
    for (char &c : s) {
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string Utf8(const fs::path &p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Everything up to the first colon is a device name (cdrom0:), as the data reader treats it.
std::string StripDevice(const std::string &path) {
    size_t colon = path.find(':');
    return colon == std::string::npos ? path : path.substr(colon + 1);
}

std::string TrimSlashes(std::string s) {
    while (!s.empty() && s.front() == '/') {
        s.erase(s.begin());
    }
    return s;
}

bool ModEnabled(const fs::path &dir) {
    std::ifstream in(dir / "mod.json");
    if (!in) {
        return true;
    }
    auto json = nlohmann::json::parse(in, nullptr, false, true);
    if (json.is_discarded() || !json.is_object()) {
        std::fprintf(stderr, "mods: %s has a mod.json that is not valid JSON; loading it anyway\n",
                     Utf8(dir.filename()).c_str());
        return true;
    }
    return json.value("enabled", true);
}

void ScanMod(const fs::path &mod) {
    fs::path textures = mod / "textures";
    std::error_code error;
    if (!fs::is_directory(textures, error)) {
        return;
    }
    int count = 0;
    for (fs::recursive_directory_iterator it(textures, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error)) {
        std::error_code entry_error;
        if (!it->is_regular_file(entry_error)) {
            continue;
        }
        const fs::path &file = it->path();
        std::string     ext = Fold(Utf8(file.extension()));
        if (ext != ".png") {
            continue;
        }
        std::string relative = Fold(Utf8(file.lexically_relative(textures).replace_extension()));
        std::string stem = Fold(Utf8(file.stem()));
        std::string path = Utf8(file);
        if (relative == stem) {
            g.by_stem[stem] = path;
        } else {
            g.by_scoped[relative] = path;
            // A file kept inside a folder still works for any texture of that name when it
            // does not match a game file: the folder is an organiser, not a requirement.
            g.by_stem.emplace(stem, path);
        }
        count++;
    }
    std::fprintf(stderr, "mods: %s: %d texture override(s)\n", Utf8(mod.filename()).c_str(), count);
}

void Init() {
    g.inited = true;
    const char *dump = std::getenv("DC_DUMP_TEXTURES");
    g.dump = dump != nullptr && dump[0] != '0';
    g.root = PathsSaveRoot() / "mods";
    std::error_code error;
    if (!fs::is_directory(g.root, error)) {
        if (g.dump) {
            fs::create_directories(g.root, error);
        }
        return;
    }
    std::vector<fs::path> mods = ModsLoadOrder(g.root);
    for (const fs::path &mod : mods) {
        if (!ModEnabled(mod)) {
            std::fprintf(stderr, "mods: %s is disabled\n", Utf8(mod.filename()).c_str());
            continue;
        }
        ScanMod(mod);
    }
}

SDL_Surface *Scaled(SDL_Surface *source, int w, int h) {
    if (source->w == w && source->h == h) {
        return SDL_DuplicateSurface(source);
    }
    return SDL_ScaleSurface(source, w, h, SDL_SCALEMODE_LINEAR);
}

void Dump(const char *name, int bpp, int block, unsigned width, unsigned height, bool indexed, const uint32_t *palette,
          const std::vector<std::vector<uint8_t>> &levels) {
    std::string source = TrimSlashes(StripDevice(g.last_file));
    std::string key = Fold(source + "/" + name);
    if (!g.dumped.insert(key).second) {
        return;
    }
    if (levels.empty() || width == 0 || height == 0) {
        return;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    const std::vector<uint8_t> &base = levels[0];
    if (indexed) {
        for (size_t i = 0; i < static_cast<size_t>(width) * height && i < base.size(); i++) {
            uint32_t color = palette[base[i]];
            std::memcpy(&rgba[i * 4], &color, 4);
        }
    } else if (base.size() >= rgba.size()) {
        std::memcpy(rgba.data(), base.data(), rgba.size());
    } else {
        return;
    }
    fs::path dir = g.root / "_dump" / (source.empty() ? fs::path("unknown") : fs::path(source));
    std::error_code error;
    fs::create_directories(dir, error);
    fs::path file = dir / (std::string(name) + ".png");
    SDL_Surface *surface = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32,
                                                 rgba.data(), static_cast<int>(width) * 4);
    if (surface != nullptr) {
        SDL_SavePNG(surface, Utf8(file).c_str());
        SDL_DestroySurface(surface);
    }
    // One flat copy per texture name: a mod file named after the texture replaces it wherever the game loads it.
    if (g.dumped_names.insert(Fold(name)).second) {
        fs::path flat = g.root / "_dump" / "by_name";
        fs::create_directories(flat, error);
        SDL_Surface *copy = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32,
                                                  rgba.data(), static_cast<int>(width) * 4);
        if (copy != nullptr) {
            SDL_SavePNG(copy, Utf8(flat / (std::string(name) + ".png")).c_str());
            SDL_DestroySurface(copy);
        }
    }
    std::ofstream index(g.root / "_dump" / "textures.csv", std::ios::app);
    index << source << ',' << name << ',' << width << 'x' << height << ',' << bpp << ',' << block << '\n';
}


// ---- Multiplayer colours -----------------------------------------------------------------------------------------------------
// Tunic colour indices: 0 natural (orange), 1..15 presets, 16 custom (the player's own textures). A ghost's renamed copy of the poncho
// textures ("<letter><digit>1d04" / "...05") carries its colour in the first letter; the player's own are "c01d04" / "c01d05".
struct TunicPreset { float hue, sat, val; };
const TunicPreset kTunic[16] = {
    {0, 1, 1},          // 0 natural (unused)
    {215, 1, 1},        // 1 blue
    {125, 1, 0.95f},    // 2 green
    {2, 1, 1},          // 3 red
    {275, 1, 0.95f},    // 4 purple
    {175, 1, 0.95f},    // 5 teal
    {325, 0.75f, 1.15f},// 6 pink
    {52, 1, 1.1f},      // 7 yellow
    {85, 1, 1},         // 8 lime
    {190, 1, 1},        // 9 cyan
    {300, 1, 1},        // 10 magenta
    {25, 0.55f, 0.55f}, // 11 brown
    {40, 0.06f, 1.35f}, // 12 white
    {30, 0.25f, 0.3f},  // 13 black
    {46, 1, 1.15f},     // 14 gold
    {235, 1, 0.55f},    // 15 navy
};
const char kTunicLetters[] = "hgijklmnopqrstuvw"; // first letter of a ghost's renamed texture, by colour index 0..16

int g_local_tunic = -1; // -1 = not read from the launcher's environment yet
int g_launch_tunic = 0; // what the launcher chose

struct CustomTunic {
    std::vector<uint8_t> front, back; // 256x256 RGBA
};
std::map<char, CustomTunic> g_custom; // key: the ghost's digit, or 'L' for the player's own

// Pixels that are clearly orange/red/yellow (the poncho) are moved to the preset's hue; shading is kept.
void ShiftTunic(uint8_t *px, const TunicPreset &t) {
    float r = px[0] / 255.0f, gr = px[1] / 255.0f, b = px[2] / 255.0f;
    float mx = std::max({r, gr, b}), mn = std::min({r, gr, b});
    float d = mx - mn;
    float s = mx > 0.0f ? d / mx : 0.0f;
    if (s < 0.3f || mx < 0.2f || d <= 0.0f) {
        return;
    }
    float h;
    if (mx == r) {
        h = 60.0f * std::fmod((gr - b) / d, 6.0f);
    } else if (mx == gr) {
        h = 60.0f * ((b - r) / d + 2.0f);
    } else {
        h = 60.0f * ((r - gr) / d + 4.0f);
    }
    if (h < 0.0f) {
        h += 360.0f;
    }
    if (h > 55.0f && h < 345.0f) {
        return; // not poncho-coloured
    }
    float rel = h <= 55.0f ? h - 30.0f : h - 390.0f;
    float nh = std::fmod(t.hue + rel * 0.4f + 720.0f, 360.0f);
    float v = std::clamp(mx * t.val, 0.0f, 1.0f);
    float sat = std::clamp(s * t.sat, 0.0f, 1.0f);
    float c = v * sat;
    float x = c * (1.0f - std::fabs(std::fmod(nh / 60.0f, 2.0f) - 1.0f));
    float m0 = v - c;
    float nr, ng, nb;
    int   sector = static_cast<int>(nh / 60.0f);
    switch (sector) {
    case 0: nr = c; ng = x; nb = 0; break;
    case 1: nr = x; ng = c; nb = 0; break;
    case 2: nr = 0; ng = c; nb = x; break;
    case 3: nr = 0; ng = x; nb = c; break;
    case 4: nr = x; ng = 0; nb = c; break;
    default: nr = c; ng = 0; nb = x; break;
    }
    px[0] = static_cast<uint8_t>(std::clamp((nr + m0) * 255.0f, 0.0f, 255.0f));
    px[1] = static_cast<uint8_t>(std::clamp((ng + m0) * 255.0f, 0.0f, 255.0f));
    px[2] = static_cast<uint8_t>(std::clamp((nb + m0) * 255.0f, 0.0f, 255.0f));
}

// PNG bytes -> 256x256 RGBA (scaled), or empty.
std::vector<uint8_t> DecodeTunicPng(SDL_Surface *loaded) {
    std::vector<uint8_t> out;
    if (loaded == nullptr) {
        return out;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr) {
        return out;
    }
    SDL_Surface *scaled = Scaled(rgba, 256, 256);
    SDL_DestroySurface(rgba);
    if (scaled == nullptr) {
        return out;
    }
    out.resize(256 * 256 * 4);
    for (int y = 0; y < 256; y++) {
        std::memcpy(&out[static_cast<size_t>(y) * 1024], static_cast<const uint8_t *>(scaled->pixels) + static_cast<size_t>(y) * scaled->pitch, 1024);
    }
    SDL_DestroySurface(scaled);
    return out;
}

void LoadLocalTunic() {
    g_local_tunic = 0;
    if (const char *e = std::getenv("DC_LAUNCH_TUNIC"); e != nullptr && e[0] != 0) {
        g_local_tunic = std::clamp(std::atoi(e), 0, 16);
    }
    if (g_local_tunic == 16) {
        CustomTunic c;
        if (const char *f = std::getenv("DC_LAUNCH_TUNIC_FRONT"); f != nullptr && f[0] != 0) {
            c.front = DecodeTunicPng(SDL_LoadPNG(f));
        }
        if (const char *k = std::getenv("DC_LAUNCH_TUNIC_BACK"); k != nullptr && k[0] != 0) {
            c.back = DecodeTunicPng(SDL_LoadPNG(k));
        }
        if (c.front.empty() && c.back.empty()) {
            g_local_tunic = 0; // the pictures could not be read: keep the natural colour
        } else {
            g_custom['L'] = std::move(c);
        }
    }
    g_launch_tunic = g_local_tunic;
}

// Is `name` a poncho texture that needs a colour? Gives the colour index, whether it is the back, and the key into g_custom.
bool TunicOf(const char *name, int &color, bool &back, char &key) {
    std::string n = Fold(name);
    if (n.size() != 6 || (n.compare(2, 4, "1d04") != 0 && n.compare(2, 4, "1d05") != 0)) {
        return false;
    }
    back = n.compare(2, 4, "1d05") == 0;
    if (n[0] == 'c' && n[1] == '0') {
        if (g_local_tunic < 0) {
            LoadLocalTunic();
        }
        color = g_local_tunic;
        key = 'L';
        return color > 0;
    }
    const char *at = std::strchr(kTunicLetters, n[0]);
    if (at == nullptr || n[0] == 'h') {
        return false;
    }
    color = static_cast<int>(at - kTunicLetters);
    key = n[1];
    return true;
}

bool RecolorTunic(int color, unsigned width, unsigned height, bool &indexed, bool &four_bit, const uint32_t *palette,
                  std::vector<std::vector<uint8_t>> &levels) {
    if (color < 1 || color > 15 || levels.empty() || width == 0 || height == 0 || (indexed && palette == nullptr)) {
        return false;
    }
    std::vector<std::vector<uint8_t>> out;
    for (size_t level = 0; level < levels.size(); level++) {
        size_t w = std::max<size_t>(1, width >> level), h = std::max<size_t>(1, height >> level);
        std::vector<uint8_t> rgba(w * h * 4, 0);
        const std::vector<uint8_t> &src = levels[level];
        if (indexed) {
            for (size_t i = 0; i < w * h && i < src.size(); i++) {
                uint32_t c = palette[src[i]];
                std::memcpy(&rgba[i * 4], &c, 4);
            }
        } else {
            std::memcpy(rgba.data(), src.data(), std::min(rgba.size(), src.size()));
        }
        for (size_t i = 0; i < w * h; i++) {
            ShiftTunic(&rgba[i * 4], kTunic[color]);
        }
        out.push_back(std::move(rgba));
    }
    levels = std::move(out);
    indexed = false;
    four_bit = false;
    return true;
}

} // namespace

std::vector<fs::path> ModsLoadOrder(const fs::path &root) {
    std::error_code       error;
    std::vector<fs::path> mods;
    for (const auto &entry : fs::directory_iterator(root, error)) {
        std::string name = Utf8(entry.path().filename());
        if (entry.is_directory(error) && !name.empty() && name[0] != '_' && name[0] != '.') {
            mods.push_back(entry.path());
        }
    }
    std::vector<std::string> order;
    {
        std::ifstream in(root / "load_order.json");
        auto          json = in ? nlohmann::json::parse(in, nullptr, false, true) : nlohmann::json();
        if (json.is_object() && json.contains("order") && json["order"].is_array()) {
            for (const auto &name : json["order"]) {
                if (name.is_string()) {
                    order.push_back(Fold(name.get<std::string>()));
                }
            }
        }
    }
    // Unlisted mods first (alphabetical), listed mods after, in the listed order: the last one wins a conflict.
    auto rank = [&](const fs::path &p) {
        std::string name = Fold(Utf8(p.filename()));
        auto        at = std::find(order.begin(), order.end(), name);
        return at == order.end() ? -1 : static_cast<int>(at - order.begin());
    };
    std::sort(mods.begin(), mods.end(), [&](const fs::path &a, const fs::path &b) {
        int ra = rank(a), rb = rank(b);
        if (ra != rb) {
            return ra < rb;
        }
        return Fold(Utf8(a.filename())) < Fold(Utf8(b.filename()));
    });
    return mods;
}

namespace {

struct FileOverrides {
    bool                             built = false;
    std::map<std::string, fs::path>  files; // folded game path -> the mod's file
    std::map<std::string, std::string> mod; // and which mod it came from
    std::set<std::string>            warned;
};
FileOverrides fo;

} // namespace

const fs::path *ModsFileOverride(const char *key, const fs::path *original) {
    if (!fo.built) {
        fo.built = true;
        fs::path        root = PathsSaveRoot() / "mods";
        std::error_code error;
        if (fs::is_directory(root, error)) {
            for (const fs::path &mod : ModsLoadOrder(root)) {
                if (!ModEnabled(mod)) {
                    continue;
                }
                fs::path dir = mod / "files";
                if (!fs::is_directory(dir, error)) {
                    continue;
                }
                int count = 0;
                for (fs::recursive_directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
                    std::error_code e2;
                    if (!it->is_regular_file(e2)) {
                        continue;
                    }
                    std::string rel = Fold(Utf8(it->path().lexically_relative(dir)));
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                    fo.files[rel] = it->path();
                    fo.mod[rel] = Utf8(mod.filename());
                    count++;
                }
                if (count > 0) {
                    std::fprintf(stderr, "mods: %s: %d game file override(s)\n", Utf8(mod.filename()).c_str(), count);
                }
            }
        }
    }
    if (fo.files.empty() || key == nullptr) {
        return nullptr;
    }
    auto it = fo.files.find(key);
    if (it == fo.files.end()) {
        return nullptr;
    }
    if (fo.warned.insert(key).second) {
        std::error_code e1, e2;
        auto            mine = fs::file_size(it->second, e1);
        auto            theirs = original != nullptr ? fs::file_size(*original, e2) : 0;
        std::fprintf(stderr, "mods: %s overrides game file %s (%llu bytes)\n", fo.mod[key].c_str(), key,
                     static_cast<unsigned long long>(e1 ? 0 : mine));
        if (original != nullptr && !e1 && !e2 && mine > theirs) {
            std::fprintf(stderr, "mods: warning: %s is larger than the game's file (%llu > %llu bytes); the game may not have room for it\n",
                         key, static_cast<unsigned long long>(mine), static_cast<unsigned long long>(theirs));
        }
    }
    return &it->second;
}


void ModsSetLocalTunicBlue(bool blue) { // a guest with no colour of their own is shown in blue; any chosen colour stays
    if (g_local_tunic < 0) {
        LoadLocalTunic();
    }
    g_local_tunic = (blue && g_launch_tunic == 0) ? 1 : g_launch_tunic;
}

int ModsLocalTunic() {
    if (g_local_tunic < 0) {
        LoadLocalTunic();
    }
    return g_local_tunic;
}

bool ModsSetTunicCustom(char key, const void *front, size_t front_len, const void *back, size_t back_len) {
    CustomTunic c;
    auto        decode = [](const void *bytes, size_t len) {
        if (bytes == nullptr || len == 0 || len > (1u << 20)) {
            return std::vector<uint8_t>();
        }
        SDL_IOStream *io = SDL_IOFromConstMem(bytes, len);
        return io != nullptr ? DecodeTunicPng(SDL_LoadPNG_IO(io, true)) : std::vector<uint8_t>();
    };
    c.front = decode(front, front_len);
    c.back = decode(back, back_len);
    if (c.front.empty() && c.back.empty()) {
        g_custom.erase(key);
        return false;
    }
    g_custom[key] = std::move(c);
    return true;
}

void ModsNoteFile(const char *path) {
    if (path != nullptr) {
        g.last_file = path; // dumps and scoped overrides both name the game file a texture came from
    }
}

namespace {
bool ReplaceLevels(SDL_Surface *rgba, unsigned width, unsigned height, bool &indexed, bool &has_alpha, bool &four_bit,
                   std::vector<std::vector<uint8_t>> &levels) {
    int level_count = std::max<int>(1, static_cast<int>(levels.size()));
    std::vector<std::vector<uint8_t>> out;
    bool                              translucent = false;
    for (int level = 0; level < level_count; level++) {
        int          w = std::max<int>(1, static_cast<int>(width) >> level);
        int          h = std::max<int>(1, static_cast<int>(height) >> level);
        SDL_Surface *scaled = Scaled(rgba, w, h);
        if (scaled == nullptr) {
            return false;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; y++) {
            const uint8_t *row = static_cast<const uint8_t *>(scaled->pixels) + static_cast<size_t>(y) * scaled->pitch;
            std::memcpy(&bytes[static_cast<size_t>(y) * w * 4], row, static_cast<size_t>(w) * 4);
        }
        if (level == 0) {
            for (size_t i = 3; i < bytes.size(); i += 4) {
                if (bytes[i] != 255) {
                    translucent = true;
                    break;
                }
            }
        }
        SDL_DestroySurface(scaled);
        out.push_back(std::move(bytes));
    }
    levels = std::move(out);
    indexed = false;
    has_alpha = translucent || has_alpha;
    four_bit = false;
    return true;
}

} // namespace


namespace {
// ---- Button prompts as keys ----------------------------------------------------------------------------------------------------
// The game's messages show pad buttons as glyphs of its "gaiji" sheet (a circle, a triangle, L1 ...). When the player plays with the keyboard
// and mouse those cells are redrawn as key caps showing what is bound to the action now (F, Tab, a mouse with the right button lit ...).
struct CapCell {
    const char *action;
    int         x, y, w, h;
};
const CapCell kCapCells[] = {
    {"circle", 0, 22, 22, 22},   {"triangle", 22, 22, 22, 22}, {"square", 44, 22, 22, 22}, {"cross", 66, 22, 22, 22},
    {"l2", 0, 132, 32, 22},      {"l1", 32, 132, 32, 22},      {"r1", 64, 132, 32, 22},    {"r2", 96, 132, 32, 22},
};

std::string CapLabel(const std::string &action, int &mouse_button) {
    mouse_button = 0;
    std::string text = InputBindingText(action);
    std::string first = text.substr(0, text.find(','));
    while (!first.empty() && first.front() == ' ') {
        first.erase(first.begin());
    }
    if (first.rfind("Mouse", 0) == 0 && first.size() == 6) {
        mouse_button = first[5] - '0';
        return {};
    }
    std::string up;
    for (char c : first) {
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    static const std::map<std::string, std::string> kShort = {
        {"LEFT CTRL", "CTL"}, {"RIGHT CTRL", "CTL"}, {"LEFT SHIFT", "SHF"}, {"RIGHT SHIFT", "SHF"}, {"LEFT ALT", "ALT"}, {"RIGHT ALT", "ALT"},
        {"SPACE", "SPC"},     {"RETURN", "ENT"},     {"ESCAPE", "ESC"},     {"BACKSPACE", "BSP"},   {"TAB", "TAB"},       {"DELETE", "DEL"},
        {"UP", "UP"},         {"DOWN", "DN"},        {"LEFT", "LT"},        {"RIGHT", "RT"},        {"CAPS LOCK", "CAP"}, {"PAGE UP", "PGU"},
        {"PAGE DOWN", "PGD"}, {"HOME", "HOM"},       {"END", "END"},        {"INSERT", "INS"}};
    if (auto it = kShort.find(up); it != kShort.end()) {
        return it->second;
    }
    return up.size() > 3 ? up.substr(0, 3) : up;
}

int NearestIndex(const uint32_t *palette, int limit, int r, int g, int b, bool opaque_only) {
    int best = 0, best_d = 1 << 30;
    for (int i = 0; i < limit; i++) {
        uint8_t px[4];
        std::memcpy(px, &palette[i], 4);
        if (opaque_only && px[3] < 0x40) {
            continue;
        }
        int d = (px[0] - r) * (px[0] - r) + (px[1] - g) * (px[1] - g) + (px[2] - b) * (px[2] - b);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void ComposeKeyCaps(unsigned width, unsigned height, bool four_bit, const uint32_t *palette, std::vector<std::vector<uint8_t>> &levels) {
    if (levels.empty() || levels[0].size() < static_cast<size_t>(width) * height || width < 128 || height < 160) {
        return;
    }
    const int limit = four_bit ? 16 : 256;
    int       clear = 0;
    for (int i = 0; i < limit; i++) { // the transparent index
        uint8_t px[4];
        std::memcpy(px, &palette[i], 4);
        if (px[3] == 0) {
            clear = i;
            break;
        }
    }
    const uint8_t light = static_cast<uint8_t>(NearestIndex(palette, limit, 222, 222, 226, true));
    const uint8_t mid = static_cast<uint8_t>(NearestIndex(palette, limit, 150, 150, 160, true));
    const uint8_t dark = static_cast<uint8_t>(NearestIndex(palette, limit, 30, 30, 38, true));
    const uint8_t hot = static_cast<uint8_t>(NearestIndex(palette, limit, 240, 150, 30, true));
    std::vector<uint8_t> &px = levels[0];
    auto put = [&](int x, int y, uint8_t v) {
        if (x >= 0 && y >= 0 && x < static_cast<int>(width) && y < static_cast<int>(height)) {
            px[static_cast<size_t>(y) * width + x] = v;
        }
    };
    for (const CapCell &cell : kCapCells) {
        int         mouse = 0;
        std::string label = CapLabel(cell.action, mouse);
        if (label.empty() && mouse == 0) {
            continue; // nothing bound: leave the pad glyph
        }
        for (int y = 0; y < cell.h; y++) {
            for (int x = 0; x < cell.w; x++) {
                put(cell.x + x, cell.y + y, static_cast<uint8_t>(clear));
            }
        }
        const int w = cell.w >= 32 ? 26 : 20, h = cell.w >= 32 ? 18 : 20;
        const int x0 = cell.x + (cell.w - w) / 2, y0 = cell.y + (cell.h - h) / 2;
        if (mouse == 0) { // a key cap: dark outline, light top, shaded foot, rounded corners
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    bool corner = (x == 0 || x == w - 1) && (y == 0 || y == h - 1);
                    if (corner) {
                        continue;
                    }
                    bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
                    uint8_t v = edge ? dark : (y >= h - 3 ? mid : light);
                    put(x0 + x, y0 + y, v);
                }
            }
            int         text_w = static_cast<int>(label.size()) * kOverlayAdvance - 1;
            int         tx = x0 + (w - text_w) / 2, ty = y0 + (h - 3 - kOverlayGlyphHeight) / 2 + 1;
            for (char c : label) {
                if (const std::uint8_t *rows = OverlayGlyph(c)) {
                    for (int r = 0; r < kOverlayGlyphHeight; r++) {
                        for (int col = 0; col < kOverlayGlyphWidth; col++) {
                            if (rows[r] & (0x10 >> col)) {
                                put(tx + col, ty + r, dark);
                            }
                        }
                    }
                }
                tx += kOverlayAdvance;
            }
        } else { // a mouse seen from above, the pressed button lit
            const int mw = 12, mh = 18;
            const int mx = cell.x + (cell.w - mw) / 2, my = cell.y + (cell.h - mh) / 2;
            for (int y = 0; y < mh; y++) {
                for (int x = 0; x < mw; x++) {
                    bool corner = (x == 0 || x == mw - 1) && (y == 0 || y == mh - 1);
                    if (corner) {
                        continue;
                    }
                    bool edge = x == 0 || y == 0 || x == mw - 1 || y == mh - 1;
                    uint8_t v = edge ? dark : light;
                    if (!edge && y < 7) { // the two buttons
                        bool left = x < mw / 2;
                        if ((mouse == 1 && left) || (mouse == 2 && !left)) {
                            v = hot;
                        }
                        if (x == mw / 2 || x == mw / 2 - 1) {
                            v = (mouse == 3) ? hot : mid;
                        }
                    }
                    if (!edge && y == 7) {
                        v = dark;
                    }
                    put(mx + x, my + y, v);
                }
            }
        }
    }
}
} // namespace

void ModsTextureHook(const char *name, int bpp, int block, unsigned width, unsigned height, bool &indexed, bool &has_alpha,
                     bool &four_bit, const uint32_t *palette, std::vector<std::vector<uint8_t>> &levels) {
    if (!g.inited) {
        Init();
    }
    if (g.dump) {
        Dump(name, bpp, block, width, height, indexed, palette, levels);
    }
    if (indexed && palette != nullptr && std::strcmp(name, "gaiji") == 0 && !std::getenv("DC_PAD_PROMPTS")) {
        ComposeKeyCaps(width, height, four_bit, palette, levels);
    }
    {
        int  tc = 0;
        bool tback = false;
        char tkey = 0;
        if (TunicOf(name, tc, tback, tkey)) {
            if (tc >= 1 && tc <= 15 && RecolorTunic(tc, width, height, indexed, four_bit, palette, levels)) {
                return;
            }
            if (tc == 16) {
                auto it = g_custom.find(tkey);
                if (it != g_custom.end()) {
                    const std::vector<uint8_t> &px = (tback && !it->second.back.empty()) || it->second.front.empty() ? it->second.back : it->second.front;
                    if (!px.empty()) {
                        SDL_Surface *surf = SDL_CreateSurfaceFrom(256, 256, SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t *>(px.data()), 1024);
                        bool         done = surf != nullptr && ReplaceLevels(surf, width, height, indexed, has_alpha, four_bit, levels);
                        if (surf != nullptr) {
                            SDL_DestroySurface(surf);
                        }
                        if (done) {
                            return;
                        }
                    }
                }
            }
        }
    }
    if (g.by_stem.empty() && g.by_scoped.empty()) {
        return;
    }
    std::string source = TrimSlashes(Fold(StripDevice(g.last_file)));
    std::string folded = Fold(name);
    const std::string *png = nullptr;
    auto scoped = g.by_scoped.find(source + "/" + folded);
    if (scoped != g.by_scoped.end()) {
        png = &scoped->second;
    } else {
        auto stem = g.by_stem.find(folded);
        if (stem != g.by_stem.end()) {
            png = &stem->second;
        }
    }
    if (png == nullptr) {
        return;
    }
    SDL_Surface *loaded = SDL_LoadPNG(png->c_str());
    if (loaded == nullptr) {
        std::fprintf(stderr, "mods: cannot read %s: %s\n", png->c_str(), SDL_GetError());
        return;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr) {
        return;
    }
    if (!ReplaceLevels(rgba, width, height, indexed, has_alpha, four_bit, levels)) {
        SDL_DestroySurface(rgba);
        return;
    }
    SDL_DestroySurface(rgba);
    if (g.replaced++ < 50) {
        std::fprintf(stderr, "mods: replaced texture %s (from %s)\n", name, source.c_str());
    }
}
