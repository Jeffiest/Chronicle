# Chronicle mod guide

What is installed, what each mod does, and every control. Everything lives in `win-save\mods\<mod>\`. Turn mods on and off and set the
load order with `python mod_manager.py`; restart the game after saving. Open the in-game settings screen with **F1** or **L2+R2+R3**.

## Launcher
`ChronicleLauncher.exe` (build it with `build_launcher.ps1`; keep it beside `win-save` and `Chronicle-win-build`) is the one place to start the game:
**Game** tab (data and save folders, window size, fullscreen, frame limit, volume), **Multiplayer** tab (Single player / Host / Join, IP, port, a
connection test, your addresses to give friends), **Mods** tab (turn mods on and off, load order, conflicts). **PLAY** saves everything, turns the
co-op mod on or off to match, and starts the game; as host or guest the game connects by itself a second after the world loads. The game's
output goes to `win-save\launcher_run.log`. The **Character** tab sets Toan's tunic: 16 colours (orange original, blue, green, red, purple, teal, pink, yellow, lime, cyan, magenta, brown, white, black, gold, navy) or a **Custom** design: pick your own front (and optional back) picture for the poncho, shrunk to 256x256 and sent to the other players, who see it on your Toan. "Save the template pictures" copies the game's own poncho textures (needs a texture dump once, `run_win.ps1 -DumpTextures`) to paint over. Colours apply the next time the game loads Toan (a door or a dungeon). Everyone in a co-op game needs the same mods in the same order.

## Co-op world sync (host's NPCs, shared Georama)
In towns the host's villagers are the real ones: a guest sees them walk and turn the way the host does, and a villager a guest talks to stands still
for the host too. The Georama is shared live: anything one player builds or removes appears in the other's town if they are in the same town map
(a toast says who did it). If a part is built on top of a player, that player is put into the Georama view and must move the cursor to a free spot and
leave the view to stand there. Parts built before the other player joined are not copied over; both players only share edits made while connected.

## Joining the host's world (Dark Souls style)
A guest plays in the **host's world**: the host's story progress, unlocked towns and dungeon floors, Georama and time of day. The guest's own **items,
gilda, weapons and party stay theirs**: whatever they pick up or spend is theirs when they leave. On join the guest is taken to the host's town (or the
host's dungeon's floor list); starting a game from the title screen while connected does the same. When they leave, or the connection drops, their own
world comes back and they are sent to the place their own save resumes on. Anything written to the guest's save during the visit holds the guest's own
world with the items and gilda they have now, so the visit never overwrites their story progress. The guest does not advance their own story while
visiting, and the host's world is refreshed every few seconds.

## Native Menu and Controls (Modernization Core)
The game's own main menu has a **MODS** entry in the slot the Manuals used to have (same art style, redrawn from the game's menu sheet). It opens one page
listing everything the mods add; the last row, **Manuals (how to play)**, opens the original manuals. The **Controls** mod adds 1-3 quick items,
4-9 party members, the mouse wheel, and **Hub or MODS > Controls** to remap every key in the game (Cross replaces, Square adds a second key).
Default layout: WASD move, mouse look, left click or F attack/talk/open/confirm, E or Space or Ctrl lock on (and Back), right click guard, G use item,
Tab menu, Esc or Enter pause, Q camera behind, R first-person look, Z next target, 1-3 quick items, 4-9 party, wheel = choose item / move in menus.

## The mouse
In every menu the game's pointing hand is the mouse: it follows the pointer anywhere on screen, the item under it becomes the selected one, a left click confirms and a
right click goes back. In play the mouse turns the camera directly (Settings > controls.mouse_look scales it). On the title screen any key or click starts. Button
prompts in the game's messages show your keys (a key cap for F, Tab, G ..., a mouse for a click); set DC_PAD_PROMPTS=1 to see the pad glyphs again.

## Controls at a glance

| Where | Input | Does |
|---|---|---|
| Anywhere | **K** or **L1+R1+R3** (click the right stick) | Open the Hub menu: skill tree, bestiary, trophies, mastery, co-op. Select is not used because the game uses it to switch characters. |
| Dungeon | hold **L2** + **Square / Triangle / Circle** | Cast Shockwave / Second Wind / Berserk. A panel lists them while L2 is held. |
| Floor-select list | **Square** / **Triangle** | Raise / lower the Dungeon+ tier of the floor under the cursor (`]` and `[` on a keyboard). |
| Town | **L1+L3** or **O** | Bird's-eye camera on and off (Expanded Town). |
| Anywhere | **F1** or **L2+R2+R3** | Mod Settings screen. |
| Anywhere | **F3** | FPS counter (the game's own). |

Inside the skill tree: d-pad moves, **Cross** buys a rank, **Square** respecs (costs gilda), **Circle** closes.
Inside Mod Settings: Up/Down choose a row, Left/Right change a value, **Cross** applies the highlighted preset, **Circle** closes.

## The mods

**Ascension** (levels and combat). Every character earns XP and levels (+3 max HP, +2.5% damage per level, a little less damage taken).
Crits and damage variance; enemies scale with dungeon and floor; about 8% of enemies are **champions** (3x HP, bigger, themed loot, a
weapon stat boost on some drops); weapons you pick up may roll bonus stats (Fine / Superior / Exquisite / Legendary). An 18-skill tree
(Might, Vitality, Focus) with passives and three active skills that spend **Focus** (the purple bar). The XP bar, level and Focus show at the
bottom while you are in a dungeon. Numbers: `ascension\scripts\config.lua` or the Settings screen.

**Dungeon+** (replay floors harder). On the floor-select list, set a tier (1-5) per floor. A tiered floor has bigger rooms, tougher
enemies (+35% HP, +2 defense per tier), more gilda, a higher champion chance and XP in Ascension, and a bonus item reward when you clear it.
The floor builder cannot add rooms (it fits 5-6), so floors grow through larger rooms. Unlock rule: Settings > "Dungeon+: unlocked
everywhere" (on by default; turn it off to require reaching a dungeon's last floor).

**Paragon (Ascension).** After the level cap, XP keeps filling a Paragon bar; each rank (up to 50) gives +1.5% damage and +4 max HP.

**Enemy Variety.** Every enemy gets its own random speed, and any ordinary enemy has a 25% chance to survive one killing blow, drop to 30% life
and get back up (after DarkCloud-Expanded). Bosses and key carriers are exempt. Tune it on the Settings screen.

**Floor Bounty.** The first enemy kind you meet on a floor becomes its bounty: kill 6 for a gilda payout scaled by dungeon and floor.

**Dungeon Feats.** Floor-clear gilda, kill streaks (HUD counter), a bestiary with milestones per enemy kind, and 11 trophies.

**Floor Mutators.** Each floor may roll a modifier with a banner: Glass Cannon, Iron Hide, Bounty, Fountain, Scorched, Lucky Day.

**Weapon Mastery.** Kills with a weapon type earn mastery levels (+2% damage each, up to +10%).

**Sturdy.** Weapons wear more slowly and thirst drains more slowly.

**Expanded Data** (after DarkCloud-Expanded). Rebalances about 60 weapons (stats, slayer values, slots, build-up paths, effects), 229 shop
prices, a daily rotating item in seven shops. **Expanded Abilities:** Cross Hinder x2 against the undead, Mobius Ring ramps up in a fight,
Brave Ark cures ailments, Heaven's Cloud and Snail goo enemies, Frozen Tuna can stop them, Cactus absorbs water, Aga's Sword takes 15
off damage; menu descriptions updated to match. **Harder AI:** five undead species revive 45% of the time. **Expanded Town:** the
overhead camera. All credit DarkCloud-Expanded (BSD-2-Clause); each mod folder holds their license.

**Mod Settings.** The in-game settings screen with Relaxed / Normal / Hardcore presets (values in `mods\_shared.json`).

**Example and test mods (off by default):** `anim_test` (a wider Toan), `data_test` (stat, shop and text examples), `files_test`, `engine_test`,
`combat_demo`, `regen_example`, `mymod`, `mymodels`. `coop` and `net_test` are from another work stream and not covered here.

## Things that stack
Ascension, Floor Mutators, Weapon Mastery, Expanded Abilities and Dungeon+ all change damage dealt or taken; they apply one after another
in load order, so a floor with several effects can be very strong or very harsh. If it feels off, use the Hardcore / Relaxed presets or
turn a mod off in the manager.

## If something misbehaves
Run the game with output to a file and read it: `.\run_win.ps1 *> run_mod.log` (UTF-16). Lines starting `[mod name]` are a mod talking; a
`script error, mod disabled` line names the file and line. To undo all settings changes, delete `win-save\mods\_shared.json`.

## Not yet seen in play
The menu description edits, champion size and hitboxes, the daily shop rotating across days, status effects landing on enemies, the Dungeon+
bonus rewards. If any looks wrong, note which and send it back.
