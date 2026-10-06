// Mod framework phases 3 and 4: Lua 5.4 scripts and native plugins (Windows fork).
//
//   mods/<mod>/scripts/main.lua   sandboxed Lua script (needs Lua in win-deps, see setup_lua.ps1)
//   mods/<mod>/plugin.dll         native plugin, only with mods/mods.json {"allow_native":true} and mod.json {"native":true}
// Events: init, tick, floor_change, item_pickup. See win-save/mods/SCRIPTING.md for the Lua API.

#include "script.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <array>
#include <set>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Lua first: the game headers define one-letter macros (B, ...) that break its prototypes.
#ifdef DC_HAVE_LUA
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#endif

#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "camera.hpp"
#include "camerafollow.hpp"
#include "btactstatus.hpp"
#include "gamepad.hpp"
#include "edit.hpp"
#include "editground.hpp"
#include "editmapscript.hpp"
#include "editloop.hpp"
#include "npcharacter.hpp"
#include "memcard.hpp"
#include "memorycardaccess.hpp"
#include "mainitemmodel.hpp"
#include "itemdata.hpp"
#include "mglib.hpp"
#include "savedata.hpp"
#include "shop.hpp"
#include "mod_api.h"
#include "moddata.hpp"
#include "platform/mods.hpp"
#include "monstorunit.hpp"
#include "platform/input.hpp"
#include "platform/modtext.hpp"
#include "platform/net.hpp"
#include "gfx/gfx.hpp"
#include "platform/window.hpp"
#include "platform/mouse.hpp"
#include "ghost.hpp"
#include "guestworld.hpp"
#include "nativeui.hpp"
#include "platform/clock.hpp"
#include "platform/paths.hpp"
#include "userstatus.hpp"

namespace fs = std::filesystem;

extern int gameTask;               // the dungeon's task state (gameloop.cpp)
void       PlayTimeCountFlag(int flag); // the play clock

extern s32 itemNowSel; // the dungeon active-item slot (1..3), defined in dun/gameloop.cpp
void EditSave(); // editloop.cpp: the town's state into the save data, as the save menu does first
extern s32 MapNo; // the game's current map (a town below 200, a dungeon at 200 and up)

int L_first_person(lua_State *L); // defined with the camera code below
int L_town_chara(lua_State *L);

namespace {

constexpr int kInstructionLimit = 2000000; // per callback; a runaway loop is stopped and the mod disabled
constexpr int kHookStep = 1000;

struct Listener {
    std::string event;
    ModEventFn  fn;
    void       *user;
    std::string mod;
};

#ifdef DC_HAVE_LUA
struct LuaMod {
    std::string                        name;
    fs::path                           dir;
    lua_State                         *L = nullptr;
    bool                               dead = false;
    std::map<std::string, std::vector<int>> handlers; // event -> registry refs
    std::map<std::string, std::vector<int>> msg_handlers; // dc.msg_on(name, fn): messages between mods
    nlohmann::json                     store = nlohmann::json::object(); // dc.store_get/store_set, kept per save slot
    bool                               store_dirty = false;
};
#endif

struct State {
    bool                   inited = false;
    bool                   allow_native = false;
    fs::path               root;
    uint64_t               ticks = 0;
    int                    prev_dungeon = -1000;
    int                    prev_floor = -1000;
    std::vector<Listener>  listeners;
    std::string            current_mod; // native mod being initialised
    std::vector<std::string> native_dirs;
    // input edges and on-screen text
    std::map<std::string, bool>        input_prev;
    std::map<std::string, ModTextLine> texts;
    struct RectEntry { ModRect r; int z = 0; uint64_t seq = 0; };
    std::map<std::string, RectEntry>   rects;
    uint64_t                           rect_seq = 0;
    int                                slot = -1;           // save slot of the last load or save, -1 before either
    struct FloorSelectState { int dungeon = -1, selected = 0, top = 0, count = 0; float list_y = 0; uint64_t seen_tick = 0; };
    FloorSelectState                   floor_select;        // the dungeon entrance menu, reported by the game each frame it is open
    int                                hurt[16] = {};       // damage queued by dc.hurt_monster, taken in CheckDmg
    int                                hurt_owner[16] = {};
    bool                               frozen = false;      // dc.freeze: world held, game input hidden
    unsigned short                     block_buttons = 0;   // dc.block_input
    bool                               block_sticks = false;
    void                              *freeze_owner = nullptr;
    struct FloorSize { int rooms = 0; float mult = 1.0f; int bonus = 0; }; // dc.set_floor_size
    FloorSize                          floor_default;       // for every dungeon
    FloorSize                          floor_dungeon[7];    // and per dungeon, which wins when it was set
    bool                               floor_dungeon_set[7] = {};
    nlohmann::json                     shared = nlohmann::json::object(); // dc.shared_get/shared_set: settings every mod can read, in mods/_shared.json
    bool                               shared_loaded = false;
    bool                               cam_active = false; // dc.camera: the town camera is held where a script put it
    float                              cam_eye[4] = {0, 0, 0, 1};
    float                              cam_ref[4] = {0, 0, 0, 1};
    bool                               loaded_pending = false;
    uint64_t                           loaded_at = 0;
    struct Toast { std::string text; uint64_t expire_ms; };
    std::vector<Toast>                 toasts;
    bool                               texts_dirty = false;
#ifdef DC_HAVE_LUA
    std::vector<std::unique_ptr<LuaMod>> lua;
    std::map<lua_State *, LuaMod *>      by_state;
    int                                  instructions = 0;
#endif
};

State g;

std::string Utf8(const fs::path &p) {
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string Fold(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

void Log(const std::string &mod, const std::string &text) {
    std::fprintf(stderr, "[mod %s] %s\n", mod.c_str(), text.c_str());
}

nlohmann::json ReadJson(const fs::path &file) {
    std::ifstream in(file);
    if (!in) {
        return nlohmann::json::object();
    }
    auto json = nlohmann::json::parse(in, nullptr, false, true);
    if (json.is_discarded() || !json.is_object()) {
        std::fprintf(stderr, "mods: %s is not valid JSON; ignoring it\n", Utf8(file).c_str());
        return nlohmann::json::object();
    }
    return json;
}

// ---- Game state accessors shared by Lua and native plugins ----------------------------------------------------------

bool Ok(int chara) { return UserStatus != nullptr && chara >= 0 && chara < 6; }

int  ApiGetHp(int c) { return Ok(c) ? UserStatus->hp[c] : 0; }
void ApiSetHp(int c, int v) {
    if (Ok(c)) {
        int cap = UserStatus->max_hp[c];
        v = std::clamp(v, 0, cap > 0 ? cap : 32767);
        UserStatus->hp[c] = static_cast<s16>(v);
        UserStatus->next_hp[c] = static_cast<s16>(v); // keep the life gauge from sliding back to its old target
        UserStatus->life_step[c] = 0;
    }
}
int  ApiGetMaxHp(int c) { return Ok(c) ? UserStatus->max_hp[c] : 0; }
void ApiSetMaxHp(int c, int v) {
    if (Ok(c)) {
        UserStatus->max_hp[c] = static_cast<s16>(std::clamp(v, 1, 32767));
    }
}
float ApiGetWater(int c) { return Ok(c) ? UserStatus->water_now[c] : 0.0f; }
void  ApiSetWater(int c, float v) {
    if (Ok(c)) {
        UserStatus->water_now[c] = std::clamp(v, 0.0f, UserStatus->water_max[c]);
    }
}
int  ApiGetGilda() { return UserStatus != nullptr ? static_cast<int>(UserStatus->money) : 0; }
void ApiSetGilda(int v) {
    if (UserStatus != nullptr) {
        UserStatus->money = static_cast<u16>(std::clamp(v, 0, 65535));
    }
}
int ApiGetDungeon() { return UserStatus != nullptr ? UserStatus->cur_georama : -1; }
int ApiGetFloor() { return UserStatus != nullptr ? UserStatus->cur_floor : -1; }
int ApiGetChara() { return UserStatus != nullptr ? UserStatus->cur_chara : -1; }
int ApiGetParty() { return UserStatus != nullptr ? UserStatus->party_size : 0; }
int ApiGiveItem(int id, int qty) {
    if (UserStatus == nullptr || id < 0 || id > 1000) {
        return 0;
    }
    return ((CDngStatusData *) UserStatus)->GetItem(id, qty);
}
uint64_t ApiGetTick() { return g.ticks; }


// ---- Monsters --------------------------------------------------------------------------------------------------------

bool MOk(int i) { return NowMonstorUnit != nullptr && i >= 0 && i < 16; }
int  ApiMonsterCount() { return NowMonstorUnit != nullptr ? NowMonstorUnit->alive_count : 0; }
int  ApiMonsterAlive(int i) { return MOk(i) && NowMonstorUnit->monster[i].state == 2 && NowMonstorUnit->monster[i].hp > 0; }
int  ApiGetMHp(int i) { return MOk(i) ? NowMonstorUnit->monster[i].hp : 0; }
void ApiSetMHp(int i, int v) {
    if (MOk(i) && NowMonstorUnit->monster[i].hp > 0) { // writing zero would skip the game's kill handling
        NowMonstorUnit->monster[i].hp = std::clamp(v, 1, std::max(1, static_cast<int>(NowMonstorUnit->monster[i].max_hp)));
    }
}
int  ApiGetMMax(int i) { return MOk(i) ? NowMonstorUnit->monster[i].max_hp : 0; }
void ApiSetMMax(int i, int v) {
    if (MOk(i)) {
        NowMonstorUnit->monster[i].max_hp = std::clamp(v, 1, 1000000);
    }
}
int  ApiGetMKind(int i) { return MOk(i) ? NowMonstorUnit->monster[i].kind : -1; }
int  ApiGetMDef(int i) { return MOk(i) ? NowMonstorUnit->monster[i].defense : 0; }
void ApiSetMDef(int i, int v) {
    if (MOk(i)) {
        NowMonstorUnit->monster[i].defense = static_cast<s16>(std::clamp(v, 0, 32767));
    }
}
int  ApiGetMDrop(int i) { return MOk(i) ? NowMonstorUnit->monster[i].drop_item : -1; }
void ApiSetMDrop(int i, int v) {
    if (MOk(i)) {
        NowMonstorUnit->monster[i].drop_item = static_cast<s16>(std::clamp(v, -1, 1000));
    }
}
int  ApiGetMMoney(int i) { return MOk(i) ? NowMonstorUnit->monster[i].money : 0; }
void ApiSetMMoney(int i, int v) {
    if (MOk(i)) {
        NowMonstorUnit->monster[i].money = std::clamp(v, 0, 65535);
    }
}
int  ApiGetMExp(int i) { return MOk(i) ? NowMonstorUnit->monster[i].exp : 0; }
void ApiSetMExp(int i, int v) {
    if (MOk(i)) {
        NowMonstorUnit->monster[i].exp = std::clamp(v, 0, 1000000);
    }
}

// ---- Input -----------------------------------------------------------------------------------------------------------

struct PadName { const char *name; uint16_t bit; };
const PadName kPadNames[] = {
    {"l2", kInputL2}, {"r2", kInputR2}, {"l1", kInputL1}, {"r1", kInputR1}, {"triangle", kInputTriangle},
    {"circle", kInputCircle}, {"cross", kInputCross}, {"square", kInputSquare}, {"select", kInputSelect},
    {"l3", kInputL3}, {"r3", kInputR3}, {"start", kInputStart}, {"up", kInputUp}, {"right", kInputRight},
    {"down", kInputDown}, {"left", kInputLeft}};

bool TokenDown(std::string token) {
    while (!token.empty() && token.front() == ' ') token.erase(token.begin());
    while (!token.empty() && token.back() == ' ') token.pop_back();
    if (token.empty()) {
        return false;
    }
    std::string low = Fold(token);
    for (const PadName &p : kPadNames) {
        if (low == p.name) {
            return (InputGetPadRaw(0).buttons & p.bit) != 0; // what the player holds, not what the game is shown
        }
    }
    SDL_Scancode code = SDL_GetScancodeFromName(token.c_str());
    if (code == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    const bool *state = SDL_GetKeyboardState(nullptr);
    return state != nullptr && state[code];
}

bool ChordDown(const std::string &chord) {
    size_t at = 0;
    bool   any = false;
    while (at <= chord.size()) {
        size_t plus = chord.find('+', at);
        if (plus == at && at + 1 < chord.size() && chord[at] == '+') { // a lone "+" key is not supported: treat as separator
            at++;
            continue;
        }
        std::string token = chord.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
        if (!token.empty()) {
            any = true;
            if (!TokenDown(token)) {
                return false;
            }
        }
        if (plus == std::string::npos) {
            break;
        }
        at = plus + 1;
    }
    return any;
}

int ApiKeyDown(const char *name) {
    if (name == nullptr) {
        return 0;
    }
    bool now = ChordDown(name);
    g.input_prev.emplace(name, false); // tracked from now on, so the edge is right next tick
    return now;
}

int ApiKeyPressed(const char *name) {
    if (name == nullptr) {
        return 0;
    }
    bool now = ChordDown(name);
    auto it = g.input_prev.emplace(name, false).first;
    return now && !it->second;
}

void UpdateInputEdges() {
    for (auto &entry : g.input_prev) {
        entry.second = ChordDown(entry.first);
    }
}

// ---- On-screen text --------------------------------------------------------------------------------------------------

void ApiTextEx(const char *id, const char *text, float x, float y, float scale, uint32_t rgba, bool backdrop);
void ApiText(const char *id, const char *text, float x, float y, float scale, uint32_t rgba) {
    ApiTextEx(id, text, x, y, scale, rgba, true);
}

void ApiRect(const char *id, float x, float y, float w, float h, uint32_t rgba, int z) {
    if (id == nullptr) {
        return;
    }
    if (w <= 0.0f || h <= 0.0f) {
        g.rects.erase(id);
    } else {
        auto &e = g.rects[id];
        if (e.seq == 0) {
            e.seq = ++g.rect_seq; // draw order: z, then the order the rectangle was first made
        }
        e.z = z;
        e.r.x = x;
        e.r.y = y;
        e.r.w = w;
        e.r.h = h;
        e.r.rgba = rgba;
    }
    g.texts_dirty = true;
}

void ApiTextEx(const char *id, const char *text, float x, float y, float scale, uint32_t rgba, bool backdrop) {
    if (id == nullptr) {
        return;
    }
    if (text == nullptr || text[0] == '\0') {
        g.texts.erase(id);
    } else {
        ModTextLine line;
        line.text = text;
        line.x = x;
        line.y = y;
        line.scale = scale > 0.0f ? scale : 2.0f;
        line.rgba = rgba;
        line.backdrop = backdrop;
        g.texts[id] = std::move(line);
    }
    g.texts_dirty = true;
}

void ApiToast(const char *text, float seconds) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    if (g.toasts.size() >= 5) {
        g.toasts.erase(g.toasts.begin());
    }
    g.toasts.push_back({text, SDL_GetTicks() + static_cast<uint64_t>(std::max(0.5f, seconds) * 1000.0f)});
    g.texts_dirty = true;
}

void FlushText() {
    uint64_t now = SDL_GetTicks();
    size_t   before = g.toasts.size();
    std::erase_if(g.toasts, [now](const State::Toast &t) { return t.expire_ms <= now; });
    if (!g.texts_dirty && before == g.toasts.size()) {
        return;
    }
    g.texts_dirty = false;
    std::vector<ModTextLine> lines;
    for (auto &entry : g.texts) {
        lines.push_back(entry.second);
    }
    float y = 28.0f;
    for (const State::Toast &t : g.toasts) {
        ModTextLine line;
        line.text = t.text;
        line.scale = 2.0f;
        line.x = std::max(4.0f, (640.0f - static_cast<float>(t.text.size()) * 6.0f * line.scale) / 2.0f);
        line.y = y;
        y += 20.0f;
        lines.push_back(std::move(line));
    }
    std::vector<const State::RectEntry *> order;
    for (auto &entry : g.rects) {
        order.push_back(&entry.second);
    }
    std::sort(order.begin(), order.end(), [](const State::RectEntry *a, const State::RectEntry *b) {
        return a->z != b->z ? a->z < b->z : a->seq < b->seq;
    });
    std::vector<ModRect> rects;
    for (const State::RectEntry *e : order) {
        rects.push_back(e->r);
    }
    ModTextSet(std::move(lines), std::move(rects));
}

// ---- Event dispatch --------------------------------------------------------------------------------------------------

#ifdef DC_HAVE_LUA
void LuaFire(const char *name, ModEvent &ev);
#endif

void Fire(const char *name, ModEvent &ev) {
    ev.name = name;
    for (const Listener &l : g.listeners) {
        if (l.event == name) {
            l.fn(l.user, &ev);
        }
    }
#ifdef DC_HAVE_LUA
    LuaFire(name, ev);
#endif
}

// ---- Native plugins -------------------------------------------------------------------------------------------------

int HostLog(const char *message) { Log(g.current_mod, message != nullptr ? message : "(null)"); return 0; }
void HostLogV(const char *message) { HostLog(message); }
int HostOn(const char *event, ModEventFn fn, void *user) {
    if (event == nullptr || fn == nullptr) {
        return 1;
    }
    g.listeners.push_back({event, fn, user, g.current_mod});
    return 0;
}

void LoadNative(const fs::path &mod, const std::string &name) {
    fs::path dll = mod / "plugin.dll";
    std::error_code error;
    if (!fs::is_regular_file(dll, error)) {
        return;
    }
    nlohmann::json meta = ReadJson(mod / "mod.json");
    if (!g.allow_native || !meta.value("native", false)) {
        Log(name, "plugin.dll found but native mods are not allowed (mods/mods.json allow_native and mod.json native must both be true); skipped");
        return;
    }
    SDL_SharedObject *lib = SDL_LoadObject(Utf8(dll).c_str());
    if (lib == nullptr) {
        Log(name, std::string("cannot load plugin.dll: ") + SDL_GetError());
        return;
    }
    using InitFn = int (*)(const ModHostApi *);
    auto init = reinterpret_cast<InitFn>(SDL_LoadFunction(lib, "ModInit"));
    if (init == nullptr) {
        Log(name, "plugin.dll has no ModInit export; skipped");
        return;
    }
    g.native_dirs.push_back(Utf8(mod));
    static ModHostApi api;
    api = {};
    api.version = MOD_API_VERSION;
    api.size = sizeof(ModHostApi);
    api.mod_dir = g.native_dirs.back().c_str();
    api.log = HostLogV;
    api.on = HostOn;
    api.get_hp = ApiGetHp;
    api.set_hp = ApiSetHp;
    api.get_max_hp = ApiGetMaxHp;
    api.set_max_hp = ApiSetMaxHp;
    api.get_water = ApiGetWater;
    api.set_water = ApiSetWater;
    api.get_gilda = ApiGetGilda;
    api.set_gilda = ApiSetGilda;
    api.get_dungeon = ApiGetDungeon;
    api.get_floor = ApiGetFloor;
    api.get_current_chara = ApiGetChara;
    api.get_party_size = ApiGetParty;
    api.give_item = ApiGiveItem;
    api.get_tick = ApiGetTick;
    api.monster_count = ApiMonsterCount;
    api.monster_alive = ApiMonsterAlive;
    api.get_monster_hp = ApiGetMHp;
    api.set_monster_hp = ApiSetMHp;
    api.get_monster_max_hp = ApiGetMMax;
    api.set_monster_max_hp = ApiSetMMax;
    api.get_monster_kind = ApiGetMKind;
    api.get_monster_defense = ApiGetMDef;
    api.set_monster_defense = ApiSetMDef;
    api.get_monster_drop = ApiGetMDrop;
    api.set_monster_drop = ApiSetMDrop;
    api.get_monster_money = ApiGetMMoney;
    api.set_monster_money = ApiSetMMoney;
    api.get_monster_exp = ApiGetMExp;
    api.set_monster_exp = ApiSetMExp;
    api.key_down = ApiKeyDown;
    api.key_pressed = ApiKeyPressed;
    api.text = [](const char *id, const char *t, float x, float y, float scale, uint32_t rgba) {
        ApiText((std::string("n:") + (id != nullptr ? id : "")).c_str(), t, x, y, scale, rgba);
    };
    api.toast = ApiToast;
    g.current_mod = name;
    int result = init(&api);
    if (result != 0) {
        Log(name, "ModInit returned " + std::to_string(result) + "; its event handlers stay registered");
    } else {
        Log(name, "native plugin loaded");
    }
}

// ---- Lua ------------------------------------------------------------------------------------------------------------

#ifdef DC_HAVE_LUA

LuaMod *Self(lua_State *L) {
    auto it = g.by_state.find(L);
    return it == g.by_state.end() ? nullptr : it->second;
}

void Hook(lua_State *L, lua_Debug *) {
    g.instructions += kHookStep;
    if (g.instructions > kInstructionLimit) {
        luaL_error(L, "script ran too long (runaway loop?)");
    }
}

int MessageHandler(lua_State *L) {
    const char *message = lua_tostring(L, 1);
    luaL_traceback(L, L, message != nullptr ? message : "(error object is not a string)", 1);
    return 1;
}

// Calls the function and `nargs` arguments already on the stack. On error the mod is disabled.
void ApplyBlock();
// A mod that errors is switched off; what it had put on screen and the pad it had blocked must not stay behind.
void ClearDeadModHud(const std::string &name) {
    std::string prefix = name + ":";
    std::erase_if(g.texts, [&](const auto &e) { return e.first.rfind(prefix, 0) == 0; });
    std::erase_if(g.rects, [&](const auto &e) { return e.first.rfind(prefix, 0) == 0; });
    g.texts_dirty = true;
    g.block_buttons = 0;
    g.block_sticks = false;
    ApplyBlock();
}
bool Protected(LuaMod &mod, int nargs, int nresults) {
    int base = lua_gettop(mod.L) - nargs; // index of the function (the chunk or handler plus its nargs arguments above it)
    lua_pushcfunction(mod.L, MessageHandler);
    lua_insert(mod.L, base);
    g.instructions = 0;
    int status = lua_pcall(mod.L, nargs, nresults, base);
    lua_remove(mod.L, base);
    if (status != LUA_OK) {
        Log(mod.name, std::string("script error, mod disabled:\n") + lua_tostring(mod.L, -1));
        lua_pop(mod.L, 1);
        mod.dead = true;
        ClearDeadModHud(mod.name);
        return false;
    }
    return true;
}

int L_log(lua_State *L) {
    LuaMod *m = Self(L);
    std::string text;
    int         n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        size_t      len = 0;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1) {
            text += '\t';
        }
        text.append(s, len);
        lua_pop(L, 1);
    }
    Log(m != nullptr ? m->name : "?", text);
    return 0;
}

int L_on(lua_State *L) {
    LuaMod *m = Self(L);
    const char *event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m->handlers[event].push_back(ref);
    return 0;
}

int CharaArg(lua_State *L, int index) {
    if (lua_isnoneornil(L, index)) {
        return ApiGetChara();
    }
    return static_cast<int>(luaL_checkinteger(L, index));
}

int L_hp(lua_State *L) { lua_pushinteger(L, ApiGetHp(CharaArg(L, 1))); return 1; }
int L_max_hp(lua_State *L) { lua_pushinteger(L, ApiGetMaxHp(CharaArg(L, 1))); return 1; }
int L_set_hp(lua_State *L) { ApiSetHp(CharaArg(L, 2), static_cast<int>(luaL_checkinteger(L, 1))); return 0; }
int L_set_max_hp(lua_State *L) { ApiSetMaxHp(CharaArg(L, 2), static_cast<int>(luaL_checkinteger(L, 1))); return 0; }
int L_water(lua_State *L) { lua_pushnumber(L, ApiGetWater(CharaArg(L, 1))); return 1; }
int L_set_water(lua_State *L) { ApiSetWater(CharaArg(L, 2), static_cast<float>(luaL_checknumber(L, 1))); return 0; }
int L_gilda(lua_State *L) { lua_pushinteger(L, ApiGetGilda()); return 1; }
int L_set_gilda(lua_State *L) { ApiSetGilda(static_cast<int>(luaL_checkinteger(L, 1))); return 0; }
int L_dungeon(lua_State *L) { lua_pushinteger(L, ApiGetDungeon()); return 1; }
int L_floor(lua_State *L) { lua_pushinteger(L, ApiGetFloor()); return 1; }
int L_chara(lua_State *L) { lua_pushinteger(L, ApiGetChara()); return 1; }
int L_party_size(lua_State *L) { lua_pushinteger(L, ApiGetParty()); return 1; }
int L_give_item(lua_State *L) {
    lua_pushinteger(L, ApiGiveItem(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_optinteger(L, 2, 1))));
    return 1;
}
int L_ticks(lua_State *L) { lua_pushinteger(L, static_cast<lua_Integer>(g.ticks)); return 1; }
int L_mod_dir(lua_State *L) {
    LuaMod *m = Self(L);
    lua_pushstring(L, Utf8(m->dir).c_str());
    return 1;
}


int L_monster_count(lua_State *L) { lua_pushinteger(L, ApiMonsterCount()); return 1; }
int L_monster_alive(lua_State *L) { lua_pushboolean(L, ApiMonsterAlive(static_cast<int>(luaL_checkinteger(L, 1)))); return 1; }
int L_monsters(lua_State *L) {
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < 16; i++) {
        if (ApiMonsterAlive(i)) {
            lua_pushinteger(L, i);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}
#define MONSTER_GETTER(fn, call) int fn(lua_State *L) { lua_pushinteger(L, call(static_cast<int>(luaL_checkinteger(L, 1)))); return 1; }
#define MONSTER_SETTER(fn, call) int fn(lua_State *L) { call(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2))); return 0; }
MONSTER_GETTER(L_monster_hp, ApiGetMHp)
MONSTER_SETTER(L_set_monster_hp, ApiSetMHp)
MONSTER_GETTER(L_monster_max_hp, ApiGetMMax)
MONSTER_SETTER(L_set_monster_max_hp, ApiSetMMax)
MONSTER_GETTER(L_monster_kind, ApiGetMKind)
MONSTER_GETTER(L_monster_defense, ApiGetMDef)
MONSTER_SETTER(L_set_monster_defense, ApiSetMDef)
MONSTER_GETTER(L_monster_drop, ApiGetMDrop)
MONSTER_SETTER(L_set_monster_drop, ApiSetMDrop)
MONSTER_GETTER(L_monster_money, ApiGetMMoney)
MONSTER_SETTER(L_set_monster_money, ApiSetMMoney)
MONSTER_GETTER(L_monster_exp, ApiGetMExp)
MONSTER_SETTER(L_set_monster_exp, ApiSetMExp)

int L_key_down(lua_State *L) { lua_pushboolean(L, ApiKeyDown(luaL_checkstring(L, 1))); return 1; }
int L_key_pressed(lua_State *L) { lua_pushboolean(L, ApiKeyPressed(luaL_checkstring(L, 1))); return 1; }

int L_text(lua_State *L) {
    LuaMod     *m = Self(L);
    std::string id = (m != nullptr ? m->name : std::string("?")) + ":" + luaL_checkstring(L, 1);
    const char *text = luaL_optstring(L, 2, "");
    bool backdrop = lua_isnoneornil(L, 7) ? true : lua_toboolean(L, 7) != 0;
    ApiTextEx(id.c_str(), text, static_cast<float>(luaL_optnumber(L, 3, 8)), static_cast<float>(luaL_optnumber(L, 4, 8)),
              static_cast<float>(luaL_optnumber(L, 5, 2)), static_cast<uint32_t>(luaL_optinteger(L, 6, 0xFFFFFFFFLL)), backdrop);
    return 0;
}
// dc.rect(id, x, y, w, h, rgba [, z]): a filled box in the 640x480 frame, under the text; w or h <= 0 removes it.
// Rectangles draw in z order (default 0), then in the order they were first created.
int L_rect(lua_State *L) {
    LuaMod     *m = Self(L);
    std::string id = (m != nullptr ? m->name : std::string("?")) + ":" + luaL_checkstring(L, 1);
    ApiRect(id.c_str(), static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
            static_cast<float>(luaL_optnumber(L, 4, 0)), static_cast<float>(luaL_optnumber(L, 5, 0)),
            static_cast<uint32_t>(luaL_optinteger(L, 6, 0xFFFFFFFFLL)), static_cast<int>(luaL_optinteger(L, 7, 0)));
    return 0;
}

// World to the 640x480 frame, through the game's own camera. `up` lifts the point (world units) so a label sits over a head.
bool ScreenOf(const float *world, float up, float &x, float &y, float *pixels_per_unit = nullptr) {
    float p[4] = {world[0], world[1] + up, world[2], 1.0f};
    int   s[4] = {0, 0, 0, 0};
    int   visible = MGRotTransPers2D(s, p, 0);
    x = static_cast<float>(s[0]);
    y = static_cast<float>(s[1]);
    if (pixels_per_unit != nullptr) { // how tall one world unit is on screen at this distance
        float q[4] = {world[0], world[1] + up + 1.0f, world[2], 1.0f};
        int   t[4] = {0, 0, 0, 0};
        MGRotTransPers2D(t, q, 0);
        *pixels_per_unit = static_cast<float>(s[1] - t[1]);
    }
    return visible != 0;
}
int L_monster_screen(lua_State *L) {
    int i = static_cast<int>(luaL_checkinteger(L, 1));
    if (!MOk(i)) {
        return 0;
    }
    float pos[4] = {0, 0, 0, 1};
    NowMonstorUnit->chara[i][0].GetPosition(pos);
    float x, y, scale = 0.0f;
    if (!ScreenOf(pos, static_cast<float>(luaL_optnumber(L, 2, 0)), x, y, &scale)) {
        return 0;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, scale);
    return 3;
}
int L_player_screen(lua_State *L) {
    float pos[4] = {0, 0, 0, 1};
    CharaMain.GetPosition(pos);
    float x, y, scale = 0.0f;
    if (!ScreenOf(pos, static_cast<float>(luaL_optnumber(L, 1, 0)), x, y, &scale)) {
        return 0;
    }
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, scale);
    return 3;
}

// ---- Held weapons ------------------------------------------------------------------------------------------------------

WEAPON_HAVE *WeaponAt(int chara, int slot) {
    if (!Ok(chara)) {
        return nullptr;
    }
    if (slot < 0) {
        slot = UserStatus->equipped_weapon_slot[chara];
    }
    if (slot < 0 || slot >= 11) {
        return nullptr;
    }
    WEAPON_HAVE *w = &UserStatus->chara_weapons[chara][slot];
    return w->item_no > 0 ? w : nullptr;
}
void PushIntArray(lua_State *L, const signed char *a, int n) {
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_pushinteger(L, a[i]);
        lua_rawseti(L, -2, i + 1);
    }
}
// dc.weapon([chara [, slot]]) -> {slot, item, level, attack, endurance, speed, magic, durability, elem={5}, vs_monster={10}, flags}
// slot defaults to the equipped weapon; nil when there is none.
int L_weapon(lua_State *L) {
    int chara = CharaArg(L, 1);
    int slot = lua_isnoneornil(L, 2) ? -1 : static_cast<int>(luaL_checkinteger(L, 2));
    if (slot < 0 && Ok(chara)) {
        slot = UserStatus->equipped_weapon_slot[chara];
    }
    WEAPON_HAVE *w = WeaponAt(chara, slot);
    if (w == nullptr) {
        return 0;
    }
    lua_createtable(L, 0, 12);
    auto set = [&](const char *k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); };
    set("slot", slot);
    set("item", w->item_no);
    set("level", w->level);
    set("attack", w->attack);
    set("endurance", w->endurance);
    set("speed", w->speed);
    set("magic", w->magic);
    set("durability", w->durability);
    set("flags", w->flags);
    PushIntArray(L, reinterpret_cast<const signed char *>(w->elem), 5);
    lua_setfield(L, -2, "elem");
    PushIntArray(L, reinterpret_cast<const signed char *>(w->vs_monster), 10);
    lua_setfield(L, -2, "vs_monster");
    return 1;
}
bool ReadInt(lua_State *L, int table, const char *key, int &out) {
    lua_getfield(L, table, key);
    bool ok = lua_isnumber(L, -1) != 0;
    if (ok) {
        out = static_cast<int>(std::llround(lua_tonumber(L, -1)));
    }
    lua_pop(L, 1);
    return ok;
}
void ReadArray(lua_State *L, int table, const char *key, char *dst, int n) {
    lua_getfield(L, table, key);
    if (lua_istable(L, -1)) {
        for (int i = 0; i < n; i++) {
            lua_rawgeti(L, -1, i + 1);
            if (lua_isnumber(L, -1)) {
                dst[i] = static_cast<char>(std::clamp<long long>(std::llround(lua_tonumber(L, -1)), -128, 127));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}
// dc.set_weapon({attack=.., endurance=.., speed=.., magic=.., durability=.., level=.., flags=.., elem={..}, vs_monster={..}} [, chara [, slot]])
// Only the fields present change. Values are clamped to what the game stores (16-bit stats, 8-bit element and monster bonuses).
int L_set_weapon(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int chara = CharaArg(L, 2);
    int slot = lua_isnoneornil(L, 3) ? -1 : static_cast<int>(luaL_checkinteger(L, 3));
    WEAPON_HAVE *w = WeaponAt(chara, slot);
    if (w == nullptr) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int v = 0;
    auto s16 = [](int x) { return static_cast<short>(std::clamp(x, -32768, 32767)); };
    if (ReadInt(L, 1, "attack", v)) w->attack = s16(v);
    if (ReadInt(L, 1, "endurance", v)) w->endurance = s16(v);
    if (ReadInt(L, 1, "speed", v)) w->speed = s16(v);
    if (ReadInt(L, 1, "magic", v)) w->magic = s16(v);
    if (ReadInt(L, 1, "level", v)) w->level = static_cast<short>(std::clamp(v, 1, 99));
    if (ReadInt(L, 1, "flags", v)) w->flags = s16(v);
    if (ReadInt(L, 1, "durability", v)) {
        w->durability = s16(v);
        w->durability_f = static_cast<float>(w->durability);
    }
    ReadArray(L, 1, "elem", reinterpret_cast<char *>(w->elem), 5);
    ReadArray(L, 1, "vs_monster", reinterpret_cast<char *>(w->vs_monster), 10);
    lua_pushboolean(L, 1);
    return 1;
}

// ---- Persistent storage ------------------------------------------------------------------------------------------------
// dc.store_get(key) / dc.store_set(key, value): numbers, strings, booleans and tables of them, kept per mod and per save slot.
// The store is written when the game saves to a slot and read back when a slot is loaded (event "game_loaded" fires shortly
// after). Before the first save or load of a session it is empty. A new game started after loading another one keeps the old
// store until it is saved to a slot: start new games from a fresh launch.

nlohmann::json LuaToJson(lua_State *L, int index, int depth) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
        case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
        case LUA_TNUMBER: return lua_isinteger(L, index) ? nlohmann::json(static_cast<long long>(lua_tointeger(L, index))) : nlohmann::json(lua_tonumber(L, index));
        case LUA_TSTRING: return std::string(lua_tostring(L, index));
        case LUA_TTABLE: {
            if (depth > 6) {
                return nullptr;
            }
            bool       array = true;
            lua_Integer n = static_cast<lua_Integer>(luaL_len(L, index));
            lua_Integer count = 0;
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                count++;
                if (lua_type(L, -2) != LUA_TNUMBER) {
                    array = false;
                }
                lua_pop(L, 1);
            }
            if (count != n) {
                array = false;
            }
            nlohmann::json out = array ? nlohmann::json::array() : nlohmann::json::object();
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                nlohmann::json value = LuaToJson(L, -1, depth + 1);
                if (array) {
                    out.push_back(std::move(value));
                } else if (lua_type(L, -2) == LUA_TSTRING || lua_type(L, -2) == LUA_TNUMBER) {
                    lua_pushvalue(L, -2);
                    out[lua_tostring(L, -1)] = std::move(value);
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
            return out;
        }
        default: return nullptr;
    }
}
void PushJson(lua_State *L, const nlohmann::json &j) {
    if (j.is_boolean()) {
        lua_pushboolean(L, j.get<bool>());
    } else if (j.is_number_integer()) {
        lua_pushinteger(L, j.get<long long>());
    } else if (j.is_number()) {
        lua_pushnumber(L, j.get<double>());
    } else if (j.is_string()) {
        lua_pushstring(L, j.get<std::string>().c_str());
    } else if (j.is_array()) {
        lua_createtable(L, static_cast<int>(j.size()), 0);
        int i = 1;
        for (const auto &e : j) {
            PushJson(L, e);
            lua_rawseti(L, -2, i++);
        }
    } else if (j.is_object()) {
        lua_createtable(L, 0, static_cast<int>(j.size()));
        for (auto it = j.begin(); it != j.end(); ++it) {
            PushJson(L, it.value());
            lua_setfield(L, -2, it.key().c_str());
        }
    } else {
        lua_pushnil(L);
    }
}
int L_store_get(lua_State *L) {
    LuaMod     *m = Self(L);
    const char *key = luaL_checkstring(L, 1);
    if (m == nullptr || !m->store.contains(key)) {
        lua_pushnil(L);
        return 1;
    }
    PushJson(L, m->store[key]);
    return 1;
}
int L_store_set(lua_State *L) {
    LuaMod     *m = Self(L);
    const char *key = luaL_checkstring(L, 1);
    if (m == nullptr) {
        return 0;
    }
    if (lua_isnoneornil(L, 2)) {
        m->store.erase(key);
    } else {
        nlohmann::json value = LuaToJson(L, 2, 0);
        if (value.dump().size() > 65536) {
            return luaL_error(L, "store_set: value too large");
        }
        m->store[key] = std::move(value);
    }
    if (m->store.dump().size() > 262144) {
        m->store.erase(key);
        return luaL_error(L, "store_set: this mod's store is full (256 KB)");
    }
    m->store_dirty = true;
    return 0;
}
int L_store_slot(lua_State *L) {
    lua_pushinteger(L, g.slot);
    return 1;
}

// ---- Shared settings ---------------------------------------------------------------------------------------------------------
// dc.shared_get(namespace, key) / dc.shared_set(namespace, key, value) / dc.shared_all(namespace): small settings (numbers, strings,
// booleans) that every mod can read and the settings screen can write. Stored in mods/_shared.json, not per save slot. By convention the
// namespace is the mod's folder name; a mod reads its tunables here and falls back to its own defaults when nothing is set.

void SharedLoad() {
    if (g.shared_loaded) {
        return;
    }
    g.shared_loaded = true;
    g.shared = ReadJson(g.root / "_shared.json");
}
void SharedSave() {
    std::error_code error;
    fs::create_directories(g.root, error);
    std::ofstream out(g.root / "_shared.json");
    out << g.shared.dump(1) << std::endl;
}
int L_shared_get(lua_State *L) {
    SharedLoad();
    const char *ns = luaL_checkstring(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!g.shared.contains(ns) || !g.shared[ns].is_object() || !g.shared[ns].contains(key)) {
        lua_pushnil(L);
        return 1;
    }
    PushJson(L, g.shared[ns][key]);
    return 1;
}
int L_shared_set(lua_State *L) {
    SharedLoad();
    const char *ns = luaL_checkstring(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (!g.shared.contains(ns) || !g.shared[ns].is_object()) {
        g.shared[ns] = nlohmann::json::object();
    }
    if (lua_isnoneornil(L, 3)) {
        g.shared[ns].erase(key);
    } else {
        nlohmann::json value = LuaToJson(L, 3, 0);
        if (value.is_array() || value.is_object()) {
            return luaL_error(L, "shared_set: only numbers, strings and booleans");
        }
        g.shared[ns][key] = std::move(value);
    }
    SharedSave();
    return 0;
}
int L_shared_all(lua_State *L) {
    SharedLoad();
    const char *ns = luaL_checkstring(L, 1);
    if (!g.shared.contains(ns)) {
        lua_newtable(L);
        return 1;
    }
    PushJson(L, g.shared[ns]);
    return 1;
}

// ---- World freeze, input blocking, positions and damage ------------------------------------------------------------------

void ApplyBlock() {
    if (g.frozen) {
        InputModBlock(0xFFFF, true);
    } else {
        InputModBlock(g.block_buttons, g.block_sticks);
    }
}
// Holds the dungeon (player, enemies and effects stop, the play clock stops) and hides every pad input from the game, as its own
// pause does; the mod keeps reading the raw pad. Only starts from normal play, and lets go by itself if the game moves on.
std::map<int, std::array<float, 3>> g_town_freeze_pos; // villager id -> where it stands while a menu freezes the town
bool g_town_frozen = false; // the freeze is a town freeze: input blocked, villagers held, the play clock stopped
bool InWalkingTown() { return pEditGround != nullptr && MapNo >= 0 && MapNo < 200 && GameMode == ED_MODE_WALK; }
void SetFrozen(bool on, void *owner) {
    if (on == g.frozen) {
        return;
    }
    if (on && InWalkingTown()) { // a town has no dungeon task: hold the villagers and the pad instead
        g.frozen = true;
        g.freeze_owner = owner;
        g_town_frozen = true;
        g_town_freeze_pos.clear();
        PlayTimeCountFlag(0);
        ApplyBlock();
        return;
    }
    if (!on && g_town_frozen) {
        g.frozen = false;
        g.freeze_owner = nullptr;
        g_town_frozen = false;
        PlayTimeCountFlag(1);
        ApplyBlock();
        return;
    }
    if (on && gameTask != GAME_TASK_PLAY) {
        return;
    }
    g.frozen = on;
    g.freeze_owner = on ? owner : nullptr;
    driveStepHold = on ? 1 : 0;
    CMonUnitHold = on ? 1 : 0;
    CEffectHold = on ? 1 : 0;
    PlayTimeCountFlag(on ? 0 : 1);
    ApplyBlock();
}
int L_freeze(lua_State *L) {
    SetFrozen(lua_toboolean(L, 1) != 0, L);
    lua_pushboolean(L, g.frozen);
    return 1;
}
// dc.block_input(nil|false) shows the game every input; true hides every button and both sticks; a table of pad button names
// ("cross", "square", "l2", ... and "sticks") hides just those from the game while the mod can still read them.
int L_block_input(lua_State *L) {
    unsigned short mask = 0;
    bool           sticks = false;
    if (lua_istable(L, 1)) {
        lua_pushnil(L);
        while (lua_next(L, 1) != 0) {
            if (lua_isstring(L, -1)) {
                std::string name = Fold(lua_tostring(L, -1));
                if (name == "sticks") {
                    sticks = true;
                }
                for (const PadName &p : kPadNames) {
                    if (name == p.name) {
                        mask |= p.bit;
                    }
                }
            }
            lua_pop(L, 1);
        }
    } else if (lua_toboolean(L, 1)) {
        mask = 0xFFFF;
        sticks = true;
    }
    if (mask != 0 && std::getenv("DC_LOG_BLOCK") != nullptr) {
        std::printf("[block] %s sets mask %x", Self(L) != nullptr ? Self(L)->name.c_str() : "?", mask);
        std::printf("%c", 10);
    }
    g.block_buttons = mask;
    g.block_sticks = sticks;
    ApplyBlock();
    return 0;
}
// dc.monster_model(i): the enemy's index in the game's monster table (what the data tables' "monsters" section is keyed by).
int L_monster_model(lua_State *L) {
    int i = static_cast<int>(luaL_checkinteger(L, 1));
    if (!MOk(i)) {
        return 0;
    }
    lua_pushinteger(L, NowMonstorUnit->monster[i].base_model);
    return 1;
}
int L_day(lua_State *L) {
    lua_pushinteger(L, SaveData != nullptr ? SaveData->GetDay() : 0);
    return 1;
}
// dc.shop_list(n) -> {item ids} (shops 0-17); dc.set_shop_list(n, {item ids}) replaces the list (up to 20 items).
int L_shop_list(lua_State *L) {
    int n = static_cast<int>(luaL_checkinteger(L, 1));
    if (n < 0 || n >= 18) {
        return 0;
    }
    short *list = GetItemShopList(n);
    lua_newtable(L);
    for (int i = 0; i < 20 && list[i] != -1; i++) {
        lua_pushinteger(L, list[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}
int L_set_shop_list(lua_State *L) {
    int n = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);
    if (n < 0 || n >= 18) {
        return 0;
    }
    short list[20];
    std::fill(std::begin(list), std::end(list), static_cast<short>(-1));
    int count = static_cast<int>(luaL_len(L, 2));
    for (int i = 0; i < count && i < 20; i++) {
        lua_rawgeti(L, 2, i + 1);
        int id = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        if (id > 0 && id < 400) {
            list[i] = static_cast<short>(id);
        }
    }
    short *dst = GetItemShopList(n);
    for (int i = 0; i < 20; i++) {
        dst[i] = list[i];
    }
    return 0;
}
// dc.launch(key): a value the launcher passed in the environment as DC_LAUNCH_<KEY> (key upper-cased), or nil. Used by co-op to host or join at start.
int L_launch(lua_State *L) {
    std::string name = "DC_LAUNCH_";
    for (const char *c = luaL_checkstring(L, 1); *c != 0; c++) {
        name += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    }
    const char *e = std::getenv(name.c_str());
    if (e == nullptr || e[0] == 0) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, e);
    }
    return 1;
}
// dc.set_monster_speed(i, m): enemy i moves and animates m times as fast (0.3..3). Scripts re-apply it each tick; 1 is normal.
float g_mon_speed[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
int L_set_monster_speed(lua_State *L) {
    int i = static_cast<int>(luaL_checkinteger(L, 1));
    if (MOk(i)) {
        g_mon_speed[i] = static_cast<float>(std::clamp(luaL_checknumber(L, 2), 0.3, 3.0));
    }
    return 0;
}
} // namespace
// Called where enemy scripts set a movement or motion speed. Outside the anonymous namespace: the game code links to it by name.
float ModsSpeedMul(int i) { return (i >= 0 && i < 16) ? g_mon_speed[i] : 1.0f; }
namespace {

// dc.set_monster_scale(i, s): draws enemy i (and its attachments) s times its size. The game may reset it, so scripts re-apply it each tick.
int L_set_monster_scale(lua_State *L) {
    int   i = static_cast<int>(luaL_checkinteger(L, 1));
    float k = static_cast<float>(std::clamp(luaL_checknumber(L, 2), 0.2, 4.0));
    if (!MOk(i)) {
        return 0;
    }
    for (int part = 0; part < 3; part++) {
        NowMonstorUnit->chara[i][part].SetScale(k, k, k);
    }
    return 0;
}
// Status effects. Player: dc.ailments(c) -> bit mask (0x04 freeze, 0x08 stamina, 0x10 poison, 0x20 curse, 0x40 goo);
// dc.set_ailments(c, mask [, frames]). Enemies: dc.set_monster_status(i, "stop"|"poison"|"slow"|"anger", ticks) (0 clears) and
// dc.monster_status(i, name) -> ticks left.
int L_ailments(lua_State *L) {
    int c = CharaArg(L, 1);
    lua_pushinteger(L, Ok(c) ? UserStatus->ailments[c] : 0);
    return 1;
}
int L_set_ailments(lua_State *L) {
    int mask = static_cast<int>(luaL_checkinteger(L, 1));
    int c = CharaArg(L, 2);
    if (Ok(c)) {
        UserStatus->ailments[c] = mask & 0x7E;
        if (lua_isnumber(L, 3)) {
            UserStatus->ailment_frames[c] = static_cast<short>(std::clamp<long long>(luaL_checkinteger(L, 3), 0, 32767));
        }
    }
    return 0;
}
int *MonsterTimer(int i, const std::string &name) {
    if (!MOk(i)) {
        return nullptr;
    }
    MONSTOR &m = NowMonstorUnit->monster[i];
    if (name == "stop") return &m.stop_timer;
    if (name == "poison") return &m.poison_timer;
    if (name == "slow") return &m.slow_timer;
    if (name == "anger") return &m.anger_timer;
    return nullptr;
}
int L_set_monster_status(lua_State *L) {
    int         i = static_cast<int>(luaL_checkinteger(L, 1));
    std::string name = luaL_checkstring(L, 2);
    int         ticks = static_cast<int>(std::clamp<long long>(luaL_checkinteger(L, 3), 0, 36000));
    if (int *t = MonsterTimer(i, name)) {
        *t = ticks;
        if (name == "stop" && ticks > 0) { // as the game's own stop hit: the enemy freezes in place and sheds other afflictions
            NowMonstorUnit->monster[i].poison_timer = 0;
            NowMonstorUnit->monster[i].slow_timer = 0;
            NowMonstorUnit->monster[i].anger_timer = 0;
            NowMonstorUnit->monster[i].movement_speed = 0;
        } else if (name == "slow" && ticks > 0) {
            NowMonstorUnit->monster[i].poison_timer = 0;
        }
    }
    return 0;
}
int L_monster_status(lua_State *L) {
    int *t = MonsterTimer(static_cast<int>(luaL_checkinteger(L, 1)), luaL_checkstring(L, 2));
    lua_pushinteger(L, t != nullptr ? *t : 0);
    return 1;
}
// dc.town_pos() -> x, y, z of the player in a town. dc.camera(ex,ey,ez, rx,ry,rz): hold the town camera at an eye point looking at a
// reference point every tick; dc.camera() with no arguments gives it back to the game.
int L_town_pos(lua_State *L) {
    float pos[4] = {0, 0, 0, 1};
    if (Chara != nullptr) {
        Chara->GetPosition(pos);
    }
    for (int k = 0; k < 3; k++) {
        lua_pushnumber(L, pos[k]);
    }
    return 3;
}
int L_camera(lua_State *L) {
    if (lua_gettop(L) < 6) {
        g.cam_active = false;
        return 0;
    }
    for (int k = 0; k < 3; k++) {
        g.cam_eye[k] = static_cast<float>(luaL_checknumber(L, 1 + k));
        g.cam_ref[k] = static_cast<float>(luaL_checknumber(L, 4 + k));
    }
    g.cam_active = true;
    return 0;
}
// dc.set_floor_size(rooms [, enemy_multiplier [, room_bonus [, dungeon]]]): how big the NEXT floors are built. `rooms`: the game uses 6
// and its builder only fits 5-6 in its area whatever is asked (0 restores the default). `room_bonus` (0-3): rooms may be that many
// cells bigger than the usual 3-4. `enemy_multiplier` (1-3): scales how many enemies are placed (never past the game's 16 slots).
// With `dungeon` (0-6) it applies only to that dungeon (the one being entered, as the game itself knows it); otherwise to all.
// Floors already built are not changed.
int L_set_floor_size(lua_State *L) {
    State::FloorSize f;
    f.rooms = static_cast<int>(std::clamp<long long>(luaL_optinteger(L, 1, 0), 0, 14));
    f.mult = static_cast<float>(std::clamp(luaL_optnumber(L, 2, 1.0), 1.0, 3.0));
    f.bonus = static_cast<int>(std::clamp<long long>(luaL_optinteger(L, 3, 0), 0, 3));
    if (lua_isnoneornil(L, 4)) {
        g.floor_default = f;
    } else {
        int d = static_cast<int>(luaL_checkinteger(L, 4));
        if (d >= 0 && d < 7) {
            g.floor_dungeon[d] = f;
            g.floor_dungeon_set[d] = true;
        }
    }
    return 0;
}
// dc.floor_reached(dungeon) -> the deepest floor reached in that dungeon (0-6), -1 if never entered
int L_floor_reached(lua_State *L) {
    int d = static_cast<int>(luaL_checkinteger(L, 1));
    lua_pushinteger(L, UserStatus != nullptr && d >= 0 && d < 7 ? UserStatus->floor_reached[d] : -1);
    return 1;
}
// dc.floor_select() -> dungeon, selected_floor, scroll_top, list_y, floor_count while the dungeon entrance (floor select) menu is open,
// nothing otherwise. The row of floor f is drawn at logical y = list_y + 40 * f (the list's top row is 118 when it is not scrolled).
int L_floor_select(lua_State *L) {
    if (g.ticks - g.floor_select.seen_tick > 3 || g.floor_select.dungeon < 0) {
        return 0;
    }
    lua_pushinteger(L, g.floor_select.dungeon);
    lua_pushinteger(L, g.floor_select.selected);
    lua_pushinteger(L, g.floor_select.top);
    lua_pushnumber(L, g.floor_select.list_y);
    lua_pushinteger(L, g.floor_select.count);
    return 5;
}
int L_monster_pos(lua_State *L) {
    int i = static_cast<int>(luaL_checkinteger(L, 1));
    if (!MOk(i)) {
        return 0;
    }
    float pos[4] = {0, 0, 0, 1};
    NowMonstorUnit->chara[i][0].GetPosition(pos);
    for (int k = 0; k < 3; k++) {
        lua_pushnumber(L, pos[k]);
    }
    return 3;
}
// ---- Multiplayer ghosts and floor seeds (see ghost_win.cpp) ---------------------------------------------------------------
// dc.player_state() -> x, y, z, rx, ry, rz, motion, flags: everything a peer needs to draw this player.
int L_player_state(lua_State *L) {
    float pos[3], rot[3];
    int   motion = 0, flags = 0;
    GhostLocalState(pos, rot, &motion, &flags);
    for (int k = 0; k < 3; k++) lua_pushnumber(L, pos[k]);
    for (int k = 0; k < 3; k++) lua_pushnumber(L, rot[k]);
    lua_pushinteger(L, motion);
    lua_pushinteger(L, flags);
    return 8;
}
// dc.ghost(slot, x, y, z, rx, ry, rz, motion, flags [, color [, weapon_item]]): color 0 natural / 1 blue tunic; weapon drawn in dungeons. Show/move remote player `slot` (1..3). dc.ghost_clear(slot) hides it.
int L_ghost(lua_State *L) {
    float pos[3], rot[3];
    int   slot = static_cast<int>(luaL_checkinteger(L, 1));
    for (int k = 0; k < 3; k++) {
        pos[k] = static_cast<float>(luaL_checknumber(L, 2 + k));
        rot[k] = static_cast<float>(luaL_optnumber(L, 5 + k, 0));
    }
    GhostSet(slot, pos, rot, static_cast<int>(luaL_optinteger(L, 8, 0)), static_cast<int>(luaL_optinteger(L, 9, 0)),
             static_cast<int>(luaL_optinteger(L, 10, 0)), static_cast<int>(luaL_optinteger(L, 11, 0)));
    return 0;
}
// dc.set_tunic(blue): paint the local player's tunic blue (a guest) or leave it orange; applies when the game next loads the model.
int L_set_tunic(lua_State *L) {
    ModsSetLocalTunicBlue(lua_toboolean(L, 1) != 0);
    return 0;
}
// ---- Georama (shared town building) --------------------------------------------------------------------------------------
// Every part the local player puts down or takes up is queued as an op (dc.georama_take) for the co-op script to send; ops from
// others are applied with dc.georama_apply. A placed part that lands on the local player forces the Georama view so they have to pick
// a new spot to stand (leaving the view puts the character at the cursor).
struct GeoOp {
    int   kind = 0; // 1 placed, 2 removed
    int   map = 0;
    int   parts = 0;
    float pos[3] = {0, 0, 0};
    int   rot = 0;
};
std::deque<GeoOp> g_geo_ops;
bool              g_geo_force = false;
long long         g_geo_force_until = 0;

int L_georama_take(lua_State *L) {
    if (g_geo_ops.empty()) {
        return 0;
    }
    GeoOp op = g_geo_ops.front();
    g_geo_ops.pop_front();
    lua_pushinteger(L, op.kind);
    lua_pushinteger(L, op.map);
    lua_pushinteger(L, op.parts);
    for (int k = 0; k < 3; k++) {
        lua_pushnumber(L, op.pos[k]);
    }
    lua_pushinteger(L, op.rot);
    return 7;
}
// dc.georama_apply(kind, map, parts, x, y, z, rot) -> true when it changed this player's town (kind 1 place, 2 remove).
int L_georama_apply(lua_State *L) {
    int   kind = static_cast<int>(luaL_checkinteger(L, 1));
    int   map = static_cast<int>(luaL_checkinteger(L, 2));
    int   parts = static_cast<int>(luaL_checkinteger(L, 3));
    float x = static_cast<float>(luaL_checknumber(L, 4)), y = static_cast<float>(luaL_checknumber(L, 5)), z = static_cast<float>(luaL_checknumber(L, 6));
    int   rot = static_cast<int>(luaL_checkinteger(L, 7));
    if (pEditGround == nullptr || pEditGround->map_no != map) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool ok = false;
    if (kind == 1) {
        int plot = pEditGround->SetMapParts(parts, x, y, z, rot);
        ok = plot >= 0;
        if (ok) {
            pEditGround->SetBuildEffect(plot);
            if (GameMode == ED_MODE_WALK && Chara != nullptr) {
                float me[4] = {0, 0, 0, 1};
                Chara->GetPosition(me);
                if (pEditGround->GetParts(me[0], me[1], me[2]) == &pEditGround->parts[plot]) {
                    g_geo_force = true; // it landed on the player
                    g_geo_force_until = ClockTickCount() + 1800;
                }
            }
        }
    } else if (kind == 2) {
        int a = 0, b = 0;
        ok = pEditGround->DeleteMapParts(&a, &b, x, y, z) >= 0;
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
// dc.georama_map() -> the town map number whose ground is loaded, or -1.
int L_georama_map(lua_State *L) {
    lua_pushinteger(L, pEditGround != nullptr ? pEditGround->map_no : -1);
    return 1;
}

// ---- Town villagers (NPC sync) ---------------------------------------------------------------------------------------------
// The host's villagers are the real ones; a guest's copies are puppets that follow the host's snapshot. Villagers are matched by
// their villager_id (the table id), not by slot, because two saves can have different villagers in the same slot.
struct NpcPuppet {
    bool  on = false;
    float pos[3] = {0, 0, 0};
    float ry = 0;
    int   motion = 0, flags = 0;
    float speed = -1.0f;
};
std::map<int, NpcPuppet> g_npc_puppets;  // villager_id -> where the host has it
std::map<int, long long> g_npc_hold;     // villager_id -> tick until which it stands still (someone is talking to it)
std::map<int, std::array<float, 3>> g_npc_hold_pos;

int FindVillager(int id) {
    for (int i = 0; i < 10; i++) {
        if (EdVillager[i].initialized != 0 && EdVillager[i].villager_id == id) {
            return i;
        }
    }
    return -1;
}

// dc.npc_list() -> { {id=, name=, x=, y=, z=, ry=, m=, fl=, sp=}, ... } for the villagers standing in this town now.
int L_npc_list(lua_State *L) {
    lua_newtable(L);
    int n = 0;
    for (int i = 0; i < 10; i++) {
        CNPCharacter &v = EdVillager[i];
        if (v.initialized == 0) {
            continue;
        }
        float pos[4] = {0, 0, 0, 1}, rot[4] = {0, 0, 0, 1};
        v.GetPosition(pos);
        v.GetRotation(rot);
        lua_newtable(L);
        lua_pushinteger(L, v.villager_id);
        lua_setfield(L, -2, "id");
        char name[0x21] = {0};
        std::memcpy(name, v.resource_name, 0x20);
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");
        const char *keys[] = {"x", "y", "z"};
        for (int k = 0; k < 3; k++) {
            lua_pushnumber(L, pos[k]);
            lua_setfield(L, -2, keys[k]);
        }
        lua_pushnumber(L, rot[1]);
        lua_setfield(L, -2, "ry");
        lua_pushinteger(L, v.motion_no);
        lua_setfield(L, -2, "m");
        lua_pushinteger(L, v.motion_flags);
        lua_setfield(L, -2, "fl");
        lua_pushnumber(L, v.motion_speed);
        lua_setfield(L, -2, "sp");
        lua_pushinteger(L, v.draw_enabled);
        lua_setfield(L, -2, "on");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
// dc.npc_puppet(id, x, y, z, ry, motion, flags, speed): from now on villager `id` follows these values (a guest, from the host's snapshot).
int L_npc_puppet(lua_State *L) {
    int       id = static_cast<int>(luaL_checkinteger(L, 1));
    NpcPuppet &p = g_npc_puppets[id];
    p.on = true;
    for (int k = 0; k < 3; k++) {
        p.pos[k] = static_cast<float>(luaL_checknumber(L, 2 + k));
    }
    p.ry = static_cast<float>(luaL_checknumber(L, 5));
    p.motion = static_cast<int>(luaL_optinteger(L, 6, 0));
    p.flags = static_cast<int>(luaL_optinteger(L, 7, 0));
    p.speed = static_cast<float>(luaL_optnumber(L, 8, -1.0));
    return 0;
}
// dc.npc_puppet_clear(): villagers go back to their own behaviour.
int L_npc_puppet_clear(lua_State *) {
    g_npc_puppets.clear();
    return 0;
}
// dc.npc_hold(id, ticks): villager `id` stands still where it is for that many ticks (someone is talking to it).
int L_npc_hold(lua_State *L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    int ticks = static_cast<int>(luaL_checkinteger(L, 2));
    int i = FindVillager(id);
    if (i >= 0 && ticks > 0) {
        if (g_npc_hold.find(id) == g_npc_hold.end()) {
            float pos[4] = {0, 0, 0, 1};
            EdVillager[i].GetPosition(pos);
            g_npc_hold_pos[id] = {pos[0], pos[1], pos[2]};
        }
        g_npc_hold[id] = ClockTickCount() + ticks;
    } else {
        g_npc_hold.erase(id);
    }
    return 0;
}
// dc.npc_talking() -> villager id the local player is in conversation with, or -1. EdTalkMode reports it every step it runs.
long long g_talk_tick = -1000;
int       g_talk_id = -1;
int NpcTalkingNow() { return ClockTickCount() - g_talk_tick <= 3 ? g_talk_id : -1; }
int L_npc_talking(lua_State *L) {
    lua_pushinteger(L, NpcTalkingNow());
    return 1;
}
// Guest world. dc.world_capture() -> the host's world as a binary string (nil before a game is loaded); dc.world_join(blob) -> the guest enters or
// refreshes the host's world; dc.world_leave() -> back to their own world; dc.world_active() -> true while in the host's world.
int L_world_capture(lua_State *L) {
    std::string w = GwCapture();
    if (w.empty()) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, w.data(), w.size());
    }
    return 1;
}
int L_world_join(lua_State *L) {
    size_t      n = 0;
    const char *b = luaL_checklstring(L, 1, &n);
    GwSetHostWorld(std::string(b, n));
    return 0;
}
int L_world_leave(lua_State *) {
    GwLeave();
    return 0;
}
int L_world_active(lua_State *L) {
    lua_pushboolean(L, GwActive() ? 1 : 0);
    return 1;
}
// dc.town_clock([hours]) -> the town's time of day (0 to 12); with an argument it sets it. Co-op keeps guests on the host's clock.
int L_town_clock(lua_State *L) {
    if (lua_isnumber(L, 1)) {
        NowTime = static_cast<float>(luaL_checknumber(L, 1));
    }
    lua_pushnumber(L, NowTime);
    return 1;
}
// dc.edit_mode() -> the town's mode: 1 walking, 2 talking, 4 Georama, 14 event, ... (editmapscript.hpp), or -1 outside a town.
int L_edit_mode(lua_State *L) {
    lua_pushinteger(L, pEditGround != nullptr ? GameMode : -1);
    return 1;
}
// ---- Controls: rebinding, injected presses, key capture ---------------------------------------------------------------------
// The player's key bindings live in mods/_bindings.json ({"cross": ["Mouse1", "F"], ...}); they are applied over the defaults and over
// config.json when the scripts start, and rewritten when dc.rebind changes one.
nlohmann::json g_bindings_json = nlohmann::json::object();

void BindingsApply() {
    g_bindings_json = ReadJson(g.root / "_bindings.json");
    if (!g_bindings_json.is_object()) {
        g_bindings_json = nlohmann::json::object();
        return;
    }
    for (auto it = g_bindings_json.begin(); it != g_bindings_json.end(); ++it) {
        if (!it.value().is_array()) {
            continue;
        }
        std::vector<std::string> names;
        for (const auto &n : it.value()) {
            if (n.is_string()) {
                names.push_back(n.get<std::string>());
            }
        }
        std::vector<std::string_view> views(names.begin(), names.end());
        if (!InputBindKeys(it.key(), views)) {
            std::fprintf(stderr, "controls: cannot bind %s\n", it.key().c_str());
        }
    }
}
void BindingsSave() {
    std::error_code error;
    fs::create_directories(g.root, error);
    std::ofstream out(g.root / "_bindings.json");
    out << g_bindings_json.dump(1) << std::endl;
}
// dc.rebind(action, {"F", "Mouse1"}) -> true when the names fit the action. Remembered between runs.
int L_rebind(lua_State *L) {
    std::string action = luaL_checkstring(L, 1);
    std::vector<std::string> names;
    if (lua_istable(L, 2)) {
        int n = static_cast<int>(lua_rawlen(L, 2));
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, 2, i);
            if (lua_isstring(L, -1)) {
                names.push_back(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    } else if (lua_isstring(L, 2)) {
        names.push_back(lua_tostring(L, 2));
    }
    std::vector<std::string_view> views(names.begin(), names.end());
    bool ok = InputBindKeys(action, views);
    if (ok) {
        g_bindings_json[action] = names;
        BindingsSave();
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
// dc.reset_bindings(): every action back to its default (config.json is not consulted again until the next start).
int L_reset_bindings(lua_State *) {
    InputMouseSettings mouse = InputGetMouseSettings();
    InputResetBindings();
    InputSetMouseSettings(mouse);
    g_bindings_json = nlohmann::json::object();
    BindingsSave();
    return 0;
}
// dc.binding(action) -> "F, Mouse1": what is bound now.
int L_binding(lua_State *L) {
    std::string text = InputBindingText(luaL_checkstring(L, 1));
    lua_pushstring(L, text.c_str());
    return 1;
}
// dc.press(button(s) [, ticks]): presses pad buttons for the game ("cross", {"l1", "r1"}, ...) for a few ticks (default 3).
int L_press(lua_State *L) {
    unsigned short mask = 0;
    auto           add = [&](const char *name) {
        std::string low = Fold(name);
        for (const PadName &p : kPadNames) {
            if (low == p.name) {
                mask |= p.bit;
            }
        }
    };
    if (lua_istable(L, 1)) {
        int n = static_cast<int>(lua_rawlen(L, 1));
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, 1, i);
            if (lua_isstring(L, -1)) {
                add(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    } else {
        add(luaL_checkstring(L, 1));
    }
    InputInjectButtons(mask, static_cast<int>(luaL_optinteger(L, 2, 3)));
    return 0;
}
// dc.wheel_mode(0|1): 0 a wheel notch is Left/Right (choosing the dungeon's active item), 1 it is Up/Down (menus).
int L_wheel_mode(lua_State *L) {
    InputSetWheelMode(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}
// dc.capture_key() -> the name of a key or mouse button that went down since the last call, or nil. For a "press the new key" prompt.
int L_capture_key(lua_State *L) {
    static std::array<bool, SDL_SCANCODE_COUNT> before{};
    static std::array<bool, 6>                  mouse_before{};
    int                                         count = 0;
    const bool                                 *now = SDL_GetKeyboardState(&count);
    std::string                                 found;
    if (now != nullptr) {
        for (int i = 0; i < count && i < SDL_SCANCODE_COUNT; i++) {
            if (now[i] && !before[i] && found.empty()) {
                const char *name = SDL_GetScancodeName(static_cast<SDL_Scancode>(i));
                if (name != nullptr && name[0] != 0) {
                    found = name;
                }
            }
            before[i] = now[i];
        }
    }
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
    for (int b = 1; b <= 5; b++) {
        bool down = (buttons & SDL_BUTTON_MASK(b)) != 0;
        if (down && !mouse_before[b] && found.empty()) {
            found = "Mouse" + std::to_string(b);
        }
        mouse_before[b] = down;
    }
    if (found.empty()) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, found.c_str());
    }
    return 1;
}
// dc.select_quick_item(n): the dungeon's active item slot 1..3 (the one Square uses). Returns the slot, or 0 outside a dungeon.
int L_select_quick_item(lua_State *L) {
    int n = static_cast<int>(luaL_checkinteger(L, 1));
    if (n < 1 || n > 3 || gameTask != GAME_TASK_PLAY) {
        lua_pushinteger(L, 0);
        return 1;
    }
    itemNowSel = n;
    activeItem.now = n;
    lua_pushinteger(L, n);
    return 1;
}

// The game's main menu has a MODS entry in the Manuals slot. Picking it asks the scripts to open the mods page instead of the manuals; the page's own
// "Manuals" row lets one pick through (dc.manual_pass, then a Cross press).
bool g_mods_menu_requested = false;
bool g_manual_pass = false;
int L_mods_menu_requested(lua_State *L) {
    lua_pushboolean(L, g_mods_menu_requested ? 1 : 0);
    g_mods_menu_requested = false;
    return 1;
}
// dc.nui_set({rows = {"Controls", "Skill Tree", ...}, cursor = 1, help = "text"}): the MODS page shown in the game's own manual screen (nativeui_win.cpp).
// dc.nui_ready() -> true when it can take a page; dc.nui_close() runs the screen's closing transition; dc.input_blocked() -> some mod is holding the pad.
std::string g_nui_owner; // the mod whose page is in the window
int L_nui_set(lua_State *L) {
    if (LuaMod *m = Self(L)) {
        g_nui_owner = m->name;
    }
    std::vector<std::string> rows;
    int                      cursor = 0;
    std::string              help;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "rows");
        if (lua_istable(L, -1)) {
            int n = static_cast<int>(lua_rawlen(L, -1));
            for (int i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                rows.push_back(lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        lua_getfield(L, 1, "cursor");
        cursor = lua_isnumber(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) - 1 : 0;
        lua_pop(L, 1);
        lua_getfield(L, 1, "help");
        help = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
    }
    NuSetPage(rows, cursor, help);
    return 0;
}
int L_nui_ready(lua_State *L) {
    lua_pushboolean(L, NuReady() ? 1 : 0);
    return 1;
}
int L_nui_close(lua_State *) {
    NuClose();
    g_nui_owner.clear();
    return 0;
}
// dc.nui_owner() -> name of the mod whose page is showing ("" none); dc.nui_release() -> give the window back (the MODS list takes it).
int L_nui_owner(lua_State *L) {
    lua_pushstring(L, g_nui_owner.c_str());
    return 1;
}
int L_nui_release(lua_State *) {
    g_nui_owner.clear();
    return 0;
}
int L_input_blocked(lua_State *L) {
    lua_pushboolean(L, (g.frozen || g.block_buttons != 0 || g.block_sticks) ? 1 : 0);
    lua_pushboolean(L, g.frozen ? 1 : 0);
    lua_pushinteger(L, g.block_buttons);
    lua_pushboolean(L, g.block_sticks ? 1 : 0);
    return 4;
}
int L_manual_pass(lua_State *) {
    g_manual_pass = true;
    return 0;
}
struct MenuMouse {
    bool      enabled = true;
    bool      free = false;          // the pointer is released for the menu
    long long hand_seen = -1000;
    long long title_seen = -1000;     // tick the title's hand was last drawn     // tick the game last drew its hand
    float     hand_x = 0, hand_y = 0; // where the game put the hand (top-left of the 32x32 sprite), this frame
    float     last_hx = -1, last_hy = -1;
    int       still = 0;             // ticks the game's hand has stayed put
    long long next_press = 0;
    float     mouse_x = -1000, mouse_y = -1000; // pointer in the 640x480 frame
    float     prev_err = 1e9f;
    int       last_dir = 0;           // 1 up 2 down 3 left 4 right: the press sent last
    bool      left_before = false, right_before = false;
    float     parked_x = -1, parked_y = -1; // pointer position at which a press last did nothing
    bool      awaiting = false;      // a pad press was sent and the hand has not settled yet
    bool      moved_since = false;   // the hand moved since that press
    long long press_tick = 0;
    int       blocked_dir = 0;       // a direction that did nothing at parked_x/y
    float     step_y = 0;            // how far the hand jumped on the last vertical press
    int       h_state = 0;           // sideways presses in this menu: 0 not tried, 1 they move the hand sideways, -1 they do something else
    float     press_hx = 0;
    float     press_hy = 0;
    float     ring_x[12] = {0}, ring_y[12] = {0};
    int       ring_n = 0, ring_at = 0;
    float     avg_x = 0, avg_y = 0, prev_avg_y = 0, prev_avg_x = 0;
    int       settled = 0;           // ticks the averaged hand position has barely changed
} g_mm;

// dc.menu_mouse(on): the hand-and-click mouse in the game's menus (default on).
int L_menu_mouse(lua_State *L) {
    g_mm.enabled = lua_toboolean(L, 1) != 0;
    return 0;
}

// dc.autosave() -> true when the game was written to save file 1 (the top slot of the save list, kept for the autosave). Only in towns: a save made in
// a dungeon would resume mid-run. Runs the memory card emulation's own save operation to the end at once.
bool g_autosaving = false;
// The memory card emulation steps once per frame like the save menu drives it, so the autosave is a small state machine run from the script tick.
std::vector<char> g_as_work(0x20000);
bool              g_as_active = false;
int               g_as_ticks = 0;
bool              g_as_started_ok = false;
int               g_as_last = 0; // 0 none yet, 1 saved, -1 failed
// Slot 0 of the save list is the autosave. Saves made before that existed sit in slot 0 too, so the first autosave moves every save one
// slot down (darkcloud0 -> darkcloud1 ...) and leaves a marker. False when there is no room (all twelve slots in use).
bool AutosaveMigrate() {
    namespace fs = std::filesystem;
    fs::path        dir = PathsSaveRoot() / "mc0" / McAccess.dir_name;
    fs::path        marker = dir / "autosave.v1";
    std::error_code ec;
    if (fs::exists(marker, ec)) {
        return true;
    }
    if (fs::exists(dir / "darkcloud11", ec) && fs::exists(dir / "darkcloud0", ec)) {
        return false;
    }
    for (int i = 10; i >= 0; i--) {
        fs::path from = dir / ("darkcloud" + std::to_string(i)), to = dir / ("darkcloud" + std::to_string(i + 1));
        if (fs::exists(from, ec) && !fs::exists(to, ec)) {
            fs::rename(from, to, ec);
        }
    }
    if (std::FILE *f = std::fopen(marker.string().c_str(), "wb")) {
        std::fputs("slot 0 is the autosave", f);
        std::fclose(f);
    }
    return true;
}
int L_autosave(lua_State *L) {
    bool started = false;
    if (!g_as_active && SaveData != nullptr && McAccess.GetFuncNo() <= MC_OPERATION_IDLE && pEditGround != nullptr && MapNo >= 0 && MapNo < 200) {
        if (McAccess.file_name[0] == 0) {
            McAccess.InitForMC();
        }
        McAccess.SetFuncNo(MC_OPERATION_IDLE);
        McAccess.port = 0;
        if (!AutosaveMigrate()) {
            lua_pushboolean(L, 0);
            return 1;
        }
        {   // the save menu works from inside the game's folder on the card
            char dir[0x60];
            std::snprintf(dir, sizeof dir, "/%s", McAccess.dir_name);
            int cmd = 0, result = 0;
            sceMcChdir(0, 1, dir, McAccess.current_dir);
            sceMcSync(MC_NOWAIT, &cmd, &result);
        }
        EditSave(); // the town's own state (the Georama, the clock) goes into the save data first, as the save menu does
        McAccess.SetBuff(g_as_work.data());
        McAccess.file_no = 0;
        McAccess.SetFuncNo(MC_OPERATION_SAVE);
        g_as_active = true;
        g_as_ticks = 0;
        started = true;
    }
    lua_pushboolean(L, started ? 1 : 0);
    return 1;
}
// dc.autosave_state() -> "idle", "saving", "saved" or "failed" (the last one finished).
int L_autosave_state(lua_State *L) {
    lua_pushstring(L, g_as_active ? "saving" : g_as_last > 0 ? "saved" : g_as_last < 0 ? "failed" : "idle");
    return 1;
}
// dc.my_tunic(): the local player's tunic colour now (0 natural, 1..15 presets, 16 custom).
int L_my_tunic(lua_State *L) {
    lua_pushinteger(L, ModsLocalTunic());
    return 1;
}
// dc.launch_file(key): the bytes of the file named by DC_LAUNCH_<KEY> (a path the launcher set), or nil; at most 400 KB.
int L_launch_file(lua_State *L) {
    std::string name = "DC_LAUNCH_";
    for (const char *c = luaL_checkstring(L, 1); *c != 0; c++) {
        name += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    }
    const char *path = std::getenv(name.c_str());
    if (path == nullptr || path[0] == 0) {
        lua_pushnil(L);
        return 1;
    }
    std::ifstream in(path, std::ios::binary);
    std::string   bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.is_open() || bytes.empty() || bytes.size() > 400000) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, bytes.data(), bytes.size());
    }
    return 1;
}
// dc.ghost_tunic(slot, color [, front_png, back_png]): another player's tunic; for colour 16 the bytes of their pictures.
int L_ghost_tunic(lua_State *L) {
    size_t      fl = 0, bl = 0;
    const char *front = lua_isstring(L, 3) ? lua_tolstring(L, 3, &fl) : nullptr;
    const char *back = lua_isstring(L, 4) ? lua_tolstring(L, 4, &bl) : nullptr;
    GhostSetTunic(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)), front, fl, back, bl);
    return 0;
}
int L_ghost_clear(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        GhostClearAll();
    } else {
        GhostClear(static_cast<int>(luaL_checkinteger(L, 1)));
    }
    return 0;
}
// dc.floor_seed(): the seed the current dungeon floor was built from. dc.set_floor_seed(dungeon, floor, seed) makes the next
// build of that floor use `seed` (a guest does this with the host's seeds); dc.clear_floor_seeds() stops overriding.
// The unopened chest a hiding monster (a mimic) belongs to: its box and the event that opens it. Returns true if one was removed.
bool RemoveChestBox(int slot) {
    if (NowDngMap == nullptr) {
        return false;
    }
    for (int e = 0; e < 48; e++) {
        if (NowDngMap->events[e].kind == DNG_EVENT_MIMIC) {
            int box = NowDngMap->events[e].index;
            if (box >= 0 && box < 24 && NowDngMap->boxes[box].item_no == slot) {
                NowDngMap->boxes[box].used = 0;
                NowDngMap->boxes[box].closed = 0;
                NowDngMap->events[e].kind = DNG_EVENT_NONE;
                return true;
            }
        }
    }
    return false;
}

// ---- Multiplayer monster puppets ---------------------------------------------------------------------------------------------
// A guest does not run the host's monsters; it mirrors them. dc.puppet(i, kind, x, y, z, ry, motion, flags, hp, max_hp) says where
// slot i is on the host. Every game tick the slot's position and motion are eased toward that, its AI is held (stop status, so it
// neither moves nor attacks), and its life follows the host's. Kills go through dc.hurt_monster. Returns false when the slot is
// empty here or holds a different kind of monster (the floors differ). dc.puppet_clear() releases them all.
struct Puppet {
    bool  on = false;
    bool  snap = true;
    int   kind = 0;
    float target[3] = {0, 0, 0};
    float shown[3] = {0, 0, 0};
    float ry = 0, shown_ry = 0;
    int   motion = 0, flags = 0, hp = 0, max_hp = 0, state = 2, revealed = -1;
};
Puppet       g_puppet[16];
std::int64_t g_puppet_tick = 0;

float WrapPi(float a) {
    const float pi = 3.14159265f;
    while (a > pi) a -= 2.0f * pi;
    while (a < -pi) a += 2.0f * pi;
    return a;
}

void PuppetApply() {
    if (NowMonstorUnit == nullptr) {
        return;
    }
    std::int64_t now = ClockTickCount();
    std::int64_t steps = std::clamp<std::int64_t>(now - g_puppet_tick, 0, 4);
    g_puppet_tick = now;
    for (int i = 0; i < 16; i++) {
        Puppet &p = g_puppet[i];
        if (!p.on) {
            continue;
        }
        MONSTOR &m = NowMonstorUnit->monster[i];
        if (m.state == -1 || m.hp <= 0 || m.kind != p.kind) { // gone here, or dying on its own: the game's own handling takes over
            p.on = false;
            continue;
        }
        float cur[4] = {0, 0, 0, 1};
        NowMonstorUnit->chara[i][0].GetPosition(cur);
        float dx = p.target[0] - cur[0], dz = p.target[2] - cur[2];
        if (p.snap || dx * dx + dz * dz > 400.0f * 400.0f) { // first sight, or far off (a warp): jump
            for (int k = 0; k < 3; k++) p.shown[k] = p.target[k];
            p.shown_ry = p.ry;
            p.snap = false;
        } else {
            for (std::int64_t s = 0; s < steps; s++) {
                for (int k = 0; k < 3; k++) p.shown[k] += (p.target[k] - p.shown[k]) * 0.4f;
                p.shown_ry = WrapPi(p.shown_ry + WrapPi(p.ry - p.shown_ry) * 0.4f);
            }
        }
        float rot[4] = {0, 0, 0, 0};
        NowMonstorUnit->chara[i][0].GetRotation(rot);
        for (int j = 0; j <= m.attachment_count && j < 3; j++) {
            CCharacter &c = NowMonstorUnit->chara[i][j];
            c.SetPosition(p.shown[0], p.shown[1], p.shown[2]);
            c.SetRotation(rot[0], p.shown_ry, rot[2]);
            if (c.motion_no != p.motion || (c.motion_flags & 3) != (p.flags & 3)) {
                c.SetMotion(p.motion, p.flags & 3);
            }
        }
        if (p.state == 1 || p.state == 2) {
            m.state = p.state; // asleep/awake as on the host (CheckViewLevel and the AI skip puppets, see MpIsPuppet)
        }
        // A monster that came out of a chest on the host hides in its own (unopened) chest here: show it where the host's is.
        if (p.revealed != 0 && (m.revealed == 0 || m.revealed == 1)) {
            if (m.revealed == 0) {
                RemoveChestBox(i); // it came out of the host's chest: the matching chest here goes too
            }
            m.revealed = -1;
        }
        if (p.hp > 0 && m.hp > 0) {
            m.hp = std::clamp(p.hp, 1, std::max(1, static_cast<int>(m.max_hp)));
        }
    }
}

// dc.monster_state(i) -> alive, kind, hp, max_hp, x, y, z, ry, motion, flags (everything a guest needs to puppet slot i).
int L_monster_state(lua_State *L) {
    int i = static_cast<int>(luaL_checkinteger(L, 1));
    if (!MOk(i)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    MONSTOR &m = NowMonstorUnit->monster[i];
    CCharacter &c = NowMonstorUnit->chara[i][0];
    float pos[4] = {0, 0, 0, 1}, rot[4] = {0, 0, 0, 0};
    c.GetPosition(pos);
    c.GetRotation(rot);
    lua_pushboolean(L, ApiMonsterAlive(i));
    lua_pushinteger(L, m.kind);
    lua_pushinteger(L, m.hp);
    lua_pushinteger(L, m.max_hp);
    for (int k = 0; k < 3; k++) lua_pushnumber(L, pos[k]);
    lua_pushnumber(L, rot[1]);
    lua_pushinteger(L, c.motion_no);
    lua_pushinteger(L, c.motion_flags);
    lua_pushinteger(L, m.state);
    lua_pushinteger(L, m.revealed);
    return 12;
}
int L_puppet(lua_State *L) {
    int i = static_cast<int>(luaL_checkinteger(L, 1));
    if (!MOk(i) || NowMonstorUnit->monster[i].state == -1 || NowMonstorUnit->monster[i].hp <= 0 ||
        NowMonstorUnit->monster[i].kind != static_cast<int>(luaL_checkinteger(L, 2))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    Puppet &p = g_puppet[i];
    // This player opened their OWN copy of the chest (chests are per player) while the host's monster still hides in its chest: the
    // monster that came out here belongs to this player, so leave it to the game instead of mirroring a hidden one.
    if (luaL_optinteger(L, 12, -1) == 0 && NowMonstorUnit->monster[i].revealed != 0) {
        p.on = false;
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!p.on) {
        p.snap = true;
    }
    p.on = true;
    p.kind = static_cast<int>(luaL_checkinteger(L, 2));
    for (int k = 0; k < 3; k++) p.target[k] = static_cast<float>(luaL_checknumber(L, 3 + k));
    p.ry = static_cast<float>(luaL_checknumber(L, 6));
    p.motion = static_cast<int>(luaL_checkinteger(L, 7));
    p.flags = static_cast<int>(luaL_checkinteger(L, 8));
    p.hp = static_cast<int>(luaL_optinteger(L, 9, 0));
    p.max_hp = static_cast<int>(luaL_optinteger(L, 10, 0));
    p.state = static_cast<int>(luaL_optinteger(L, 11, 2));
    p.revealed = static_cast<int>(luaL_optinteger(L, 12, -1));
    lua_pushboolean(L, 1);
    return 1;
}
int L_puppet_clear(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        for (Puppet &p : g_puppet) p.on = false;
    } else {
        int i = static_cast<int>(luaL_checkinteger(L, 1));
        if (i >= 0 && i < 16) g_puppet[i].on = false;
    }
    return 0;
}
// ---- Messages between mods ---------------------------------------------------------------------------------------------------
// Every mod runs in its own Lua state, so they cannot call each other. dc.msg_on(name, fn) listens for a message and
// dc.msg(name, a, b) sends one to every mod (the sender too); handlers are called as fn(a, b, sender_mod_name) with a and b as
// strings. Nothing is returned: answer by sending another message. The in-game hub menu is built on this (hub_collect/hub_entry/hub_open).
int L_msg_on(lua_State *L) {
    LuaMod *m = Self(L);
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    m->msg_handlers[name].push_back(ref);
    return 0;
}
int L_msg(lua_State *L) {
    LuaMod *from = Self(L);
    std::string name = luaL_checkstring(L, 1);
    std::string a = luaL_optstring(L, 2, "");
    std::string b = luaL_optstring(L, 3, "");
    static int depth = 0;
    if (depth > 4) { // a handler that answers with the message it handles would never end
        return 0;
    }
    depth++;
    std::string sender = from != nullptr ? from->name : std::string("?");
    std::vector<LuaMod *> targets;
    for (auto &owned : g.lua) {
        targets.push_back(owned.get());
    }
    for (LuaMod *m : targets) {
        if (m->dead) {
            continue;
        }
        auto it = m->msg_handlers.find(name);
        if (it == m->msg_handlers.end()) {
            continue;
        }
        for (int ref : std::vector<int>(it->second)) {
            if (m->dead) {
                break;
            }
            lua_State *T = m->L;
            lua_rawgeti(T, LUA_REGISTRYINDEX, ref);
            lua_pushstring(T, a.c_str());
            lua_pushstring(T, b.c_str());
            lua_pushstring(T, sender.c_str());
            Protected(*m, 3, 0);
        }
    }
    depth--;
    return 0;
}
// dc.menu_open(): true while the game's own pause or battle menu is up in a dungeon (used to pause other players too).
// dc.frozen(): true while some mod holds the world with dc.freeze.
int L_menu_open(lua_State *L) {
    bool open = false;
    switch (gameTask) {
        case GAME_TASK_MENU_OPEN: case GAME_TASK_MENU_WAIT_FRAME: case GAME_TASK_MENU_SKIP: case GAME_TASK_MENU_INIT:
        case GAME_TASK_MENU_CLOSE: case GAME_TASK_PAUSE: case GAME_TASK_CHARA_SELECT_LOOP:
            open = true;
            break;
        default:
            break;
    }
    lua_pushboolean(L, open);
    return 1;
}
int L_frozen(lua_State *L) {
    lua_pushboolean(L, g.frozen);
    return 1;
}

// dc.take_ghost_hit() -> slot, monster, damage, kind, flags: a monster attack that landed on remote player `slot` since last asked
// (nothing when none). dc.hurt_player(damage, kind, flags, monster): make a monster attack land on the local player (the game's own
// damage, flinch and knockback run, and player_damage fires). The host sends the first on behalf of a guest and the guest does the second.
int L_take_ghost_hit(lua_State *L) {
    GhostHit h;
    if (!GhostTakeHit(&h)) {
        return 0;
    }
    lua_pushinteger(L, h.slot);
    lua_pushinteger(L, h.monster);
    lua_pushinteger(L, h.damage);
    lua_pushinteger(L, h.kind);
    lua_pushinteger(L, h.flags);
    return 5;
}
int L_hurt_player(lua_State *L) {
    GhostHurtLocal(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_optinteger(L, 2, 0)),
                   static_cast<int>(luaL_optinteger(L, 3, 0)), static_cast<int>(luaL_optinteger(L, 4, 0)));
    return 0;
}

// dc.remove_chest_monster(slot): a monster that is still hiding in an unopened chest (a mimic) dies with it: the chest vanishes and the
// monster slot is emptied without a fight. Returns true if it did. Used when another player killed the same mimic.
int L_remove_chest_monster(lua_State *L) {
    int slot = static_cast<int>(luaL_checkinteger(L, 1));
    if (!MOk(slot) || NowMonstorUnit->monster[slot].state == -1 || NowMonstorUnit->monster[slot].revealed != 0 || !RemoveChestBox(slot)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    NowMonstorUnit->monster[slot].hp = 0;
    NowMonstorUnit->monster[slot].state = -1;
    if (NowMonstorUnit->alive_count > 0) {
        NowMonstorUnit->alive_count--;
    }
    lua_pushboolean(L, 1);
    return 1;
}
// dc.take_event() -> script, mode, ext, px, py, pz, dx, dy, dz: a door/gate script this player just ran (nothing when none).
// dc.run_event(script, mode, ext, px, py, pz, dx, dy, dz): run that script here too, as soon as the game is free.
int L_take_event(lua_State *L) {
    MpEvent e;
    if (!GhostTakeEvent(&e)) {
        return 0;
    }
    lua_pushinteger(L, e.script);
    lua_pushinteger(L, e.mode);
    lua_pushinteger(L, e.ext);
    for (int k = 0; k < 3; k++) lua_pushnumber(L, e.pos[k]);
    for (int k = 0; k < 3; k++) lua_pushnumber(L, e.dir[k]);
    return 9;
}
int L_run_event(lua_State *L) {
    MpEvent e{};
    e.script = static_cast<int>(luaL_checkinteger(L, 1));
    e.mode = static_cast<int>(luaL_optinteger(L, 2, 1));
    e.ext = static_cast<int>(luaL_optinteger(L, 3, 0));
    for (int k = 0; k < 3; k++) {
        e.pos[k] = static_cast<float>(luaL_optnumber(L, 4 + k, 0));
        e.dir[k] = static_cast<float>(luaL_optnumber(L, 7 + k, 0));
    }
    MpRunEvent(e);
    return 0;
}
// dc.logic_ticks(): game logic ticks since the clock started (60 a second); mods that pace network sends should use this.
int L_logic_ticks(lua_State *L) {
    lua_pushinteger(L, static_cast<lua_Integer>(ClockTickCount()));
    return 1;
}

// dc.scene() -> "D<dungeon>:<floor>" in a dungeon, "T<town>:<map>:<interior>" in a town, "-" anywhere else (menus, loading).
int L_scene(lua_State *L) {
    char buf[96];
    GhostScene(buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}
int L_floor_seed(lua_State *L) {
    lua_pushinteger(L, GhostFloorSeed());
    return 1;
}
int L_set_floor_seed(lua_State *L) {
    GhostSetFloorSeed(static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)), static_cast<int>(luaL_checkinteger(L, 3)));
    return 0;
}
int L_clear_floor_seeds(lua_State *) {
    GhostClearFloorSeeds();
    return 0;
}

// ---- net.*: multiplayer transport (see win-save/mods/MULTIPLAYER.md) -----------------------------------------------------
// net.host(port) / net.join(ip, port) return true, or nil + reason. Everything else is polled from tick: net.poll() returns
// kind, peer, data (kind is "join", "leave", "message", "connected", "failed" or "disconnected") or nothing when the queue is empty.
int L_net_host(lua_State *L) {
    std::string err;
    if (!NetHost(static_cast<int>(luaL_optinteger(L, 1, 7777)), &err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
int L_net_join(lua_State *L) {
    std::string err;
    if (!NetJoin(luaL_checkstring(L, 1), static_cast<int>(luaL_optinteger(L, 2, 7777)), &err)) {
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
int L_net_stop(lua_State *) {
    NetStop();
    return 0;
}
int L_net_status(lua_State *L) {
    static const char *names[] = {"idle", "hosting", "connecting", "connected"};
    lua_pushstring(L, names[NetStatusNow()]);
    return 1;
}
int L_net_id(lua_State *L) {
    lua_pushinteger(L, NetLocalId());
    return 1;
}
int L_net_peers(lua_State *L) {
    lua_newtable(L);
    int i = 1;
    for (int id : NetPeers()) {
        lua_pushinteger(L, id);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}
// net.send(to, text): to is a peer id, or -1 for everyone. A guest's peer 0 is the host. Returns true if it was queued.
int L_net_send(lua_State *L) {
    size_t len = 0;
    int to = static_cast<int>(luaL_checkinteger(L, 1));
    const char *data = luaL_checklstring(L, 2, &len);
    lua_pushboolean(L, NetSend(to, data, len) ? 1 : 0);
    return 1;
}
int L_net_poll(lua_State *L) {
    NetEvent e;
    if (!NetPoll(e)) {
        return 0;
    }
    static const char *kinds[] = {"?", "join", "leave", "message", "connected", "failed", "disconnected"};
    lua_pushstring(L, kinds[e.kind]);
    lua_pushinteger(L, e.peer);
    lua_pushlstring(L, e.data.data(), e.data.size());
    return 3;
}

int L_player_pos(lua_State *L) {
    float pos[4] = {0, 0, 0, 1};
    CharaMain.GetPosition(pos);
    for (int k = 0; k < 3; k++) {
        lua_pushnumber(L, pos[k]);
    }
    return 3;
}
// dc.hurt_monster(i, amount [, chara]): real damage on the game's own path: the damage number, hit, death and drops all happen,
// and monster_killed fires with `chara` as the attacker (-1 when omitted). Taken on the enemy's next update.
int L_hurt_monster(lua_State *L) {
    int i = static_cast<int>(luaL_checkinteger(L, 1));
    int amount = static_cast<int>(luaL_checkinteger(L, 2));
    if (MOk(i) && amount > 0) {
        g.hurt[i] = std::min(g.hurt[i] + amount, 1000000);
        g.hurt_owner[i] = static_cast<int>(luaL_optinteger(L, 3, -1));
    }
    return 0;
}
int L_toast(lua_State *L) {
    ApiToast(luaL_checkstring(L, 1), static_cast<float>(luaL_optnumber(L, 2, 3)));
    return 0;
}

bool SafeName(const std::string &name) {
    if (name.empty() || name.find("..") != std::string::npos || name.front() == '/' || name.front() == '.') {
        return false;
    }
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/')) {
            return false;
        }
    }
    return true;
}

bool LoadChunk(lua_State *L, const fs::path &file, const std::string &chunk) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        lua_pushstring(L, ("cannot open " + Utf8(file)).c_str());
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) { // utf-8 BOM
        text.erase(0, 3);
    }
    return luaL_loadbufferx(L, text.data(), text.size(), ("@" + chunk).c_str(), "t") == LUA_OK;
}

int L_require(lua_State *L) {
    LuaMod *m = Self(L);
    std::string name = luaL_checkstring(L, 1);
    std::replace(name.begin(), name.end(), '.', '/');
    if (!SafeName(name)) {
        return luaL_error(L, "bad module name");
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "dc_loaded");
    lua_getfield(L, -1, name.c_str());
    if (!lua_isnil(L, -1)) {
        return 1;
    }
    lua_pop(L, 1);
    if (!LoadChunk(L, m->dir / "scripts" / (name + ".lua"), name + ".lua")) {
        return lua_error(L);
    }
    lua_call(L, 0, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name.c_str());
    return 1;
}

void OpenLib(lua_State *L, const char *name, lua_CFunction open) {
    luaL_requiref(L, name, open, 1);
    lua_pop(L, 1);
}

void LoadLua(const fs::path &mod, const std::string &name) {
    fs::path main = mod / "scripts" / "main.lua";
    std::error_code error;
    if (!fs::is_regular_file(main, error)) {
        return;
    }
    auto  owned = std::make_unique<LuaMod>();
    LuaMod &m = *owned;
    m.name = name;
    m.dir = mod;
    m.L = luaL_newstate();
    if (m.L == nullptr) {
        Log(name, "cannot create a Lua state");
        return;
    }
    lua_State *L = m.L;
    g.by_state[L] = &m;
    // Only safe libraries: no io, os, package, debug, and no loadfile/dofile.
    OpenLib(L, LUA_GNAME, luaopen_base);
    OpenLib(L, LUA_TABLIBNAME, luaopen_table);
    OpenLib(L, LUA_STRLIBNAME, luaopen_string);
    OpenLib(L, LUA_MATHLIBNAME, luaopen_math);
    OpenLib(L, LUA_UTF8LIBNAME, luaopen_utf8);
    OpenLib(L, LUA_COLIBNAME, luaopen_coroutine);
    for (const char *unsafe : {"dofile", "loadfile", "load", "collectgarbage"}) {
        lua_pushnil(L);
        lua_setglobal(L, unsafe);
    }
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "dc_loaded");
    lua_pushcfunction(L, L_log);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, L_require);
    lua_setglobal(L, "require");

    static const luaL_Reg functions[] = {
        {"log", L_log},           {"on", L_on},           {"hp", L_hp},           {"set_hp", L_set_hp},
        {"max_hp", L_max_hp},     {"set_max_hp", L_set_max_hp}, {"water", L_water}, {"set_water", L_set_water},
        {"gilda", L_gilda},       {"set_gilda", L_set_gilda},   {"dungeon", L_dungeon}, {"floor", L_floor},
        {"chara", L_chara},       {"party_size", L_party_size}, {"give_item", L_give_item}, {"ticks", L_ticks},
        {"mod_dir", L_mod_dir},   {"monster_count", L_monster_count}, {"monster_alive", L_monster_alive},
        {"monsters", L_monsters}, {"monster_hp", L_monster_hp}, {"set_monster_hp", L_set_monster_hp},
        {"monster_max_hp", L_monster_max_hp}, {"set_monster_max_hp", L_set_monster_max_hp},
        {"monster_kind", L_monster_kind}, {"monster_defense", L_monster_defense},
        {"set_monster_defense", L_set_monster_defense}, {"monster_drop", L_monster_drop},
        {"set_monster_drop", L_set_monster_drop}, {"monster_money", L_monster_money},
        {"set_monster_money", L_set_monster_money}, {"monster_exp", L_monster_exp},
        {"set_monster_exp", L_set_monster_exp}, {"key_down", L_key_down}, {"key_pressed", L_key_pressed},
        {"pad_down", L_key_down}, {"pad_pressed", L_key_pressed}, {"text", L_text}, {"toast", L_toast},
        {"rect", L_rect}, {"monster_screen", L_monster_screen}, {"player_screen", L_player_screen}, {"weapon", L_weapon},
        {"set_weapon", L_set_weapon}, {"store_get", L_store_get}, {"store_set", L_store_set}, {"store_slot", L_store_slot}, {"shared_get", L_shared_get}, {"shared_set", L_shared_set}, {"shared_all", L_shared_all},
        {"freeze", L_freeze}, {"block_input", L_block_input}, {"monster_pos", L_monster_pos}, {"floor_select", L_floor_select}, {"set_floor_size", L_set_floor_size}, {"floor_reached", L_floor_reached}, {"town_pos", L_town_pos}, {"camera", L_camera}, {"ailments", L_ailments}, {"set_ailments", L_set_ailments}, {"set_monster_status", L_set_monster_status}, {"monster_status", L_monster_status}, {"set_monster_scale", L_set_monster_scale}, {"day", L_day}, {"shop_list", L_shop_list}, {"set_shop_list", L_set_shop_list}, {"monster_model", L_monster_model}, {"player_pos", L_player_pos},
        {"hurt_monster", L_hurt_monster}, {"set_monster_speed", L_set_monster_speed}, {"launch", L_launch},
        {"player_state", L_player_state}, {"ghost", L_ghost}, {"ghost_clear", L_ghost_clear}, {"set_tunic", L_set_tunic}, {"my_tunic", L_my_tunic}, {"first_person", L_first_person}, {"town_chara", L_town_chara}, {"autosave", L_autosave}, {"autosave_state", L_autosave_state}, {"menu_mouse", L_menu_mouse}, {"mods_menu_requested", L_mods_menu_requested}, {"nui_set", L_nui_set}, {"nui_ready", L_nui_ready}, {"nui_close", L_nui_close}, {"nui_owner", L_nui_owner}, {"nui_release", L_nui_release}, {"input_blocked", L_input_blocked}, {"manual_pass", L_manual_pass}, {"rebind", L_rebind}, {"reset_bindings", L_reset_bindings}, {"binding", L_binding}, {"press", L_press}, {"wheel_mode", L_wheel_mode}, {"capture_key", L_capture_key}, {"select_quick_item", L_select_quick_item}, {"edit_mode", L_edit_mode}, {"town_clock", L_town_clock}, {"world_capture", L_world_capture}, {"world_join", L_world_join}, {"world_leave", L_world_leave}, {"world_active", L_world_active}, {"npc_list", L_npc_list}, {"georama_take", L_georama_take}, {"georama_apply", L_georama_apply}, {"georama_map", L_georama_map}, {"npc_puppet", L_npc_puppet}, {"npc_puppet_clear", L_npc_puppet_clear}, {"npc_hold", L_npc_hold}, {"npc_talking", L_npc_talking}, {"launch_file", L_launch_file}, {"ghost_tunic", L_ghost_tunic}, {"floor_seed", L_floor_seed}, {"scene", L_scene}, {"monster_state", L_monster_state}, {"puppet", L_puppet},
        {"puppet_clear", L_puppet_clear}, {"logic_ticks", L_logic_ticks},
        {"remove_chest_monster", L_remove_chest_monster}, {"take_event", L_take_event}, {"run_event", L_run_event}, {"msg_on", L_msg_on}, {"msg", L_msg}, {"menu_open", L_menu_open}, {"frozen", L_frozen},
        {"take_ghost_hit", L_take_ghost_hit}, {"hurt_player", L_hurt_player},
        {"set_floor_seed", L_set_floor_seed}, {"clear_floor_seeds", L_clear_floor_seeds},
        {nullptr, nullptr}};
    luaL_newlib(L, functions);
    lua_pushinteger(L, 4);
    lua_setfield(L, -2, "api_version");
    lua_setglobal(L, "dc");
    static const luaL_Reg net_functions[] = {
        {"host", L_net_host}, {"join", L_net_join},   {"stop", L_net_stop}, {"status", L_net_status},
        {"id", L_net_id},     {"peers", L_net_peers}, {"send", L_net_send}, {"poll", L_net_poll},
        {nullptr, nullptr}};
    if (ReadJson(mod / "mod.json").value("net", false)) { // sockets only for mods that ask (mod.json {"net":true})
        luaL_newlib(L, net_functions);
        lua_setglobal(L, "net");
    }

    lua_sethook(L, Hook, LUA_MASKCOUNT, kHookStep);

    if (!LoadChunk(L, main, name + "/scripts/main.lua")) {
        Log(name, std::string("cannot load main.lua: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        m.dead = true;
    } else if (Protected(m, 0, 0)) {
        Log(name, "script loaded");
    }
    g.lua.push_back(std::move(owned));
}

struct EventSpec {
    const char *name;
    int         args;     // ev.i[0..args-1] are passed to the handler
    int         returns;  // how many values a handler may return to change the event
    int         slot[2];  // which ev.i each returned value replaces
};
const EventSpec kEvents[] = {
    {"init", 0, 0, {0, 0}},          {"tick", 1, 0, {0, 0}},          {"floor_change", 4, 0, {0, 0}},
    {"item_pickup", 2, 2, {0, 1}},   {"monster_hit", 5, 1, {3, 0}},   {"monster_killed", 3, 0, {0, 0}},
    {"player_damage", 2, 1, {1, 0}}, {"player_heal", 2, 1, {1, 0}},
    {"game_loaded", 0, 0, {0, 0}},
};

void LuaFire(const char *name, ModEvent &ev) {
    const EventSpec *spec = nullptr;
    for (const EventSpec &e : kEvents) {
        if (std::strcmp(e.name, name) == 0) {
            spec = &e;
        }
    }
    if (spec == nullptr) {
        return;
    }
    for (auto &owned : g.lua) {
        LuaMod &m = *owned;
        if (m.dead) {
            continue;
        }
        auto it = m.handlers.find(name);
        if (it == m.handlers.end()) {
            continue;
        }
        for (int ref : std::vector<int>(it->second)) {
            if (m.dead) {
                break;
            }
            lua_State *L = m.L;
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            for (int k = 0; k < spec->args; k++) {
                lua_pushinteger(L, ev.i[k]);
            }
            if (!Protected(m, spec->args, spec->returns)) {
                continue;
            }
            for (int k = 0; k < spec->returns; k++) {
                int index = -(spec->returns - k);
                if (lua_isnumber(L, index)) {
                    ev.i[spec->slot[k]] = static_cast<int32_t>(std::llround(lua_tonumber(L, index)));
                }
            }
            lua_pop(L, spec->returns);
        }
    }
}

#endif // DC_HAVE_LUA

void Init() {
    g.inited = true;
    g.root = PathsSaveRoot() / "mods";
    std::error_code error;
    if (!fs::is_directory(g.root, error)) {
        return;
    }
    BindingsApply(); // the player's own key bindings, over the defaults
    g.allow_native = ReadJson(g.root / "mods.json").value("allow_native", false);
    std::vector<fs::path> mods = ModsLoadOrder(g.root);
    for (const fs::path &mod : mods) {
        std::string name = Utf8(mod.filename());
        if (!ReadJson(mod / "mod.json").value("enabled", true)) {
            continue;
        }
        ModDataApply(mod, name);
        LoadNative(mod, name);
#ifdef DC_HAVE_LUA
        LoadLua(mod, name);
#else
        if (fs::is_regular_file(mod / "scripts" / "main.lua", error)) {
            Log(name, "has Lua scripts but this build has no Lua (run setup_lua.ps1 and rebuild); skipped");
        }
#endif
    }
    if (const char *dump = std::getenv("DC_DUMP_DATA"); dump != nullptr && dump[0] == '1') {
        ModDataDump(g.root / "_dump" / "data");
    }
    ModEvent ev{};
    Fire("init", ev);
}

} // namespace

// Called from the monster update (gen_win_src.py patches): a puppet keeps its animation but runs no script, movement or attack.
bool MpIsPuppet(int i) { return i >= 0 && i < 16 && g_puppet[i].on; }

void MenuMouseTick();
static void TownCharaTick();

// One memory card step per tick while an autosave runs (the card emulation is driven a frame at a time, as the save menu does).
static void AutosaveStep() {
    if (!g_as_active) {
        return;
    }
    int r = McAccess.Step();
    if (r < 0 && McAccess.step >= 6) { // the save file is written by step 3; later steps only refresh the settings file, which may not exist yet
        r = 1;
        McAccess.step = 0;
        McAccess.SetFuncNo(MC_OPERATION_IDLE);
    }
    if (r < 0 || ++g_as_ticks > 600) {
        g_as_active = false;
        g_as_last = -1;
        McAccess.SetFuncNo(MC_OPERATION_IDLE);
        std::printf("[autosave] failed at tick %d (step result %d)", g_as_ticks, r);
        std::printf("%c", 10);
    } else if (r == 1 || McAccess.GetFuncNo() == MC_OPERATION_IDLE) {
        g_as_active = false;
        g_as_last = 1;
        std::printf("[autosave] saved to file 1");
        std::printf("%c", 10);
    }
}

void ScriptTick() {
    if (!g.inited) {
        Init();
    }
    MenuMouseTick();
    AutosaveStep();
    TownCharaTick();
    NetPump();
    PuppetApply();
    MpEventPoll();
    if (g.listeners.empty()
#ifdef DC_HAVE_LUA
        && g.lua.empty()
#endif
    ) {
        return;
    }
    g.ticks++;
    if (UserStatus != nullptr) {
        int dungeon = UserStatus->cur_georama;
        int floor = UserStatus->cur_floor;
        if (dungeon != g.prev_dungeon || floor != g.prev_floor) {
            ModEvent ev{};
            ev.i[0] = dungeon;
            ev.i[1] = floor;
            ev.i[2] = g.prev_dungeon == -1000 ? -1 : g.prev_dungeon;
            ev.i[3] = g.prev_floor == -1000 ? -1 : g.prev_floor;
            g.prev_dungeon = dungeon;
            g.prev_floor = floor;
            Fire("floor_change", ev);
        }
    }
    if (const char *debug_slot = std::getenv("DC_DEBUG_SAVESLOT"); debug_slot != nullptr && (g.ticks == 10 || g.ticks == 100)) {
        ScriptSaveSlot(std::atoi(debug_slot), g.ticks == 100 ? 1 : 0); // test aid: pretend the save menu loaded, then saved, this slot
    }
    if (g.frozen) { // let go if the game moved on (floor change, death) or the mod that froze it died
        bool owner_dead = false;
#ifdef DC_HAVE_LUA
        for (auto &owned : g.lua) {
            if (owned->L == static_cast<lua_State *>(g.freeze_owner) && owned->dead) {
                owner_dead = true;
            }
        }
#endif
        bool lost = g_town_frozen ? !InWalkingTown() : gameTask != GAME_TASK_PLAY;
        if (owner_dead || lost) {
            g_town_frozen = false;
            g.frozen = false;
            g.freeze_owner = nullptr;
            driveStepHold = 0;
            CMonUnitHold = 0;
            CEffectHold = 0;
            PlayTimeCountFlag(1);
            InputModBlock(g.block_buttons, g.block_sticks);
        }
    }
    if (g.loaded_pending && g.ticks >= g.loaded_at) {
        g.loaded_pending = false;
        ModEvent loaded{};
        Fire("game_loaded", loaded);
    }
    ModEvent ev{};
    ev.i[0] = static_cast<int32_t>(g.ticks);
    Fire("tick", ev);
    FlushText();
    UpdateInputEdges(); // after the handlers: key_pressed compares against the state at the end of the previous tick
}

void ScriptItemPickup(int *item, int *qty) {
    static bool busy = false; // a mod calling give_item from item_pickup must not recurse
    if (!g.inited || busy || item == nullptr || qty == nullptr) {
        return;
    }
    busy = true;
    ModEvent ev{};
    ev.i[0] = *item;
    ev.i[1] = *qty;
    Fire("item_pickup", ev);
    *item = ev.i[0];
    *qty = ev.i[1];
    busy = false;
}

void ScriptMonsterHit(int index, int attacker, int element, int *amount) {
    if (!g.inited || amount == nullptr || !MOk(index)) {
        return;
    }
    ModEvent ev{};
    ev.i[0] = index;
    ev.i[1] = NowMonstorUnit->monster[index].kind;
    ev.i[2] = attacker;
    ev.i[3] = *amount;
    ev.i[4] = element;
    Fire("monster_hit", ev);
    *amount = std::max(0, static_cast<int>(ev.i[3]));
}

void ScriptMonsterKilled(int index, int attacker) {
    if (!g.inited || !MOk(index)) {
        return;
    }
    ModEvent ev{};
    ev.i[0] = index;
    ev.i[1] = NowMonstorUnit->monster[index].kind;
    ev.i[2] = attacker;
    Fire("monster_killed", ev);
}

void ScriptPlayerLife(int chara, short *amount) {
    static bool busy = false;
    if (!g.inited || busy || amount == nullptr || *amount == 0) {
        return;
    }
    busy = true;
    ModEvent ev{};
    ev.i[0] = chara;
    if (*amount < 0) {
        ev.i[1] = -*amount;
        Fire("player_damage", ev);
        *amount = static_cast<short>(-std::clamp<int>(ev.i[1], 0, 32767));
    } else {
        ev.i[1] = *amount;
        Fire("player_heal", ev);
        *amount = static_cast<short>(std::clamp<int>(ev.i[1], 0, 32767));
    }
    busy = false;
}

// Called from the save menu when a slot is chosen to save to (saving = 1) or to load (saving = 0).
void ScriptSaveSlot(int slot, int saving) {
    if (!g.inited) {
        return;
    }
    g.slot = slot;
#ifdef DC_HAVE_LUA
    std::error_code error;
    for (auto &owned : g.lua) {
        LuaMod  &m = *owned;
        fs::path file = g.root / "_store" / m.name / ("slot" + std::to_string(slot) + ".json");
        if (saving != 0) {
            if (m.store.empty() && !fs::exists(file, error)) {
                continue;
            }
            fs::create_directories(file.parent_path(), error);
            std::ofstream out(file);
            out << m.store.dump(1) << std::endl;
            m.store_dirty = false;
        } else {
            m.store = ReadJson(file); // an empty object when the slot has none
        }
    }
#endif
    if (saving == 0) {
        g.loaded_pending = true;
        g.loaded_at = g.ticks + 90; // the game is still restoring its state for a moment
    }
}

// Damage dc.hurt_monster queued for enemy slot i, handed to CheckDmg once (see the monstorunit.cpp patch).
int ModsTakeDamage(int i, int *owner) {
    if (i < 0 || i >= 16 || g.hurt[i] <= 0) {
        return 0;
    }
    int amount = g.hurt[i];
    g.hurt[i] = 0;
    if (owner != nullptr) {
        *owner = g.hurt_owner[i];
    }
    return amount;
}

// Called each time the game builds a view matrix from a camera: while a script holds the town camera, the active one is put where the
// script said (eye and reference), so the game's own camera logic cannot move it back.
void ModsCameraApply(CCamera *camera) {
    if (!g.cam_active || camera == nullptr || camera != NowCamera) {
        return;
    }
    for (int k = 0; k < 4; k++) {
        camera->pos[k] = camera->next_pos[k] = g.cam_eye[k];
        camera->ref[k] = camera->next_ref[k] = g.cam_ref[k];
    }
}

namespace {
const State::FloorSize &SizeFor(int dungeon) {
    return dungeon >= 0 && dungeon < 7 && g.floor_dungeon_set[dungeon] ? g.floor_dungeon[dungeon] : g.floor_default;
}
} // namespace

// Called where the game builds a floor's rooms (it passes 6) for the dungeon being entered. DC_ROOM_MAX overrides it for testing.
int ModsRoomMax(int original, int dungeon) {
    const State::FloorSize &f = SizeFor(dungeon);
    int n = f.rooms > 0 ? f.rooms : original;
    if (const char *e = std::getenv("DC_ROOM_MAX"); e != nullptr && e[0] != 0) {
        n = std::atoi(e);
    }
    return std::clamp(n, 2, 14);
}

// Called where the editor places or removes a part for the local player.
void ModsGeoPlaced(int parts_no, const float *pos, int rot) {
    if (g_geo_ops.size() > 64) {
        return;
    }
    GeoOp op;
    op.kind = 1;
    op.map = pEditGround != nullptr ? pEditGround->map_no : -1;
    op.parts = parts_no;
    for (int k = 0; k < 3; k++) op.pos[k] = pos[k];
    op.rot = rot;
    g_geo_ops.push_back(op);
}
void ModsGeoRemoved(const float *pos) {
    if (g_geo_ops.size() > 64) {
        return;
    }
    GeoOp op;
    op.kind = 2;
    op.map = pEditGround != nullptr ? pEditGround->map_no : -1;
    for (int k = 0; k < 3; k++) op.pos[k] = pos[k];
    g_geo_ops.push_back(op);
}
// Called in the editor's walk/Georama toggle test: true once when a remote placement forces this player into the Georama view.
bool ModsGeoForce() {
    if (!g_geo_force) {
        return false;
    }
    if (ClockTickCount() > g_geo_force_until || GameMode == ED_MODE_GEORAMA) {
        g_geo_force = false; // in the view already, or too late
        return false;
    }
    return GameMode == ED_MODE_WALK;
}

// The manual screen is opening from the main menu: it is the MODS page, unless the page's Manuals row asked for the real manuals.
void ModsPageNote() {
    g_nui_owner.clear();
    if (g_manual_pass) {
        g_manual_pass = false;
        NuNoteOpen(false);
    } else {
        g_mods_menu_requested = true;
        NuNoteOpen(true);
    }
}

// ---- Play as another character in a town --------------------------------------------------------------------------------------
// The town only has Toan's own model (chara/c01d.chr + info.cfg). The other five bodies come from the dungeon packs (dun/mainchara/<id>.chr +
// base.cfg), loaded through the same EdLoadMainChara. The town asks for motion numbers of Toan's town set (0 stand, 1 run, 2 walk, 3/4 doors, 5/6
// item get, 7 no, 8 fall, 9 land, 10 double door); the dungeon sets agree on 0-2 and have the rest elsewhere, so ModsRemapMotion translates.
static int g_town_chara = 0;     // the body the player asked for
static int g_town_loaded = 0;    // the body that is loaded now
static bool g_town_reload = false;
static const char *const kTownPack[6] = {"chara/c01d.chr", "dun/mainchara/c04b.chr", "dun/mainchara/c06b.chr", "dun/mainchara/c05a.chr", "dun/mainchara/c10b.chr", "dun/mainchara/c18a.chr"};

static bool TownCharaAvailable(int n) {
    static const bool any = std::getenv("DC_TOWN_ANY") != nullptr; // testing: every body without the story
    return n == 0 || (n > 0 && n < 6 && (any || (UserStatus != nullptr && UserStatus->max_hp[n] > 0)));
}

void ModsLoadTownChara() { // from the town's initialisation in place of the plain load of Toan
    int n = TownCharaAvailable(g_town_chara) ? g_town_chara : 0;
    EdLoadMainChara(const_cast<char *>(kTownPack[n]), const_cast<char *>(n == 0 ? "info.cfg" : "base.cfg"), &CharaBuffer);
    g_town_loaded = n;
}

int ModsRemapMotion(CCharacter *c, int motion) {
    if (g_town_loaded == 0 || c != static_cast<CCharacter *>(&MainChara)) {
        return motion;
    }
    static const int map[11] = {0, 1, 2, 0, 0, 34, 35, 31, 32, 0, 0};
    return motion >= 0 && motion < 11 ? map[motion] : motion;
}

static void TownCharaTick() {
    if (!g_town_reload || Chara == nullptr || pEditGround == nullptr || MapNo >= 200) {
        return;
    }
    g_town_reload = false;
    float pos[4], rot[4];
    Chara->GetPosition(pos);
    Chara->GetRotation(rot);
    ModsLoadTownChara();
    Chara->SetPosition(pos);
    Chara->SetRotation(rot);
    Chara->motion_no = 0;
}

// dc.town_chara([n]) -> the body in use in towns (0 Toan, 1 Xiao, 2 Goro, 3 Ruby, 4 Ungaga, 5 Osmond). With an argument it switches to that character
// (when they have joined), at once when a town is open. Returns the body now selected.
int L_town_chara(lua_State *L) {
    if (lua_isnumber(L, 1)) {
        int n = static_cast<int>(lua_tointeger(L, 1));
        if (TownCharaAvailable(n) && n != g_town_chara) {
            g_town_chara = n;
            g_town_reload = true;
        }
    }
    lua_pushinteger(L, g_town_chara);
    return 1;
}

// ---- First person -------------------------------------------------------------------------------------------------------------
// dc.first_person(on) puts the dungeon camera at the character's eyes: the mouse (or right stick) looks around freely, the character still walks
// where the stick points relative to the view, and looking down shows the body and the weapon. The game's own follow camera keeps running (its
// angle is the yaw, the movement code reads it); this overrides where the eye sits and what it looks at after each camera step.
struct FirstPerson {
    bool  on = false;
    bool  applied = false;
    float pitch = 0.0f; // radians above the horizon
    float dy = 0.0f;    // mouse movement waiting to be turned into pitch
} g_fp;

extern s32 viewMode__2;

// Puts `cam` at the eyes of the character standing at `feet`, looking along the follow camera's heading and the current pitch.
static void ApplyFirstPerson(CCameraFollow *cam, const float *feet, float eye_height) {
    g_fp.pitch = std::clamp(g_fp.pitch - g_fp.dy * 0.0375f - GamePad.GetRYf() * 0.03f, -1.45f, 1.45f);
    g_fp.dy = 0.0f;
    float yaw = cam->angle;
    float fx = -std::sin(yaw), fz = -std::cos(yaw);
    float cp = std::cos(g_fp.pitch), sp = std::sin(g_fp.pitch);
    // a little ahead of the face, so the head is not around the eye (looking straight down shows the body, hands and weapon instead)
    float pos[3] = {feet[0] + fx * 1.3f, feet[1] + eye_height, feet[2] + fz * 1.3f};
    float ref[3] = {pos[0] + fx * cp * 20.0f, pos[1] + sp * 20.0f, pos[2] + fz * cp * 20.0f};
    for (int i = 0; i < 3; i++) {
        cam->pos[i] = cam->next_pos[i] = pos[i];
        cam->ref[i] = cam->next_ref[i] = ref[i];
    }
    cam->SetSpeed(1.0f); // the view turns at once, not by easing toward the angle
    g_fp.applied = true;
}

static bool FirstPersonIdle(CCameraFollow *cam, float restore_speed) {
    if (!g_fp.on) {
        if (g_fp.applied) {
            cam->SetSpeed(restore_speed);
            g_fp.applied = false;
        }
        g_fp.dy = 0.0f;
        return true;
    }
    return false;
}

void ModsFirstPerson() { // dungeon, after the camera step
    if (NowCamera__3 == nullptr || FirstPersonIdle(NowCamera__3, 8.0f)) {
        return;
    }
    if (gameTask != GAME_TASK_PLAY || viewMode__2 != 0 || BtActStatus.camera_hold != 0 || g.frozen) {
        g_fp.dy = 0.0f;
        return;
    }
    static const float eye[6] = {14.5f, 12.5f, 14.5f, 14.5f, 16.5f, 13.5f};
    ApplyFirstPerson(NowCamera__3, CharaMain.pos, eye[std::clamp(static_cast<int>(UserStatus->cur_chara), 0, 5)]);
}

void ModsFirstPersonTown() { // town, after the walking camera step
    if (Chara == nullptr || FirstPersonIdle(&MainCamera, 4.0f)) {
        return;
    }
    if (g.frozen) {
        g_fp.dy = 0.0f;
        return;
    }
    float pos[4];
    Chara->GetPosition(pos);
    ApplyFirstPerson(&MainCamera, pos, Chara->body_height * 0.9f);
}
// dc.first_person([on]) -> whether first person is on (with an argument it switches it).
int L_first_person(lua_State *L) {
    if (lua_gettop(L) >= 1) {
        bool on = lua_toboolean(L, 1) != 0;
        if (on != g_fp.on) {
            g_fp.pitch = 0.0f;
        }
        g_fp.on = on;
        if (lua_isnumber(L, 2)) {
            g_fp.pitch = std::clamp(static_cast<float>(lua_tonumber(L, 2)), -1.45f, 1.45f);
        }
    }
    lua_pushboolean(L, g_fp.on ? 1 : 0);
    return 1;
}

// The mouse turns the follow cameras directly (not through the right stick): radians and height units to add this tick. A mod menu or a freeze
// holds the camera still. The "controls" setting mouse_look scales it (1 is the default).
void ModsMouseLook(float *turn, float *height) {
    *turn = 0.0f;
    *height = 0.0f;
    if (g.frozen || g.block_sticks) {
        return;
    }
    float dx = 0.0f, dy = 0.0f;
    InputMouseLook(&dx, &dy);
    SharedLoad();
    float k = 1.0f;
    if (g.shared.contains("controls") && g.shared["controls"].is_object() && g.shared["controls"].contains("mouse_look") &&
        g.shared["controls"]["mouse_look"].is_number()) {
        k = std::clamp(g.shared["controls"]["mouse_look"].get<float>(), 0.1f, 5.0f);
    }
    *turn = -dx * 0.0030f * k;
    *height = dy * 0.0800f * k;
    g_fp.dy += *height;
}

// True for one call when any key or mouse button has just gone down (the title's "push start" takes any of them).
bool ModsAnyKeyEdge() {
    static std::array<bool, SDL_SCANCODE_COUNT> before{};
    static unsigned                              mouse_before = 0;
    int                                          count = 0;
    const bool                                  *now = SDL_GetKeyboardState(&count);
    bool                                         edge = false;
    if (now != nullptr) {
        for (int i = 0; i < count && i < SDL_SCANCODE_COUNT; i++) {
            if (now[i] && !before[i]) {
                edge = true;
            }
            before[i] = now[i];
        }
    }
    unsigned mouse = SDL_GetMouseState(nullptr, nullptr);
    if ((mouse & ~mouse_before) != 0) {
        edge = true;
    }
    mouse_before = mouse;
    return edge;
}

// ---- Menu mouse ------------------------------------------------------------------------------------------------------------------
// In the game's menus the mouse is the pointing hand: the hand follows the pointer, the item under it becomes the selected one (the game's own
// selection is walked there with pad presses, watching where the game puts its hand), a left click is Cross and a right click is Circle.
// The hand is the stayframe sprite at texel (64,40) 32x32; the game draws it at its selected item, so its place tells where the selection is.

bool ModsSprite(const char *texture, int &sx, int &sy, int sw, int sh, int u, int v, int tw, int th) {
    static const bool log = std::getenv("DC_LOG_SPRITES") != nullptr;
    if (log && texture != nullptr && sw <= 48 && sh <= 48) {
        static std::set<std::string> seen;
        std::string                  key = std::string(texture) + ":" + std::to_string(u) + "," + std::to_string(v) + "," + std::to_string(tw) + "," + std::to_string(th);
        if (seen.insert(key).second) {
            std::printf("[sprite] %s screen %d,%d %dx%d  texel %d,%d %dx%d", texture, sx, sy, sw, sh, u, v, tw, th);
            std::printf("%c", 10);
        }
    }
    if (const char *all = std::getenv("DC_LOG_SPRITES_FROM"); all != nullptr && texture != nullptr && ClockTickCount() >= std::atoll(all)) {
        static int n = 0;
        if (n++ < 300) {
            std::printf("[sp] %s screen %d,%d %dx%d texel %d,%d %dx%d", texture, sx, sy, sw, sh, u, v, tw, th);
            std::printf("%c", 10);
        }
    }
    if (texture != nullptr && g_mm.enabled && sw == 32 && sh == 32 && tw == 32 && th == 32 &&
        ((u == 64 && v == 40 && std::strcmp(texture, "stayframe") == 0) || (u == 210 && v == 248 && std::strcmp(texture, "option2") == 0) ||
         (u == 96 && v == 96 && std::strcmp(texture, "gaiji") == 0) || (u == 0 && v == 0 && std::strcmp(texture, "icon01") == 0))) { // icon01: the title's hand
        g_mm.hand_x = static_cast<float>(sx);
        g_mm.hand_y = static_cast<float>(sy);
        g_mm.hand_seen = ClockTickCount();
        if (u == 0 && v == 0 && std::strcmp(texture, "icon01") == 0) {
            g_mm.title_seen = g_mm.hand_seen; // the title menu is chosen by where the pointer is (ModsTitleHover), not by walking the selection
        }
        if (g_mm.free && g_mm.mouse_x > -500) { // the hand goes where the pointer is, its fingertip (right edge, middle) on it
            sx = static_cast<int>(g_mm.mouse_x) - 30;
            sy = static_cast<int>(g_mm.mouse_y) - 16;
        }
    }
    return false;
}

// Pointer position in the 640x480 frame, or false when it cannot be read.
bool PointerInFrame(float &x, float &y) {
    if (const char *fake = std::getenv("DC_FAKE_MOUSE"); fake != nullptr && std::sscanf(fake, "%f,%f", &x, &y) == 2) {
        return true; // a test aid: the pointer stays at this place in the 640x480 frame
    }
    SDL_Window *window = WindowHandle();
    if (window == nullptr) {
        return false;
    }
    float wx = 0, wy = 0;
    SDL_GetMouseState(&wx, &wy);
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    if (ww <= 0 || wh <= 0) {
        return false;
    }
    gfx::LogicalMapping m = gfx::GetUiMapping(gfx::kMainTarget);
    if (m.scale_x <= 0.0f || m.scale_y <= 0.0f) {
        return false;
    }
    float px = wx * static_cast<float>(pw) / static_cast<float>(ww);
    float py = wy * static_cast<float>(ph) / static_cast<float>(wh);
    x = (px - m.offset_x) / m.scale_x;
    y = (py - m.offset_y) / m.scale_y;
    return true;
}

void MenuMouseTick() {
    if (!g_mm.enabled) {
        if (g_mm.free) {
            MouseSetFree(false);
            SDL_ShowCursor();
            g_mm.free = false;
        }
        return;
    }
    long long now = ClockTickCount();
    bool      menu = now - g_mm.hand_seen <= 3; // the game drew its hand a moment ago: a menu is up
    if (menu && !g_mm.free) {
        MouseSetFree(true);
        SDL_HideCursor();
        g_mm.free = true;
        g_mm.prev_err = 1e9f;
        g_mm.last_dir = 0;
        g_mm.h_state = 0;
        g_mm.step_y = 0;
        g_mm.blocked_dir = 0;
        g_mm.awaiting = false;
        g_mm.ring_n = 0;
    } else if (!menu && g_mm.free) {
        MouseSetFree(false);
        SDL_ShowCursor();
        g_mm.free = false;
        g_mm.mouse_x = -1000;
        return;
    }
    if (!g_mm.free) {
        return;
    }
    float mx = 0, my = 0;
    if (!PointerInFrame(mx, my)) {
        return;
    }
    g_mm.mouse_x = mx;
    g_mm.mouse_y = my;
    // clicks
    Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
    bool   left = (buttons & SDL_BUTTON_LMASK) != 0, right = (buttons & SDL_BUTTON_RMASK) != 0;
    if (left && !g_mm.left_before) {
        InputInjectButtons(kInputCross, 3);
    }
    if (right && !g_mm.right_before) {
        InputInjectButtons(kInputCircle, 3);
    }
    g_mm.left_before = left;
    g_mm.right_before = right;
    if (now - g_mm.title_seen <= 3) {
        return; // the title: ModsTitleHover picks the row
    }
    // walk the game's selection to the item under the pointer: press a direction, wait for the hand to settle, see whether it moved.
    // The game's hand sways where it rests, so its place is averaged over a dozen ticks.
    g_mm.ring_x[g_mm.ring_at] = g_mm.hand_x;
    g_mm.ring_y[g_mm.ring_at] = g_mm.hand_y;
    g_mm.ring_at = (g_mm.ring_at + 1) % 12;
    g_mm.ring_n = std::min(12, g_mm.ring_n + 1);
    {
        float ax = 0, ay = 0;
        for (int i = 0; i < g_mm.ring_n; i++) {
            ax += g_mm.ring_x[i];
            ay += g_mm.ring_y[i];
        }
        g_mm.prev_avg_x = g_mm.avg_x;
        g_mm.prev_avg_y = g_mm.avg_y;
        g_mm.avg_x = ax / static_cast<float>(g_mm.ring_n);
        g_mm.avg_y = ay / static_cast<float>(g_mm.ring_n);
    }
    bool calm = std::fabs(g_mm.avg_y - g_mm.prev_avg_y) < 0.7f && std::fabs(g_mm.avg_x - g_mm.prev_avg_x) < 0.7f && g_mm.ring_n >= 12;
    g_mm.settled = calm ? g_mm.settled + 1 : 0;
    if (std::fabs(g_mm.avg_y - g_mm.press_hy) > 8.0f || std::fabs(g_mm.avg_x - g_mm.last_hx) > 8.0f) {
        g_mm.moved_since = true;
    }
    if (left || right) {
        return;
    }
    float tipx = g_mm.avg_x + 30.0f, tipy = g_mm.avg_y + 16.0f; // where the game's hand points
    if (g_mm.awaiting) {
        if (g_mm.settled >= 6 && (g_mm.moved_since || now - g_mm.press_tick > 40)) {
            g_mm.awaiting = false;
            if (!g_mm.moved_since) { // that direction does nothing here (the end of the list, a wall): leave it be until the pointer moves a way
                g_mm.blocked_dir = g_mm.last_dir;
                g_mm.parked_x = g_mm.mouse_x;
                g_mm.parked_y = g_mm.mouse_y;
            } else if (g_mm.last_dir == 1 || g_mm.last_dir == 2) {
                g_mm.step_y = std::fabs(g_mm.avg_y - g_mm.press_hy);
            } else if (g_mm.last_dir == 3 || g_mm.last_dir == 4) {
                bool sideways = std::fabs(g_mm.avg_x - g_mm.press_hx) > 8.0f && std::fabs(g_mm.avg_y - g_mm.press_hy) < 10.0f;
                g_mm.h_state = sideways ? 1 : -1;
            }
        } else {
            return;
        }
    }
    if (g_mm.settled < 4 || now < g_mm.next_press) {
        return;
    }
    float dx = mx - tipx, dy = my - tipy;
    float tolerance = g_mm.step_y > 4.0f ? g_mm.step_y * 0.5f + 1.0f : 15.0f; // half a row apart, so rows 32 apart (the title) still tell apart
    int   dir = 0;
    if (std::fabs(dy) > tolerance) {
        dir = dy < 0 ? 1 : 2;
    } else if (g_mm.h_state >= 0) {
        if (dx > 110.0f) {
            dir = 4;
        } else if (dx < -50.0f) {
            dir = 3;
        }
    }
    if (dir != 0 && dir == g_mm.blocked_dir && std::fabs(mx - g_mm.parked_x) + std::fabs(my - g_mm.parked_y) < 24.0f) {
        dir = 0;
    }
    if (dir == 0) {
        return;
    }
    if (std::getenv("DC_LOG_MM") != nullptr) {
        std::printf("[mm] dir %d dy %.0f hand %.0f,%.0f mouse %.0f,%.0f step %.0f", dir, dy, g_mm.hand_x, g_mm.hand_y, mx, my, g_mm.step_y);
        std::printf("%c", 10);
    }
    unsigned short pad = dir == 1 ? kInputUp : dir == 2 ? kInputDown : dir == 3 ? kInputLeft : kInputRight;
    InputInjectButtons(pad, 2);
    g_mm.last_dir = dir;
    g_mm.awaiting = true;
    g_mm.moved_since = false;
    g_mm.press_tick = now;
    g_mm.press_hy = g_mm.avg_y;
    g_mm.press_hx = g_mm.avg_x;
    g_mm.last_hx = g_mm.avg_x;
    g_mm.next_press = now + 4;
}

// The title menu's row under the pointer (0 new game, 1 load, 2 option), or -1 when the pointer has not moved since the last call or is on no row.
// The rows are the start3 sprites at y 296, 328 and 360 (32 high); the game takes the row as its selection (patched into the title's menu step).
int ModsTitleHover() {
    static float last_x = -1, last_y = -1;
    if (!g_mm.free || g_mm.mouse_x < -500) {
        return -1;
    }
    float x = g_mm.mouse_x, y = g_mm.mouse_y;
    bool  moved = std::fabs(x - last_x) + std::fabs(y - last_y) > 2.0f;
    last_x = x;
    last_y = y;
    if (!moved || x < 190.0f || x > 450.0f || y < 296.0f || y >= 392.0f) {
        return -1;
    }
    return static_cast<int>((y - 296.0f) / 32.0f);
}

void ModsNpcTalkTick(int villager_id) {
    g_talk_id = villager_id;
    g_talk_tick = ClockTickCount();
}

// Co-op: both games must shuffle a town's villager schedule the same way, so while the launcher has switched co-op on the shuffle is seeded from
// the map. The ordinary random stream is restarted afterwards.
unsigned ModsVillagerSeed(int map_no) {
    if (std::getenv("DC_NO_VSEED") != nullptr) {
        return 0u;
    }
    const char *e = std::getenv("DC_LAUNCH_COOP");
    return (e != nullptr && e[0] != 0) ? 12345u + static_cast<unsigned>(map_no) * 7919u : 0u;
}
unsigned ModsVillagerReseed() { // a fresh random seed for the game's own generator, once the shared shuffle is done
    return static_cast<unsigned>(ClockTickCount()) * 2654435761u ^ static_cast<unsigned>(std::time(nullptr)) ^ 0x9E3779B9u;
}

// Called at the end of EdMoveVillager: villagers that follow the host (or stand still while talked to) are put where they belong.
void ModsNpcStep() {
    if (g_town_frozen) { // a mod menu is open in the town: everyone stands still
        for (int i = 0; i < 10; i++) {
            CNPCharacter &v = EdVillager[i];
            if (v.initialized == 0) {
                continue;
            }
            auto at = g_town_freeze_pos.find(v.villager_id);
            if (at == g_town_freeze_pos.end()) {
                float pos[4] = {0, 0, 0, 1};
                v.GetPosition(pos);
                at = g_town_freeze_pos.emplace(v.villager_id, std::array<float, 3>{pos[0], pos[1], pos[2]}).first;
            }
            v.SetPosition(at->second[0], at->second[1], at->second[2]);
            v.sequence_enabled = 0;
        }
        return;
    }
    if (g_npc_puppets.empty() && g_npc_hold.empty()) {
        return;
    }
    long long now = ClockTickCount();
    int       talking = NpcTalkingNow();
    for (int i = 0; i < 10; i++) {
        CNPCharacter &v = EdVillager[i];
        if (v.initialized == 0) {
            continue;
        }
        int  id = v.villager_id;
        auto hold = g_npc_hold.find(id);
        if (hold != g_npc_hold.end()) {
            if (now > hold->second) {
                g_npc_hold.erase(hold);
                g_npc_hold_pos.erase(id);
            } else {
                const auto &h = g_npc_hold_pos[id];
                v.SetPosition(h[0], h[1], h[2]);
                continue;
            }
        }
        auto it = g_npc_puppets.find(id);
        if (it == g_npc_puppets.end() || !it->second.on || id == talking) {
            continue; // the villager the local player talks to keeps its own behaviour
        }
        const NpcPuppet &p = it->second;
        float            pos[4] = {0, 0, 0, 1};
        v.GetPosition(pos);
        float dx = p.pos[0] - pos[0], dz = p.pos[2] - pos[2];
        bool  snap = dx * dx + dz * dz > 250.0f * 250.0f;
        float k = snap ? 1.0f : 0.35f;
        v.SetPosition(pos[0] + dx * k, snap ? p.pos[1] : pos[1] + (p.pos[1] - pos[1]) * k, pos[2] + dz * k);
        float rot[4] = {0, 0, 0, 1};
        v.GetRotation(rot);
        float da = p.ry - rot[1];
        while (da > 3.14159265f) da -= 6.2831853f;
        while (da < -3.14159265f) da += 6.2831853f;
        v.SetRotation(rot[0], rot[1] + da * (snap ? 1.0f : 0.4f), rot[2]);
        if (v.motion_no != p.motion || v.motion_flags != p.flags) {
            v.SetMotion(p.motion, p.flags, p.speed);
        }
        v.sequence_enabled = 0; // its own walking plan must not move it
    }
}

// Called where the game places a floor's enemies: scaled by dc.set_floor_size, never past its 16 slots. DC_MONSTER_MULT overrides it.
int ModsMonsterCount(int original, int dungeon) {
    float m = SizeFor(dungeon).mult;
    if (const char *e = std::getenv("DC_MONSTER_MULT"); e != nullptr && e[0] != 0) {
        m = static_cast<float>(std::atof(e));
    }
    if (original <= 0 || m <= 1.0f) {
        return original;
    }
    int scaled = std::min(16, std::max(original, static_cast<int>(std::lround(original * m))));
    std::printf("enemies placed: %d -> %d (dungeon %d)", original, scaled, dungeon);
    std::printf("%c", 10);
    return scaled;
}

// Called where the floor builder rolls a room's width and height (3 + 0..1 cells): how many extra cells the range may reach.
float ModsRoomBonus(int dungeon) {
    int bonus = SizeFor(dungeon).bonus;
    if (const char *e = std::getenv("DC_ROOM_BONUS"); e != nullptr && e[0] != 0) {
        bonus = std::atoi(e);
    }
    return static_cast<float>(std::clamp(bonus, 0, 3));
}

// Called by the dungeon entrance menu every frame its floor list is open.
void ModsFloorSelect(int dungeon, int selected, int top, float list_y, int count) {
    g.floor_select.dungeon = dungeon;
    g.floor_select.selected = selected;
    g.floor_select.top = top;
    g.floor_select.list_y = list_y;
    g.floor_select.count = count;
    g.floor_select.seen_tick = g.ticks;
}
