Dungeon+ v0.3 - per-floor tiers
On the floor-select screen (where you pick B1, B2, ...) put the cursor on a floor and press SQUARE to raise that floor's Dungeon+ tier (after tier 5 it goes back to normal) or TRIANGLE to lower it.
A +n appears beside that floor only; every floor keeps its own tier, so you can run B6 at +5 and leave B7 normal. A line at the top shows the tier of the floor under the cursor.
The ] and [ keys do the same on a keyboard.
A tiered floor has: +35% enemy HP, +2 enemy defense and +40% gilda from enemies per tier, and rooms 1 cell bigger at tiers 1-2 and 2 cells bigger from tier 3.
How big a floor can get is limited by the game's floor builder (5-6 rooms in a fixed area; 15 of 16 enemy slots already used), so floors grow through larger rooms.
Tiers are saved with your game slot. UNLOCK_ALL at the top of scripts/main.lua is true so you can try it everywhere; set it to false to require reaching a dungeon's last floor.
The floor size is set before a floor is built: for the chosen floor, or (inside a dungeon) for the floor below the one you are on.
