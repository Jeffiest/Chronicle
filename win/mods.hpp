#pragma once

#include <cstdint>
#include <vector>

// Mod framework hooks (Windows fork). See mods.cpp. Plain types only: host code does not see the game headers.
void ModsNoteFile(const char *path);
// Called with a decoded texture before it reaches the renderer; may replace its pixels with a mod's PNG.
void ModsTextureHook(const char *name, int bpp, int block, unsigned width, unsigned height, bool &indexed, bool &has_alpha,
                     bool &four_bit, const uint32_t *palette, std::vector<std::vector<uint8_t>> &levels);
