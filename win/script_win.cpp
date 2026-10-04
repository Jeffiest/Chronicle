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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "mod_api.h"
#include "platform/paths.hpp"
#include "userstatus.hpp"

#ifdef DC_HAVE_LUA
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#endif

namespace fs = std::filesystem;

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
bool Protected(LuaMod &mod, int nargs, int nresults) {
    int base = lua_gettop(mod.L) - nargs; // index of the function (arguments sit above it)
    lua_pushcfunction(mod.L, MessageHandler);
    lua_insert(mod.L, base);
    g.instructions = 0;
    int status = lua_pcall(mod.L, nargs, nresults, base);
    lua_remove(mod.L, base);
    if (status != LUA_OK) {
        Log(mod.name, std::string("script error, mod disabled:\n") + lua_tostring(mod.L, -1));
        lua_pop(mod.L, 1);
        mod.dead = true;
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
        {"mod_dir", L_mod_dir},   {nullptr, nullptr}};
    luaL_newlib(L, functions);
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "api_version");
    lua_setglobal(L, "dc");

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

void LuaFire(const char *name, ModEvent &ev) {
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
            int nargs = 0;
            if (std::strcmp(name, "tick") == 0) {
                lua_pushinteger(L, ev.i[0]);
                nargs = 1;
            } else if (std::strcmp(name, "floor_change") == 0 || std::strcmp(name, "item_pickup") == 0) {
                int count = std::strcmp(name, "item_pickup") == 0 ? 2 : 4;
                for (int k = 0; k < count; k++) {
                    lua_pushinteger(L, ev.i[k]);
                }
                nargs = count;
            }
            bool item = std::strcmp(name, "item_pickup") == 0;
            if (!Protected(m, nargs, item ? 2 : 0)) {
                continue;
            }
            if (item) { // a handler may return replacement item and quantity
                if (lua_isinteger(L, -2)) {
                    ev.i[0] = static_cast<int32_t>(lua_tointeger(L, -2));
                }
                if (lua_isinteger(L, -1)) {
                    ev.i[1] = static_cast<int32_t>(lua_tointeger(L, -1));
                }
                lua_pop(L, 2);
            }
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
    g.allow_native = ReadJson(g.root / "mods.json").value("allow_native", false);
    std::vector<fs::path> mods;
    for (const auto &entry : fs::directory_iterator(g.root, error)) {
        std::string name = Utf8(entry.path().filename());
        if (entry.is_directory(error) && !name.empty() && name[0] != '_' && name[0] != '.') {
            mods.push_back(entry.path());
        }
    }
    std::sort(mods.begin(), mods.end(), [](const fs::path &a, const fs::path &b) { return Fold(Utf8(a)) < Fold(Utf8(b)); });
    for (const fs::path &mod : mods) {
        std::string name = Utf8(mod.filename());
        if (!ReadJson(mod / "mod.json").value("enabled", true)) {
            continue;
        }
        LoadNative(mod, name);
#ifdef DC_HAVE_LUA
        LoadLua(mod, name);
#else
        if (fs::is_regular_file(mod / "scripts" / "main.lua", error)) {
            Log(name, "has Lua scripts but this build has no Lua (run setup_lua.ps1 and rebuild); skipped");
        }
#endif
    }
    ModEvent ev{};
    Fire("init", ev);
}

} // namespace

void ScriptTick() {
    if (!g.inited) {
        Init();
    }
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
    ModEvent ev{};
    ev.i[0] = static_cast<int32_t>(g.ticks);
    Fire("tick", ev);
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
