Ascension v0.1 - a modern-RPG layer. Everything tunable is in scripts/config.lua (restart the game after editing).

Character levels: each character earns XP from kills, levels up to 40, +3 max HP and +2.5% damage per level, -0.8% damage taken
  per level. Progress is saved with your slot (mods/_store/ascension/slot<N>.json is written when you save in the game).
Combat: damage varies +/-10%; 8% crit chance (+ weapon speed bonus) for x1.75 with a CRIT! callout.
Enemies: HP +10% per dungeon and +3.5% per floor, extra defense per floor, and they hit harder with depth. 10% of enemies are
  ELITE (red tag): 2.5x HP, more defense, 3x XP and gilda.
HUD: a thin XP bar with name, level and XP at the bottom centre while in a dungeon.
Start new games from a fresh launch (a New Game after loading another slot would inherit that slot's levels until you save).
To switch off: untick Ascension in mod_manager.py.
