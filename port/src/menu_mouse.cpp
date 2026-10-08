#include "menu_pointer.hpp"

#include "battlemenu.hpp"
#include "gamepad.hpp"
#include "menu_draw.hpp"
#include "menu_manual.hpp"
#include "menu_option.hpp"
#include "menu_mouse.hpp"
#include "name_mouse.hpp"
#include "platform/input.hpp"

// The mouse in the game's menus. Each tick, ahead of the game's read of the pad, the screen that is
// open (the Register Name screen, or a page of the pause menu) takes the mouse as a pointer: what
// the pointer is over becomes the game's own cursor, a click a pad press the game reads, so the
// game's code does the rest. A screen with no handler leaves the mouse to the pad as before.

namespace {

struct Rect {
    int left;
    int top;
    int right;
    int bottom;

    bool Contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};

// Which pause-menu screen is on, drawn since the last tick.
bool g_battle_drawn = false;

MenuPointer g_battle;

// How many icons the ring shows, as the game's GetMenuModeMax counts them.
int RingIconCount() {
    int count = BtlMenuMode == BATTLE_MENU_MODE_DUNGEON ? 7 : 8;
    if (GetGameFlagForManualMenu() == 0) {
        count--;
    }
    return count;
}

// The row of one ring icon: its icon and the label beside it, where the brackets stand.
Rect RingRow(int icon) {
    int left = static_cast<int>(NorMenuIcon[icon].x) - 18;
    int top = static_cast<int>(NorMenuIcon[icon].y) - 8;
    return Rect{left, top, left + 250, top + 34};
}

// The bar of icons that opens the pause menu.
void RingMouse(const MenuPointerTake &take) {
    if (MenuWarningMsgFlag != 0) {
        // A warning is up: any click dismisses it, as Cross and Circle do.
        if (take.clicked != 0) {
            MenuPointerPress(kInputCross);
        }
        return;
    }
    if (!take.pointing) {
        return;
    }
    int hit = -1;
    for (int icon = 0; icon < RingIconCount(); ++icon) {
        if (RingRow(icon).Contains(g_battle.x, g_battle.y)) {
            hit = icon;
        }
    }
    if (hit >= 0 && hit != MenuSelect[1] && (take.moved || (take.clicked & 1) != 0)) {
        MenuSelect[1] = hit;
        ComMenuSePlay(MENU_SOUND_CURSOR);
    }
    if ((take.clicked & 1) != 0) {
        if (hit >= 0) {
            MenuPointerPress(kInputCross);
        }
    } else if ((take.clicked & 2) != 0) {
        MenuPointerPress(kInputCircle);
    }
}

void BattleMouse() {
    if (!g_battle.IsOpen()) {
        g_battle.Open();
    }
    MenuPointerShow(&g_battle);
    MenuPointerTake take = g_battle.Take(SysCur[0], SysCur[1]);

    switch (BattleMenuFlag) {
        case BTLMENU_STATE_MAIN:
            RingMouse(take);
            break;
        default:
            // The page being opened or one without a handler: the mouse goes back to the pad.
            MenuPointerShow(nullptr);
            g_battle.Close();
            break;
    }
}

} // namespace

void MenuMouseNoteBattleMenu() { g_battle_drawn = true; }

void MenuMouseUpdate() {
    MenuPointerClearPresses();
    MenuPointerShow(nullptr);
    bool battle = g_battle_drawn;
    g_battle_drawn = false;

    if (MenuOptionOpen()) {
        // The Options screen takes the mouse for itself (options/screen.cpp).
        g_battle.Abandon();
        return;
    }
    if (NameMouseOpen()) {
        g_battle.Close();
        NameMouseUpdate();
        return;
    }
    NameMouseRelease();

    if (battle && BattleMenuFlag != BTLMENU_STATE_EXIT && BattleMenuFlag != BTLMENU_STATE_APPEAR) {
        BattleMouse();
    } else {
        g_battle.Close();
    }
}
