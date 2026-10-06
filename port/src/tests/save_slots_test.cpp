#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "memorycardaccess.hpp"
#include "platform/paths.hpp"
#include "platform_fixture.hpp"
#include "save_slots.hpp"
#include "savedata.hpp"

// The save screens' list and CMemoryCardAccess on flat files, driven as the save menu does:
// SetFuncNo, then Step until the operation finishes, on a save root in a temporary directory.

namespace fs = std::filesystem;

namespace {

constexpr int kImageSize = 0x136A7;
constexpr int kSaveDataSize = 0x131C0;
constexpr int kChecksumOffset = 0x131E0;
constexpr int kConfigSize = 0x40;
constexpr int kSaveMapNoOffset = 0x1C8;

alignas(64) char g_menu_buffer[0x30000];
alignas(64) CSaveData g_save;
CMemoryCardAccess g_mc;

fs::path UseTempSaveRoot() {
    fs::path root = fs::temp_directory_path() / ("dc_save_test_" + std::to_string(dc::test::ProcessId()));
    fs::remove_all(root);
    fs::create_directories(root);
    PathsSetSaveRoot(root);
    return root;
}

int RunOperation(CMemoryCardAccess &mc, int operation) {
    mc.SetFuncNo(operation);
    for (int step = 0; step < 100; ++step) {
        int result = mc.Step();
        if (result != 0) {
            return result;
        }
    }
    ADD_FAILURE() << "operation never finished";
    return 0;
}

std::vector<char> ReadFile(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void WriteFile(const fs::path &path, const std::vector<char> &data) {
    std::ofstream(path, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
}

s32 &MapNoOf(CSaveData &save) {
    return *reinterpret_cast<s32 *>(reinterpret_cast<char *>(&save) + kSaveMapNoOffset);
}

s32 &LastFileNo() {
    return static_cast<s32 *>(g_save.GetConfigData())[17];
}

void PrepareSave() {
    g_save.Initialize();
    SaveData = &g_save;
    s16 *name = g_save.GetCharaName(0);
    for (int i = 0; i < 5; ++i) {
        name[i] = static_cast<s16>("Toan"[i]);
    }
    g_save.AddPlayTime(123456);
    g_save.QuestDungeon(0, 7);
    g_save.QuestDungeon(6, 3);
    MapNoOf(g_save) = 42;
    ASSERT_TRUE(g_mc.InitForMC() == 0);
    g_mc.SetBuff(g_menu_buffer);
}

SAVEDATA_INFO Save(int file_no) {
    SAVEDATA_INFO info = {};
    info.state = 1;
    info.file_no = file_no + 1;
    return info;
}

} // namespace

TEST(SaveSlots, SaveDataIsPodSized) {
    ASSERT_TRUE(sizeof(CSaveData) == kSaveDataSize);
}

TEST(SaveSlots, FileNamesAreTheGames) {
    ASSERT_EQ(SaveSlotFileNo("darkcloud0"), 0);
    ASSERT_EQ(SaveSlotFileNo("darkcloud12"), 12);
    ASSERT_EQ(SaveSlotFileNo("darkcloud123456789"), 123456789);
    for (const char *name : {"darkcloud", "darkcloud01", "darkcloud3.tmp", "darkcloud-1", "darkcloud1234567890",
                             "BESCES-50295dkcloud", "sysconfig.bin", "Darkcloud1"}) {
        ASSERT_EQ(SaveSlotFileNo(name), -1) << name;
    }
}

TEST(SaveSlots, BoardsListSavesThenTheNewOne) {
    // Files 0, 1, 3 and 5 hold saves; 2 is unreadable; 4 is free.
    SaveSlotList list;
    list.files = {0, 1, 2, 3, 5};
    list.saves = {Save(0), Save(1), Save(3), Save(5)};

    ASSERT_EQ(SaveSlotFirstFree({}), 0);
    ASSERT_EQ(SaveSlotFirstFree(list.files), 4);
    ASSERT_EQ(SaveSlotRows(list, true), 5);
    ASSERT_EQ(SaveSlotRows(list, false), 4);

    ASSERT_EQ(SaveSlotRow(list, 3, true), 2);
    ASSERT_EQ(SaveSlotFileAt(list, 2), 3);
    ASSERT_EQ(SaveSlotFileAt(list, 4), 4);

    // A file without a board of its own, unreadable or gone, is the new save on the save screen
    // and the first save on the load screen.
    for (int file_no : {2, 4, 200}) {
        ASSERT_EQ(SaveSlotRow(list, file_no, true), 4);
        ASSERT_EQ(SaveSlotRow(list, file_no, false), 0);
    }

    SaveSlotList empty;
    ASSERT_EQ(SaveSlotRows(empty, false), 0);
    ASSERT_EQ(SaveSlotRow(empty, 3, true), 0);
    ASSERT_EQ(SaveSlotFileAt(empty, 0), 0);
}

TEST(SaveSlots, SaveListAndLoadFlatFiles) {
    fs::path root = UseTempSaveRoot();
    PrepareSave();

    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_SEARCH_TYPE) == 1);
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_GET_DIR) == 1);
    ASSERT_TRUE(g_mc.card[0].present == 1 && g_mc.card[0].formatted == 1 && g_mc.card[0].dir_exists == 1);
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_GET_ALL_SAVE_FILE_INFO) == 1);
    ASSERT_TRUE(SaveSlots.files.empty() && SaveSlots.saves.empty());
    ASSERT_FALSE(g_mc.CheckFileNo(13));

    g_mc.file_no = 13;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_SAVE) == 1);
    std::vector<char> image = ReadFile(root / "darkcloud13");
    ASSERT_TRUE(image.size() == static_cast<std::size_t>(kImageSize));
    ASSERT_TRUE(std::memcmp(image.data(), g_mc.save_buffer, kImageSize) == 0);
    ASSERT_TRUE(std::strcmp(image.data() + kSaveDataSize, "darkcloudVer1.9") == 0);
    char total = 0;
    for (int i = 0; i < kSaveDataSize; ++i) {
        total = static_cast<char>(total + image[i]);
        if (i % 64 == 63) {
            ASSERT_TRUE(image[kChecksumOffset + i / 64] == total);
            total = 0;
        }
    }
    std::vector<char> config = ReadFile(root / "sysconfig.bin");
    ASSERT_TRUE(config.size() == kConfigSize + 4);
    ASSERT_TRUE(config[17] == 13 && config[18 + 17] == 13 && config[36 + 17] == 13);
    ASSERT_TRUE(LastFileNo() == 13);
    ASSERT_TRUE(g_mc.CheckFileNo(13));
    ASSERT_FALSE(fs::exists(root / "mc0"));
    ASSERT_FALSE(fs::exists(root / "darkcloud13.tmp"));

    // A save copied out of a card holds the same bytes.
    WriteFile(root / "darkcloud2", image);
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_GET_ALL_SAVE_FILE_INFO) == 1);
    ASSERT_TRUE((SaveSlots.files == std::vector<int>{2, 13}));
    ASSERT_TRUE(SaveSlots.saves.size() == 2);
    const SAVEDATA_INFO &info = SaveSlots.saves[1];
    ASSERT_TRUE(info.state == 1 && info.file_no == 14);
    ASSERT_TRUE(info.map_no == 42);
    ASSERT_TRUE(info.play_time == 123456.0f);
    ASSERT_TRUE(info.quest_total == 10);
    ASSERT_TRUE(std::memcmp(info.name, g_save.GetCharaName(0), 10) == 0);

    std::vector<char> expected(image.begin(), image.begin() + kSaveDataSize);
    g_save.AddPlayTime(99);
    MapNoOf(g_save) = 7;
    g_mc.file_no = 2;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_LOAD) == 1);
    reinterpret_cast<s32 *>(expected.data() + (static_cast<char *>(g_save.GetConfigData()) - reinterpret_cast<char *>(&g_save)))[17] = 2;
    ASSERT_TRUE(std::memcmp(&g_save, expected.data(), kSaveDataSize) == 0);

    // A flipped byte fails the checksum and leaves the save in memory alone.
    image[100] ^= 0x40;
    WriteFile(root / "darkcloud2", image);
    MapNoOf(g_save) = 9;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_LOAD) == -1);
    ASSERT_TRUE(MapNoOf(g_save) == 9);

    // A short file is not listed, but keeps its number from a new save.
    WriteFile(root / "darkcloud0", std::vector<char>(100, 'x'));
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_GET_ALL_SAVE_FILE_INFO) == 1);
    ASSERT_TRUE(SaveSlots.saves.size() == 2);
    ASSERT_TRUE(SaveSlotFirstFree(SaveSlots.files) == 1);

    g_mc.file_no = 2;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_DELETE) == 1);
    ASSERT_FALSE(fs::exists(root / "darkcloud2"));

    fs::remove_all(root);
}

TEST(SaveSlots, ConfigurationKeepsTheLastFileWhole) {
    fs::path root = UseTempSaveRoot();
    PrepareSave();

    // The card's image keeps the last save in a signed byte.
    g_mc.file_no = 200;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_SAVE) == 1);
    std::vector<char> config = ReadFile(root / "sysconfig.bin");
    ASSERT_TRUE(config[17] == static_cast<char>(200));
    LastFileNo() = 0;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_LOAD_CONFIG) == 1);
    ASSERT_TRUE(LastFileNo() == 200);

    // A configuration file copied out of a card has the byte alone.
    config[17] = config[18 + 17] = config[36 + 17] = 5;
    config.resize(kConfigSize);
    WriteFile(root / "sysconfig.bin", config);
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_LOAD_CONFIG) == 1);
    ASSERT_TRUE(LastFileNo() == 5);

    // With none, the game keeps its own.
    fs::remove(root / "sysconfig.bin");
    LastFileNo() = 8;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_LOAD_CONFIG) == 1);
    ASSERT_TRUE(LastFileNo() == 8);

    fs::remove_all(root);
}

TEST(SaveSlots, AnotherVersionStopsTheListing) {
    fs::path root = UseTempSaveRoot();
    PrepareSave();

    g_mc.file_no = 1;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_SAVE) == 1);
    std::vector<char> image = ReadFile(root / "darkcloud1");
    std::strcpy(image.data() + kSaveDataSize, "darkcloudVer1.0");
    WriteFile(root / "darkcloud4", image);

    // The save screen sees the error while the operation runs, and offers to delete the file.
    g_mc.SetFuncNo(MC_OPERATION_GET_ALL_SAVE_FILE_INFO);
    ASSERT_TRUE(g_mc.Step() == 0);
    ASSERT_TRUE(g_mc.error.code == MC_ERROR_VERSION && g_mc.error.file_no == 4);
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_DELETE) == 1);
    ASSERT_FALSE(fs::exists(root / "darkcloud4"));
    g_mc.error.code = MC_ERROR_NONE;
    ASSERT_TRUE(RunOperation(g_mc, MC_OPERATION_GET_ALL_SAVE_FILE_INFO) == 1);
    ASSERT_TRUE(SaveSlots.saves.size() == 1);

    fs::remove_all(root);
}
