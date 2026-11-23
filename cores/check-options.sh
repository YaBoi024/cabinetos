#!/usr/bin/env bash
#
# THE CORE'S OPTIONS, AGAINST THE LIST IN THE REPOSITORY (#63, MMagTech
# 2026-10-02). The console tells cores option values by name; an upgrade that
# renames one, drops a value or moves a default leaves those answers going
# nowhere, and nothing says so. Every core is pinned, so it can only happen
# when a commit moves a pin, and this is where that commit finds out: any
# difference from cores/options/<core>.txt fails the build, with the
# difference printed. Check it against what the console sets (coreoptions.cpp,
# quality.cpp, sysopts.cpp), then refresh the file in the same commit.
#
# A SCRIPT OF ITS OWN, not a function in build-core.sh, because every core's
# CI cache key covers build-core.sh's shared lines (ci/core-cache-key.sh):
# while the check lived there, editing it rebuilt every core (2026-10-02).
# build-core.sh runs this after every build and on every cache hit, so the
# check still runs every time.
#
# Dolphin and FBNeo declare their options only with a real game loaded, which
# CI does not have; their lists come from the A9 (tools/check-applied.sh).
# FCEUmm and MAME 2003-Plus declare inside retro_load_game before reading the
# file, so a stub is enough.
#
# Usage: cores/check-options.sh <core>      the .so is read from
#                                           $CABINETOS_CORE_OUT (cores/build),
#                                           in $CABINETOS_BUILDER

set -euo pipefail

CORE="${1:?usage: cores/check-options.sh <core>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${CABINETOS_CORE_OUT:-$ROOT/cores/build}"
BUILDER="${CABINETOS_BUILDER:-cabinetos-builder}"
# The same name build-core.sh files the artifact under.
SO="${CORE%_libretro}_libretro.so"

list="$ROOT/cores/options/$CORE.txt"
case "$CORE" in
    dolphin|fbneo_libretro)
        echo "options     not listed here: needs a real game (tools/check-applied.sh)"
        exit 0
        ;;
esac
if [ ! -f "$list" ]; then
    echo "$CORE: no cores/options/$CORE.txt to compare its options with" >&2
    exit 1
fi
now=$(podman run --rm -v "$ROOT":/repo:Z -v "$OUT":/out:Z -w /tmp "$BUILDER" \
    sh -c 'gcc -O2 -Wall -Wextra -o /tmp/core-options /repo/tools/core-options.c -ldl || exit 1
           printf "NES\032\001\001\000\000\000\000\000\000\000\000\000\000\000" > blank.nes
           head -c 24576 /dev/zero >> blank.nes
           printf "PK\005\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000" > asteroid.zip
           case "$2" in
               fceumm) exec /tmp/core-options "/out/$1" --game blank.nes ;;
               mame2003_plus) exec /tmp/core-options "/out/$1" --game asteroid.zip ;;
               *) exec /tmp/core-options "/out/$1" ;;
           esac' _ "$SO" "$CORE") || {
    echo "$CORE: could not list its options" >&2
    exit 1
}
# ONLY THE OPTION LINES, `name<TAB>default<TAB>values`. A core may print
# chatter of its own while it loads, and PCSX ReARMed's depends on the
# processor: "Using 32-bit LUT" was there on one CI runner and not on the next
# (2026-10-12), which failed a build that changed no core.
now=$(printf '%s\n' "$now" | grep -P '\t' || true)
if ! diff -u <(grep -P '\t' "$list" || true) <(printf '%s\n' "$now") > /tmp/options.diff; then
    echo "$CORE: ITS OPTIONS ARE NOT THE ONES IN cores/options/$CORE.txt" >&2
    cat /tmp/options.diff >&2
    exit 1
fi
echo "options     $(printf '%s\n' "$now" | grep -c .), as listed in cores/options/$CORE.txt"
