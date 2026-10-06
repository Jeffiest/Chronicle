# Changes

A record of each modification to the port: what the code did before, what it does now, and why.
Newest first. Every entry names the files it touched and how it was checked.

## Options: the resolution list offers every size the display supports

**Branch:** `fix/options-resolution-list`

**Before.** The Display page's Resolution row stepped through a fixed table of nine sizes
(`kResolutions` in `port/src/menu_option.cpp`: 1024x768, 1280x720, 1280x960, 1366x768, 1440x1080,
1600x900, 1920x1080, 2560x1440, 3840x2160), after Desktop, and dropped any that did not fit the
monitor. Sizes the display supports but the table lacks, such as 800x600, 1360x768, 1680x1050,
1920x1200, 2560x1080, 2560x1600 or 3440x1440, could not be chosen from the screen. A size written
in `config.json` that was not in the table was not listed either, so the row showed the nearest
table entry and the first step moved the window off the configured size.

**After.** `ListResolutions` builds the list from three sources: the curated table, every mode the
window's display reports (`SDL_GetFullscreenDisplayModes`, through the new `WindowDisplayModes` in
`port/src/platform/window.cpp`, one entry per width and height whatever the refresh rate), and the
size `config.json` holds. Sizes below 800x600 from the display are left out. Desktop stays first, the
rest run from the smallest area up, duplicates are listed once, and the fit rule is as it was: a size
larger than the monitor (or, windowed, than its usable area) is not offered, except the configured
one. Headless there is no display to ask, so the list is the curated table and the configured size,
as before.

The merge itself is `OptionResolutionList` (declared in `port/src/menu_option.hpp`), a function of the display's
modes, the configured size and what the display can show, so a test can drive it without a display.

**Files.** `port/src/menu_option.cpp` (`kMinWidth`, `kMinHeight`, `ListResolutions`, `OptionResolutionList`),
`port/src/menu_option.hpp` (`OptionResolution`, `OptionResolutionList`),
`port/src/platform/window.hpp` and `window.cpp` (`DisplayModeSize`, `WindowDisplayModes`),
`port/src/tests/menu_option_test.cpp` (`ResolutionListMergesDisplayModes`, `ResolutionListKeepsConfiguredSize`).

**Why.** Retail has no resolution row; the row is the port's, and a display-aware list is what a
settings screen on PC offers. Nothing about the game's own options or their retail help changes.

**Checked.** Builds with the upstream Windows scripts (clang, lld, SDL3). The two new unit tests cover the merge with
fake display modes: de-duplication, the 800x600 floor, the fit rule, the ordering and the configured size.
