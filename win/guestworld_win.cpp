// Guest world (Windows fork multiplayer): "join their world", as in Dark Souls. A guest plays in the host's world (story flags, unlocked towns and
// floors, Georama, time of day) while their own inventory, gilda, weapons and party stay theirs. The guest's own world is kept aside and put back when
// they leave, and any save written during the session holds the guest's own world with the items and gilda they have now.
//
// "World" is the set of regions in Regions() below; everything else in the save (the party's status, the pack, the stock) is the player's.
#include "guestworld.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "dngstatusdata.hpp"
#include "savedata.hpp"
#include "userstatus.hpp"

extern s32 MapNo;
extern s32 NextMapNo;
void       MapJump(int map_no, int event_no);

// A friend of CSaveData (patched in by gen_win_src.py) so the private world fields can be reached.
struct GuestWorldAccess {
    // Calls f(pointer, bytes) for every world region of `s`. `live_only` regions (flags and progress) are the ones refreshed while a session runs;
    // the rest (the Georama and the clock) are only taken when the guest first enters, because the Georama is kept level by edit messages.
    template <class F>
    static void Regions(CSaveData &s, bool entering, F f) {
        f(s.game_flags, sizeof(s.game_flags));
        f(s.game_int_flag, sizeof(s.game_int_flag));
        f(s.map_flags, sizeof(s.map_flags));
        f(s.map_init_flags, sizeof(s.map_init_flags));
        f(s.visit_map, sizeof(s.visit_map));
        f(s.quest_dungeon, sizeof(s.quest_dungeon));
        f(&s.quest_dungeon_total, sizeof(s.quest_dungeon_total));
        CDngStatusData &d = s.dng_status;
        f(d.floor_reached, sizeof(d.floor_reached));
        f(d.atra_grid, sizeof(d.atra_grid));
        f(d.atra_registry, sizeof(d.atra_registry));
        f(d.kills, sizeof(d.kills));
        f(d.res_limit_zone_id, sizeof(d.res_limit_zone_id));
        if (entering) {
            f(&s.now_time, sizeof(s.now_time));
            f(&s.day, sizeof(s.day));
            f(s.georama, sizeof(s.georama));
            f(s.elem_data, sizeof(s.elem_data));
            f(s.special_npc, sizeof(s.special_npc));
        }
    }
    static std::string Pack(CSaveData &s, bool entering) {
        std::string out;
        Regions(s, entering, [&](void *p, size_t n) { out.append(static_cast<const char *>(p), n); });
        return out;
    }
    // false when `bytes` is not the size Pack made
    static bool Unpack(CSaveData &s, bool entering, const std::string &bytes) {
        size_t need = 0;
        Regions(s, entering, [&](void *, size_t n) { need += n; });
        if (bytes.size() != need) {
            return false;
        }
        size_t at = 0;
        Regions(s, entering, [&](void *p, size_t n) {
            std::memcpy(p, bytes.data() + at, n);
            at += n;
        });
        return true;
    }
    // The part of an entering-world payload that holds the flags and progress (listed first), for refreshing a running session.
    static std::string LiveSlice(const std::string &payload) {
        size_t need = 0;
        static CSaveData dummy;
        Regions(dummy, false, [&](void *, size_t n) { need += n; });
        return payload.size() >= need ? payload.substr(0, need) : std::string();
    }
    static int MapOf(CSaveData &s) { return s.map_no; }
    static void SetMap(CSaveData &s, int map) { s.map_no = map; }
};

namespace {

std::string g_backup;      // the guest's own world while a session runs (everything Regions(entering=true) holds)
int         g_home_map = 0; // the map their own save resumes on
bool        g_in = false;
bool        g_loaded = false; // a save was loaded (kept for GwAfterLoad)

// A game is running when the current map is a town or a dungeon (not the title or a movie).
bool Running() { return MapNo >= 0 && MapNo < 300; }
std::string g_host_world;  // the last world the host sent: header int map, then Pack(entering=true)
bool        g_jump_pending = false;

constexpr char kMagic[4] = {'G', 'W', '1', 0};

void Enter(const std::string &world, bool jump) {
    if (world.size() < 8 || std::memcmp(world.data(), kMagic, 4) != 0) {
        return;
    }
    CSaveData &s = *SaveData;
    int        host_map;
    std::memcpy(&host_map, world.data() + 4, 4);
    std::string payload = world.substr(8);
    if (!g_in) {
        g_backup = GuestWorldAccess::Pack(s, true);
        g_home_map = GuestWorldAccess::MapOf(s);
    }
    if (!GuestWorldAccess::Unpack(s, true, payload)) {
        std::printf("[world] the host's world does not match this build (%zu bytes); staying in your own world\n", payload.size());
        return;
    }
    g_in = true;
    std::printf("[world] entered the host's world (map %d)\n", host_map);
    if (host_map >= 0 && host_map < 300) {
        GuestWorldAccess::SetMap(s, host_map < 200 ? host_map : g_home_map);
        if (jump) {
            MapJump(host_map, -1);
            g_jump_pending = true;
        }
    }
}

} // namespace

std::string GwCapture() {
    if (SaveData == nullptr || !Running()) {
        return std::string();
    }
    CSaveData  &s = *SaveData;
    std::string out(kMagic, 4);
    int         map = MapNo;
    out.append(reinterpret_cast<const char *>(&map), 4);
    out += GuestWorldAccess::Pack(s, true);
    return out;
}

void GwSetHostWorld(const std::string &world) {
    g_host_world = world;
    if (SaveData == nullptr || !Running()) {
        return; // nothing loaded yet: the world is taken when a save is loaded
    }
    if (!g_in) {
        Enter(world, true);
    } else if (world.size() >= 8 && std::memcmp(world.data(), kMagic, 4) == 0) {
        GuestWorldAccess::Unpack(*SaveData, false, GuestWorldAccess::LiveSlice(world.substr(8))); // flags and progress only; the Georama follows edit messages
    }
}

void GwLeave() {
    g_host_world.clear();
    if (!g_in || SaveData == nullptr) {
        return;
    }
    GuestWorldAccess::Unpack(*SaveData, true, g_backup);
    GuestWorldAccess::SetMap(*SaveData, g_home_map);
    g_in = false;
    std::printf("[world] back in your own world (map %d)\n", g_home_map);
    if (Running()) {
        MapJump(g_home_map, -1);
        g_jump_pending = true;
    }
}

bool GwActive() { return g_in; }

bool GwTakeJump() {
    bool jump = g_jump_pending;
    g_jump_pending = false;
    return jump;
}

// Called right after a save file has been loaded into SaveData.
void GwAfterLoad() {
    g_loaded = true;
    g_in = false; // a fresh load is the player's own world
    if (!g_host_world.empty()) {
        Enter(g_host_world, false); // the game goes on to the map the save now names
    }
}

// Called on the copy of SaveData that is about to be written: it gets the player's own world back (and their own resume map).
void GwFixSave(CSaveData *copy) {
    if (!g_in || copy == nullptr) {
        return;
    }
    GuestWorldAccess::Unpack(*copy, true, g_backup);
    GuestWorldAccess::SetMap(*copy, g_home_map);
}
