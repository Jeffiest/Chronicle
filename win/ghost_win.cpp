// Remote players in the dungeon and in towns (Windows fork multiplayer). Each ghost is a CCharacter loaded from the Toan pack of
// the scene (dungeon: dun/mainchara/c01d.chr, town: chara/c01d.chr) with its own frame, motions, memory AND textures, so its pose
// and its colours never disturb the real player's. To keep the textures separate the ghost loads a private copy of the pack in
// which every texture name is renamed ("c01d04" -> "g11d04": first byte = colour 'g' blue / 'h' natural, second byte = a number
// that is unique per ghost and scene), loaded into its own texture block; the model binds to the renamed textures by name when it
// is built. The mod layer (mods_win.cpp ModsTextureHook) recolours the blue ones' poncho. In a dungeon the ghost also carries the
// weapon its player sent, loaded the same way and attached to the model's "weapon" joint. A ghost animates once per game tick,
// counted from the logic clock, from inside the draw hook, so it needs no step hook of its own.
#include "ghost.hpp"
#include "platform/mods.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <utility>
#include <vector>

#include "btactstatus.hpp"
#include "btmisc.hpp"
#include "character.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "editloop.hpp"
#include "frame.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "platform/clock.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"

namespace {

enum Ctx { CTX_DUNGEON = 0, CTX_TOWN = 1, CTX_COUNT = 2 };

constexpr int   kSlots = 3;
constexpr int   kArenaQuads = 0x40000;        // 4 MB for one character's model, skeleton, motions and weapon
constexpr int   kPackBytes = 8 * 1024 * 1024; // room for the character pack
constexpr int   kWeaponBytes = 512 * 1024;
constexpr int   kFirstBlock = 0x40;           // blocks 0x40..0x45: one per (scene, ghost); nothing else in the game uses them
constexpr float kEase = 0.35f;
constexpr float kPi = 3.14159265f;

unsigned g_epoch = 1; // bumped whenever the game reloads its own player model; ghosts rebuild after it

struct Model {
    CCharacter  *chara = nullptr;
    CCharacter  *weapon = nullptr;
    bool         failed = false;
    std::int64_t last_tick = 0;
    unsigned     epoch = 0;
    int          block = -1;       // texture block this ghost's textures live in
    char         probe[8] = {0};   // name of one of its textures, to check the block was not taken from under it
    int          color = 0;
    int          custom_ver = 0;
    int          weapon_item = 0;
    u_char      *arena_memory = nullptr;
    unsigned int *pack = nullptr; // this ghost's renamed copy, kept: the loaders may keep pointers into it
    unsigned char *weapon_pack = nullptr;
};

struct Ghost {
    bool  want = false;
    bool  snap = true;
    float target[3] = {0, 0, 0};
    float rot[3] = {0, 0, 0};
    float shown[3] = {0, 0, 0};
    float shown_rot[3] = {0, 0, 0};
    int   motion = 0;
    int   flags = 0;
    int   color = 0;       // tunic colour: 0 natural (orange), 1..15 presets, 16 custom pictures
    int   custom_ver = 0;  // bumped when the custom pictures change
    int   weapon_item = 0; // dungeon only
    Model model[CTX_COUNT];
};

struct CtxInfo {
    const char   *pack;
    const char   *cfg;
    int           anim_count;
    unsigned int *data;  // the original pack bytes, kept for the life of the process
    int           size;
};

Ghost         g_ghost[kSlots];
CtxInfo       g_ctx[CTX_COUNT] = {{"dun/mainchara/c01d.chr", "base.cfg", 64, nullptr, 0}, {"chara/c01d.chr", "info.cfg", 128, nullptr, 0}};
int           g_scene_ctx = -1;      // which scene the last draw hook saw
std::int64_t  g_scene_tick = -1000;  // when
std::map<std::pair<int, int>, int> g_seeds;

float Wrap(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

void NoteScene(int ctx) {
    g_scene_ctx = ctx;
    g_scene_tick = ClockTickCount();
}

int CurrentScene() { // the scene drawn within the last few ticks, or -1 (menus, loading, title, cutscene models)
    return ClockTickCount() - g_scene_tick <= 3 ? g_scene_ctx : -1;
}

bool IsNameChar(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Renames every token that starts with `prefix` (4 characters, e.g. "c01d") and is followed by two characters of `suffixes`
// pairs: the first two bytes of the token become `variant` and `digit`. Same length, so nothing in the pack moves.
int RenameTokens(unsigned char *data, int size, const char *prefix, const char *const *suffixes, int suffix_count, char variant, char digit) {
    int renamed = 0;
    for (int i = 0; i + 6 <= size; i++) {
        if (data[i] != static_cast<unsigned char>(prefix[0]) || std::memcmp(data + i, prefix, 4) != 0) {
            continue;
        }
        // No check on the byte before: the model's material records store the name right after a two-byte tag ("HB"), which looks
        // like part of a longer word; skipping those left the ghost's materials pointing at the player's own textures.
        bool match = suffix_count == 0;
        for (int k = 0; k < suffix_count && !match; k++) {
            match = std::memcmp(data + i + 4, suffixes[k], 2) == 0;
        }
        if (!match) {
            continue;
        }
        // the 4th character is a letter ('d' or 'w'), the following two are digits: a texture/weapon name, not "c01d.mds"
        if (data[i + 4] < '0' || data[i + 4] > '9') {
            continue;
        }
        data[i] = static_cast<unsigned char>(variant);
        data[i + 1] = static_cast<unsigned char>(digit);
        renamed++;
    }
    return renamed;
}

// A texture block no texture belongs to right now, searched from the top (the game numbers its own from the bottom). The game's
// own loads delete whatever is in the block they use, so the ghost re-checks its probe texture before every draw and rebuilds
// into another block when it has been taken.
int FindFreeBlock() {
    bool used[72] = {};
    for (int i = 0; i < 196; i++) {
        const CTexture &t = TexManager.textures[i];
        if (t.name[0] != 0 && t.block >= 0 && t.block < 72) {
            used[t.block] = true;
        }
    }
    for (int b = 71; b >= 0; b--) {
        if (!used[b]) {
            return b;
        }
    }
    return -1;
}

bool ModelAlive(const Model &m) {
    if (m.chara == nullptr || m.block < 0) {
        return false;
    }
    char name[8];
    std::memcpy(name, m.probe, sizeof(name));
    return TexManager.GetTexture(name, m.block) != nullptr;
}

bool LoadWeapon(Ghost &g, Model &m, int slot, int block, CDataAlloc2<1> *alloc) {
    char chr[64] = {0};
    char cfg[64] = {0};
    BtGetWeaponNamePath3(chr, cfg, g.weapon_item);
    if (chr[0] == 0 || std::strncmp(chr, "c01w", 4) != 0) {
        return false; // not one of Toan's weapons
    }
    char path[128];
    std::snprintf(path, sizeof(path), "commenu/weapon/%s", chr);
    auto *bytes = static_cast<unsigned char *>(std::malloc(kWeaponBytes));
    if (bytes == nullptr) {
        return false;
    }
    int size = 0;
    LoadFile(path, bytes, &size);
    if (size <= 0 || size > kWeaponBytes) {
        std::free(bytes);
        return false;
    }
    char digit = static_cast<char>('1' + slot);
    RenameTokens(bytes, size, "c01w", nullptr, 0, g.color == 1 ? 'x' : 'y', digit);
    cfg[0] = g.color == 1 ? 'x' : 'y';
    cfg[1] = digit;
    void *raw = std::calloc(1, sizeof(CCharacter));
    auto *w = new (raw) CCharacter();
    w->LoadPackData3(reinterpret_cast<unsigned int *>(bytes), cfg, alloc, block, alloc, 1, 0);
    if (w->frame == nullptr) {
        std::printf("[ghost] weapon did not load (%s)\n", path);
        return false;
    }
    CFrameAttr attr;
    attr.fog_enable = true;
    attr.clip_enable = false;
    attr.program_option = 0;
    w->frame->SetAttr(attr, 1, 4);
    CFrame *hand = m.chara->frame->SearchFrame(const_cast<char *>("weapon"));
    if (hand == nullptr) {
        return false;
    }
    float zero = 0.0f;
    w->SetPosition(zero, zero, zero);
    w->SetRotation(zero, zero, zero);
    w->frame->SetReference(hand);
    w->Step();
    m.weapon = w;
    m.weapon_pack = bytes;
    return true;
}

bool Load(Ghost &g, int ctx, int slot) {
    Model   &m = g.model[ctx];
    CtxInfo &info = g_ctx[ctx];
    if (info.data == nullptr) {
        auto *bytes = static_cast<unsigned int *>(std::malloc(kPackBytes));
        if (bytes == nullptr) {
            m.failed = true;
            return false;
        }
        char path[64];
        std::snprintf(path, sizeof(path), "%s", info.pack);
        int size = 0;
        LoadFile(path, bytes, &size);
        if (size <= 0 || size > kPackBytes) {
            std::printf("[ghost] cannot read %s (size %d)\n", info.pack, size);
            std::free(bytes);
            m.failed = true;
            return false;
        }
        info.data = bytes;
        info.size = size;
    }
    // This ghost's own copy of the pack with its textures renamed.
    if (m.pack == nullptr) {
        m.pack = static_cast<unsigned int *>(std::malloc(static_cast<size_t>(info.size)));
        if (m.pack == nullptr) {
            m.failed = true;
            return false;
        }
    }
    std::memcpy(m.pack, info.data, static_cast<size_t>(info.size));
    static const char *const kSuffix[] = {"01", "02", "03", "04", "05", "11"};
    static const char kLetters[] = "hgijklmnopqrstuvw"; // by colour index; mods_win.cpp reads the colour back from this letter
    char variant = kLetters[std::clamp(g.color, 0, 16)];
    char digit = static_cast<char>('1' + ctx * kSlots + slot);
    int  renamed = RenameTokens(reinterpret_cast<unsigned char *>(m.pack), info.size, "c01d", kSuffix, 6, variant, digit);

    if (m.arena_memory == nullptr) {
        u_char *memory = static_cast<u_char *>(std::malloc(static_cast<size_t>(kArenaQuads) * 16 + 64));
        if (memory == nullptr) {
            m.failed = true;
            return false;
        }
        m.arena_memory = memory;
    }
    u_char *memory = reinterpret_cast<u_char *>((reinterpret_cast<uintptr_t>(m.arena_memory) + 63) & ~static_cast<uintptr_t>(63));
    auto *alloc = new CDataAlloc2<1>(-1);
    alloc->base = memory;
    alloc->buffer = memory;
    alloc->limit = kArenaQuads;
    alloc->used = 0;

    // Zeroed first, like the global player objects: Initialize() leaves images[], the frame bindings and the motion tables as
    // found, and the loader treats a non-null images[0] as an image to load.
    void *raw = std::calloc(1, sizeof(CCharacter));
    auto *c = new (raw) CCharacter();
    void *anim_raw = std::calloc(static_cast<size_t>(info.anim_count), sizeof(CTexAnimeData));
    auto *anim = static_cast<CTexAnimeData *>(anim_raw);
    for (int i = 0; i < info.anim_count; i++) {
        anim[i].Initialize();
    }
    c->InitializeTexAnime(anim, info.anim_count);
    char cfg[32];
    std::snprintf(cfg, sizeof(cfg), "%s", info.cfg);
    int block = FindFreeBlock();
    if (block < 0) {
        std::printf("[ghost] no free texture block\n");
        m.failed = true;
        return false;
    }
    m.block = block;
    std::snprintf(m.probe, sizeof(m.probe), "%c%c1d03", variant, digit);
    c->LoadPackData2(m.pack, cfg, alloc, block, alloc, 0);
    if (c->frame == nullptr) {
        std::printf("[ghost] model did not load (%s)\n", info.pack);
        m.failed = true;
        return false;
    }
    CFrameAttr attr;
    attr.fog_enable = true;
    attr.clip_enable = false;
    attr.program_option = 0;
    c->frame->SetAttr(attr, 1, 4);
    m.chara = c;
    m.weapon = nullptr;
    m.color = g.color;
    m.custom_ver = g.custom_ver;
    m.weapon_item = 0;
    if (ctx == CTX_DUNGEON && g.weapon_item > 0) {
        if (LoadWeapon(g, m, slot, block, alloc)) {
            m.weapon_item = g.weapon_item;
        }
    }
    m.epoch = g_epoch;
    m.last_tick = ClockTickCount();
    std::printf("[ghost] loaded %s for slot %d (block 0x%X, %d names renamed, colour %d, weapon %d)\n", info.pack, slot + 1, block, renamed,
                g.color, m.weapon_item);
    return true;
}

void Advance(Ghost &g, Model &m) {
    if (g.snap) {
        for (int i = 0; i < 3; i++) {
            g.shown[i] = g.target[i];
            g.shown_rot[i] = g.rot[i];
        }
        g.snap = false;
    }
    std::int64_t now = ClockTickCount();
    std::int64_t steps = now - m.last_tick;
    if (steps < 0) {
        steps = 0;
    }
    if (steps > 4) {
        steps = 4;
    }
    m.last_tick = now;
    for (std::int64_t s = 0; s < steps; s++) {
        for (int i = 0; i < 3; i++) {
            g.shown[i] += (g.target[i] - g.shown[i]) * kEase;
            g.shown_rot[i] = Wrap(g.shown_rot[i] + Wrap(g.rot[i] - g.shown_rot[i]) * kEase);
        }
        m.chara->SetPosition(g.shown[0], g.shown[1], g.shown[2]);
        m.chara->SetRotation(g.shown_rot[0], g.shown_rot[1], g.shown_rot[2]);
        m.chara->SetMotion(g.motion, g.flags);
        m.chara->Step();
        m.chara->ClothStep(0); // the poncho is cloth: without this it is never simulated and never drawn
    }
    m.chara->SetPosition(g.shown[0], g.shown[1], g.shown[2]);
    m.chara->SetRotation(g.shown_rot[0], g.shown_rot[1], g.shown_rot[2]);
}

int g_last_draw_ctx = -1;

// Leaving a scene frees the other scene's ghost textures: the registry holds only 196 textures in all and the game stops dead when it
// runs out.
void ReleaseOtherScene(int ctx) {
    for (Ghost &g : g_ghost) {
        Model &o = g.model[1 - ctx];
        if (o.chara != nullptr) {
            if (ModelAlive(o)) {
                TexManager.DeleteTextureBlock(o.block);
            }
            o.chara = nullptr;
            o.weapon = nullptr;
            o.block = -1;
        }
    }
}

void DrawScene(int ctx) {
    NoteScene(ctx);
    if (g_last_draw_ctx != ctx) {
        if (g_last_draw_ctx != -1) {
            ReleaseOtherScene(ctx);
        }
        g_last_draw_ctx = ctx;
    }
    for (int slot = 0; slot < kSlots; slot++) {
        Ghost &g = g_ghost[slot];
        if (!g.want) {
            continue;
        }
        Model &m = g.model[ctx];
        bool   stale = m.chara != nullptr && (m.epoch != g_epoch || m.color != g.color || m.custom_ver != g.custom_ver || (ctx == CTX_DUNGEON && m.weapon_item != g.weapon_item) ||
                                              !ModelAlive(m));
        if (stale) {
            m.chara = nullptr; // rebuilt below into the same memory
            m.weapon = nullptr;
            m.failed = false;
        }
        if (m.chara == nullptr && !m.failed && !Load(g, ctx, slot)) {
            continue;
        }
        if (m.chara == nullptr || m.chara->frame == nullptr) {
            continue;
        }
        Advance(g, m);
        TexManager.ReloadTexture(Vif1Packet, m.block);
        m.chara->Draw();
        if (m.weapon != nullptr && m.weapon->frame != nullptr) {
            m.weapon->Draw();
        }
    }
}

} // namespace

void GhostInvalidate() { g_epoch++; }

void GhostSet(int slot, const float pos[3], const float rot[3], int motion_no, int motion_flags, int color, int weapon_item) {
    if (slot < 1 || slot > kSlots) {
        return;
    }
    Ghost &g = g_ghost[slot - 1];
    for (int i = 0; i < 3; i++) {
        g.target[i] = pos[i];
        g.rot[i] = rot[i];
    }
    if (!g.want) {
        g.snap = true;
    }
    g.want = true;
    g.motion = motion_no;
    g.flags = motion_flags & 3;
    g.color = std::clamp(color, 0, 16);
    g.weapon_item = weapon_item > 0 ? weapon_item : 0;
}

void GhostSetTunic(int slot, int color, const void *front, std::size_t front_len, const void *back, std::size_t back_len) {
    if (slot < 1 || slot > kSlots) {
        return;
    }
    Ghost &g = g_ghost[slot - 1];
    color = std::clamp(color, 0, 16);
    if (color == 16) {
        bool ok = false;
        for (int ctx = 0; ctx < CTX_COUNT; ctx++) {
            ok = ModsSetTunicCustom(static_cast<char>('1' + ctx * kSlots + (slot - 1)), front, front_len, back, back_len) || ok;
        }
        if (!ok) {
            color = 0; // the pictures could not be read
        }
    }
    if (color != g.color || color == 16) {
        g.custom_ver++;
    }
    g.color = color;
}

void GhostClear(int slot) {
    if (slot >= 1 && slot <= kSlots) {
        g_ghost[slot - 1].want = false;
    }
}

void GhostClearAll() {
    for (Ghost &g : g_ghost) {
        g.want = false;
    }
}

void GhostScene(char *out, int size) {
    int ctx = CurrentScene();
    int georama = UserStatus != nullptr ? UserStatus->cur_georama : 0;
    if (ctx == CTX_DUNGEON) {
        std::snprintf(out, static_cast<size_t>(size), "D%d:%d", georama, UserStatus != nullptr ? UserStatus->cur_floor : 0);
    } else if (ctx == CTX_TOWN) {
        std::snprintf(out, static_cast<size_t>(size), "T%d:%s:%s", georama, EditMapName[0] != 0 ? EditMapName : "_",
                      EdInteriorName[0] != 0 ? EdInteriorName : "_");
    } else {
        std::snprintf(out, static_cast<size_t>(size), "-");
    }
}

void GhostLocalState(float pos[3], float rot[3], int *motion_no, int *motion_flags) {
    CCharacter *who = CurrentScene() == CTX_TOWN && Chara != nullptr ? Chara : static_cast<CCharacter *>(&CharaMain);
    float p[4] = {0, 0, 0, 1};
    float r[4] = {0, 0, 0, 0};
    who->GetPosition(p);
    who->GetRotation(r);
    for (int i = 0; i < 3; i++) {
        pos[i] = p[i];
        rot[i] = r[i];
    }
    *motion_no = who->motion_no;
    *motion_flags = who->motion_flags;
}

int GhostFloorSeed() {
    return NowDngMap != nullptr ? NowDngMap->map_seed : 0;
}

void GhostSetFloorSeed(int dungeon, int floor, int seed) {
    g_seeds[{dungeon, floor}] = seed;
}

void GhostClearFloorSeeds() {
    g_seeds.clear();
}

int MpFloorSeed(int random_seed) {
    if (UserStatus != nullptr) {
        auto it = g_seeds.find({UserStatus->cur_georama, UserStatus->cur_floor});
        if (it != g_seeds.end()) {
            std::printf("[mp] floor %d/%d built with the host's seed %d (own roll %d)\n", UserStatus->cur_georama, UserStatus->cur_floor,
                        it->second, random_seed);
            return it->second;
        }
    }
    return random_seed;
}

void GhostDrawDungeon() { DrawScene(CTX_DUNGEON); }
void GhostDrawTown() { DrawScene(CTX_TOWN); }

// ---- Monsters that see every player ---------------------------------------------------------------------------------------

namespace {

float                 g_saved_pos[4] = {0, 0, 0, 1};
std::vector<GhostHit> g_ghost_hits;
std::int64_t          g_hit_cooldown[kSlots] = {0, 0, 0};
struct LocalHit { int damage, kind, flags, monster; };
std::vector<LocalHit> g_local_hits;

float Dist3(const float *a, const float *b) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Remote players that count as targets: the ones the scripts show here.
bool GhostHere(const Ghost &g) { return g.want && CurrentScene() == CTX_DUNGEON; }

} // namespace

float MpNearestDistance(const float *player, const float *monster) {
    float best = Dist3(player, monster);
    for (const Ghost &g : g_ghost) {
        if (GhostHere(g)) {
            best = std::min(best, Dist3(g.target, monster));
        }
    }
    return best;
}

void MpMonsterStepBegin() {
    std::memcpy(g_saved_pos, CharaMain.pos, sizeof(g_saved_pos));
    for (const LocalHit &h : g_local_hits) {
        float at[4] = {g_saved_pos[0], g_saved_pos[1] + 20.0f, g_saved_pos[2], 1.0f};
        NowColData->Set(at, h.damage, 2, 30.0f, 0.0f, 1, h.kind, h.flags, 0);
        NowColData->SetUserID(h.monster * 5 + 200, 0);
    }
    g_local_hits.clear();
}

void MpMonsterTarget(int monster) {
    if (NowMonstorUnit == nullptr) {
        return;
    }
    float mp[4] = {0, 0, 0, 1};
    NowMonstorUnit->chara[monster][0].GetPosition(mp);
    const float *best = g_saved_pos;
    float        best_d = Dist3(g_saved_pos, mp);
    for (const Ghost &g : g_ghost) {
        if (GhostHere(g)) {
            float d = Dist3(g.target, mp);
            if (d < best_d) {
                best_d = d;
                best = g.target;
            }
        }
    }
    CharaMain.pos[0] = best[0];
    CharaMain.pos[1] = best[1];
    CharaMain.pos[2] = best[2];
}

void MpMonsterStepEnd() {
    std::memcpy(CharaMain.pos, g_saved_pos, sizeof(g_saved_pos));
    if (NowMonstorUnit == nullptr || CurrentScene() != CTX_DUNGEON) {
        return;
    }
    std::int64_t now = ClockTickCount();
    for (int s = 0; s < kSlots; s++) {
        const Ghost &g = g_ghost[s];
        if (!GhostHere(g) || now < g_hit_cooldown[s] || g_ghost_hits.size() >= 4) {
            continue;
        }
        bool hit = false;
        for (int i = 0; i < 16 && !hit; i++) {
            MONSTOR &m = NowMonstorUnit->monster[i];
            if (m.state != 2 || m.hp <= 0 || m.stop_timer > 0) {
                continue;
            }
            MONSTOR_EFFECT_STATE2 &e = NowMonstorUnit->effect2[i];
            float time = NowMonstorUnit->chara[i][0].motion_type.state.time;
            for (int k = 0; k < 16; k++) {
                if (e.active[k] == 0 || !(e.motion_start[k] < time) || e.motion_end[k] <= time) {
                    continue;
                }
                float dx = e.position[k][0] - g.target[0], dz = e.position[k][2] - g.target[2];
                float dy = e.position[k][1] - g.target[1];
                float reach = e.radius[k] + 14.0f;
                if (dx * dx + dz * dz <= reach * reach && dy > -e.radius[k] - 10.0f && dy < 55.0f + e.radius[k]) {
                    int damage = e.damage[k] * (m.anger_timer > 0 ? 2 : 1);
                    g_ghost_hits.push_back({s + 1, i, damage, e.kind[k], e.flags[k]});
                    g_hit_cooldown[s] = now + 60; // about the game's own invulnerability after a hit
                    hit = true;
                    break;
                }
            }
        }
    }
}

bool GhostTakeHit(GhostHit *out) {
    if (g_ghost_hits.empty()) {
        return false;
    }
    *out = g_ghost_hits.front();
    g_ghost_hits.erase(g_ghost_hits.begin());
    return true;
}

void GhostHurtLocal(int damage, int kind, int flags, int monster) {
    if (damage > 0 && g_local_hits.size() < 4) {
        g_local_hits.push_back({damage, kind, flags, monster & 15});
    }
}

// ---- Doors and gates -------------------------------------------------------------------------------------------------------

extern int gameTask; // the dungeon's task state (gameloop.cpp)

namespace {

constexpr int kTaskPlay = 0, kTaskScriptStart = 0x190, kTaskScriptRun = 0x191, kTaskItemSelect = 0x19A, kTaskScriptRestart = 0x1F4;

struct Trigger {
    bool  valid = false, remote = false, world = false, transition = false, emitted = false;
    int   script = -1, mode = 0, ext = 0;
    float pos[4] = {0, 0, 0, 1};
    float dir[4] = {0, 0, 0, 0};
};
Trigger                 g_trig;
int                     g_prev_task = 0;
bool                    g_remote_next = false;
struct Pending { bool on = false; MpEvent e{}; std::int64_t deadline = 0; };
Pending                 g_pending;
std::vector<MpEvent>    g_events_out;

bool InScript(int t) { return t == kTaskScriptStart || t == kTaskScriptRun || t == kTaskItemSelect || t == kTaskScriptRestart; }

} // namespace

void MpScriptOp(int cls) {
    if (!g_trig.valid) {
        return;
    }
    if (cls == 2) {
        g_trig.transition = true;
    } else if (cls == 1) {
        g_trig.world = true;
    }
    if (!g_trig.remote && g_trig.world && !g_trig.transition && !g_trig.emitted && g_trig.script >= 0 && g_events_out.size() < 4) {
        g_trig.emitted = true;
        MpEvent e{};
        e.script = g_trig.script;
        e.mode = g_trig.mode;
        e.ext = g_trig.ext;
        for (int i = 0; i < 3; i++) {
            e.pos[i] = g_trig.pos[i];
            e.dir[i] = g_trig.dir[i];
        }
        g_events_out.push_back(e);
    }
}

void MpEventPoll() {
    int t = gameTask;
    bool now_script = InScript(t);
    if (now_script && !InScript(g_prev_task) && !g_trig.valid) {
        g_trig = Trigger();
        g_trig.valid = true;
        g_trig.script = BtEventInfo.script_no != -1 ? BtEventInfo.script_no : BtEventInfo.running_script_no;
        g_trig.mode = BtEventInfo.action_mode;
        g_trig.ext = BtEventInfo.ext_memory;
        g_trig.remote = g_remote_next;
        g_remote_next = false;
        for (int i = 0; i < 4; i++) {
            g_trig.pos[i] = BtEventInfo.position[i];
            g_trig.dir[i] = BtEventInfo.direction[i];
        }
    }
    if (!now_script && g_trig.valid) {
        g_trig = Trigger();
    }
    g_prev_task = t;

    if (g_pending.on) {
        if (ClockTickCount() > g_pending.deadline) {
            g_pending.on = false;
        } else if (t == kTaskPlay && BtActStatus.can_act != 0 && BtActStatus.movement_locked == 0 && UserStatus != nullptr) {
            const MpEvent &e = g_pending.e;
            BtEventInfo.script_no = e.script;
            BtEventInfo.ext_memory = e.ext;
            BtEventInfo.event_marker = 0;
            BtEventInfo.action_mode = e.mode;
            BtEventInfo.chara_help = 0;
            for (int i = 0; i < 3; i++) {
                BtEventInfo.position[i] = e.pos[i];
                BtEventInfo.direction[i] = e.dir[i];
            }
            BtEventInfo.position[3] = 1.0f;
            g_remote_next = true;
            gameTask = kTaskScriptStart;
            g_pending.on = false;
            std::printf("[mp] running script %d for another player's door\n", e.script);
        }
    }
}

bool GhostTakeEvent(MpEvent *out) {
    if (g_events_out.empty()) {
        return false;
    }
    *out = g_events_out.front();
    g_events_out.erase(g_events_out.begin());
    return true;
}

void MpRunEvent(const MpEvent &e) {
    g_pending.on = true;
    g_pending.e = e;
    g_pending.deadline = ClockTickCount() + 1800; // wait up to 30 s for the game to be free (not in a menu or another script)
}

int MpAutoItemSelect() {
    if (!(g_trig.valid && g_trig.remote)) {
        return 0;
    }
    int pick = -1;
    if (BtEventInfo.item_select_filtered != 0 && BtEventInfo.item_select_list[0] != -1) {
        pick = BtEventInfo.item_select_list[0]; // the key the script asks for: it is "used" for the other player's door
    }
    if (BtEventInfo.item_select_result != 0) {
        reinterpret_cast<int *>(static_cast<std::intptr_t>(BtEventInfo.item_select_result))[1] = pick;
    }
    BtEventInfo.item_select_result = 0;
    return 1;
}
