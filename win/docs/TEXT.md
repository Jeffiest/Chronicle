# Text overrides (item names, descriptions, dialogue)

Put JSON files in `mods/<mod>/text/*.json`. Format: game file -> message id -> new text.

```json
{
 "meswin/system14*.mes": { "152": "Fairy's Ring+", "158": "Fighting Stick{-737}" },
 "gedit/e01/e01talk_*.mes": { "10": "Hello!\nHow should I rebuild Nolun?" }
}
```

* The file name may contain `*` (any run of characters), so one entry covers every language (`_2` is English).
* Mods apply in load order; a later mod wins for the same message. Messages are replaced as the game loads each file.
* **Find the file and id:** `.\run_win.ps1 -DumpText`, then play to the menus/town you want. Every message file the game loads is written,
  decoded, to `win-save\mods\_dump\text\<path with _>.json`. Copy the lines you want into your own file. Menu item names are in
  `meswin/system14_2.mes` (id = the id in item_index.csv, roughly), town dialogue in `gedit/<town>/<name>_2.mes`.
  Set `DC_DUMP_TEXT=2` instead of `1` to also get `_dump\text_patched` (the text after your mods) for checking.
* **Characters that work in plain text:** letters, digits, space, `' " & - ( ) . , ? !`, `\n` (new line) and `{page}` (wait for a button).
  Anything else is a raw glyph code written `{-737}`. A dump shows those codes as `{-761}` etc: they are button icons, colours, the
  player's name (`{-1286}`) and so on. Copy them as they are. No lower-case accents or other punctuation yet.
* **Length:** a message that fits where the old one was is written in place. A longer one is appended to the file; that works for
  small and medium files. For a very large file (long dialogue packs) there may be no room, and the log says
  `has no room to grow it; skipped (shorten it)`.
* Unknown file patterns are ignored silently (the file may simply not be loaded yet); unknown ids and unencodable characters log
  `[mod x] text: ...`.
* New messages (ids that do not exist) cannot be added yet.
