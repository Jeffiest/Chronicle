#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

#include <SDL3/SDL_gamepad.h>
#include <nlohmann/json.hpp>

#include "../platform/config.hpp"
#include "../platform/input.hpp"

// The button symbols (platform/glyphs.hpp): their two settings, the key names the keyboard style looks
// glyphs up by, and the shipped glyphs.json the names must be found in.

namespace {

nlohmann::json ReadShippedIndex() {
    std::filesystem::path file = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / "glyphs" / "glyphs.json";
    std::ifstream         stream(file);
    return nlohmann::json::parse(stream, nullptr, false);
}

} // namespace

TEST(Glyphs, SettingsDefaultToNewSymbolsOnTheDeviceInUse) {
    Config config = ConfigParse("");
    ASSERT_TRUE(config.glyphs_new);
    ASSERT_EQ(config.glyph_device, ConfigGlyphDevice::Auto);
}

TEST(Glyphs, SettingsParseAndRoundTrip) {
    Config config = ConfigParse(R"({"input":{"glyphs":"original","glyph_device":"switch"}})");
    ASSERT_FALSE(config.glyphs_new);
    ASSERT_EQ(config.glyph_device, ConfigGlyphDevice::Switch);
    ASSERT_EQ(ConfigParse(ConfigSerialize(config)), config);
    for (std::string_view name : {"auto", "ps3", "ps4", "ps5", "xbox", "switch", "steamdeck", "steamcontroller", "keyboard"}) {
        Config c = ConfigParse(std::string(R"({"input":{"glyph_device":")") + std::string(name) + R"("}})");
        ASSERT_EQ(ConfigParse(ConfigSerialize(c)).glyph_device, c.glyph_device) << name;
    }
    ASSERT_EQ(ConfigParse(R"({"input":{"glyph_device":"steamdeck"}})").glyph_device, ConfigGlyphDevice::SteamDeck);
    ASSERT_EQ(ConfigParse(R"({"input":{"glyph_device":"steamcontroller"}})").glyph_device,
              ConfigGlyphDevice::SteamController);
}

TEST(Glyphs, GamepadsAreDrawnWithTheirOwnSymbolsAndUnknownOnesAsXbox) {
    constexpr unsigned kSony = 0x054C, kMicrosoft = 0x045E, kValve = 0x28DE, kOther = 0x1234;
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_PS3, kSony, 0x0268), InputGlyphFamily::Ps3);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_PS4, kSony, 0x09CC), InputGlyphFamily::Ps4);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_PS5, kSony, 0x0CE6), InputGlyphFamily::Ps5);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_XBOXONE, kMicrosoft, 0x02EA), InputGlyphFamily::Xbox);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057E, 0x2009),
              InputGlyphFamily::Switch);
    // Valve's pads: SDL may call them standard; the product tells the Deck from the Controllers.
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_STANDARD, kValve, 0x1205), InputGlyphFamily::SteamDeck);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_STANDARD, kValve, 0x1102), InputGlyphFamily::SteamController);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_UNKNOWN, kValve, 0x1302), InputGlyphFamily::SteamController);
    // Anything else SDL does not name is an Xbox-layout pad.
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_STANDARD, kOther, 1), InputGlyphFamily::Xbox);
    EXPECT_EQ(InputGlyphFamilyForGamepad(SDL_GAMEPAD_TYPE_UNKNOWN, 0, 0), InputGlyphFamily::Xbox);
}

TEST(Glyphs, BadSettingsAreIgnored) {
    Config config = ConfigParse(R"({"input":{"glyphs":"fancy","glyph_device":"atari"}})");
    ASSERT_TRUE(config.glyphs_new);
    ASSERT_EQ(config.glyph_device, ConfigGlyphDevice::Auto);
}

TEST(Glyphs, BindingNamesAreTheGlyphTableSpelling) {
    InputResetBindings();
    ASSERT_EQ(InputPrimaryBindingName("cross"), "mouse1");
    ASSERT_EQ(InputPrimaryBindingName("circle"), "mouse2");
    ASSERT_EQ(InputPrimaryBindingName("square"), "e");
    ASSERT_EQ(InputPrimaryBindingName("triangle"), "tab");
    ASSERT_EQ(InputPrimaryBindingName("start"), "return");
    ASSERT_EQ(InputPrimaryBindingName("select"), "backspace");
    ASSERT_EQ(InputPrimaryBindingName("up"), "up");
    ASSERT_EQ(InputPrimaryBindingName("nonsense"), "");
    const std::string_view keys[] = {"Left Ctrl"};
    ASSERT_TRUE(InputBindKeys("cross", keys));
    ASSERT_EQ(InputPrimaryBindingName("cross"), "leftctrl");
    InputResetBindings();
}

TEST(Glyphs, ShippedIndexHasEveryPadButtonInEveryPadStyle) {
    nlohmann::json index = ReadShippedIndex();
    ASSERT_TRUE(index.is_object());
    const std::set<std::string> buttons = {"cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "start",
                                           "select", "dpad", "dpad_ud", "dpad_lr", "up", "down", "left", "right",
                                           "l3", "r3", "lstick", "rstick"};
    for (const char *style : {"ps3", "ps4", "ps5", "xbox", "switch", "steamdeck", "steamcontroller", "keyboard"}) {
        ASSERT_TRUE(index["styles"].contains(style)) << style;
        const auto &glyphs = index["styles"][style]["glyphs"];
        for (const std::string &button : buttons) {
            // The keyboard style shows the key bound to a button (the next test); only the directional
            // symbols, which are no one key, are drawn for it.
            bool pad_only = std::string_view(style) == "keyboard" && button != "dpad" && button != "dpad_ud" &&
                            button != "dpad_lr" && button != "lstick" && button != "rstick";
            if (!pad_only) {
                ASSERT_TRUE(glyphs.contains(button)) << style << " lacks " << button;
            }
        }
    }
}

TEST(Glyphs, ShippedKeyboardIndexHasTheDefaultBindings) {
    nlohmann::json index = ReadShippedIndex();
    ASSERT_TRUE(index.is_object());
    const auto &glyphs = index["styles"]["keyboard"]["glyphs"];
    InputResetBindings();
    for (const char *action : {"cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "start",
                               "select", "up", "down", "left", "right"}) {
        std::string name = InputPrimaryBindingName(action);
        ASSERT_FALSE(name.empty()) << action;
        ASSERT_TRUE(glyphs.contains(name)) << action << " is bound to " << name << ", which has no symbol";
    }
}

TEST(Glyphs, EveryAtlasRectIsInsideItsAtlasFile) {
    nlohmann::json index = ReadShippedIndex();
    ASSERT_TRUE(index.is_object());
    for (const auto &[style, value] : index["styles"].items()) {
        std::filesystem::path png = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / "glyphs" /
                                    value["atlas"].get<std::string>();
        ASSERT_TRUE(std::filesystem::exists(png)) << png;
        // PNG: the width and height are the two big-endian words after the signature and "IHDR".
        std::ifstream      stream(png, std::ios::binary);
        unsigned char      header[24] = {};
        stream.read(reinterpret_cast<char *>(header), sizeof(header));
        auto word = [&](int at) {
            return (header[at] << 24) | (header[at + 1] << 16) | (header[at + 2] << 8) | header[at + 3];
        };
        const int width = word(16);
        const int height = word(20);
        for (const auto &[name, rect] : value["glyphs"].items()) {
            ASSERT_GE(rect[0].get<int>(), 0) << style << " " << name;
            ASSERT_GE(rect[1].get<int>(), 0) << style << " " << name;
            ASSERT_LE(rect[0].get<int>() + rect[2].get<int>(), width) << style << " " << name;
            ASSERT_LE(rect[1].get<int>() + rect[3].get<int>(), height) << style << " " << name;
        }
    }
}
