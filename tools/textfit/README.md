# Text-Fit Comparison Tool (`tools/textfit`)

A static analysis and text-fit measurement tool for Dark Cloud translations.

It measures translated strings across all languages against the original English reference using the game's actual font metrics and layout box constraints, reporting any overflow in width or line count.

---

## 1. Where Messages, Box Widths, Heights, and Line Limits Come From

### 1.1 Retail Disc & Pack Architecture
Game text is stored on the retail disc image (`DATA.DAT` and indexed by `DATA.HD2`, extracted via `dcdata extract`):
- **Loose message files (`.mes`)**: 16-bit binary word files.
  - Word 0 (`s16`): message count $N$.
  - Followed by $N$ index pairs: `id` (`s16`) and `offset` (`s16`).
  - Text data starts at `(1 + count) + offset` words from the beginning.
  - Codes run until `MES_CODE_END` (`-0xFF` / `-255`). Un-terminated entries reaching end-of-file without `MES_CODE_END` are invalid dummy entries and discarded (matching `port/src/localize.cpp` `ReadRetail`).
- **Archive packs (`.pak` and `.pac`)**:
  - Archive entries start with a 76-byte header:
    - Bytes `0..63`: Entry filename (`char[64]`).
    - Bytes `64..67`: Data offset (`s32`).
    - Bytes `68..71`: Data size in bytes (`s32`).
    - Bytes `72..75`: Next entry offset (`s32`).
  - Entries ending in `.mes` contain internal `.mes` files (e.g. `commenu/a_eng/option.pac/allmenu.mes`).
- **Language mapping**:
  - PAL discs include five localized European languages, identified by folder prefixes or numerical suffixes:
    - `0`: Japanese (`ja_jp`, folder `a_jpn`)
    - `1`: American English (`en_us`, folder `a_usa`)
    - `2`: British English (`en_gb`, folder `a_eng`) &mdash; **Reference**
    - `3`: French (`fr_fr`, folder `a_fre`)
    - `4`: German (`de_de`, folder `a_ger`)
    - `5`: Italian (`it_it`, folder `a_ita`)
    - `6`: Spanish (`es_es`, folder `a_spa`)
- **Key Normalization (`localize_message_key`)**:
  - Standardizes paths by stripping language directory prefixes (`a_eng/`, `a_spa/`, etc.) and language numerical suffixes (`_2.mes`, `_6.mes`), converting file paths to dots:
    - `commenu/a_spa/option.pac/allmenu.mes` (id 11) &rarr; `commenu.option.pac.allmenu.11`
    - `commenu/a_eng/option.pac/allmenu.mes` (id 11) &rarr; `commenu.option.pac.allmenu.11`
  - This guarantees identical, paired keys across all five languages.

### 1.2 Port Localization (`port/lang/` & `port/src/options/`)
The PC port's Options screen strings are not on the PS2 disc:
- Shipped translations live in `port/lang/<language>.json` (`es_es.json`, `fr_fr.json`, `de_de.json`, `it_it.json`).
- Reference English strings live in `port/src/options/screen.cpp` and `rows.cpp` (`OptionStrings()`).

---

## 2. Window Box Constraints and Line Limits

Box dimensions and line limits originate from four distinct subsystems:

### 2.1 Options Screen Layout (`port/src/options/screen.hpp`, `draw.cpp`)
All drawn on the game's $640 \times 480$ 2D canvas:
- **Row Labels (`options.<setting>.label`)**:
  - Positioned from `kLabelX = 138` to `kValueX = 374`.
  - **Width Limit**: $374 - 138 = 236\text{ px}$.
  - **Line Limit**: $1\text{ line}$.
- **Choices & Values (`options.<setting>.choice.<n>`, `options.<setting>.value.<name>`)**:
  - Positioned from `kValueX = 374` to `kValueRight = 554`.
  - **Width Limit**: $554 - 374 = 180\text{ px}$.
  - **Line Limit**: $1\text{ line}$.
- **Help Panes (`options.<setting>.help`, `options.help.*`)**:
  - Starts at `kHelpY = 396`, width $\sim 270\text{ px}$.
  - **Width Limit**: $270\text{ px}$.
  - **Line Limit**: $4\text{ lines}$ (documented in `LOCALIZATION.md`: "Help windows hold four lines of about 22 characters").
- **Shortcuts Footer (`options.shortcuts`)**:
  - Width $\sim 300\text{ px}$, $3\text{ lines}$.
- **Page Tabs (`options.page.*`)**:
  - Width $100\text{ px}$, $1\text{ line}$.

### 2.2 Dungeon Message Windows (`ps2/src/dun/gameloop.cpp`, `ps2/src/dngmessageman.cpp`)
- **System Dialogue (`DngMes1`, `DngMes2`)**:
  - Configured in `gameloop.cpp`: `columns = 15; rows = 3; char_width = 11; char_height = 22;`
  - **Width Limit**: $15 \times 11 = 165\text{ px}$.
  - **Line Limit**: $3\text{ lines}$.
- **Steve Window (`DngMesStb` in `CDngMessageMan::SetSteevMes`)**:
  - Configured in `gameloop.cpp`: `columns = 15; rows = 4; char_width = 12; char_height = 24;`
  - **Width Limit**: $15 \times 12 = 180\text{ px}$.
  - **Line Limit**: $4\text{ lines}$.
- **Battle Event Windows (`BtEventMes0`, `BtEventMes1`)**:
  - `BtEventMes0`: `columns = 21; rows = 4; char_width = 12;` &rarr; $252\text{ px}$, $4\text{ lines}$.
  - `BtEventMes1`: `columns = 22; rows = 5; char_width = 12;` &rarr; $264\text{ px}$, $5\text{ lines}$.

### 2.3 Common Menu Message Windows (`ps2/src/menu_draw.cpp`)
Configured in `InitMenuMesSet`:
- **Menu Help (`CommonMenuMes3` under `MENU_MES_SET_ALLMENU`)**:
  - `columns = 29; rows = 4; char_width = 11; char_height = 20;`
  - **Width Limit**: $29 \times 11 = 319\text{ px}$.
  - **Line Limit**: $4\text{ lines}$.
- **Shop Dialogue (`CommonMenuMes3` under `MENU_MES_SET_SHOP`)**:
  - `columns = 30; rows = 3; char_width = 12; char_height = 20;`
  - **Width Limit**: $30 \times 12 = 360\text{ px}$.
  - **Line Limit**: $3\text{ lines}$.
- **Menu System Notice (`CommonMenuMes1`)**:
  - `columns = 15; rows = 3; char_width = 11;` &rarr; $165\text{ px}$, $3\text{ lines}$.
- **Character Name (`AtoraNameMes`)**:
  - `rows = 1`.

### 2.4 Dynamic Script Dialogue & Speech Bubbles (`ps2/src/clsmes.cpp`)
For general story dialogues (`gedit.*`, `rmdat.*`, etc.):
- Sized dynamically by `ClsMes::NeedMesWinWH` and `MakeMesTexture`:
  $$\text{win\_width} = \text{char\_width} \times (3.0 + 3.0 + \text{text\_columns})$$
- While speech bubbles can expand, the original English text established the visual footprint on the $640 \times 480$ screen.
- A translation that exceeds the original English maximum line width or has more lines than the English text will overflow its speech bubble, cover character models, or clip against screen boundaries.
- **Limit**: The original English string's measured width ($\text{ref\_max\_width}$) and line count ($\text{ref\_lines}$).

---

## 3. Font Metrics & Token Expansion

### 3.1 Game Font & Gaiji Metrics
- Font character widths are derived from `port/src/gametext.cpp` and `ps2/src/gameutil.cpp` (`GaijiDataTbl`):
  - Standard characters have cell width $\text{char\_width} = 11.0\text{ px}$ (or $12.0\text{ px}$ for shop/dungeon).
  - Accented characters (`á`, `é`, `ñ`, `ç`, etc.) advance by $1\text{ cell}$.
  - Space (`MES_CODE_SPACE`) advances by $1\text{ cell}$.
  - `{gap N}` advances by $N\text{ pixels}$.
  - Formatting codes (`{white}`, `{cyan}`, `{/color}`, `{wait N}`, etc.) advance by $0\text{ px}$.
  - Numeric glyph escapes such as `{338}` are display notation for one unmapped PAL glyph code and advance by one game-font cell. The braces and digits are not rendered.

### 3.2 Button & Symbol Glyph Widths
Button tokens from `GAIJI_CELL_COUNTS` (`ps2/src/gameutil.cpp`):
- Face buttons (`{cross}`, `{circle}`, `{square}`, `{triangle}`): $2\text{ cells} = 22.0\text{ px}$.
- Shoulder buttons (`{L1}`, `{R1}`, `{L2}`, `{R2}`): $2\text{ cells} = 22.0\text{ px}$.
- D-Pad (`{dpad}`, `{dpad-updown}`, `{dpad-sides}`, `{up}`, `{down}`, `{left}`, `{right}`): $2\text{ cells} = 22.0\text{ px}$.
- Menu buttons (`{select}`, `{start}`): $8\text{ cells} = 88.0\text{ px}$.
- Symbols (`{alert}`, `{heart}`, `{note}`, `{red-x}`, `{icon-sword}`): $2\text{ cells} = 22.0\text{ px}$.
- Special icons: `{hand}` ($3\text{ cells} = 33.0\text{ px}$), `{monster}` ($4\text{ cells} = 44.0\text{ px}$), `{bait}` ($5\text{ cells} = 55.0\text{ px}$).

### 3.3 TrueType Font Rendering
- When Pillow is available, glyph widths use the scaled raster bounds and 0.1-em inter-glyph gap used by `port/src/clsmes.cpp`'s `TtfLayoutRows`. The port caps each glyph to its game cell; spaces and gaiji retain their cell advances.
- In HTML reports, `DarkCloudCompendium.ttf` is embedded inline via base64 `@font-face`, allowing any browser to render authentic game font previews.

### 3.4 PAL Page and Window Layout
- `{page}` starts a new rendered page and resets its line counter. Line-limit checks use the greatest number of rows on any one page, not the total across pages.
- PAL speech bubbles resize from their current text (`NeedMesWinWH` / `MakeMesTexture` in `ps2/src/clsmes.cpp`). When an exact fixed window cannot be identified, the fallback is the available 640x480 canvas after the six-cell horizontal and three-row vertical frame margins, rather than the unrelated English sentence's previous width and line count.
- `CommonMenuMes3` can be 29 columns by four rows (`ps2/src/menu_draw.cpp`), but `allmenu.mes` is a shared resource used by multiple menu windows, so its filename alone does not prove that every message uses those dimensions. Message 422 is the weapon ability list, displayed by `MenuClsMes` with nine rows (`ps2/src/battlemenu.cpp`), and has its own rule.

---

## 4. CLI Usage

```bash
# Compare Spanish against English and generate HTML report
python tools/textfit/textfit.py --lang es_es --out report.html

# Specify reference language (default: english / en_gb)
python tools/textfit/textfit.py --lang es_es --ref en_gb --out report.html

# Also write CSV output
python tools/textfit/textfit.py --lang es_es --out report.html --csv report.csv

# Evaluate all four European languages (es_es, fr_fr, de_de, it_it)
python tools/textfit/textfit.py --all-langs

# Run unit tests
python -m unittest tools.textfit.test_textfit
```

---

## 5. Severity Ranking Formula

All results are sorted in descending order of severity:
$$\text{Severity} = (\text{line\_overflow} \times 1000.0) + \text{width\_overflow\_px} + (\text{overflow\_pct} \times 1.5)$$

1. **Line Overflows** ($\ge 1000$ points per extra line): Highest severity, as extra lines can cause text to clip completely outside fixed UI panes.
2. **Width Overflows**: Ranked by excess pixels and percentage overflow past the box boundary.
3. **Safe Fits**: Zero severity.
