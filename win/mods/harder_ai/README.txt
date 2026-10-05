Harder AI v0.1 - after DarkCloud-Expanded's "harder enemy AI" (https://github.com/Tororoi/DarkCloud-Expanded, BSD-2-Clause; license in LICENSE-DarkCloud-Expanded.txt).

The five enemies whose scripts already roll a chance to get back up after death (Master Jacket, Mummy, Gacious, Horn Head, Silver Gear)
now do it 45% of the time instead of 10-18%. This is done by supplying edited copies of their script files (files/dun/monstor/*.stb);
each edit changes one number and was checked against the original bytes first.

Not included: their splice that lets every enemy with a get-up animation revive 30% of the time (it rewrites enemy script bytecode),
and the faster / randomized enemy options. "Stronger enemies" is what the Ascension mod already does with its dungeon and floor scaling.
To turn the effect off, untick Harder AI in mod_manager.py.
