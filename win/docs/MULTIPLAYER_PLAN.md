# Chronicle multiplayer mod: plan (started 2026-10-04)

Host-authoritative visiting, Animal Crossing style. Host runs their world; guests join over direct IP / Tailscale and appear as a
tinted Toan clone that fights alongside the host. Guest save is untouched; guest inventory is their own. Phases 1-5 below.
Overlay files go in `Chronicle-src\win\`; registration/patches in `gen_win_src.py`; docs in `win-save\mods\`.

## Feasibility findings (code read, nothing changed yet)
- Player = one global `CMainChara CharaMain` (ps2/include/dun/gameloop.hpp:184), loaded in ps2/src/dun/gameloop.cpp:6894-6901
  (`LoadPackData2(chara_data,"base.cfg",...)`). A guest avatar = a second `CCharacter` loaded the same way, drawn each frame
  (`Draw()`), positioned with `SetPosition`, animated with `SetMotion(no,mode)`. Position/motion are plain fields, so we can
  send them. Tint: `ambient_offset` / `ambient_tint` fields on CCharacter.
- Floors are deterministic from a seed: `CDungeonMap::map_seed` (dungeonmap.hpp:212), `srand(map_seed)` at dungeonmap.cpp:3457.
  Host sends dungeon id + floor + seed; guest builds the same floor. No map data on the wire.
- Monsters: `NowMonstorUnit->monster[16]` + `chara[16]` (monstorunit.hpp:273), `alive_count`. Host streams per-monster
  pos/motion/HP/alive; guest freezes its own monster AI and mirrors the host's. Kill/hit hooks already exist
  (`ScriptMonsterHit/Killed`), so guest damage = a message to the host that applies the hit.
- Existing script layer already reads `CharaMain.GetPosition`, monster pos, and runs from `ScriptTick` in `GameRenderTick`:
  the right place to pump the network too.
- Risks: global single-player state (party, camera follows CharaMain, lock-on, input); drops/chests/items must be resolved on
  host only; `rand()` use outside map build must not desync (host is truth, so mostly fine); Georama/town state later.

## Phases
1. (BUILT 2026-10-04, selftest passes, TCP not UDP) Transport: `port/src/platform/net.cpp/.hpp` (dc_host, no game headers). UDP, Winsock2, host/join, 20 Hz snapshots + reliable
   events, ping/timeouts. Lua `net.*` API in script_win.cpp. Test: two instances on one PC (separate --save dirs) via 127.0.0.1.
2. Presence: ghost Toan at the other player's pos/motion/map/floor, tinted, name tag via modtext. Guest follows host's
   dungeon/floor/seed.
3. Shared fight: host streams monster state; guest hits sent to host; drops/chests/deaths on host; guest HP is the guest's own.
4. Shared town (Georama) edits follow the host.
5. Join UX: in-game prompt / host code, Tailscale instructions.

## Decisions (user, 2026-10-04)
Guest = tinted Toan clone. Guests fight alongside host. Direct IP / Tailscale is fine (no relay yet).
