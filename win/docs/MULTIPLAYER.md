# Multiplayer scripting API (phase 1: transport)

Host-authoritative sessions: one host, up to 3 guests, over TCP. Join by IP (LAN, or the host's Tailscale IP). Default port 7777;
forward it on the router, or just use Tailscale. A mod gets the `net` table only if its `mod.json` has `"net": true`.

| call | does |
|---|---|
| `net.host([port])` | start hosting. Returns `true`, or `nil, reason` |
| `net.join(ip, [port])` | connect to a host. Returns at once; a `"connected"` or `"failed"` event follows |
| `net.stop()` | leave / stop hosting |
| `net.status()` | `"idle"`, `"hosting"`, `"connecting"`, `"connected"` |
| `net.id()` | 0 for the host, 1..3 for a guest, -1 when not in a session |
| `net.peers()` | ids of everyone else (a guest sees only the host, 0) |
| `net.send(to, text)` | `to` = a peer id, or `-1` for everyone else. A guest can message any peer; the host relays. Returns `true` if queued |
| `net.poll()` | next event as `kind, peer, data`, or nothing. Call in a loop from `tick` until it returns nothing |

Event kinds: `join` (peer), `leave` (peer), `message` (peer = sender, data = text; binary-safe), `connected` (peer = your id),
`failed` (data = reason), `disconnected` (host went away).

Testing on one PC: `.\run_win.ps1` (host) and `.\run_win.ps1 -Guest` (second instance with its own save folder `win-save-guest`,
same mods). Example mod: `net_test` (F8 host, F9 join 127.0.0.1, F10 stop; HUD shows peers' positions).

## Phase 2: ghosts and shared floors (mod `coop`)
Engine calls (Lua `dc.*`, no `mod.json` flag needed): `dc.player_state()` -> x,y,z,rx,ry,rz,motion,flags;
`dc.ghost(slot,x,y,z,rx,ry,rz,motion,flags)` shows/moves remote player `slot` (1..3) as a blue Toan clone, easing toward each update;
`dc.ghost_clear([slot])`; `dc.floor_seed()` = seed the current floor was built from; `dc.set_floor_seed(dungeon,floor,seed)` makes the
next build of that floor use that seed; `dc.clear_floor_seeds()`.
Limits: ghosts only draw while the local character is Toan (they share his texture), no weapon, no shadow yet. Guests must walk to the
same dungeon floor themselves; seeds make the map identical, monsters are NOT synced yet (phase 3).
Mod `coop`: Ctrl+H host, Ctrl+J join (edit HOST_IP in scripts/main.lua for another machine), Ctrl+K leave; F8/F9/F10 also work.
Log lines to look for: `[ghost] loaded`, `[mp] floor d/f built with the host's seed`.

## Phase 2b/3: every scene, shared monsters (mod `coop`)
- `dc.scene()` -> `"D<dungeon>:<floor>"` in a dungeon, `"T<town>:<map>:<interior>"` in a town, `"-"` elsewhere (menus, loading). Ghosts are
  drawn in dungeons AND towns (the town ghost uses the town Toan model, texture block 8); scripts show a ghost only when both
  players' scene strings match. Positions are per scene, so the same string means the same coordinates.
- `dc.logic_ticks()` = game logic ticks (60/s); use it, not `tick` event counts, to pace sends.
- `dc.monster_state(i)` -> alive, kind, hp, max_hp, x, y, z, ry, motion, flags. `dc.puppet(i, kind, x,y,z, ry, motion, flags, hp, max_hp)`
  makes slot i mirror the host: eased position, motion, life follow the values; its own AI is held with the stop status. Returns false
  if the slot is empty or holds another kind (floors differ). `dc.puppet_clear([i])`.
- coop: the host sends all 16 slots every 3 ticks; a guest puppets them, kills its copy (through `dc.hurt_monster`) when the host's is
  dead, and forwards its own weapon hits (`H slot damage`) to the host, which applies them. Loot drops on both machines (each player
  picks up their own). Not done: monsters attacking guests (they only chase the host), guest-only status effects, chests.

## Monsters see every player (mod `coop`)
- Monster AI reads `CharaMain.pos` as "the player". Three patches (monstorunit.cpp CheckViewLevel distance, the per-monster loop head,
  and a Begin/End wrapper around `NowMonstorUnit->Step`) point it at the nearest player (local or ghost) for that monster, and wake
  monsters near any of them. A monster attack sphere that overlaps a ghost is queued (`dc.take_ghost_hit()` -> slot, monster, damage,
  kind, flags); the host sends it to that guest as `D`, and the guest calls `dc.hurt_player(damage, kind, flags, monster)`, which
  injects a real monster attack at the local player (the game's damage, flinch, knockback and `player_damage` event all run).
- `dc.monster_state(i)` also returns `state` (-1 empty/dead, 1 asleep, 2 active). A guest only kills its copy when the host's slot is
  dead (state -1 or hp <= 0); asleep monsters are just held in place (this was the "monsters die when I first see them" bug).

## Colours, weapons, shared pause, hub (this round)
- Ghosts now have their own textures. `ghost_win.cpp` loads a private copy of the Toan pack with every texture name renamed
  (`c01d04` -> `g11d04`: 1st byte g = blue / h = natural, 2nd = a digit unique per ghost and scene) into its own texture block
  (0x40-0x45), so colour and pose never touch the real player's. `ModsTextureHook` hue-shifts the poncho textures (c01d04/c01d05) to blue
  for `g...` names, and for the local player's own textures while `dc.set_tunic(true)` is on (coop does that on connect; it applies the
  next time the game loads the model, e.g. after a door). Host = natural orange, guests = blue, on every screen.
- Dungeon ghosts carry the weapon the other player has equipped (Toan weapons only; `SHOW_WEAPON` at the top of coop's main.lua turns it
  off). Town ghosts have no weapon. `dc.ghost(slot, x,y,z, rx,ry,rz, motion, flags, color, weapon_item)`.
- Guest monsters now keep animating: puppets are no longer held with the stop status (which froze the motion and tinted them grey); three
  monstorunit.cpp patches skip the AI, attacks and view-limit check for a puppet via `MpIsPuppet`, and a puppet copies the host's
  asleep/awake state. A guest reports (`X <bitmask>`) the monster slots it cannot mirror and the host sends no damage from those.
- Shared pause: each player's state message carries whether the pause/battle menu (or a mod screen) is up (`dc.menu_open()`,
  `dc.frozen()`); everyone else in the dungeon holds the world (`dc.freeze`) until it closes. The menus still pause the game as before.
- Messages between mods: `dc.msg_on(name, fn)` / `dc.msg(name, a, b)`; handlers get (a, b, sender). The `hub` mod (L1+R1+Select or K) asks
  every mod (`hub_collect`), lists their `hub_entry` answers and tells the owner (`hub_open`). Entries so far: Skill Tree and Character
  Levels (ascension, the tree no longer has its own key), Bestiary and Trophies (dungeon_feats), Weapon Mastery, Co-op (host / join /
  set the host's address / leave). `scripts/listui.lua` (copied into each mod) draws a scrollable list screen.

## Doors, gates and key rooms
A door, gate or key room is a map event whose system script moves a map object (`_SET_OBJHDL_POS/ROT`) or switches an event (`_SET_EVENT_SW`).
`MpEventPoll` (called from ScriptTick) notices a script starting from PLAY (BtEventInfo.script_no/position/action_mode); opcode patches
(`MpScriptOp`) classify it while it runs: class 1 = world change, class 2 = changes floor/character/dungeon (never mirrored). The first
world-change op of a non-mirrored script queues it (`dc.take_event()`); coop sends `E scene script mode ext pos dir` to everyone, and on
a machine in the same scene `dc.run_event(...)` starts the same script as soon as the game is free (PLAY, can act, 30 s limit). While a mirrored
script runs, its item window (where you pick the key) answers itself with the key the script asks for (`MpAutoItemSelect`), so the other
player needs no key. A "PLAYER n OPENED A DOOR" toast shows on the receiving side and `door script <n>` goes to the log; if some
non-door script ever mirrors by mistake, its number is in that log line.
