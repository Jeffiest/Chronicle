#include "memorycardaccess.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "dngstatusdata.hpp"
#include "save_slots.hpp"
#include "savedata.hpp"

// The port has no memory card: the operations read and write the flat files save_slots.hpp names.
// Each finishes in the step that starts it and returns 1, or -1 where retail's card would have
// failed, so the save screen's own flow and alerts still apply. The card the screens check is
// always there, formatted and roomy, and its save directory always exists.

namespace fs = std::filesystem;

namespace {

constexpr int kImageSize = 0x136A7;
constexpr int kVersionSize = 0x20;

// The whole save, read before any of it reaches SaveData.
alignas(64) char g_image[kImageSize];

// Retail's ((int)p >> 6) + 1) << 6 on the whole pointer: the next 64-byte boundary strictly after p.
char *PastNext64(char *pointer) {
    return pointer + (64 - reinterpret_cast<std::uintptr_t>(pointer) % 64);
}

std::size_t ReadFile(const fs::path &path, void *data, std::size_t size) {
    std::ifstream file(path, std::ios::binary);
    file.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
    return static_cast<std::size_t>(file.gcount());
}

// Written beside the file and renamed over it, so a failed write leaves the old file whole.
bool WriteFile(const fs::path &path, const void *data, std::size_t size) {
    fs::path        temp = path;
    std::error_code error;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
        if (!file.flush()) {
            file.close();
            fs::remove(temp, error);
            return false;
        }
    }
    fs::rename(temp, path, error);
    if (error) {
        fs::remove(temp, error);
        return false;
    }
    return true;
}

s32 &LastFileNo() {
    return static_cast<s32 *>(SaveData->GetConfigData())[17];
}

// The configuration file is the card's 0x40-byte image followed by the last save's whole number:
// the image keeps it in one signed byte, which holds only files 0 to 127.
bool WriteConfig() {
    char data[sizeof(SV_CONFIG_SYS) + sizeof(s32)];
    s32  file_no = LastFileNo();
    std::memcpy(data, &sys_config, sizeof(SV_CONFIG_SYS));
    std::memcpy(data + sizeof(SV_CONFIG_SYS), &file_no, sizeof(file_no));
    return WriteFile(SaveConfigPath(), data, sizeof(data));
}

bool VersionMatches(const char *image, const char *version) {
    return std::strncmp(image + sizeof(CSaveData), version, kVersionSize) == 0;
}

} // namespace

PC_OVERRIDE int CMemoryCardAccess::InitForMC() {
    this->Initialize();
    InitSaveFileInfoTbl();
    return 0;
}

PC_OVERRIDE void CMemoryCardAccess::SetBuff(char *buffer) {
    char *data;
    char *sum;
    u32   i;
    char  total;

    buffer = PastNext64(buffer);
    this->save_buffer = (CSaveData *) buffer;
    memcpy(this->save_buffer, SaveData, 0x131C0);
    this->save_buffer->ConvertConfig(&sys_config);
    char *version = (char *) this->save_buffer + 0x131C0;
    strcpy(version, this->version);
    this->check_sum = version + 0x20;
    data = (char *) this->save_buffer;
    sum = this->check_sum;
    memset(sum, 0, 0x4C7);
    total = 0;

    for (i = 0; i < 0x131C0; i++) {
        total += *data++;

        if ((int) i % 64 == 63) {
            *sum++ = total;
            total = 0;
        }
    }

    this->read_buffer = PastNext64(sum);
    this->load_buffer = this->read_buffer;
}

PC_OVERRIDE void CMemoryCardAccess::SetFuncNo(int func_no) {
    this->func_no = func_no;
    this->step = 0;

    if (func_no == MC_OPERATION_IDLE) {
        this->idle_code = 0x3D;
    }
}

PC_OVERRIDE int CMemoryCardAccess::SearchMcType() {
    MC_CARD_INFO *card = &this->card[this->port];

    card->present = 1;
    card->type = sceMcTypePS2;
    card->formatted = 1;
    card->format_change = 0;
    card->free_size = 8000;
    card->result = sceMcResSucceed;
    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::GetDir() {
    MC_CARD_INFO *card = &this->card[this->port];

    InitSaveFileInfoTbl();
    card->dir_exists = 1;
    card->dir_entries = static_cast<s32>(SaveSlotFiles().size());
    return 1;
}

// A missing or short file leaves the game's configuration as it is, as a card with no save
// directory did.
PC_OVERRIDE int CMemoryCardAccess::LoadSysConfig() {
    char        data[sizeof(SV_CONFIG_SYS) + sizeof(s32)];
    std::size_t size = ReadFile(SaveConfigPath(), data, sizeof(data));

    if (size < sizeof(SV_CONFIG_SYS)) {
        return 1;
    }

    std::memcpy(&sys_config, data, sizeof(SV_CONFIG_SYS));

    if (SaveData->InvertConfig(&sys_config) != 0 && size == sizeof(data)) {
        s32 file_no;
        std::memcpy(&file_no, data + sizeof(SV_CONFIG_SYS), sizeof(file_no));

        if (static_cast<s8>(file_no) == sys_config.values[17]) {
            LastFileNo() = file_no;
        }
    }

    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::SaveSysConfig() {
    return WriteConfig() ? 1 : -1;
}

// The card's write test and the copy out of the NTSC 1.0 directory have nothing to work on.
PC_OVERRIDE int CMemoryCardAccess::Write() {
    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::Convert() {
    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::MakeDir() {
    this->card[this->port].dir_exists = 1;
    return 1;
}

// Adds the file to SaveSlots when it is a whole save of this version. Returns 0 for a save of
// another version, with the error the save screen answers by offering to delete it.
PC_OVERRIDE int CMemoryCardAccess::GetSaveFileInfoFromMc(int file_no) {
    if (ReadFile(SaveSlotPath(file_no), g_image, kImageSize) < kImageSize) {
        return 1;
    }

    const char *version = g_image + sizeof(CSaveData);

    if (!VersionMatches(g_image, this->GetVersion())) {
        if (std::strncmp(version, this->file_name, std::strlen(this->file_name)) != 0) {
            return 1;
        }

        this->error.step = this->step;
        this->error.file_no = file_no;
        this->error.code = MC_ERROR_VERSION;
        return 0;
    }

    CSaveData    *save = reinterpret_cast<CSaveData *>(g_image);
    SAVEDATA_INFO info = {};
    info.state = 1;
    info.file_no = file_no + 1;
    std::memcpy(info.name, save->GetCharaName(0), sizeof(info.name));
    info.map_no = save->map_no;
    info.play_time = save->GetPlayTime();
    info.party_size = save->GetDngStatus()->GetPartySize();

    for (int dungeon = 0; dungeon < 7; dungeon++) {
        info.quest_total += save->QuestDungeon(dungeon, 0);
    }

    if (info.quest_total > 9999) {
        info.quest_total = 9999;
    }

    SaveSlots.saves.push_back(info);
    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::GetAllSaveFileInfo() {
    memset(this->file_info, 0, sizeof(this->file_info));
    SaveSlots.files = SaveSlotFiles();
    SaveSlots.saves.clear();

    for (int file_no : SaveSlots.files) {
        if (this->GetSaveFileInfoFromMc(file_no) == 0) {
            return 0;
        }
    }

    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::CheckFileNo(int file_no) {
    std::error_code error;
    return file_no >= 0 && fs::exists(SaveSlotPath(file_no), error);
}

PC_OVERRIDE int CMemoryCardAccess::SaveToMc(int file_no) {
    sys_config.values[17] = file_no;
    sys_config.values_copy1[17] = file_no;
    sys_config.values_copy2[17] = file_no;

    if (!WriteFile(SaveSlotPath(file_no), this->save_buffer, kImageSize)) {
        return -1;
    }

    LastFileNo() = file_no;
    this->card[this->port].dir_exists = 1;
    return WriteConfig() ? 1 : -1;
}

PC_OVERRIDE int CMemoryCardAccess::LoadFromMc(int file_no) {
    if (ReadFile(SaveSlotPath(file_no), g_image, kImageSize) < kImageSize || !VersionMatches(g_image, this->GetVersion())) {
        return -1;
    }

    const char *data = g_image;
    const char *sum = g_image + sizeof(CSaveData) + kVersionSize;
    char        total = 0;

    for (u32 i = 0; i < sizeof(CSaveData); i++) {
        total += *data++;

        if (i % 64 == 63) {
            if (total != *sum++) {
                return -1;
            }

            total = 0;
        }
    }

    memcpy(SaveData, g_image, sizeof(CSaveData));
    LastFileNo() = file_no;
    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::FormatForMc() {
    this->card[this->port].formatted = 1;
    return 1;
}

PC_OVERRIDE int CMemoryCardAccess::DeleteFile(int file_no) {
    std::error_code error;
    fs::remove(SaveSlotPath(file_no), error);
    return 1;
}

PC_OVERRIDE void CMemoryCardAccess::DmySync() {}

PC_OVERRIDE int CMemoryCardAccess::McUnFormatForDebug() {
    return 1;
}
