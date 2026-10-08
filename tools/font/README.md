# DarkCloudCompendium.ttf

The Dark Cloud Compendium Community Font ("Dark Cloud Speech", version 001.000), by Dayuppy and Moonbunny,
copyright (c) 2020 Dayuppy & Moonbunny. It was released to the community to use as they wish, and Dayuppy
confirmed on 2026-10-06 that it may be used in this port.

It draws the FPS counter (`gen_overlay_font.py`) and the game's message text (docs/LOCALIZATION.md): the build
copies it to `lang/font.ttf` beside the executable. Its accented letters, the cedilla, `¡`, `¿`, `œ` and `Œ`
are blank in the file; the port builds them from the font's own letters and marks when it draws.

To use another font for message text, put it at `lang/font.ttf` in the save folder (that wins over this
one) or start the game with `--font <file>`.
