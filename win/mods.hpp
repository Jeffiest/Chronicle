#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

// Mod framework hooks (Windows fork). See mods.cpp. Plain types only: host code does not see the game headers.
void ModsNoteFile(const char *path);
// Called with a decoded texture before it reaches the renderer; may replace its pixels with a mod's PNG.
void ModsTextureHook(const char *name, int bpp, int block, unsigned width, unsigned height, bool &indexed, bool &has_alpha,
                     bool &four_bit, const uint32_t *palette, std::vector<std::vector<uint8_t>> &levels);

// Every mod folder under `root` (not starting with _ or .) in load order: the names in root/load_order.json {"order": [...]}
// come last, in that order (later mods win), after the unlisted ones in alphabetical order. Callers check mod.json "enabled".
// The mod manager (mod_manager.py) writes load_order.json.
std::vector<std::filesystem::path> ModsLoadOrder(const std::filesystem::path &root);

// Game data file overrides: mods/<mod>/files/<game path> replaces the game's file of that path (later mods win). `key` is the
// folded game path (lower case, '/'); `original` is the game's own file, or null when the game has none. Returns the mod's
// file or null. Used by the data reader (dataread.cpp Lookup).
const std::filesystem::path *ModsFileOverride(const char *key, const std::filesystem::path *original);

// Multiplayer colours (see ghost_win.cpp). The Toan poncho textures (c01d04, c01d05) are hue-shifted from orange to blue when the
// texture is loaded: always for the renamed copies a ghost uses for a blue player, and for the player's own textures while this is
// on (a guest sees themselves in blue). Takes effect the next time the game loads the player's model (entering a town or dungeon).
void ModsSetLocalTunicBlue(bool blue);

// Tunic colours (0 natural, 1..15 presets, 16 custom pictures). The player's own comes from the launcher's environment
// (DC_LAUNCH_TUNIC, DC_LAUNCH_TUNIC_FRONT / _BACK). ModsSetTunicCustom stores PNG bytes (any size, scaled to 256x256) for a ghost's digit.
int ModsLocalTunic();
bool ModsSetTunicCustom(char key, const void *front, size_t front_len, const void *back, size_t back_len);
