#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "gametext.hpp"
#include "options/rows.hpp"
#include "platform/config.hpp"

class CTexture;

namespace options {

// Where the screen draws on the game's 640x480 2D screen.
constexpr int kTabY = 96;
constexpr int kTabGap = 24;
constexpr int kTabsRight = 532;
constexpr int kTabArrow = 14;
constexpr int kRowY = 138;
constexpr int kRowStep = 25;
constexpr int kVisibleRows = 8;
constexpr int kLabelX = 138;
constexpr int kValueX = 374;
constexpr int kValueRight = 554;
constexpr int kBarX = 580;
constexpr int kExitX = 136;
constexpr int kExitY = 352;
constexpr int kExitWidth = 60;
constexpr int kExitHeight = 32;
constexpr int kHelpY = kExitY + 44;

// The cursor on the page names, a row, or EXIT (the page's row count).
constexpr int kOnTabs = -1;

enum class Glyph {
    None,
    L1,
    R1,
    Up,
    Down,
};

struct Screen {
    bool        open = false;
    int         mode = 0;
    int         block_no = 0;
    int         step = 0;
    int         step_count = 0;
    bool        texture_ready = false;
    CTexture   *texture = nullptr;
    int         page = 0;
    int         row = 0;
    int         first_tab = 0;
    std::vector<int> first_row;
    float       cursor_x = 0.0f;
    float       cursor_y = 0.0f;
    int         bar_frames = 0;
    Config      opened;
    bool        save_failed = false;
    std::string display_now;
    short      *game_messages = nullptr;
    int         held[16] = {};
    bool        pointing = false;
    float       pointer_x = 0.0f;
    float       pointer_y = 0.0f;
    std::uint32_t mouse_buttons = 0;
    Glyph       glyph = Glyph::None;
    // The binding row listening for a new key, or -1, and what its value shows meanwhile.
    int         binding_row = -1;
    std::string binding_prompt;
    // The source just bound, held until it is let go so its press does not act on the new binding.
    std::string binding_wait;
};

extern Screen g_screen;

struct Texts {
    std::deque<GameText>              tabs;
    std::deque<std::deque<GameText>> labels;
    std::deque<std::deque<GameText>> values;
    GameText                           left;
    GameText                           right;
    GameText                           l1;
    GameText                           r1;
    GameText                           shortcuts;
    GameTextFile                       help;

    Texts();
};

Texts &GetTexts();

const Page &CurrentPage();
int         RowCount(int page);
int         FirstRow();
int         RowTop(int row);
bool        Scrolls(int page);
int         TabX(int tab);
int         LastVisibleTab();
int         L1X();
int         R1X();

int  Open(int mode, int block_no, u_long128 *buffer);
int  Run();
void Draw();

} // namespace options
