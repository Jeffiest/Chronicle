#pragma once

#include <cstddef>

// Remote players drawn in the dungeon and in towns (Windows fork multiplayer). Plain types only; the game headers stay in
// ghost_win.cpp. Slots 1..3. A ghost is a second Toan model driven by what the other player sent: position, rotation, motion.
// Each scene (a dungeon floor, a town map) has its own model and coordinates, so a ghost is only drawn in the scene the scripts
// say it is in; scripts compare GhostScene() strings to decide.

// color: tunic colour 0 natural (orange), 1..15 presets, 16 custom pictures (see GhostSetTunic and mods.hpp). weapon_item: the weapon's item number, drawn in dungeons only (0 = none).
void GhostSet(int slot, const float pos[3], const float rot[3], int motion_no, int motion_flags, int color, int weapon_item);
// Gives ghost `slot` a tunic colour; for 16 the PNG bytes of its poncho front and back (either may be empty). Rebuilds the ghost.
void GhostSetTunic(int slot, int color, const void *front, std::size_t front_len, const void *back, std::size_t back_len);
void GhostInvalidate();               // the game reloaded its own player model: rebuild ghosts before the next draw
void GhostClear(int slot);
void GhostClearAll();
void GhostLocalState(float pos[3], float rot[3], int *motion_no, int *motion_flags); // what to send for the local player
void GhostScene(char *out, int size); // "D<dungeon>:<floor>", "T<town>:<map>:<interior>", or "-" when neither is on screen
int  GhostFloorSeed();                // map_seed of the floor the dungeon is on now (0 outside a dungeon)

// Floor seeds a guest must build with: the dungeon id and floor the host built, and its seed.
void GhostSetFloorSeed(int dungeon, int floor, int seed);
void GhostClearFloorSeeds();
int  MpFloorSeed(int random_seed);    // called from CDungeonMap::buildRandomMap; returns the seed to build with

// Hooks called right after the game draws the player (gen_win_src.py patches). They also advance each ghost's animation.
void GhostDrawDungeon();
void GhostDrawTown();

// Monsters that see every player. The monsters' AI reads CharaMain.pos as "the player"; around each monster's update that is
// pointed at the nearest player (the local one or a ghost), and monsters wake up near any of them. A monster attack sphere that
// overlaps a ghost is queued as a hit for that player's machine to apply (GhostTakeHit); a hit received from the host is
// injected as a real monster attack at the local player (GhostHurtLocal), so the game's own damage, flinch and knockback run.
struct GhostHit { int slot, monster, damage, kind, flags; };
bool GhostTakeHit(GhostHit *out);
void GhostHurtLocal(int damage, int kind, int flags, int monster);
void MpMonsterStepBegin();            // before CMonstorUnit::Step
void MpMonsterStepEnd();              // after it
void MpMonsterTarget(int monster);    // at the start of one monster's update
float MpNearestDistance(const float *player, const float *monster); // the distance CheckViewLevel should use

// Doors and gates. A script that a player starts from a map event and that moves a map object or switches an event (a door, a gate, a
// key room) is reported by GhostTakeEvent so the other players can run the same script (MpRunEvent) and see the same door open. Scripts that change
// floor or character are never reported. On a script run for another player the item window (the key to use) answers itself.
struct MpEvent { int script, mode, ext; float pos[3], dir[3]; };
bool GhostTakeEvent(MpEvent *out);
void MpRunEvent(const MpEvent &e);
void MpEventPoll();                   // once per tick
void MpScriptOp(int cls);             // from the script opcodes: 1 = moves a map object / switches an event, 2 = changes floor or character
int  MpAutoItemSelect();              // 1 when a mirrored script's item window was answered without opening it
