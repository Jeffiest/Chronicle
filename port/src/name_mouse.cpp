#include "name_mouse.hpp"

#include <algorithm>
#include <cstddef>


#include "gamepad.hpp"
#include "gfx/gfx.hpp"
#include "menu_draw.hpp"
#include "name_mouse_layout.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"

// NAME_SELECT of ps2/src/battle_globals.cpp, which keeps the struct and its enums to itself.
struct NameSelectView {
    short chara_no;
    short area;
    short name_pos;
    short side_row;
    short pushed_tab;
    short input_mode;
    int   cursor;
    short state;
    int   state_count;
    float cursor_x;
    float cursor_y;
    int   frame;
    short language;
    short texture_block;
    short loaded;
};

static_assert(sizeof(NameSelectView) == 0x2C);
static_assert(offsetof(NameSelectView, cursor) == 12 && offsetof(NameSelectView, state) == 16);
static_assert(offsetof(NameSelectView, cursor_x) == 24 && offsetof(NameSelectView, language) == 36);

extern NameSelectView NameSelect;
extern short          menu_euro_codetbl[5][2][13];
extern signed char    menu_kigoutbl[40];
extern signed char    euro_code_linelimmit[5];

namespace {

using namespace namemouse;

// NameSelectState.
constexpr int kStateIdle = 0;
constexpr int kStateExit = 3;
constexpr int kStateTabFlash = 6;
constexpr int kStateHelp = 8;

constexpr int kLanguageBritish = 2;

// Where the game's hand rests against the control it is on: the fingertip, as the Options screen's
// pointer takes it.
constexpr float kFingerX = 26.0f;
constexpr float kFingerY = 14.0f;

// Held pad buttons that hand the screen back to the pad.
constexpr std::uint16_t kPadButtons = kInputUp | kInputDown | kInputLeft | kInputRight | kInputCross | kInputCircle |
                                      kInputSquare | kInputTriangle | kInputL1 | kInputR1 | kInputL2 | kInputR2 |
                                      kInputStart;
constexpr int kStickThreshold = 120;

struct State {
    bool          active = false;
    bool          pointing = false;
    float         x = 0.0f;
    float         y = 0.0f;
    std::uint32_t buttons = 0;
    int           synthetic = 0;
    // What the pointer was last over, with the screen it was over it on.
    Hit           hovered;
    int           hovered_mode = -1;
    int           hovered_language = -1;
};

State g_state;

bool PadHeld() {
    const InputPadState &pad = InputGetPad(0);
    if ((pad.buttons & kPadButtons) != 0) {
        return true;
    }
    int x = AxisCalibration(pad.left_x);
    int y = AxisCalibration(pad.left_y);
    return x > kStickThreshold || x < -kStickThreshold || y > kStickThreshold || y < -kStickThreshold;
}

// 640x480 units per count of mouse motion along each axis, through the mapping the 2D is drawn with.
void UnitsPerCount(float &across, float &down) {
    gfx::LogicalMapping mapping = gfx::GetUiMapping(gfx::kMainTarget);
    int                 width = 0;
    int                 height = 0;
    // With no window the counts are target pixels, as a script gives them.
    if (!WindowSize(width, height)) {
        width = static_cast<int>(mapping.pixel_width);
        height = static_cast<int>(mapping.pixel_height);
    }
    across = PointerUnitsPerCount(mapping.scale_x, static_cast<float>(mapping.pixel_width), static_cast<float>(width));
    down = PointerUnitsPerCount(mapping.scale_y, static_cast<float>(mapping.pixel_height), static_cast<float>(height));
    if (across <= 0.0f || down <= 0.0f) {
        across = down = 1.0f;
    }
}

Screen CurrentScreen() {
    Screen screen;
    screen.language = NameSelect.language;
    screen.input_mode = NameSelect.input_mode;
    screen.symbol_cells = menu_kigoutbl;
    if (NameSelect.language > kLanguageBritish && NameSelect.language < 7) {
        screen.euro_codes = &menu_euro_codetbl[NameSelect.language - 2][0][0];
        screen.euro_rows = euro_code_linelimmit[NameSelect.language - 2];
    }
    return screen;
}

// Puts the game's cursor on a key or tab, and tells whether that moved it.
bool PutCursorOn(const Hit &hit) {
    int area = hit.kind == Kind::Tab ? kAreaTabs : kAreaKeyboard;
    if (NameSelect.area == area && NameSelect.cursor == hit.index && NameSelect.side_row == 0) {
        return false;
    }
    NameSelect.area = static_cast<short>(area);
    NameSelect.cursor = hit.index;
    NameSelect.side_row = 0;
    return true;
}

} // namespace

// The screen is closed: the mouse goes back to pressing the pad's buttons, once no button of it is
// held, since a held button would otherwise be a press of the next screen.
static void NameMouseClose() {
    g_state.synthetic = 0;
    if (g_state.active) {
        g_state.pointing = false;
        if (InputTakeMenuMouse().buttons == 0) {
            g_state.active = false;
            InputSetMenuMouse(false);
        }
    }
}

int NameMouseSyntheticDown() { return g_state.synthetic; }

void NameMouseUpdate() {
    g_state.synthetic = 0;

    // Open from the moment its textures are read until the game closes it, which leaves the state EXIT
    // (and the state stays so until the next InitNameRegist).
    if (NameSelect.loaded == 0 || NameSelect.state == kStateExit) {
        NameMouseClose();
        return;
    }

    if (!g_state.active) {
        // What is held as the screen opens (the click that ended the book) acts only once let go.
        g_state.active = true;
        g_state.pointing = false;
        InputSetMenuMouse(true);
        g_state.buttons = InputTakeMenuMouse().buttons;
    }

    InputMenuMouse mouse = InputTakeMenuMouse();
    std::uint32_t  clicked = mouse.buttons & ~g_state.buttons;
    bool           moved = mouse.dx != 0.0f || mouse.dy != 0.0f;
    g_state.buttons = mouse.buttons;

    // Only while the screen takes input: not fading in or out.
    bool taking = (NameSelect.state == kStateIdle || NameSelect.state == kStateTabFlash || NameSelect.state == kStateHelp);
    if (!taking) {
        return;
    }

    if (PadHeld()) {
        g_state.pointing = false;
    }
    if (moved || clicked != 0 || mouse.wheel != 0.0f) {
        if (!g_state.pointing) {
            g_state.pointing = true;
            g_state.x = NameSelect.cursor_x + kFingerX;
            g_state.y = NameSelect.cursor_y + kFingerY;
            g_state.hovered = Hit{};
            g_state.hovered_mode = -1;
        }
        float across = 1.0f;
        float down = 1.0f;
        UnitsPerCount(across, down);
        g_state.x = std::clamp(g_state.x + mouse.dx * across, 0.0f, 639.0f);
        g_state.y = std::clamp(g_state.y + mouse.dy * down, 0.0f, 479.0f);
    }
    if (!g_state.pointing) {
        return;
    }

    int area = NameSelect.area;
    if (area >= kAreaHelp) {
        // A message is up. The confirm dialog's Yes and No are its Cross and Circle; the others close
        // on a click anywhere in their window.
        bool inside = DialogRect(area).Contains(g_state.x, g_state.y);
        if (area == kAreaConfirm) {
            int choice = ConfirmChoice(g_state.x, g_state.y);
            if ((clicked & 1) != 0 && choice != 0) {
                g_state.synthetic |= choice > 0 ? kInputCross : kInputCircle;
            }
        } else if ((clicked & 1) != 0 && inside) {
            g_state.synthetic |= kInputCross;
        }
        if ((clicked & 2) != 0 && g_state.synthetic == 0) {
            g_state.synthetic |= kInputCircle;
        }
        return;
    }

    Screen screen = CurrentScreen();
    Hit    hit = HitTest(screen, g_state.x, g_state.y);

    if (mouse.wheel != 0.0f) {
        // Over the name the wheel moves the text cursor, elsewhere it turns the keyboard tabs. Away
        // from the user goes left or back, as the pad's L1 and L2 do.
        bool forward = mouse.wheel < 0.0f;
        if (hit.kind == Kind::Slot) {
            g_state.synthetic |= forward ? kInputR1 : kInputL1;
        } else {
            g_state.synthetic |= forward ? kInputR2 : kInputL2;
        }
        return;
    }

    bool changed = hit != g_state.hovered || screen.input_mode != g_state.hovered_mode ||
                   screen.language != g_state.hovered_language;
    g_state.hovered = hit;
    g_state.hovered_mode = screen.input_mode;
    g_state.hovered_language = screen.language;

    if ((hit.kind == Kind::Key || hit.kind == Kind::Tab) && (changed || (clicked & 1) != 0)) {
        if (PutCursorOn(hit)) {
            ComMenuSePlay(MENU_SOUND_CURSOR);
        }
    }

    if ((clicked & 1) != 0) {
        if (hit.kind == Kind::Key || hit.kind == Kind::Tab) {
            g_state.synthetic |= kInputCross;
        } else if (hit.kind == Kind::Slot && NameSelect.name_pos != hit.index) {
            NameSelect.name_pos = static_cast<short>(hit.index);
            ComMenuSePlay(MENU_SOUND_CONFIRM);
        }
    } else if ((clicked & 2) != 0) {
        g_state.synthetic |= kInputCircle;
    }
}

