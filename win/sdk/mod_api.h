/* Dark Cloud (Chronicle Windows fork) native mod API, version 2.
 *
 * A native mod is a DLL at  mods/<mod>/plugin.dll  exporting
 *     int ModInit(const ModHostApi *api);      // return 0 on success
 * It is loaded only when BOTH are true:
 *     mods/mods.json   has {"allow_native": true}
 *     mods/<mod>/mod.json has {"native": true}
 * Native code runs with the full rights of the game: only install DLLs you trust.
 *
 * Build with any C/C++ compiler for x64 Windows, e.g.
 *     clang --target=x86_64-w64-mingw32 -shared -O2 -o plugin.dll my_mod.c
 * All callbacks run on the game thread, once per game tick (50 or 60 Hz). Keep them short.
 * Version 2 added: combat events, monster access, input and on-screen text. Check api->size before using a
 * function added after your header's version.
 */
#ifndef DC_MOD_API_H
#define DC_MOD_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_API_VERSION 2

#ifdef _WIN32
#define MOD_EXPORT __declspec(dllexport)
#else
#define MOD_EXPORT
#endif

/* Event passed to callbacks. i[] meaning depends on the event; "(changeable)" values may be edited by the callback.
 *   "init"            (no values) once, after all mods loaded
 *   "tick"            i[0] = tick count since launch
 *   "floor_change"    i[0] = dungeon, i[1] = floor, i[2] = previous dungeon, i[3] = previous floor
 *   "item_pickup"     i[0] = item id (changeable), i[1] = quantity (changeable)
 *   "monster_hit"     i[0] = monster index 0..15, i[1] = monster kind, i[2] = attacker (character 0..5, -1 none/item),
 *                     i[3] = damage about to be dealt (changeable), i[4] = element 0..4, 5 for none
 *   "monster_killed"  i[0] = monster index, i[1] = kind, i[2] = attacker. Drops/exp/money may still be edited here.
 *   "player_damage"   i[0] = character 0..5, i[1] = HP about to be lost, positive (changeable; 0 cancels)
 *   "player_heal"     i[0] = character, i[1] = HP about to be gained (changeable)
 */
typedef struct ModEvent {
    const char *name;
    int32_t     i[6];
} ModEvent;

typedef void (*ModEventFn)(void *user, ModEvent *event);

typedef struct ModHostApi {
    uint32_t    version;  /* MOD_API_VERSION the host implements */
    uint32_t    size;     /* sizeof(ModHostApi) in the host; never read past it */
    const char *mod_dir;  /* utf-8 path of this mod's folder */

    void (*log)(const char *message);
    /* Subscribe to an event by name. Returns 0 on success. */
    int (*on)(const char *event, ModEventFn fn, void *user);

    /* Party state. Character index 0..5; out of range or no game running returns 0 / is ignored. */
    int  (*get_hp)(int chara);
    void (*set_hp)(int chara, int value);
    int  (*get_max_hp)(int chara);
    void (*set_max_hp)(int chara, int value);
    float (*get_water)(int chara);
    void (*set_water)(int chara, float value);
    int  (*get_gilda)(void);
    void (*set_gilda)(int value);
    int  (*get_dungeon)(void);
    int  (*get_floor)(void);
    int  (*get_current_chara)(void);
    int  (*get_party_size)(void);
    /* Adds an item to the dungeon pack exactly like picking it up. Returns the game's result code. */
    int  (*give_item)(int item_id, int quantity);
    uint64_t (*get_tick)(void);

    /* ---- added in version 2 ---- */

    /* Monsters on the floor: index 0..15. Out of range, or no floor loaded, returns 0. */
    int  (*monster_count)(void);                 /* monsters alive */
    int  (*monster_alive)(int index);
    int  (*get_monster_hp)(int index);
    void (*set_monster_hp)(int index, int value);   /* clamped to 1..max: kill with damage, not by writing 0 */
    int  (*get_monster_max_hp)(int index);
    void (*set_monster_max_hp)(int index, int value);
    int  (*get_monster_kind)(int index);
    int  (*get_monster_defense)(int index);
    void (*set_monster_defense)(int index, int value);
    int  (*get_monster_drop)(int index);            /* item id dropped when defeated, -1 none */
    void (*set_monster_drop)(int index, int item_id);
    int  (*get_monster_money)(int index);
    void (*set_monster_money)(int index, int value);
    int  (*get_monster_exp)(int index);
    void (*set_monster_exp)(int index, int value);

    /* Input. `name` is an SDL key name ("F5", "Left Ctrl", "1") or a pad button
     * (l1 r1 l2 r2 l3 r3 triangle circle cross square select start up down left right),
     * several joined by '+' for a chord ("l1+r1+start", "Left Ctrl+F5"): true while all are held.
     * pressed = true only on the tick the chord became held. Pad 1 only. */
    int  (*key_down)(const char *name);
    int  (*key_pressed)(const char *name);

    /* On-screen text in the 640x480 frame, font about 6 by 8 units per `scale` (1 = small, 2 = normal), upper-case
     * letters, digits and .,:;/()+-_%'[]?~ only. rgba is 0xRRGGBBAA (0xFFFFFFFF white). An empty string removes
     * the text with that id. The same id replaces its text. Pick unique ids. */
    void (*text)(const char *id, const char *text, float x, float y, float scale, uint32_t rgba);
    /* A message at the top of the screen that disappears after `seconds`. */
    void (*toast)(const char *text, float seconds);
} ModHostApi;

#ifdef __cplusplus
}
#endif
#endif
