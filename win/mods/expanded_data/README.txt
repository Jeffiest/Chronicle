Expanded Data v0.1 - weapon rebalance ported from DarkCloud-Expanded

Source: https://github.com/Tororoi/DarkCloud-Expanded (a fork of Gundorada-Workshop/DarkCloud-Enhanced), BSD-2-Clause.
Their license text is in LICENSE-DarkCloud-Expanded.txt. The numbers here come from their Weapons/WeaponBalance.cs, translated to
this port's weapon table by a script (their version writes PS2 memory in PCSX2; this mod edits the table at startup).

What it changes: about 60 weapons across Toan, Xiao, Goro, Ruby, Ungaga and Osmond: base stats, element and slayer values, max
attack/magic, extra attachment slots, build-up paths and effects (Poor, Stop, Heal, Critical, Drain, ABS Up). Ungaga's weapons get
+10 attack/+10 max attack/+15 endurance (except Babel Spear), Osmond's +15 attack/+15 max attack.
Checked: for every change the starting value in the PAL tables equals what their changelog says it was.

Not included (needs engine work or game logic, not data): custom weapon abilities (Sun Sword's flash, Big Bang's explosions, Mirage's
decoy and so on), Lamb's Sword transform threshold, shop prices, loot and chest tables.

Note: weapons you already own keep their saved stats; the new values apply to weapons you find or buy after enabling this mod.
Frozen Tuna's extra slot is ported exactly as their code writes it (their slot 4, with slot 3 left empty).
