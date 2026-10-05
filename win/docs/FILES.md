# Replacing game data files (maps, dungeon configs, scripts, anything)

`mods/<mod>/files/<path>` replaces the game's data file at that path. The path is the one the game uses, the same layout as the
extracted data folder, with `/` or `\` and any capitalisation:

```
mods/mymap/files/dun/d01main_a.cfg            replaces dun/d01main_a.cfg
mods/mymap/files/dun/d01/floor_main/d01g01_0.mds
mods/mymod/files/gedit/e01/e01talk_2.mes      replaces a whole dialogue file
```

Later mods win a clash (see `mod_manager.py`). The log says `mods: <mod> overrides game file <path>` once per file, and warns when
your file is **larger** than the game's: the game reads files into buffers sized for the original, so keep to the original size
or smaller unless you know the buffer has room. Names with Japanese (Shift-JIS) characters are written as `%xx` per byte, e.g.
`%83%41...`, the same escape the dumper uses. The text layer (`text/*.json`, see TEXT.md) still applies on top of a replaced
message file.

## What this makes possible for maps and dungeons (what I found; nothing here is a finished new-map tool)
* **Dungeon look and layout rules are plain text.** `dun/dNNmain_a.cfg`, `...inter.cfg`, `...boss.cfg` (Shift-JIS comments) set
  ambient and lights (`AMBIENT`, `LIGHT_C`), fog (`FOG`), background colour, view distance (`VIEWLEVEL`), music/images
  (`GRD_IMG`, `MINIMAP_IMG`), and the list of room pieces (`DEF_PATS ... PT_BASE "d01g01_0.mds", PT_COLS "d01g01_a.mds",
  PT_CAM ..., PT_FIRE x,y,z ... DEF_ENDS`). Copy one into `files/dun/`, change values, play: that is the quickest way to make a
  dungeon look different, or to reuse pieces in another order.
* **Room pieces are `.mds` model scenes** (`dun/d01/floor_main/*.mds`: base model, `_a` collision, `_v` camera). Replace one with
  your own `.mds` of similar size. There is no MDS authoring tool yet; the format is the game's scene file
  (see `ps2/include/mds.hpp`: a header, one record per object with name, parent and matrix, then the model data).
* **Monsters on a floor** can be edited with `data/*.json`'s `monsters` table (stats), but which models load per floor is in
  `BtEnemyLayout*` arrays inside the game code, and how many spawn comes from the map files: not editable yet.
* **New dungeons** (an 8th dungeon, more floors): not possible yet. The game has fixed tables for 7 dungeons
  (`maxFloorTbl__3`, enemy layout lists, jump tables) compiled in. Adding one needs new patches to extend those tables.
* **Towns** (`gedit/`, `dun/e01`...) are `.mds` scenes plus event scripts and message files; the same override works, but editing
  them needs tools for the game's map and event formats that do not exist yet.
