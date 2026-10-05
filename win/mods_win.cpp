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
bool g_local_tunic_blue = false;

// Pixels that are clearly orange/red/yellow (the poncho) are turned to blue; shading and saturation are kept.
void ShiftToBlue(uint8_t *px) {
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
    float nh = 215.0f + rel * 0.4f;
    float c = mx * s;
    float x = c * (1.0f - std::fabs(std::fmod(nh / 60.0f, 2.0f) - 1.0f));
    float m0 = mx - c;
    float nr = 0, ng = 0, nb = 0;
    if (nh < 180.0f)      { nr = 0; ng = x; nb = c; }
    else if (nh < 240.0f) { nr = 0; ng = x; nb = c; }
    else                  { nr = x; ng = 0; nb = c; }
    px[0] = static_cast<uint8_t>(std::clamp((nr + m0) * 255.0f, 0.0f, 255.0f));
    px[1] = static_cast<uint8_t>(std::clamp((ng + m0) * 255.0f, 0.0f, 255.0f));
    px[2] = static_cast<uint8_t>(std::clamp((nb + m0) * 255.0f, 0.0f, 255.0f));
}

// "c01d04"/"c01d05" are the poncho front and back. A ghost's renamed copy is "g<n>1d04": g = blue, h = natural.
bool IsBlueTunic(const char *name) {
    std::string n = Fold(name);
    if (n.size() != 6 || (n.compare(2, 4, "1d04") != 0 && n.compare(2, 4, "1d05") != 0)) {
        return false;
    }
    if (n[0] == 'g') {
        return true;
    }
    return g_local_tunic_blue && n[0] == 'c' && n[1] == '0';
}

bool RecolorTunic(unsigned width, unsigned height, bool &indexed, bool &four_bit, const uint32_t *palette,
                  std::vector<std::vector<uint8_t>> &levels) {
    if (levels.empty() || width == 0 || height == 0 || (indexed && palette == nullptr)) {
        return false;
    }
    std::vector<std::vector<uint8_t>> out;
    for (size_t level = 0; level < levels.size(); level++) {
        size_t w = std::max<size_t>(1, width >> level), h = std::max<size_t>(1, height >> level);
        std::vector<uint8_t> rgba(w * h * 4, 0);
        const std::vector<uint8_t> &src = levels[level];
        if (indexed) {
            for (size_t i = 0; i < w * h && i < src.size(); i++) {
                uint32_t color = palette[src[i]];
                std::memcpy(&rgba[i * 4], &color, 4);
            }
        } else {
            std::memcpy(rgba.data(), src.data(), std::min(rgba.size(), src.size()));
        }
        for (size_t i = 0; i < w * h; i++) {
            ShiftToBlue(&rgba[i * 4]);
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


void ModsSetLocalTunicBlue(bool blue) { g_local_tunic_blue = blue; }

void ModsNoteFile(const char *path) {
    if (path != nullptr) {
        g.last_file = path; // dumps and scoped overrides both name the game file a texture came from
    }
}

void ModsTextureHook(const char *name, int bpp, int block, unsigned width, unsigned height, bool &indexed, bool &has_alpha,
                     bool &four_bit, const uint32_t *palette, std::vector<std::vector<uint8_t>> &levels) {
    if (!g.inited) {
        Init();
    }
    if (g.dump) {
        Dump(name, bpp, block, width, height, indexed, palette, levels);
    }
    if (IsBlueTunic(name) && RecolorTunic(width, height, indexed, four_bit, palette, levels)) {
        return;
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
    int level_count = std::max<int>(1, static_cast<int>(levels.size()));
    std::vector<std::vector<uint8_t>> out;
    bool                              translucent = false;
    for (int level = 0; level < level_count; level++) {
        int          w = std::max<int>(1, static_cast<int>(width) >> level);
        int          h = std::max<int>(1, static_cast<int>(height) >> level);
        SDL_Surface *scaled = Scaled(rgba, w, h);
        if (scaled == nullptr) {
            SDL_DestroySurface(rgba);
            return;
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
    SDL_DestroySurface(rgba);
    levels = std::move(out);
    indexed = false;
    has_alpha = translucent || has_alpha;
    four_bit = false;
    if (g.replaced++ < 50) {
        std::fprintf(stderr, "mods: replaced texture %s (from %s)\n", name, source.c_str());
    }
}
