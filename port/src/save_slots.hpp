#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "memorycardaccess.hpp"

// The port keeps no memory card. Each save is <save root>/darkcloudN, the image retail writes to
// the card as darkcloudN, and the card's configuration file is <save root>/sysconfig.bin.

std::filesystem::path SaveSlotPath(int file_no);

std::filesystem::path SaveConfigPath();

// The N of a file named darkcloudN, or -1 for any other name.
int SaveSlotFileNo(std::string_view name);

// The N of every darkcloudN in the save root, ascending.
std::vector<int> SaveSlotFiles();

// The lowest N that ascending files does not hold.
int SaveSlotFirstFree(const std::vector<int> &files);

// What the save and load screens list.
struct SaveSlotList {
    std::vector<int>           files; // Every save file, readable or not.
    std::vector<SAVEDATA_INFO> saves; // The readable ones, ascending by file.
};

extern SaveSlotList SaveSlots;

// The screens' boards: one per readable save and, on the save screen, a last one for a new save.
int SaveSlotRows(const SaveSlotList &list, bool new_save);

// The board showing file_no; a file with no board of its own gives the new save's board, or the
// first one.
int SaveSlotRow(const SaveSlotList &list, int file_no, bool new_save);

// The file a board stands for; the new save's board stands for the first free file.
int SaveSlotFileAt(const SaveSlotList &list, int row);
