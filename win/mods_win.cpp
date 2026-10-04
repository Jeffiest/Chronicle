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
    std::vector<fs::path> mods;
    for (const auto &entry : fs::directory_iterator(g.root, error)) {
        std::string name = Utf8(entry.path().filename());
        if (entry.is_directory(error) && !name.empty() && name[0] != '_' && name[0] != '.') {
            mods.push_back(entry.path());
        }
    }
    std::sort(mods.begin(), mods.end(), [](const fs::path &a, const fs::path &b) { return Fold(Utf8(a)) < Fold(Utf8(b)); });
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
    std::ofstream index(g.root / "_dump" / "textures.csv", std::ios::app);
    index << source << ',' << name << ',' << width << 'x' << height << ',' << bpp << ',' << block << '\n';
}

} // namespace

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
