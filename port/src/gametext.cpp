#include "gametext.hpp"

#include <cstdint>
#include <string_view>

#include "gameutil.hpp"
#include "main.hpp"
#include "menu_draw.hpp"

namespace {

// gaiji.img's letter grid in code order: nine 14x20 cells a row from u 128 (GaijiDataTbl).
constexpr std::u32string_view kGrid = U"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'=\"!?#&+-*/%()@|<>{}[]:,.$0123456789";
constexpr s16                 kGridFirst = -0x2DF;

struct Extra {
    s16      code;
    char32_t ch;
};

// The rest of PAL's characters: a few cells of their own below the grid, and grid letters that
// DrawGaijiFont draws an accent over. -0x263 to -0x261, -0x253 and -0x252 draw a bare '?'.
// clang-format off
constexpr Extra kExtras[] = {
    {-0x287, U'œ'}, {-0x286, U'¡'}, {-0x285, U'¿'}, {-0x284, U'Ä'}, {-0x283, U'Ç'}, {-0x282, U'È'},
    {-0x281, U'É'}, {-0x280, U'Ö'}, {-0x27F, U'Ü'}, {-0x27E, U'ß'}, {-0x27D, U'à'}, {-0x27C, U'á'},
    {-0x27B, U'â'}, {-0x27A, U'ä'}, {-0x279, U'ç'}, {-0x278, U'è'}, {-0x277, U'é'}, {-0x276, U'ê'},
    {-0x275, U'ë'}, {-0x274, U'ì'}, {-0x273, U'í'}, {-0x272, U'î'}, {-0x271, U'ï'}, {-0x270, U'ñ'},
    {-0x26F, U'ò'}, {-0x26E, U'ó'}, {-0x26D, U'ô'}, {-0x26C, U'ö'}, {-0x26B, U'ù'}, {-0x26A, U'ú'},
    {-0x269, U'û'}, {-0x268, U'ü'}, {-0x267, U'Ú'}, {-0x266, U'Á'}, {-0x265, U'Œ'}, {-0x264, U'Ó'},
    {-0x260, U'À'}, {-0x25F, U'Â'}, {-0x25E, U'Ï'}, {-0x25D, U'Í'}, {-0x25C, U'Ì'}, {-0x25B, U'Î'},
    {-0x25A, U'Ù'}, {-0x259, U'Û'}, {-0x258, U'Ë'}, {-0x257, U'Ê'}, {-0x256, U'Ò'}, {-0x255, U'Ô'},
    {-0x254, U'Ñ'},
};
// clang-format on

constexpr s16 kUnknown = kGridFirst + static_cast<s16>(kGrid.find(U'?'));

// The code point at utf8[at], advancing at past it; U+FFFD for a byte that does not start a
// well-formed sequence, advancing one byte.
char32_t NextChar(std::string_view utf8, size_t &at) {
    const auto lead = static_cast<unsigned char>(utf8[at]);
    int        length;
    char32_t   ch;
    char32_t   least;

    if (lead < 0x80) {
        at++;
        return lead;
    }
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        ch = lead & 0x1F;
        least = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        ch = lead & 0x0F;
        least = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        ch = lead & 0x07;
        least = 0x10000;
    } else {
        at++;
        return 0xFFFD;
    }
    if (at + length > utf8.size()) {
        at++;
        return 0xFFFD;
    }
    for (int i = 1; i < length; i++) {
        const auto next = static_cast<unsigned char>(utf8[at + i]);
        if ((next & 0xC0) != 0x80) {
            at++;
            return 0xFFFD;
        }
        ch = ch << 6 | (next & 0x3F);
    }
    if (ch < least || ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
        at++;
        return 0xFFFD;
    }
    at += length;
    return ch;
}

void AppendUtf8(std::string &out, char32_t ch) {
    if (ch < 0x80) {
        out += static_cast<char>(ch);
    } else if (ch < 0x800) {
        out += static_cast<char>(0xC0 | ch >> 6);
        out += static_cast<char>(0x80 | (ch & 0x3F));
    } else if (ch < 0x10000) {
        out += static_cast<char>(0xE0 | ch >> 12);
        out += static_cast<char>(0x80 | (ch >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (ch & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | ch >> 18);
        out += static_cast<char>(0x80 | (ch >> 12 & 0x3F));
        out += static_cast<char>(0x80 | (ch >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (ch & 0x3F));
    }
}

// "{N}" at utf8[at]: N into code and at past the '}'.
bool ReadEscape(std::string_view utf8, size_t &at, s16 &code) {
    size_t i = at + 1;
    bool   negative = false;
    long   value = 0;
    size_t digits = 0;

    if (i < utf8.size() && utf8[i] == '-') {
        negative = true;
        i++;
    }
    while (i < utf8.size() && utf8[i] >= '0' && utf8[i] <= '9' && digits < 6) {
        value = value * 10 + (utf8[i] - '0');
        digits++;
        i++;
    }
    if (digits == 0 || i >= utf8.size() || utf8[i] != '}') {
        return false;
    }
    value = negative ? -value : value;
    if (value < INT16_MIN || value > INT16_MAX) {
        return false;
    }
    code = static_cast<s16>(value);
    at = i + 1;
    return true;
}

} // namespace

s16 GameTextCode(char32_t ch) {
    switch (ch) {
        case U' ':
        case U' ':
            return MES_CODE_SPACE;
        case U'\n':
            return MES_CODE_NEWLINE;
        case U'‘':
        case U'’':
            ch = U'\'';
            break;
        case U'“':
        case U'”':
            ch = U'"';
            break;
        case U'–':
        case U'—':
            ch = U'-';
            break;
    }

    const size_t at = kGrid.find(ch);
    if (at != std::u32string_view::npos) {
        return static_cast<s16>(kGridFirst + at);
    }
    for (const Extra &extra : kExtras) {
        if (extra.ch == ch) {
            return extra.code;
        }
    }
    return 0;
}

char32_t GameTextChar(s16 code) {
    switch (code) {
        case MES_CODE_SPACE:
            return U' ';
        case MES_CODE_NEWLINE:
            return U'\n';
    }

    if (code >= kGridFirst && code < kGridFirst + static_cast<int>(kGrid.size())) {
        return kGrid[code - kGridFirst];
    }
    for (const Extra &extra : kExtras) {
        if (extra.code == code) {
            return extra.ch;
        }
    }
    return 0;
}

int GameTextEncode(std::string_view utf8, std::vector<s16> &out) {
    int    missing = 0;
    size_t at = 0;

    while (at < utf8.size()) {
        if (utf8[at] == '{') {
            if (at + 1 < utf8.size() && utf8[at + 1] == '{') {
                out.push_back(GameTextCode(U'{'));
                at += 2;
                continue;
            }

            s16 code;
            if (ReadEscape(utf8, at, code)) {
                out.push_back(code);
                continue;
            }
        }

        s16 code = GameTextCode(NextChar(utf8, at));
        if (code == 0) {
            code = kUnknown;
            missing++;
        }
        out.push_back(code);
    }

    out.push_back(MES_CODE_END);
    return missing;
}

std::string GameTextDecode(const s16 *codes) {
    std::string text;

    for (; *codes != MES_CODE_END; codes++) {
        const char32_t ch = GameTextChar(*codes);

        if (ch == 0) {
            text += '{' + std::to_string(*codes) + '}';
        } else if (ch == U'{') {
            text += "{{";
        } else {
            AppendUtf8(text, ch);
        }
    }

    return text;
}

int GameTextFile::Set(int id, std::string_view utf8) {
    if (id < INT16_MIN || id > INT16_MAX) {
        return -1;
    }

    std::vector<s16> codes;
    const int        missing = GameTextEncode(utf8, codes);

    const auto   found = messages_.find(id);
    const bool   added = found == messages_.end();
    const size_t count = messages_.size() + (added ? 1 : 0);
    const size_t total = codes_ - (added ? 0 : found->second.size()) + codes.size();
    size_t       last = codes.size();

    if (!messages_.empty() && messages_.rbegin()->first > id) {
        last = messages_.rbegin()->second.size();
    }
    if (1 + count + total - last > INT16_MAX) {
        return -1;
    }

    codes_ = total;
    messages_[id] = std::move(codes);
    dirty_ = true;
    return missing;
}

short *GameTextFile::Data() {
    if (dirty_) {
        const size_t count = messages_.size();

        data_.assign(2 + count * 2, 0);
        data_.reserve(data_.size() + codes_);
        data_[0] = static_cast<s16>(count);

        size_t entry = 0;
        for (const auto &[id, codes] : messages_) {
            data_[2 + entry * 2] = static_cast<s16>(id);
            data_[3 + entry * 2] = static_cast<s16>(data_.size() - (1 + count));
            data_.insert(data_.end(), codes.begin(), codes.end());
            entry++;
        }

        dirty_ = false;
    }

    return data_.data();
}

GameText::GameText() : colour_(FONT_COLOR_WHITE) {
    mes_.Preset(MES_PRESET_SYSTEM);
    mes_.char_width = 11;
    mes_.char_height = 0x14;
    mes_.narrow_gaiji_set = 2;
    mes_.tex_block = 0x1A;
    mes_.auto_pos = MES_POS_NONE;
    mes_.tail_on = false;
    mes_.value_show = false;
    mes_.centre_rows = false;
}

int GameText::Set(std::string_view utf8) {
    if (!set_ || utf8 != text_ || mes_.mes_made < 0) {
        set_ = true;
        text_ = utf8;
        missing_ = file_.Set(0, utf8);
        if (missing_ >= 0 && !Layout()) {
            missing_ = -1;
        }
    }

    return missing_;
}

void GameText::SetColour(u32 colour) {
    if (colour != colour_) {
        colour_ = colour;
        if (mes_.mes_made >= 0 && !Layout()) {
            missing_ = -1;
        }
    }
}

bool GameText::Layout() {
    mes_.SetBuff(file_.Data());
    if (SystemMes != nullptr) {
        mes_.SetBuff_system(SystemMes);
    }
    mes_.clut_default = Color2Clut(colour_) & 0xFF;
    mes_.clut_now = mes_.clut_default;
    mes_.mes_made = -1;
    mes_.MakeMesWin(0);

    if (mes_.win_line_num > 0 && mes_.win_line[mes_.win_line_num - 1].code == MES_CODE_END) {
        return true;
    }

    mes_.mes_made = -1;
    mes_.text_len = 0;
    mes_.text_width = 0;
    return false;
}

void GameText::Draw(int x, int y, int alpha) {
    if (mes_.mes_made < 0) {
        return;
    }

    mes_.edge_alpha = alpha;
    DrawMenuClsMes(&mes_, x, y);
}
