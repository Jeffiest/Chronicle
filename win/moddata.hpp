#pragma once

#include <filesystem>
#include <string>

// Game data tables from mods/<mod>/data/*.json (Windows fork). See moddata.cpp and win-save/mods/DATA.md.
void ModDataApply(const std::filesystem::path &mod_dir, const std::string &mod); // in mod load order, before the "init" event
void ModDataDump(const std::filesystem::path &out_dir);                          // DC_DUMP_DATA=1
