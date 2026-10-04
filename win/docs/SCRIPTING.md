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

Functions (`chara` is 0..5, defaults to the character being controlled): `dc.hp([chara])`, `dc.set_hp(v, [chara])`,
`dc.max_hp`, `dc.set_max_hp`, `dc.water`, `dc.set_water`, `dc.gilda()`, `dc.set_gilda(v)`, `dc.dungeon()`, `dc.floor()`,
`dc.chara()`, `dc.party_size()`, `dc.give_item(id, [qty])`, `dc.ticks()`, `dc.mod_dir()`, `dc.log(...)`.
Values read as 0 / -1 while no game is loaded.

```lua
-- mods/regen/scripts/main.lua : regenerate 1 HP a second
dc.on("tick", function(n)
  if n % 50 == 0 then dc.set_hp(dc.hp() + 1) end
end)
```

## Native plugins (`mods/<mod>/plugin.dll`)

Off by default. Needs BOTH `mods/mods.json` = `{"allow_native": true}` and `mods/<mod>/mod.json` = `{"native": true}`.
A plugin runs with full rights; only use DLLs you trust. Include `_sdk/mod_api.h` (version 1) and export
`int ModInit(const ModHostApi *api)`. The API has the same events and state access as Lua, plus the changeable
`item_pickup` event. See `_sdk/example_plugin.c`. Build for x64 Windows, e.g.
`clang --target=x86_64-w64-mingw32 -shared -O2 -o plugin.dll example_plugin.c`.

Arbitrary function detours are not offered: only the defined events above, so mods stay stable across game updates.
