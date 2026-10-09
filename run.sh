#!/usr/bin/env bash
# Build and launch the selected platform, defaulting to the optimized Linux port.
#
#   ./run.sh                     build and launch the optimized Linux port
#   REGION=PAL ./run.sh ps2       the July 12, 2001 PAL prototype instead
#   ./run.sh linux-x64           build and launch the optimized Linux port
#   ./run.sh linux-x64-debug     build and launch the Debug Linux port
#   ./run.sh linux-x64-release   build and launch the optimized Linux port
#   ./run.sh macos               build and launch the macOS port
#
# Every build but macOS's runs in the dev container, or in place when this is
# already inside one. The first run builds the image and extracts and
# disassembles the disc; later runs are incremental, since the tree is mounted
# rather than copied in. PCSX2 and the port itself run where this script does
# (they need a display); set PCSX2=/path/to/pcsx2-qt if needed.
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    echo "Usage: ./run.sh [linux-x64|linux-x64-release|linux-x64-debug|ps2|macos] [game arguments]"
    echo "Defaults to the optimized Linux port. Game arguments are forwarded to native builds."
}

platform=linux-x64-release
if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help) usage; exit 0 ;;
        -*) ;;
        *) platform=$1; shift ;;
    esac
fi
case "$platform" in
    ps2)
        [ "$#" -le 1 ] || { usage >&2; exit 2; }
        ;;
    linux-x64|linux-x64-release|linux-x64-debug|macos)
        case "$platform" in
            linux-x64|linux-x64-release)
                ./build.sh linux-x64-release
                if [ "$(uname -s)" != Linux ]; then
                    echo "port/build/pc-release/darkcloud is built; launching it requires a Linux host." >&2
                    exit 1
                fi
                exec port/build/pc-release/darkcloud "$@"
                ;;
            linux-x64-debug)
                ./build.sh linux-x64
                if [ "$(uname -s)" != Linux ]; then
                    echo "port/build/pc/darkcloud is built; launching it requires a Linux host." >&2
                    exit 1
                fi
                exec port/build/pc/darkcloud "$@"
                ;;
            macos)
                ./build.sh macos
                exec port/build/macos-arm64/darkcloud "$@"
                ;;
        esac
        ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown platform: $platform" >&2; usage >&2; exit 2 ;;
esac

. scripts/host/container.sh

require_rom
require_iso

REGION=${REGION:-NTSC}
ISO="ps2/build/$(printf %s "$REGION" | tr '[:upper:]' '[:lower:]')/Dark Cloud ($REGION Build).iso"

# Building comes first and on its own: neither target is tied to the hash
# check, so code that does not match retail still boots, which is the whole
# point of running it. `set -e` matters here -- if that step fails the run has
# to stop, or the emulator boots whatever stale image is lying around. `elf` is
# what leaves build/<region>/SCUS_971.11 for the report below; the disc carries a second
# link of its own, without debug information.
#
# The report is the same one build.sh and the Dockerfile's CMD give, and
# deliberately not the `build` target: that also pulls in verify_extracted,
# which hashes the 1.7GB DATA.DAT, and nothing about booting the image depends
# on the extracted files. It only reports, so the boot goes ahead either way.
BUILD='
    set -e
    scripts/build/cmake.sh iso elf
    scripts/build/verify_built.sh
'

# Already inside a container: the toolchain is right here, so build in place.
if in_container; then
    REGION=$REGION sh -c "$BUILD"
    exec scripts/host/pcsx2.sh "$ISO"
fi

require_builder
ensure_image dcdecomp_dev dev
report_parallelism

# -t keeps the colours and progress line, skipped when this script's own output
# is redirected.
# bash 3.2, which is what macOS ships, treats an empty array as unset under
# `set -u`, hence the guarded expansions below.
TTY=()
if [ -t 1 ]; then TTY=(-t); fi

# REGION, and JOBS when it is set, are for scripts/build/cmake.sh inside the
# container.
ENV_ARGS=(-e "REGION=$REGION")
if [ -n "${JOBS:-}" ]; then ENV_ARGS+=(-e "JOBS=$JOBS"); fi

"$BUILDER" run --rm ${TTY[@]+"${TTY[@]}"} "${ENV_ARGS[@]}" \
    -v "$PWD:$CONTAINER_WORKDIR:Z" \
    -w "$CONTAINER_WORKDIR" \
    -e HOME=/tmp \
    dcdecomp_dev sh -c "$BUILD"

exec scripts/host/pcsx2.sh "$ISO"
