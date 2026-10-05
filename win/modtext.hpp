#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gfx/gfx.hpp"

// Text mods put on screen (Windows fork): lines in the 640x480 logical frame, drawn with the FPS counter's 5x7 font into
// the same overlay list. Plain types only: host code does not see the game headers.
struct ModTextLine {
    std::string   text;
    float         x = 0.0f;
    float         y = 0.0f;
    float         scale = 2.0f; // logical units per font pixel
    std::uint32_t rgba = 0xFFFFFFFFu;
    bool          backdrop = true; // the dark box behind the text; off for floating numbers

    bool operator==(const ModTextLine &) const = default;
};

// A filled rectangle in the same 640x480 logical frame (health bars, cooldown icons). Drawn under the text.
struct ModRect {
    float         x = 0.0f;
    float         y = 0.0f;
    float         w = 0.0f;
    float         h = 0.0f;
    std::uint32_t rgba = 0xFFFFFFFFu;

    bool operator==(const ModRect &) const = default;
};

// Replaces every line. The version changes only when the lines do.
void     ModTextSet(std::vector<ModTextLine> lines, std::vector<ModRect> rects = {});
unsigned ModTextVersion();
bool     ModTextAny();

// The FPS counter text (may be empty) and the mod lines, recorded into one display list for RenderOptions::overlay.
gfx::DisplayListRef ModsOverlayRecord(std::string_view fps_text);
