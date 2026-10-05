# Game data tables (mod JSON)

Put JSON files in `mods/<mod>/data/*.json`. They are applied once at startup, in mod load order (folders alphabetical,
files alphabetical inside a folder; later wins). You only list what you want to change.

**See the current values first:** run `.\run_win.ps1 -DumpData`. It writes every table as the game has it (after all enabled
mods) to `win-save\mods\_dump\data\`: weapons.json, items.json, attachments.json, monsters.json, shops.json. Copy the
entries you want into your own file and edit them.

```json
{
 "monsters":    { "0": { "hp": 999, "defense": 7, "exp": 50, "damage_from_attacker": [100,100,100,100,100,50] } },
 "weapons":     { "257": { "attack": 77, "attack_max": 200 } },
 "items":       { "148": { "vol": 5 } },
 "attachments": { "81": { "attack": 5, "elem": [10,0,0,0,0] } },
 "shops":       { "0": [148, 145, 177] }
}
```

| table | key | fields |
|---|---|---|
| `weapons` | item id (257+, see item_index.csv) | durability attack endurance speed magic owner hole[6] hole_num elem[5] vs_monster[10] exp_base exp_per_level flags buildup_mask0 buildup_mask1 attack_max magic_max chain_pos |
| `items` | item id | sort_key use_flags kind_flags vol vol_range stack_kind |
| `attachments` | item id (81+) | sphere_weapon_no sphere_flags sphere_level attack endurance speed magic elem[5] vs_monster[10] |
| `monsters` | MonstorTable index 0-166 (not the in-room slot; `dc.monster_kind(i)`) | hp attachment_kind attachment_weight[5] collision_radius defense hardness shot_effect[2] exp money money_chance kind name_no steal_item drops_items item_damage_rate status_chance rare_item damage_from_attacker[6] knockback_scale |
| `shops` | shop number 0-17 | list of up to 20 item ids; replaces the whole list |

Arrays may be shorter than the field (the rest stays). `elem` order: fire, ice, thunder, wind, holy. A value that is the wrong
type or out of range for the field is rejected with a warning and nothing from that field is applied. Keys starting with `_`
are comments. Unknown tables, ids and fields are logged as `[mod x] data: ...` and skipped.

Not covered: which monsters appear on a floor (the floor layout tables only choose which models load; spawn counts come from
the map files), and any value the game computes at runtime. Changes made to a weapon you already own are not applied to the
copy in your save (it was seeded at pickup); use a new game or a fresh weapon.
