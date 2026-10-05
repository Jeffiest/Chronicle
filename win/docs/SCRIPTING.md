# Dark Cloud scripting and native mods (Windows fork)

Mods live in `mods/<mod>/` next to `config.json`. A mod can mix textures, a Lua script and a native plugin.
Mods load alphabetically; a mod with `{"enabled": false}` in `mod.json` is skipped. Folders starting with `_` are ignored.
Everything is logged to the console window (`[mod <name>] ...`).

## Lua (`mods/<mod>/scripts/main.lua`)

Needs a build made after `setup_lua.ps1`. Each mod has its own sandboxed Lua 5.4 state: no `io`, `os`, `package`, `debug`,
`load`, `dofile`. `require("helper")` loads `scripts/helper.lua` from your mod only. `print` is the same as `dc.log`.
A runaway loop (about 2 million VM instructions in one callback) or any error disables that mod and logs the traceback.

Events: `dc.on(name, fn)`

| event | arguments | notes |
|---|---|---|
| `init` | none | after all mods loaded (the game may not be in a save yet) |
| `tick` | tick count | every game tick (50/s, or your tick rate) |
| `floor_change` | dungeon, floor, prev_dungeon, prev_floor | also fires once at start |
| `item_pickup` | item id, quantity | may `return new_item, new_qty` to change what the player gets |
| `monster_hit` | index, kind, attacker, damage, element | `return new_damage` changes the damage (0 = no damage) |
| `monster_killed` | index, kind, attacker | attacker is -1 for poison |
| `player_damage` | chara, amount | amount is positive; `return new_amount` changes it |
| `player_heal` | chara, amount | same, for healing |

Functions (`chara` is 0..5, defaults to the character being controlled): `dc.hp([chara])`, `dc.set_hp(v, [chara])`,
`dc.max_hp`, `dc.set_max_hp`, `dc.water`, `dc.set_water`, `dc.gilda()`, `dc.set_gilda(v)`, `dc.dungeon()`, `dc.floor()`,
`dc.chara()`, `dc.party_size()`, `dc.give_item(id, [qty])`, `dc.ticks()`, `dc.mod_dir()`, `dc.log(...)`.
Values read as 0 / -1 while no game is loaded.

Enemies (index 0..15 in the current room): `dc.monsters()` (slot count), `dc.monster_count()`, `dc.monster_alive(i)`,
`dc.monster_hp(i)`, `dc.set_monster_hp(i, v)`, `dc.monster_max_hp(i)`, `dc.set_monster_max_hp(i, v)`, `dc.monster_kind(i)`,
`dc.monster_defense(i)` / `dc.set_monster_defense(i, v)`, `dc.monster_drop(i)` / `dc.set_monster_drop(i, item)`,
`dc.monster_money(i)` / `dc.set_monster_money(i, v)`, `dc.monster_exp(i)` / `dc.set_monster_exp(i, v)`.

Input: `dc.key_down(chord)` and `dc.key_pressed(chord)` (pressed = went down this tick). A chord is `"f6"`, `"ctrl+k"`, or pad
buttons `l1 r1 l2 r2 l3 r3 triangle circle cross square select start up down left right`; keys use SDL names
(`pad_down` / `pad_pressed` are aliases).

Screen: `dc.text(id, text, x, y, [scale], [rgba])` draws a line on the 640x480 logical screen; call it again with the same
id to update it, with `""` to remove it. The font is the FPS counter's 5x7 font: uppercase letters, digits and basic
punctuation only. Color is `0xRRGGBBAA`. `dc.toast(text, [seconds])` shows a short message at the top.

```lua
-- mods/regen/scripts/main.lua : regenerate 1 HP a second
dc.on("tick", function(n)
  if n % 50 == 0 then dc.set_hp(dc.hp() + 1) end
end)
```

## Native plugins (`mods/<mod>/plugin.dll`)

Off by default. Needs BOTH `mods/mods.json` = `{"allow_native": true}` and `mods/<mod>/mod.json` = `{"native": true}`.
A plugin runs with full rights; only use DLLs you trust. Include `_sdk/mod_api.h` (version 2: combat events, monster and input functions, `text`, `toast`) and export
`int ModInit(const ModHostApi *api)`. The API has the same events and state access as Lua, plus the changeable
`item_pickup` event. See `_sdk/example_plugin.c`. Build for x64 Windows, e.g.
`clang --target=x86_64-w64-mingw32 -shared -O2 -o plugin.dll example_plugin.c`.

Arbitrary function detours are not offered: only the defined events above, so mods stay stable across game updates.

## API version 3 additions (Lua only)

| function | |
|---|---|
| `dc.rect(id, x, y, w, h, rgba [, z])` | filled box in the 640x480 frame, drawn under text (health bars, cooldown icons). `rgba` like `0xE02020FF` (alpha last). `w` or `h` <= 0 removes it. Boxes draw in `z` order (default 0), then in the order first created. |
| `dc.text(id, text, x, y, scale, rgba, backdrop)` | new 7th argument: `false` draws no dark box behind the text (floating damage numbers). Empty text removes it. |
| `dc.monster_screen(i [, up])` | `x, y, pixels_per_unit` of enemy `i` in the 640x480 frame (its feet, lifted `up` world units), or nothing when off screen. `pixels_per_unit` is how tall one world unit is on screen at that distance. |
| `dc.player_screen([up])` | the same (`x, y, pixels_per_unit`) for the player in a dungeon. |
| `dc.weapon([chara [, slot]])` | table `{slot, item, level, attack, endurance, speed, magic, durability, flags, elem={5}, vs_monster={10}}` for the equipped weapon (or `slot`); nothing when there is none. |
| `dc.set_weapon({attack=.., ...} [, chara [, slot]])` | changes only the fields present: attack endurance speed magic durability level flags elem vs_monster. Returns true/false. |
| `dc.store_get(key)` / `dc.store_set(key, value)` | numbers, strings, booleans and tables of them, per mod and per save slot. `store_set(key, nil)` removes. 256 KB per mod. |
| `dc.store_slot()` | slot of the last load or save, -1 before either. |

Event `game_loaded` (no arguments) fires about 90 ticks after a slot has been loaded, when the game has restored its state: re-apply
anything you derive from the store there. The store is written to `mods/_store/<mod>/slot<N>.json` when the game saves to a slot and
read back when a slot is loaded; before the first load or save of a session it is empty. Limit: a New Game started after loading
another slot keeps that slot's store until you save: start new games from a fresh launch. `DC_DEBUG_SAVESLOT=N` pretends the save
menu loaded slot N at tick 10 and saved it at tick 100 (for testing). See `mods/engine_test` for a demo.

`dc.monster_model(i)` (API 4): the enemy's index in the game's monster table (the same key the data tables' `monsters` section uses), for telling species apart.

More API 4 functions: `dc.set_monster_scale(i, s)` (draw an enemy s times its size; re-apply each tick), `dc.ailments(c)` / `dc.set_ailments(mask, c [, frames])`
(player status bit mask: 0x04 freeze, 0x08 stamina, 0x10 poison, 0x20 curse, 0x40 goo), `dc.set_monster_status(i, "stop"|"poison"|"slow"|"anger", ticks)` /
`dc.monster_status(i, name)`, `dc.day()` (the in-game day), `dc.shop_list(n)` / `dc.set_shop_list(n, {ids})` (item shops 0-17),
`dc.town_pos()` (player position in a town) and `dc.camera(ex,ey,ez, rx,ry,rz)` (hold the town camera at an eye point looking at a reference point; `dc.camera()` gives
it back to the game).

More functions (API 4+): `dc.set_monster_speed(i, m)` (enemy `i` moves and animates `m` times as fast, 0.3 to 3; re-apply each tick, 1 is normal),
`dc.set_floor_size(...)` / `dc.floor_reached(d)` / `dc.floor_select()` (bigger floors, the floor-select list), `dc.hurt_monster`, `dc.monster_pos`/`dc.player_pos`,
`dc.freeze(on)` and `dc.block_input(on)`. Shared settings that are not per save slot (kept in `mods/_shared.json`, readable by any mod):
`dc.shared_get(mod, key)`, `dc.shared_set(mod, key, value)`, `dc.shared_all()`; the Mod Settings mod edits them.

Co-op world sync (town): `dc.npc_list()` (villagers: id, name, x, y, z, ry, m, fl, sp, on), `dc.npc_puppet(id, x, y, z, ry, motion, flags, speed)` / `dc.npc_puppet_clear()`
(villager `id` follows those values), `dc.npc_hold(id, ticks)` (stands still), `dc.npc_talking()` (villager id the local player is talking to, or -1).
Georama: `dc.georama_take()` pops the next local op `kind, map, parts, x, y, z, rot` (1 placed, 2 removed), `dc.georama_apply(kind, map, parts, x, y, z, rot)`
applies someone else's (true when it changed this town; a part landing on the player forces the Georama view), `dc.georama_map()`.
Guest world: `dc.world_capture()` (the host's world as a binary string, nil before a game runs), `dc.world_join(blob)` (a guest enters or refreshes it), `dc.world_leave()`, `dc.world_active()`.
