#include "menu_pointer.hpp"

#include <cmath>

#include "battlemenu.hpp"
#include "dngstatusdata.hpp"
#include "gamepad.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "menu_manual.hpp"
#include "menu_misc.hpp"
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

// A two-line yes-or-no plate drawn at (x, y) by DrawDngYesNoDialog: Yes is the first line.
// Returns 0 over Yes, 1 over No, -1 elsewhere.
int YesNoHit(int x, int y, float px, float py) {
    if (px < x || px >= x + 0x60) {
        return -1;
    }
    if (py >= y && py < y + 0x1C) {
        return 0;
    }
    if (py >= y + 0x1C && py < y + 0x3C) {
        return 1;
    }
    return -1;
}

// Hover and click on a yes-or-no plate whose answer the game keeps in *answer (0 is Yes).
void YesNoMouse(const MenuPointerTake &take, int x, int y, short *answer) {
    int hit = take.pointing ? YesNoHit(x, y, g_battle.x, g_battle.y) : -1;
    if (hit >= 0 && hit != *answer && take.moved) {
        *answer = static_cast<short>(hit);
        ComMenuSePlay(MENU_SOUND_CURSOR);
    }
    if ((take.clicked & 1) != 0 && hit >= 0) {
        *answer = static_cast<short>(hit);
        MenuPointerPress(kInputCross);
    } else if ((take.clicked & 2) != 0) {
        MenuPointerPress(kInputCircle);
    }
}

// Leave Dungeon and the other two-answer pages of the travel menu.
void MoveMouse(const MenuPointerTake &take) {
    if (MenuMove.state != MENU_MOVE_STATE_SELECT) {
        return;
    }
    switch (MenuMove.mode) {
        case MENU_MOVE_DUNGEON_ESCAPE:
        case MENU_MOVE_FIRST_DUNGEON:
        case MENU_MOVE_INTERIOR_OUT: {
            int y = MenuMove.mode == MENU_MOVE_DUNGEON_ESCAPE ? 0xDC : 0xD8;
            int cursor = MenuMove.cursor;
            short answer = static_cast<short>(cursor);
            YesNoMouse(take, 0x118, y, &answer);
            MenuMove.cursor = answer;
            break;
        }
        default:
            // The world map is a pad affair for now.
            if ((take.clicked & 2) != 0) {
                MenuPointerPress(kInputCircle);
            }
            break;
    }
}

// The turntable of party members. The one at the front is the one the page acts on; a click on any
// other turns it to the front, a wheel notch turns one place.
int g_allies_turns = 0;

Rect AlliesFront() {
    // DrawCharaSelect's frame round the front member.
    int left = static_cast<int>(394.0f - chara_r_long - 184.0f);
    int top = static_cast<int>(120.0f - 26.0f);
    return Rect{left, top, left + 0x102, top + 0x82};
}

Rect AlliesFace(int place) {
    float angle = 3.14159265f + (PosAngle * static_cast<float>(place));
    int   x = static_cast<int>(394.0f + chara_r_long * cosf(angle));
    int   y = static_cast<int>(120.0f + chara_r_long * sinf(angle));
    return Rect{x, y, x + 90, y + 90};
}

void AlliesMouse(const MenuPointerTake &take) {
    if (MenuChara.state != MENU_CHARA_SELECT) {
        if (MenuChara.state != MENU_CHARA_TURN) {
            g_allies_turns = 0;
        }
        return;
    }
    // One place a turn, as the d-pad turns it; Up and Right bring the one behind round, Down and Left the next.
    if (g_allies_turns != 0) {
        MenuPointerPress(g_allies_turns > 0 ? kInputUp : kInputDown);
        g_allies_turns += g_allies_turns > 0 ? -1 : 1;
        return;
    }
    if (!take.pointing) {
        return;
    }
    if (take.wheel != 0.0f) {
        g_allies_turns = take.wheel > 0.0f ? 1 : -1;
        return;
    }
    if ((take.clicked & 1) != 0) {
        if (AlliesFront().Contains(g_battle.x, g_battle.y)) {
            MenuPointerPress(kInputCross);
            return;
        }
        for (int i = 0; i < 6; ++i) {
            int place = SysChara[i].place;
            if (place != 0 && AlliesFace(place).Contains(g_battle.x, g_battle.y)) {
                // Up and Right move every place up one, so a member at place p needs 6 - p of them, or p Downs.
                g_allies_turns = place > 3 ? 6 - place : -place;
                return;
            }
        }
    } else if ((take.clicked & 2) != 0) {
        MenuPointerPress(kInputCircle);
    }
}

// The item page: the quick-use slots, the party member's panel, the personal board with its tabs,
// arrows and trash can, and the confirmation of a power-up. Each place the pointer can be over is
// where DrawMenuWaku puts the game's brackets for it (ItemMenuModeDraw).
const Rect kItemQuickSlot[3] = {
    {85,  102, 127, 136},
    {149, 102, 191, 136},
    {213, 102, 255, 136},
};
const Rect kItemWeapon = {68, 189, 110, 223};
const Rect kItemDefense = {112, 189, 154, 223};
const Rect kItemChara = {192, 188, 296, 292};
const Rect kItemBoard = {355, 124, 555, 284};
const Rect kItemTrash = {557, 270, 601, 304};
// The party arrows round the panel and the page arrows over the board.
const Rect kItemPartyLeft = {46, 150, 80, 184};
const Rect kItemPartyRight = {278, 150, 312, 184};
const Rect kItemPageLeft = {322, 38, 356, 72};
const Rect kItemPageRight = {552, 38, 586, 72};

Rect ItemPageTab(int page) {
    int left = 0x154 + 0xE + page * 0x44;
    return Rect{left, 72, left + 0x44, 120};
}

bool ItemPanelMode(int mode) {
    return mode == ITEM_MENU_QUICK_SLOTS || mode == ITEM_MENU_CHARA || mode == ITEM_MENU_WEAPON ||
           mode == ITEM_MENU_DEFENSE;
}

void ItemMouse(const MenuPointerTake &take) {
    if (ItemMenuMode.state == ITEM_MENU_STATE_POWERUP_CONFIRM) {
        YesNoMouse(take, 0x114, 0xE6, &ItemMenuMode.confirm);
        return;
    }
    if (ItemMenuMode.state != ITEM_MENU_STATE_IDLE) {
        if ((take.clicked & 2) != 0) {
            MenuPointerPress(kInputCircle);
        }
        return;
    }
    if (!take.pointing) {
        return;
    }
    float x = g_battle.x;
    float y = g_battle.y;
    PERSONAL_BOARD &board = ItemMenuMode.board;
    bool            clicked = (take.clicked & 1) != 0;
    bool            act = take.moved || clicked;

    // The place the pointer is over, as the mode and cursor the game would be in with its hand there.
    int mode = -1;
    int cursor = board.cursor;
    int area = board.cursor_area;
    int tab = -1;
    int arrow = 0;
    for (int i = 0; i < 3; ++i) {
        if (kItemQuickSlot[i].Contains(x, y)) {
            mode = ITEM_MENU_QUICK_SLOTS;
            cursor = i;
        }
    }
    if (kItemWeapon.Contains(x, y)) {
        mode = ITEM_MENU_WEAPON;
    } else if (kItemDefense.Contains(x, y)) {
        mode = ITEM_MENU_DEFENSE;
    } else if (kItemChara.Contains(x, y)) {
        mode = ITEM_MENU_CHARA;
    } else if (kItemBoard.Contains(x, y)) {
        int column = static_cast<int>((x - kItemBoard.left) / 40);
        int row = static_cast<int>((y - kItemBoard.top) / 40);
        int index = (board.top_row + row) * 5 + column;
        if (index < PersonalRetMax(board.page)) {
            mode = ITEM_MENU_BOARD;
            area = PERSONAL_BOARD_AREA_CELLS;
            cursor = index;
        }
    } else if (kItemTrash.Contains(x, y)) {
        mode = ITEM_MENU_BOARD;
        area = PERSONAL_BOARD_AREA_TRASH;
    }
    for (int i = 0; i < 3; ++i) {
        if (ItemPageTab(i).Contains(x, y)) {
            tab = i;
        }
    }
    if (BtlMenuStatusPt->party_size > 1 && ItemPanelMode(ItemMenuMode.mode)) {
        if (kItemPartyLeft.Contains(x, y)) {
            arrow = -1;
        } else if (kItemPartyRight.Contains(x, y)) {
            arrow = 1;
        }
    }
    if (!ItemPanelMode(ItemMenuMode.mode)) {
        if (kItemPageLeft.Contains(x, y)) {
            arrow = -1;
        } else if (kItemPageRight.Contains(x, y)) {
            arrow = 1;
        }
    }

    if (take.wheel != 0.0f) {
        // Over the board it scrolls; elsewhere it turns the page or the party member.
        bool forward = take.wheel < 0.0f;
        if (kItemBoard.Contains(x, y) && ItemMenuMode.mode == ITEM_MENU_BOARD) {
            MenuPointerPress(forward ? kInputDown : kInputUp);
        } else {
            MenuPointerPress(forward ? kInputR1 : kInputL1);
        }
        return;
    }

    if (mode >= 0 && act) {
        bool moved = mode != ItemMenuMode.mode || cursor != board.cursor ||
                     (mode == ITEM_MENU_BOARD && area != board.cursor_area);
        if (moved) {
            ItemMenuMode.mode = static_cast<short>(mode);
            board.cursor = cursor;
            board.cursor_area = area;
            ComMenuSePlay(MENU_SOUND_CURSOR);
        }
    }

    if (clicked) {
        if (mode >= 0) {
            MenuPointerPress(kInputCross);
        } else if (tab >= 0 || (arrow != 0 && !ItemPanelMode(ItemMenuMode.mode))) {
            // The page tabs and arrows work on the board's pages, as L1 and R1 do from the board.
            if (ItemPanelMode(ItemMenuMode.mode)) {
                ItemMenuMode.mode = ITEM_MENU_BOARD;
                board.cursor_area = PERSONAL_BOARD_AREA_CELLS;
                board.cursor = board.top_row * 5;
            }
            int steps = arrow != 0 ? arrow : (tab - board.page + 3) % 3 == 2 ? -1 : (tab - board.page + 3) % 3;
            if (steps > 0) {
                MenuPointerPress(kInputR1);
            } else if (steps < 0) {
                MenuPointerPress(kInputL1);
            }
        } else if (arrow != 0) {
            MenuPointerPress(arrow > 0 ? kInputR1 : kInputL1);
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
        case BTLMENU_STATE_ITEM:
            ItemMouse(take);
            break;
        case BTLMENU_STATE_CHARA:
            AlliesMouse(take);
            break;
        case BTLMENU_STATE_MOVE:
            MoveMouse(take);
            break;
        default:
            if (BattleMenuFlag >= BTLMENU_STATE_ITEM_OPEN && BattleMenuFlag <= BTLMENU_STATE_MANUAL_CLOSE) {
                // A page sliding in or out: the pointer stays where it is, and clicks wait for the page.
                break;
            }
            // A page without a handler: the mouse goes back to the pad.
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
