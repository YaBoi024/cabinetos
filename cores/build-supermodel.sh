#!/usr/bin/env bash
#
# Builds Supermodel, the Sega Model 3 emulator, for Linux x86-64 at an exact
# commit: the program, the one library the image lacks, and the files it
# reads, laid out as an install prefix, ready to go into the image at
# /usr/lib/cabinetos/supermodel.
#
# WHY Supermodel, AND WHY FROM SOURCE: docs/decisions/2026-10-28-model-2-and-model-3.md.
# MAME 0.289 lists 23 Model 3 games and runs none; at this pin 16 of the 17
# in the reference library reached gameplay on the A9 at 4K (2026-10-29).
# Its Flathub build moves on its own; a pinned build keeps the same emulator
# for everyone. Not patched: a patch that looks needed is named to MMagTech
# first.
#
# HOW: Supermodel's own Makefiles/Makefile.UNIX, with SDL 2 (Fedora's
# sdl2-compat, on the image's SDL 3), SDL2_net, OpenGL, GLU and zlib from
# Fedora (cores/supermodel-builder/Containerfile); everything else it builds
# from its own Src/Pkgs at the pin. SDL2_net is not in the image, so its
# Fedora 44 library goes in lib/ beside the program, which finds it by a run
# path of $ORIGIN/../lib: Supermodel stays in its own image layer.
#
# WHAT IT ASSERTS: the source is at the pin; every command-line flag and
# every setting the console writes is one this source reads, read from
# frontend/src/standalone_supermodel.cpp itself so the two cannot drift
# (which is why build-supermodel.yml's cache key covers that file); SIGTERM
# ends its loop and NVRAM is saved on the way out; every library it still
# asks the system for is in Fedora 44, and SDL2_net is taken from lib/.
# WHAT IT CANNOT: that a game runs. That is seen on the A9 for every move of
# the pin.
#
# Usage: cores/build-supermodel.sh
# Output: cores/build/supermodel/{bin/supermodel, lib/libSDL2_net-2.0.so.0,
#         share/supermodel/{Games.xml,Assets/}, VERSION}

set -euo pipefail

REPO=https://github.com/trzy/Supermodel.git
# v0.3a-20261025-git-1481545, 2026-10-25: the build run on the A9 on
# 2026-10-29. To move it: the update rule
# (docs/decisions/2026-10-23-when-an-emulator-pin-moves.md), a run of the
# Model 3 games on the A9, and ci/list-arcade-changes.sh's list on the PR.
COMMIT=14815455dc28fca2c8662fb1ed99d447ecf8f364
VERSION=0.3a-20261025-git-1481545

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/supermodel"
BUILDER="${CABINETOS_SUPERMODEL_BUILDER:-cabinetos-supermodel-builder}"
OUT="${CABINETOS_SUPERMODEL_OUT:-$ROOT/cores/build/supermodel}"
CONSOLE="$ROOT/frontend/src/standalone_supermodel.cpp"

# --- the container ---------------------------------------------------------
if ! podman image exists "$BUILDER"; then
    echo "building $BUILDER"
    podman build -t "$BUILDER" -f "$ROOT/cores/supermodel-builder/Containerfile" \
        "$ROOT/cores/supermodel-builder"
fi

# --- the source ------------------------------------------------------------
mkdir -p "$SRC_ROOT"
if [ ! -d "$SRC/.git" ]; then
    git clone --filter=blob:none "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all
git -C "$SRC" checkout --quiet --force "$COMMIT"
# Its makefile builds in the source tree (obj/, bin/): a clean tree each time,
# so nothing a previous build left stands in for this one.
git -C "$SRC" clean -fdxq
# Asserted, never trusted. Same rule as every core in build-core.sh.
HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "Supermodel is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
echo "Supermodel @ $COMMIT"

# --- the console's flags and settings, in this source ------------------------
# Read from standalone_supermodel.cpp: every "-flag=" it passes, and every key
# in its settings tables, each a quoted name at the start of a { } row. A key
# Supermodel does not know is silently ignored (its own Supermodel.ini says
# so), so a renamed one would be: this is what notices.
MAIN="$SRC/Src/OSD/SDL/Main.cpp"
INPUTS="$SRC/Src/Inputs/Inputs.cpp"
[ -f "$CONSOLE" ] || { echo "no $CONSOLE to read the console's settings from" >&2; exit 1; }
mapfile -t flags < <(grep -oE '"-[a-z-]+=' "$CONSOLE" | tr -d '"=' | sort -u)
mapfile -t keys < <(grep -oE '^ *\{"[A-Za-z0-9]+",' "$CONSOLE" | grep -oE '[A-Za-z0-9]+' | sort -u)
(( ${#flags[@]} >= 1 && ${#keys[@]} >= 100 )) ||
    { echo "read ${#flags[@]} flags and ${#keys[@]} settings from $CONSOLE; its tables changed shape" >&2; exit 1; }
fail=0
for f in "${flags[@]}"; do
    grep -qF "{ \"$f\"," "$MAIN" || { echo "Supermodel reads no $f flag at the pin" >&2; fail=1; }
done
for k in "${keys[@]}"; do
    # A setting, by its name in Main.cpp's defaults; or an input, by its id,
    # from which Supermodel makes the key "Input<id>" (Src/Inputs/Input.cpp,
    # LoadFromConfig).
    grep -qE "Set(<[a-z:_ ]+>)?\(\"$k\"" "$MAIN" && continue
    [[ "$k" == Input* ]] && grep -qE "Add[A-Za-z0-9]*Input *\(\"${k#Input}\"" "$INPUTS" && continue
    echo "Supermodel reads no $k setting at the pin" >&2
    fail=1
done
# SIGTERM IS ITS QUIT, and it writes NVRAM only on the way out: SDL turns the
# signal into SDL_QUIT, its input poll then returns false and ends the main
# loop, and SaveNVRAM runs after it (measured on the A9, 2026-10-29). Asserted.
grep -q 'case SDL_QUIT:' "$SRC/Src/OSD/SDL/SDLInputSystem.cpp" ||
    { echo "Supermodel's input no longer ends on SDL_QUIT" >&2; fail=1; }
grep -q 'if (!Inputs->Poll(' "$MAIN" ||
    { echo "Supermodel's main loop no longer ends when its input poll fails" >&2; fail=1; }
grep -qE '^ *SaveNVRAM\(Emu\);' "$MAIN" ||
    { echo "Supermodel no longer saves NVRAM in Main.cpp" >&2; fail=1; }
# Its folders come from its working folder when ./Config is there
# (FileSystemPath.cpp), which is how the console gives it a home.
grep -q 'PathExists("Config")' "$SRC/Src/OSD/Unix/FileSystemPath.cpp" ||
    { echo "Supermodel no longer keeps its folders beside a Config in its working folder" >&2; fail=1; }
# ONE MACHINE LINKED TO ITSELF is how its net board runs alone
# (docs/decisions/2026-10-29-model-3-net-board-linked-to-itself.md).
grep -q 'only continue if Supermodel is linked to itself' "$SRC/Src/Model3/SimNetBoard.cpp" ||
    { echo "Supermodel's net board no longer runs linked to itself" >&2; fail=1; }
(( fail == 0 )) || exit 1
echo "the console's ${#flags[@]} flags and ${#keys[@]} settings are all read at the pin"

run_in_builder() {
    podman run --rm -v "$SRC":/src:Z -w /src "$BUILDER" bash -c "$1"
}

# Its own Unix makefile, as its release builds use it, with the version its
# releases stamp in (Src/Version.h falls back to "0.3a-WIP" without it) and a
# run path to lib/ beside bin/. LDOPT is its makefile's own strip flag, kept.
run_in_builder "make -f Makefiles/Makefile.UNIX -j\$(nproc) \
    SUPERMODEL_BUILD_FLAGS='-DSUPERMODEL_VERSION=\\\"$VERSION\\\"' \
    LDOPT='-s -Wl,-rpath,\\\$\$ORIGIN/../lib'"

# --- what goes into the image ----------------------------------------------
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/lib" "$OUT/share/supermodel"
install -m 0755 "$SRC/bin/supermodel" "$OUT/bin/supermodel"
podman run --rm -v "$OUT/lib":/out:Z "$BUILDER" cp -L /usr/lib64/libSDL2_net-2.0.so.0 /out/
install -m 0644 "$SRC/Config/Games.xml" "$OUT/share/supermodel/Games.xml"
# The crosshair pictures: it will not start without them, whatever the game.
cp -R "$SRC/Assets" "$OUT/share/supermodel/"
rm -f "$OUT/share/supermodel/Assets/DIR.txt"
printf '%s\n' "$COMMIT" > "$OUT/VERSION"

# ITS VERSION, ITS RUN PATH, AND EVERY LIBRARY IT STILL ASKS THE SYSTEM FOR
# RESOLVING in Fedora 44, the image's base, with SDL2_net from lib/ (the
# builder's own copy moved away first); the image's install step checks the
# same against the image itself. In the builder, which has binutils.
# Not grep -q on strings: it stops reading at the first match, strings dies
# on the closed pipe, and pipefail reports the version missing.
check=$(podman run --rm -v "$OUT":/out:Z "$BUILDER" bash -c "
    strings /out/bin/supermodel | grep -F '$VERSION' >/dev/null || echo 'NO VERSION'
    readelf -d /out/bin/supermodel | grep -qF '[\$ORIGIN/../lib]' || echo 'NO RUN PATH'
    mv /usr/lib64/libSDL2_net-2.0.so.0 /tmp/ && ldd /out/bin/supermodel")
if grep -q 'NO VERSION' <<<"$check"; then
    echo "the program does not carry version $VERSION" >&2
    exit 1
fi
if grep -q 'NO RUN PATH' <<<"$check"; then
    echo "the program has no run path to its lib/" >&2
    exit 1
fi
if grep -q 'not found' <<<"$check"; then
    echo "Supermodel needs libraries Fedora 44 does not have:" >&2
    grep 'not found' <<<"$check" >&2
    exit 1
fi
grep -qE 'libSDL2_net-2.0.so.0 => /out/(bin/\.\./)?lib/' <<<"$check" ||
    { echo "Supermodel does not take SDL2_net from its own lib/" >&2; exit 1; }
echo "wrote $OUT ($(du -sh "$OUT" | cut -f1); program $(du -h "$OUT/bin/supermodel" | cut -f1))"
