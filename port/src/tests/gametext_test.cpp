#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "clsmes.hpp"
#include "gametext.hpp"

namespace {

std::vector<s16> Encode(std::string_view text, int *missing = nullptr) {
    std::vector<s16> codes;
    const int        replaced = GameTextEncode(text, codes);
    if (missing != nullptr) {
        *missing = replaced;
    }
    return codes;
}

} // namespace

// Place names as the PAL disc's meswin/system_N.mes spells them (messages 10, 11, 13 and 66).
TEST(GameText, EncodesAsTheDiscDoes) {
    EXPECT_EQ(Encode("Norune Village"), (std::vector<s16>{-0x2D2, -0x2B7, -0x2B4, -0x2B1, -0x2B8, -0x2C1, -0xFE, -0x2CA,
                                                          -0x2BD, -0x2BA, -0x2BA, -0x2C5, -0x2BF, -0x2C1, -0xFF}));
    EXPECT_EQ(Encode("Tanière de la bête"),
              (std::vector<s16>{-0x2CC, -0x2C5, -0x2B8, -0x2BD, -0x278, -0x2B4, -0x2C1, -0xFE, -0x2C2, -0x2C1, -0xFE,
                                -0x2BA, -0x2C5, -0xFE, -0x2C4, -0x276, -0x2B2, -0x2C1, -0xFF}));
    EXPECT_EQ(Encode("Götterbiest-Höhle"),
              (std::vector<s16>{-0x2D9, -0x26C, -0x2B2, -0x2B2, -0x2C1, -0x2B4, -0x2C4, -0x2BD, -0x2C1, -0x2B3, -0x2B2,
                                -0x2A3, -0x2D8, -0x26C, -0x2BE, -0x2BA, -0x2C1, -0xFF}));
    EXPECT_EQ(Encode("Più Ricco"),
              (std::vector<s16>{-0x2D0, -0x2BD, -0x26B, -0xFE, -0x2CE, -0x2BD, -0x2C3, -0x2C3, -0x2B7, -0xFF}));
}

// MakeMesWinTbl_value's own codes for the characters of a number.
TEST(GameText, NumbersMatchTheGame) {
    EXPECT_EQ(GameTextCode(U'+'), -0x2A4);
    EXPECT_EQ(GameTextCode(U'-'), -0x2A3);
    for (int digit = 0; digit < 10; digit++) {
        EXPECT_EQ(GameTextCode(U'0' + digit), -0x291 + digit);
    }
}

TEST(GameText, EveryCharacterRoundTrips) {
    int characters = 0;
    for (int code = -0x300; code < -0x251; code++) {
        const char32_t ch = GameTextChar(static_cast<s16>(code));
        if (ch != 0) {
            characters++;
            EXPECT_EQ(GameTextCode(ch), code) << code;
        }
    }
    EXPECT_EQ(characters, 88 + 49);
    // The codes that draw a bare '?' in place of a letter the font lacks.
    for (int code : {-0x263, -0x262, -0x261, -0x253, -0x252}) {
        EXPECT_EQ(GameTextChar(static_cast<s16>(code)), 0U) << code;
    }
}

TEST(GameText, ReplacesWhatTheFontLacks) {
    int missing = 0;
    EXPECT_EQ(Encode("a~b;\xFF", &missing), (std::vector<s16>{-0x2C5, -0x2A7, -0x2C4, -0x2A7, -0x2A7, -0xFF}));
    EXPECT_EQ(missing, 3);
    EXPECT_EQ(Encode("\xE2\x80\x99\xE2\x80\x9C\xE2\x80\x94", &missing), Encode("'\"-"));
    EXPECT_EQ(missing, 0);
}

TEST(GameText, EscapesRoundTrip) {
    const std::vector<s16> codes = {-0x2DF, MES_CODE_NEWLINE, -0x2F0, MES_CODE_PAGE, -0x299, -0x298, -0x2DE, MES_CODE_END};
    const std::string      text = GameTextDecode(codes.data());
    EXPECT_EQ(text, "A\n{-752}{-253}{{}B");
    EXPECT_EQ(Encode(text), codes);
    EXPECT_EQ(Encode("{x}"), Encode("{{x}"));
}

// The file reads back through the game's own lookup.
TEST(GameText, FileReadsAsAMessageFile) {
    GameTextFile file;
    file.Set(0x15E, "PC Settings");
    file.Set(3, "Mouse sensitivity\n0.10");

    ClsMes mes;
    mes.SetBuff(file.Data());
    EXPECT_EQ(GameTextDecode(mes.GetTextLineDataTop(0x15E)), "PC Settings");
    EXPECT_EQ(GameTextDecode(mes.GetTextLineDataTop(3)), "Mouse sensitivity\n0.10");
    EXPECT_EQ(mes.GetTextLineDataTop(4), nullptr);
}
