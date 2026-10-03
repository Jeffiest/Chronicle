# macOS on Apple Silicon

The PC port (`docs/PC.md`) builds natively for arm64 macOS and runs on Vulkan
through MoltenVK. Like on Linux, it needs your own PAL disc.

## Dependencies

Xcode or its command line tools, and Homebrew in `/opt/homebrew`:

```sh
brew install llvm lld cmake ninja python glslang sdl3 nlohmann-json vulkan-headers vulkan-loader vulkan-tools molten-vk
```

The presets use Homebrew's `llvm`; Apple's clang is not current enough for C++26.

## Building

```sh
./build.sh macos                              # Debug, port/build/macos-arm64
cmake --preset macos-arm64-release            # Release, port/build/macos-arm64-release
cmake --build --preset macos-arm64-release
```

`CLEAN=1` discards the build directory first; `JOBS=N` sets the number of
parallel jobs.

## Game data

```sh
port/build/macos-arm64-release/dcdata extract "rom/pal/Dark Cloud (PAL).iso" data
```

Without it, the first windowed start asks for the disc image and extracts it.
File names on the disc that are not UTF-8 are written with `%xx` escapes, since
APFS refuses them; the game looks them up the same way.

## Running

```sh
export VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
port/build/macos-arm64-release/darkcloud --data data --save save
```

The Vulkan loader does not search Homebrew's prefix, so `VK_DRIVER_FILES` is
required. Options and `config.json` are those of `docs/PC.md`.

## KosmicKrisp and the tests

`scripts/host/mesa-macos.sh` builds Mesa's KosmicKrisp (Vulkan on Metal 4,
macOS 26 or later) and lavapipe into `~/.local/mesa/<tag>` and prints their
`VK_DRIVER_FILES` lines. KosmicKrisp from `mesa-26.2.4` runs the game in a
window, but flickers and freezes in fullscreen, and shows frames out of order
with `present_mode` `immediate`.

The tests run on lavapipe:

```sh
export VK_DRIVER_FILES=~/.local/mesa/mesa-26.2.4/share/vulkan/icd.d/lvp_icd.aarch64.json
ctest --preset macos-arm64
```

A few tests assume Linux (a case-sensitive file system, `/tmp` not being a
symlink) and fail on macOS.
