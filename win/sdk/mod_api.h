/* Dark Cloud (Chronicle Windows fork) native mod API, version 1.
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
 * All callbacks run on the game thread, once per game tick (50 Hz by default). Keep them short.
 */
#ifndef DC_MOD_API_H
#define DC_MOD_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_API_VERSION 1

#ifdef _WIN32
#define MOD_EXPORT __declspec(dllexport)
#else
#define MOD_EXPORT
#endif

/* Event passed to callbacks. i[] meaning depends on the event; where noted a callback may change it.
 *   "tick"          i[0] = tick count since launch
 *   "floor_change"  i[0] = dungeon, i[1] = floor, i[2] = previous dungeon, i[3] = previous floor
 *   "item_pickup"   i[0] = item id (changeable), i[1] = quantity (changeable)
 */
typedef struct ModEvent {
    const char *name;
    int32_t     i[4];
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
} ModHostApi;

#ifdef __cplusplus
}
#endif
#endif
