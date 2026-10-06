Mod Settings v0.1

Open with F1 (keyboard) or L2+R2+R3 (controller). Up/Down choose a row, Left/Right change a value, Cross applies the highlighted preset
(Relaxed, Normal, Hardcore) from the top row, Circle or the open key closes it. In a dungeon the game is frozen while it is open.

Values are stored in mods/_shared.json (not per save slot) and take effect at once. A mod with no value set uses its own default, so deleting
that file returns everything to normal. Covered mods: Ascension, Floor Mutators, Sturdy, Weapon Mastery, Dungeon Feats, Dungeon+.
Presets only set the listed values; Normal sets every one back to its default.

It loads last (mods/load_order.json) so its input block wins over Ascension's.
