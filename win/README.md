# Dark Cloud (PAL) for Windows - native build of the Chronicle port

This branch is the upstream [Chronicle](https://github.com/TheMoonPeople/Chronicle) `port` branch plus a `win/` folder that
builds it as a **native Windows executable** (no WSL). It has been run on an RTX 4070 Ti with the PAL game data.
You need your own PAL (European) disc image; no game data is included.

## What is different on Windows
Upstream targets Linux/macOS (ELF/Mach-O, glibc). On Windows the build:
- uses the MinGW ABI (llvm-mingw clang, static libc++, lld) with SDL3 and the Vulkan loader;
- rewrites the compiler's IR (`win/weakcc.py`) so the port's replacement functions override the decompiled ones the way the
  Linux build's weak linkage does, and links with `/force:unresolved` plus `/alternatename` aliases;
- rewrites `long` to `long long` at the 69 places the decompiled code assumes a 64-bit `long`;
- converts game file names between Shift-JIS (what the game asks for) and Unicode (what Windows stores), also in the disc extractor;
- makes string literals writable (the PS2 compiler allowed it) and applies the small source patches listed in `win/gen_win_src.py`;
- adds a crash reporter, a first-run settings screen (`darkcloud.exe --setup`) and a PNG texture-mod loader.
Defaults: Mailbox presenting (keeps game logic at 50 Hz), frame cap 144, FPS counter off.

## Layout used by the scripts
The scripts expect this layout (edit `$root` at the top of each script, and the paths at the top of `win/gen_win_src.py`):

```
Chronicle-project-Claude\          <- $root
  Chronicle-src\                   <- a checkout of this repository (win\ is inside it)
  Chronicle-win-src\               <- generated: patched copy of the source (do not edit)
  Chronicle-win-build\             <- build output: darkcloud.exe, SDL3.dll
  win-deps\                        <- SDL3 (VC zip), llvm-mingw, Vulkan SDK installer
  win-save\                        <- save folder used by run_win.ps1
```

## Build
1. `win\scripts\setup_llvm_mingw.ps1` and `win\scripts\setup_win_deps.ps1` (download the toolchain, SDL3, Vulkan SDK; install the Vulkan SDK once).
2. `win\scripts\build_win.ps1` generates `Chronicle-win-src` with `gen_win_src.py` and builds `darkcloud.exe`.
3. Double-click `darkcloud.exe` (it asks for your disc image and extracts it), or `win\scripts\run_win.ps1 -Data <extracted data folder>`.
4. `win\scripts\package_win.ps1` makes a zip for friends: `darkcloud.exe` + `SDL3.dll`.

`update_upstream.ps1` pulls a newer upstream `port` and tells you which Windows patches stopped applying.

## Texture mods
See `win/docs/TEXTURE_MODS.txt`. Put PNGs in `mods\<name>\textures\` in the save folder; `DC_DUMP_TEXTURES=1` exports every texture the game loads.

## Status / known gaps
Playable through a full dungeon round. 13 `.refptr` link symbols were never hit in play. The patches live in a generator rather than in-tree;
turning them into guarded `#ifdef _WIN32` changes is planned. Not endorsed by the upstream project.
