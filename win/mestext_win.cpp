// Mod framework: text overrides for the game's message files (Windows fork).
//
//   mods/<mod>/text/*.json    { "<game file>": { "<message id>": "new text", ... }, ... }
//   DC_DUMP_TEXT=1            writes every message file the game loads, decoded, to mods/_dump/text/
//
// A message file is [count][header][ (id, offset) pairs ... ][text]: message `id` is the run of 16-bit codes that starts
// at word (1 + count + offset) and ends with -0xFF. Letters, digits and a few marks are gaiji codes in the font image.
// Overridden messages are appended to the end of the file and their offset repointed, so nothing else moves.

#include "mestext.hpp"
#include "mods.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "paths.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kEnd = -0xFF;
constexpr int kNewline = -0x100;
constexpr int kSpace = -0xFE;
constexpr int kPage = -0xFD;
constexpr int kMaxAppend = 12000; // words added to one file, at most
bool          g_no_grow = false; // the file sits inside a pack: its messages may be rewritten in place but never lengthened

// Known glyphs. Anything else is written/read as {code}.
//   A-Z -735..-710, a-z -709..-684, 0-9 -657..-648
const std::map<char, int> kMarks = {{'\'', -683}, {'&', -677}, {'-', -675}, {'(', -671}, {')', -670}, {'.', -659},
                                     {'?', -679}, {'!', -680}, {',', -660}, {'"', -681}};

bool CodeToChar(int code, std::string &out) {
    if (code >= -735 && code <= -710) {
        out += static_cast<char>('A' + (code + 735));
    } else if (code >= -709 && code <= -684) {
        out += static_cast<char>('a' + (code + 709));
    } else if (code >= -657 && code <= -648) {
        out += static_cast<char>('0' + (code + 657));
    } else if (code == kSpace) {
        out += ' ';
    } else if (code == kNewline) {
        out += '\n';
    } else if (code == kPage) {
        out += "{page}";
    } else {
        for (const auto &[c, v] : kMarks) {
            if (v == code) {
                out += c;
                return true;
            }
        }
        out += "{" + std::to_string(code) + "}";
    }
    return true;
}

bool Encode(const std::string &text, std::vector<int16_t> &out, std::string &error) {
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<int16_t>(-735 + (c - 'A')));
        } else if (c >= 'a' && c <= 'z') {
            out.push_back(static_cast<int16_t>(-709 + (c - 'a')));
        } else if (c >= '0' && c <= '9') {
            out.push_back(static_cast<int16_t>(-657 + (c - '0')));
        } else if (c == ' ') {
            out.push_back(kSpace);
        } else if (c == '\n') {
            out.push_back(kNewline);
        } else if (c == '{') {
            size_t close = text.find('}', i);
            if (close == std::string::npos) {
                error = "unclosed {";
                return false;
            }
            std::string token = text.substr(i + 1, close - i - 1);
            if (token == "page") {
                out.push_back(kPage);
            } else {
                char *end = nullptr;
                long  v = std::strtol(token.c_str(), &end, 10);
                if (token.empty() || *end != 0 || v < -32767 || v > 32767) {
                    error = "bad {" + token + "} (use {page} or a code such as {-737})";
                    return false;
                }
                out.push_back(static_cast<int16_t>(v));
            }
            i = close;
        } else if (kMarks.count(static_cast<char>(c)) != 0) {
            out.push_back(static_cast<int16_t>(kMarks.at(static_cast<char>(c))));
        } else {
            error = std::string("no glyph for '") + static_cast<char>(c) + "' (letters, digits, space and ' \" & - ( ) . , ? ! work; anything else is {code})";
            return false;
        }
    }
    return true;
}

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

// Glob with * only (matches any run of characters, slashes included).
bool Glob(const char *pat, const char *s) {
    if (*pat == 0) {
        return *s == 0;
    }
    if (*pat == '*') {
        for (const char *t = s;; t++) {
            if (Glob(pat + 1, t)) {
                return true;
            }
            if (*t == 0) {
                return false;
            }
        }
    }
    return *pat == *s && Glob(pat + 1, s + 1);
}

struct Override {
    std::string                         mod;
    std::string                         pattern; // folded game path, may contain *
    std::map<int, std::string>          text;
    std::vector<std::pair<std::string, std::string>> replace; // original text -> new text, matched whole in any file (menu packs)
};

struct State {
    bool                  loaded = false;
    fs::path              root;
    std::vector<Override> overrides;
    bool                  dump = false;
    bool                  dump_patched = false; // DC_DUMP_TEXT=2: also write the text after the mods, to _dump/text_patched
    std::set<std::string> dumped;
};
State g;

void Warn(const std::string &mod, const std::string &text) {
    std::fprintf(stderr, "[mod %s] text: %s\n", mod.c_str(), text.c_str());
}

void EnsureLoaded() {
    if (g.loaded) {
        return;
    }
    g.loaded = true;
    const char *dump = std::getenv("DC_DUMP_TEXT");
    g.dump = dump != nullptr && (dump[0] == '1' || dump[0] == '2');
    g.dump_patched = dump != nullptr && dump[0] == '2';
    g.root = PathsSaveRoot() / "mods";
    std::error_code error;
    if (!fs::is_directory(g.root, error)) {
        return;
    }
    std::vector<fs::path> mods = ModsLoadOrder(g.root);
    for (const fs::path &mod : mods) {
        std::string name = Utf8(mod.filename());
        {
            std::ifstream in(mod / "mod.json");
            json          meta = in ? json::parse(in, nullptr, false, true) : json();
            if (meta.is_object() && !meta.value("enabled", true)) {
                continue;
            }
        }
        fs::path dir = mod / "text";
        if (!fs::is_directory(dir, error)) {
            continue;
        }
        std::vector<fs::path> files;
        for (const auto &entry : fs::directory_iterator(dir, error)) {
            if (entry.is_regular_file(error) && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path &file : files) {
            std::ifstream in(file);
            json          root = in ? json::parse(in, nullptr, false, true) : json();
            if (root.is_discarded() || !root.is_object()) {
                Warn(name, Utf8(file.filename()) + " is not a valid JSON object; ignored");
                continue;
            }
            for (auto it = root.begin(); it != root.end(); ++it) {
                if (it.key().empty() || it.key()[0] == '_') {
                    continue;
                }
                Override ov;
                ov.mod = name;
                ov.pattern = Fold(it.key());
                if (!it.value().is_object()) {
                    Warn(name, "'" + it.key() + "' must be an object of message id -> text");
                    continue;
                }
                for (auto m = it.value().begin(); m != it.value().end(); ++m) {
                    if (m.key().empty() || m.key()[0] == '_') {
                        continue;
                    }
                    bool digits = m.key().find_first_not_of("0123456789") == std::string::npos && m.key().size() < 7;
                    if (!m.value().is_string()) {
                        Warn(name, "'" + it.key() + "': the text for '" + m.key() + "' must be a string");
                        continue;
                    }
                    if (digits) {
                        ov.text[std::stoi(m.key())] = m.value().get<std::string>();
                    } else {
                        ov.replace.emplace_back(m.key(), m.value().get<std::string>()); // not an id: the original text to find
                    }
                }
                if (!ov.text.empty() || !ov.replace.empty()) {
                    g.overrides.push_back(std::move(ov));
                }
            }
        }
    }
    std::fprintf(stderr, "mods: %zu text override file section(s)\n", g.overrides.size());
}

struct Table {
    std::vector<std::pair<int, size_t>> pairs; // message id -> index of its offset word
    size_t                              base = 0;  // index of the first word offsets count from
};

// Checks the layout; fails for anything that is not a message file.
bool Parse(const int16_t *w, size_t words, Table &t) {
    if (words < 8 || w[0] < 4 || static_cast<size_t>(w[0]) + 2 >= words) {
        return false;
    }
    size_t count = static_cast<size_t>(w[0]);
    t.base = 1 + count;
    int prev = -1;
    for (size_t i = 2; i + 1 <= count + 1 && i + 1 < words; i += 2) {
        int id = w[i];
        int off = w[i + 1];
        if (id <= prev && !(i == 2 && id == 0)) {
            return false; // ids ascend
        }
        if (off < 0 || t.base + static_cast<size_t>(off) >= words) {
            return false;
        }
        t.pairs.emplace_back(id, i + 1);
        prev = id;
    }
    return t.pairs.size() >= 2;
}

std::string DecodeAt(const int16_t *w, size_t words, size_t at) {
    std::string out;
    for (size_t i = at; i < words && w[i] != kEnd; i++) {
        CodeToChar(w[i], out);
    }
    return out;
}

void Dump(const std::string &path, const int16_t *w, size_t words, const Table &t, const char *sub = "text") {
    if (g.dumped.count(std::string(sub) + path) != 0) {
        return;
    }
    g.dumped.insert(std::string(sub) + path);
    std::string name = path;
    for (char &c : name) {
        if (c == '/') {
            c = '_';
        }
    }
    fs::path dir = g.root / "_dump" / sub;
    std::error_code error;
    fs::create_directories(dir, error);
    json out = json::object();
    for (const auto &[id, index] : t.pairs) {
        out[std::to_string(id)] = DecodeAt(w, words, t.base + static_cast<size_t>(w[index]));
    }
    std::ofstream file(dir / (name + ".json"));
    file << "{\n \"" << path << "\": {\n";
    size_t n = 0;
    for (auto it = out.begin(); it != out.end(); ++it, ++n) {
        file << "  " << json(it.key()).dump() << ": " << it.value().dump(-1, ' ', false, json::error_handler_t::replace)
             << (n + 1 < out.size() ? ",\n" : "\n");
    }
    file << " }\n}\n";
}

} // namespace

namespace {

// Finds `from` as a whole message or line (after a new line, a terminator or the start; before a new line, a terminator or the end)
// anywhere in the buffer and overwrites it with `to`, padded with spaces, so nothing after it moves. Used for the menu packs, where
// the item names and descriptions sit in sequential blobs with no id table. Returns how many were replaced.
int ReplaceWhole(int16_t *w, size_t words, const std::string &mod, const std::string &file, const std::string &from, const std::string &to) {
    std::vector<int16_t> a, b;
    std::string          error;
    if (!Encode(from, a, error) || a.empty()) {
        Warn(mod, "'" + from + "': " + (error.empty() ? "empty text" : error));
        return 0;
    }
    if (!Encode(to, b, error)) {
        Warn(mod, "'" + to + "': " + error);
        return 0;
    }
    if (b.size() > a.size()) {
        Warn(mod, "'" + file + "': the replacement for '" + from + "' is longer than the original (" + std::to_string(b.size()) + " > " +
                      std::to_string(a.size()) + " characters); shorten it");
        return 0;
    }
    int count = 0;
    for (size_t i = 0; i + a.size() <= words; i++) {
        if (w[i] != a[0] || std::memcmp(w + i, a.data(), a.size() * sizeof(int16_t)) != 0) {
            continue;
        }
        bool before = i == 0 || w[i - 1] == kNewline || w[i - 1] == kEnd;
        bool after = i + a.size() == words || w[i + a.size()] == kNewline || w[i + a.size()] == kEnd;
        if (!before || !after) {
            continue;
        }
        for (size_t k = 0; k < a.size(); k++) {
            w[i + k] = k < b.size() ? b[k] : static_cast<int16_t>(kSpace);
        }
        count++;
        i += a.size() - 1;
    }
    return count;
}

} // namespace

int ModTextPatch(const char *path, void *buffer, int size) {
    EnsureLoaded();
    if (path == nullptr || buffer == nullptr || size < 16 || (size & 1) != 0) {
        return size;
    }
    std::string folded = Fold(path);
    {   // replace-by-text entries apply to any file whose path they name
        int total = 0;
        for (const Override &ov : g.overrides) {
            if (ov.replace.empty() || !Glob(ov.pattern.c_str(), folded.c_str())) {
                continue;
            }
            for (const auto &[from, to] : ov.replace) {
                total += ReplaceWhole(static_cast<int16_t *>(buffer), static_cast<size_t>(size) / 2, ov.mod, folded, from, to);
            }
        }
        if (total > 0) {
            std::fprintf(stderr, "mods: text: %d text replacement(s) in %s\n", total, path);
        }
    }
    bool        message_file = folded.size() > 4 && (folded.compare(folded.size() - 4, 4, ".mes") == 0 ||
                                              (folded.compare(folded.size() - 4, 4, ".bin") == 0 && folded.rfind("meswin/", 0) == 0));
    if (!message_file || (!g.dump && g.overrides.empty())) {
        return size;
    }
    auto  *w = static_cast<int16_t *>(buffer);
    size_t words = static_cast<size_t>(size) / 2;
    Table  table;
    if (!Parse(w, words, table)) {
        return size;
    }
    if (g.dump) {
        Dump(folded, w, words, table);
    }
    std::map<int, const Override *> chosen; // later mods win
    std::map<int, std::string>      text;
    for (const Override &ov : g.overrides) {
        if (!Glob(ov.pattern.c_str(), folded.c_str())) {
            continue;
        }
        for (const auto &[id, s] : ov.text) {
            chosen[id] = &ov;
            text[id] = s;
        }
    }
    if (text.empty()) {
        return size;
    }
    std::map<int, size_t> index_of;
    std::map<int, int>    users; // how many ids share each offset
    for (const auto &[id, index] : table.pairs) {
        index_of[id] = index;
        users[w[index]]++;
    }
    std::vector<int16_t> add;
    int                  applied = 0;
    for (const auto &[id, s] : text) {
        auto at = index_of.find(id);
        if (at == index_of.end()) {
            Warn(chosen[id]->mod, "'" + folded + "' has no message " + std::to_string(id) + "; skipped");
            continue;
        }
        std::vector<int16_t> codes;
        std::string          error;
        if (!Encode(s, codes, error)) {
            Warn(chosen[id]->mod, "'" + folded + "' message " + std::to_string(id) + ": " + error);
            continue;
        }
        codes.push_back(kEnd);
        // Fits where the old message was (and no other id points there): write it in place, the file stays the same size.
        size_t old_at = table.base + static_cast<size_t>(w[at->second]);
        size_t old_len = 0;
        while (old_at + old_len < words && w[old_at + old_len] != kEnd) {
            old_len++;
        }
        old_len++; // the end code
        if (codes.size() <= old_len && users[w[at->second]] == 1) {
            std::memcpy(w + old_at, codes.data(), codes.size() * sizeof(int16_t));
            applied++;
            continue;
        }
        size_t start = words + add.size() - table.base;
        if (g_no_grow || start > 32000 || add.size() + codes.size() > static_cast<size_t>(kMaxAppend)) {
            Warn(chosen[id]->mod, "'" + folded + "': message " + std::to_string(id) + " is longer than the original and this file has no room to grow it; skipped (shorten it)");
            continue;
        }
        w[at->second] = static_cast<int16_t>(start);
        add.insert(add.end(), codes.begin(), codes.end());
        applied++;
    }
    // The caller's buffer is a game arena sized from the returned size; the appended words go straight after the file.
    std::memcpy(w + words, add.data(), add.size() * sizeof(int16_t));
    if (applied > 0) {
        std::fprintf(stderr, "mods: text: %d message(s) replaced in %s\n", applied, path);
    }
    int new_size = size + static_cast<int>(add.size() * sizeof(int16_t));
    if (g.dump_patched) {
        Table after;
        if (Parse(w, static_cast<size_t>(new_size) / 2, after)) {
            Dump(folded, w, static_cast<size_t>(new_size) / 2, after, "text_patched");
        }
    }
    return new_size;
}

// The menu message set (a message file cut out of a menu pack by the game): replace texts in place only, the file cannot grow here.
void ModTextPatchMenu(void *buffer) {
    auto *w = static_cast<int16_t *>(buffer);
    if (w == nullptr || w[0] < 4 || w[0] > 4000) {
        return;
    }
    size_t count = static_cast<size_t>(w[0]), base = 1 + count, end = base;
    for (size_t i = 2; i + 1 <= count + 1; i += 2) {
        if (w[i + 1] < 0) {
            return;
        }
        size_t at = base + static_cast<size_t>(w[i + 1]), len = 0;
        while (len < 4000 && w[at + len] != kEnd) {
            len++;
        }
        end = std::max(end, at + len + 1);
    }
    g_no_grow = true;
    ModTextPatch("commenu/allmenu.mes", buffer, static_cast<int>(end * 2));
    g_no_grow = false;
}
