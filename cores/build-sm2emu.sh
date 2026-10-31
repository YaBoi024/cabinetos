#!/usr/bin/env bash
#
# Builds sm2-emu, the Sega Model 2 emulator, for Linux x86-64 at an exact
# commit: the program and the two things it reads, laid out as an install
# prefix, ready to go into the image at /usr/lib/cabinetos/sm2emu.
#
# WHY sm2-emu, AND WHY FROM SOURCE: docs/decisions/2026-10-28-model-2-and-model-3.md.
# MAME 0.289 runs 11 of the 36 Model 2 games it lists; sm2-emu reached gameplay
# in 23 of the 27 in the reference library on the A9 (2026-10-28). It has no
# Flathub build we would take, and a pinned build keeps the same emulator for
# everyone. NEVER PATCHED: its licence lets an unchanged build keep the name
# and makes a patched copy take another (its NOTICE, section 7(e)). A patch
# that looks needed is named to MMagTech first.
#
# HOW: sm2-emu's own CMake build, with its submodules at the commits this one
# names, so every library it builds for itself is pinned by this commit. SDL 3
# comes from Fedora, the image's own (cores/sm2emu-builder/Containerfile).
#
# WHAT IT ASSERTS: the source is at the pin; the Vulkan backend is built and
# OpenGL is not; libcurl is not linked, so the artwork downloader, its one way
# to the internet, is not in it; every command-line flag and settings key the
# console uses is one this source reads (standalone_sm2emu.cpp; the lists
# below must name everything it writes); every library it still asks the
# system for is in Fedora 44. WHAT IT CANNOT: that a game runs. That is seen on
# the A9 for every move of the pin.
#
# Usage: cores/build-sm2emu.sh
# Output: cores/build/sm2emu/{bin/sm2-emu, share/sm2-emu/{games.xml,lang/}, VERSION}

set -euo pipefail

REPO=https://github.com/dmanlfc/sm2-emu.git
# v0.9.56, 2026-10-26: the build run on the A9 on 2026-10-28 (23 of 27 Model 2
# games to gameplay). To move it: the update rule
# (docs/decisions/2026-10-23-when-an-emulator-pin-moves.md), a run of the
# Model 2 games on the A9, and ci/list-arcade-changes.sh's list on the PR.
COMMIT=4c290c558afaaa85956fa2364971c92cfde707cf
VERSION=0.9.56

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/sm2emu"
BUILD=build-cabinetos
BUILDER="${CABINETOS_SM2EMU_BUILDER:-cabinetos-sm2emu-builder}"
OUT="${CABINETOS_SM2EMU_OUT:-$ROOT/cores/build/sm2emu}"

# THE CONSOLE'S SIDE OF sm2-emu, checked against this source. Every flag
# standalone_sm2emu.cpp passes, as main.cpp's parser spells it, and every key
# it writes to sm2-emu.ini, as core/config.cpp's reader spells it. A key the
# reader does not know is only a warning in sm2-emu's log, so a renamed one
# would be silently ignored: this is what notices.
FLAGS=(--config --nvram --game)
KEYS=(graphics_backend gpu vsync throttle fullscreen show_fps show_notifications
      render_scale custom_textures screenshot_dir scrape_artwork lightgun
      link_enabled outputs_network outputs_windows language input_background rewind
      log_level pad_shortcuts pad1_device pad2_device)
HOTKEYS=(exit quit overlay fullscreen screenshot pause quick_save quick_load fps
         fast_forward rewind)
PAD_ROLES=(button1 button2 button3 button4 up down left right start coin gear_up gear_down)

# --- the container ---------------------------------------------------------
if ! podman image exists "$BUILDER"; then
    echo "building $BUILDER"
    podman build -t "$BUILDER" -f "$ROOT/cores/sm2emu-builder/Containerfile" \
        "$ROOT/cores/sm2emu-builder"
fi

# --- the source ------------------------------------------------------------
mkdir -p "$SRC_ROOT"
if [ ! -d "$SRC/.git" ]; then
    git clone --filter=blob:none "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all
git -C "$SRC" checkout --quiet --force "$COMMIT"
# Asserted, never trusted. Same rule as every core in build-core.sh.
HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "sm2-emu is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
# The submodules at the commits this one names: pugixml, miniz, Dear ImGui,
# VulkanMemoryAllocator, SheenBidi, Musashi, ymfm, and SDL and HarfBuzz, which
# are not built here since the system's are found first.
git -C "$SRC" submodule sync --quiet --recursive
git -C "$SRC" submodule update --init --recursive --force --quiet
echo "sm2-emu @ $COMMIT"

# --- the console's flags and keys, in this source ---------------------------
fail=0
for f in "${FLAGS[@]}"; do
    grep -qF "\"$f\"" "$SRC/src/main.cpp" || { echo "sm2-emu reads no $f flag at the pin" >&2; fail=1; }
done
for k in "${KEYS[@]}"; do
    grep -qF "key == \"$k\"" "$SRC/src/core/config.cpp" ||
        { echo "sm2-emu reads no $k key at the pin" >&2; fail=1; }
done
for k in "${HOTKEYS[@]}"; do
    grep -qF "\"$k\"" "$SRC/src/core/config.cpp" ||
        { echo "sm2-emu has no key_$k shortcut at the pin" >&2; fail=1; }
done
for k in "${PAD_ROLES[@]}"; do
    grep -qF "\"$k\"" "$SRC/src/core/config.cpp" ||
        { echo "sm2-emu has no pad<n>_button_$k at the pin" >&2; fail=1; }
done
# SIGTERM IS ITS QUIT, and it writes NVRAM only on the way out: SDL turns the
# signal into SDL_EVENT_QUIT, which ends the main loop, and save_nvram runs
# after it (measured on the A9, 2026-10-28). Both lines asserted.
grep -q 'case SDL_EVENT_QUIT:' "$SRC/src/main.cpp" ||
    { echo "sm2-emu's main loop no longer ends on SDL_EVENT_QUIT" >&2; fail=1; }
grep -q 'machine_iface->save_nvram();' "$SRC/src/main.cpp" ||
    { echo "sm2-emu no longer saves NVRAM in main.cpp" >&2; fail=1; }
grep -qE "^ *VERSION +$VERSION\$" "$SRC/CMakeLists.txt" ||
    { echo "sm2-emu's CMakeLists.txt does not say version $VERSION" >&2; fail=1; }
(( fail == 0 )) || exit 1

run_in_builder() {
    podman run --rm -v "$SRC":/src:Z -w /src \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0='*' \
        "$BUILDER" bash -c "$1"
}

# Its own release options (BUILDING.md: Release, link-time optimisation), the
# Vulkan backend on and OpenGL off (docs/decisions/2026-10-28-model-2-and-model-3.md;
# the console's rule, Vulkan first), no light guns read straight from evdev
# (#575 will decide guns), and libcurl refused even if something installs it.
CMAKE_ARGS=(
    # A clean configure every time, so nothing a previous run cached stands in
    # for what this one says.
    --fresh
    -S . -B "$BUILD" -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/usr
    -DSM2_LTO=ON
    -DSM2_BUILD_VULKAN=ON
    -DSM2_BUILD_OPENGL_DESKTOP=OFF
    -DSM2_BUILD_OPENGL_ES=OFF
    -DSM2_BUILD_TESTS=OFF
    -DSM2_ENABLE_VALIDATION=OFF
    -DSM2_EVDEV=OFF
    -DCMAKE_DISABLE_FIND_PACKAGE_CURL=ON
)

run_in_builder "cmake $(printf '%q ' "${CMAKE_ARGS[@]}") 2>&1 | tee $BUILD.configure.log"
grep -q 'Vulkan=ON' "$SRC/$BUILD.configure.log" ||
    { echo "sm2-emu configured without its Vulkan backend" >&2; exit 1; }
grep -q 'SDL3 *: system' "$SRC/$BUILD.configure.log" ||
    { echo "sm2-emu did not find Fedora's SDL 3 and would build its own" >&2; exit 1; }
run_in_builder "cmake --build $BUILD --parallel \$(nproc)"

# --- what goes into the image ----------------------------------------------
# Laid out as an install prefix, where sm2-emu looks from where it is:
# bin/sm2-emu, share/sm2-emu/games.xml (GameDatabase::locate) and the overlay's
# translations beside it. Its icons and desktop file are left out.
rm -rf "$OUT"
run_in_builder "rm -rf $BUILD.staged && DESTDIR=/src/$BUILD.staged cmake --install $BUILD"
mkdir -p "$OUT/bin" "$OUT/share"
install -m 0755 "$SRC/$BUILD.staged/usr/bin/sm2-emu" "$OUT/bin/sm2-emu"
cp -R "$SRC/$BUILD.staged/usr/share/sm2-emu" "$OUT/share/"
printf '%s\n' "$COMMIT" > "$OUT/VERSION"
[ -f "$OUT/share/sm2-emu/games.xml" ] || { echo "no games.xml in $OUT/share/sm2-emu" >&2; exit 1; }

# NO libcurl, AND EVERY LIBRARY IT STILL ASKS THE SYSTEM FOR RESOLVES in
# Fedora 44, the image's base. The image's own install step checks the same
# against the image itself.
libs=$(podman run --rm -v "$OUT":/out:Z "$BUILDER" ldd /out/bin/sm2-emu)
if grep -q 'libcurl' <<<"$libs"; then
    echo "sm2-emu links libcurl; its artwork downloader is in" >&2
    exit 1
fi
if grep -q 'not found' <<<"$libs"; then
    echo "sm2-emu needs libraries Fedora 44 does not have:" >&2
    grep 'not found' <<<"$libs" >&2
    exit 1
fi
echo "wrote $OUT ($(du -sh "$OUT" | cut -f1); program $(du -h "$OUT/bin/sm2-emu" | cut -f1))"
