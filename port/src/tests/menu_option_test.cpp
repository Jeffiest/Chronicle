#include <gtest/gtest.h>

#include "memorycardaccess.hpp"
#include "menu_option.hpp"
#include "savedata.hpp"
#include "userstatus.hpp"

namespace {

alignas(64) CSaveData g_options_save;
alignas(64) char g_save_buffer[0x30000];

ConfigGameOptions AllChanged() {
    ConfigGameOptions options;
    options.save_cursor_position = false;
    options.vibration = false;
    options.fast_messages = true;
    options.stereo = false;
    options.clock = false;
    options.fast_time = true;
    options.map = 0;
    options.enemy_damage = false;
    options.player_damage = false;
    options.enemy_hp = false;
    options.names = false;
    options.soft_focus = false;
    return options;
}

} // namespace

// config.json's options reach the words the game reads, over whatever a loaded save brought; a save
// about to be written has reserved zero fields instead. State in the same block stays.
TEST(MenuOption, OptionsLiveInConfigNotInSaves) {
    g_options_save.Initialize();
    SaveData = &g_options_save;
    s32 *words = static_cast<s32 *>(g_options_save.GetConfigData());
    words[14] = 1;
    words[17] = 5;
    g_options_save.GetMenuCursor()->pos[0] = 3;

    GameOptionsApply(AllChanged());
    for (int word = 2; word <= 11; ++word) {
        ASSERT_TRUE(words[word] == 1) << word;
    }
    ASSERT_TRUE(reinterpret_cast<CUserStatus *>(g_options_save.GetDngStatus())->minimap_status == 3);
    ASSERT_TRUE(g_options_save.GetMenuCursor()->reset_pos == 1);
    ASSERT_TRUE(g_options_save.GetMenuCursor()->pos[0] == 0);

    CMemoryCardAccess card{};
    card.SetBuff(g_save_buffer);
    s32 *serialized = static_cast<s32 *>(card.save_buffer->GetConfigData());
    for (int word = 2; word <= 11; ++word) {
        ASSERT_EQ(serialized[word], 0) << word;
        ASSERT_EQ(words[word], 1) << word;
    }
    ASSERT_EQ(reinterpret_cast<CUserStatus *>(card.save_buffer->GetDngStatus())->minimap_status, 0);
    ASSERT_EQ(card.save_buffer->GetMenuCursor()->reset_pos, 0);
    ASSERT_EQ(serialized[15], 0);
    ASSERT_EQ(serialized[16], 0);
    ASSERT_TRUE(serialized[14] == 1 && serialized[17] == 5);
    ASSERT_EQ(reinterpret_cast<CUserStatus *>(g_options_save.GetDngStatus())->minimap_status, 3);
    ASSERT_EQ(g_options_save.GetMenuCursor()->reset_pos, 1);

    SV_CONFIG_SYS saved;
    card.save_buffer->ConvertConfig(&saved);
    ASSERT_TRUE(saved.values[5] == 0 && saved.values[15] == 0 && saved.values[16] == 0 && saved.values[14] == 1);
    // A load's neutral fields must be replaced by JSON before the game uses them again.
    g_options_save.InvertConfig(&saved);
    GameOptionsApply(AllChanged());
    ASSERT_EQ(words[5], 1);
    ASSERT_EQ(reinterpret_cast<CUserStatus *>(g_options_save.GetDngStatus())->minimap_status, 3);
    SaveData = nullptr;
}
