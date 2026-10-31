#!/usr/bin/env bash
#
# Assemble everything the image has to carry that is not in this repository:
# the frontend and the Wii bridge, the 23 libretro cores and PCSX2, MAME
# 0.289, the system files that ship with emulators rather than coming off
# RomM, Cemu, sm2-emu and Sunshine.
#
# WHY THIS EXISTS AT ALL. Until 2026-09-19 the image contained none of it. The
# Containerfile copied build_files, system_files and two licence files, so a
# freshly installed machine booted into a gamescope session running
# `sleep infinity` — a black screen — and every game this console can play
# lived on one development VM and was compiled by hand. docs/PROJECT.md,
# Phase 5.
#
# None of these are built here. They are big, they are slow, and they come from
# somewhere that already builds them properly:
#
#   the frontend   .github/workflows/build-frontend.yml (and the Wii bridge)
#   the cores      .github/workflows/build-core.yml, each pinned to an exact
#                  commit and each asserting that commit back out of the
#                  finished .so
#   PCSX2          .github/workflows/build-pcsx2.yml (cabinetos-ps2.so, its
#                  libraries and resources)
#   MAME 0.289     .github/workflows/build-mame.yml (libretro's own build,
#                  checked against its pins by cores/fetch-mame.sh)
#   Cemu           .github/workflows/build-cemu.yml
#   sm2-emu        .github/workflows/build-sm2emu.yml (and its catalogue)
#   Supermodel     .github/workflows/build-supermodel.yml (and its catalogue)
#   Sunshine       .github/workflows/build-sunshine.yml
#
# So this only ever COLLECTS and CHECKS. It builds nothing, and it fails before
# the image build starts rather than after it.
#
# Usage:
#   ci/stage-image-payload.sh              from this tree's own builds
#                                          (frontend/build, cores/build,
#                                          cores/system) — the test VM, or a
#                                          Linux box doing the whole thing by
#                                          hand
#   ci/stage-image-payload.sh <dir>        from a merged CI artifact download,
#                                          which already has this exact shape
#
# Output, always, at image_payload/ in the repository root:
#
#   bin/cabinetos-frontend
#   bin/cabinetos-wii-bridge      the Wii Remotes' bridge (#200, wiibridge/)
#   cores/<core>_libretro.so      x23
#   mame/{mame_libretro.so, .catalogue, plugins/}   MAME 0.289, beside the
#                                 cores in the image, in a layer of its own
#   system/PPSSPP/...             PPSSPP's fonts and lookup tables
#   system/dolphin-emu/Sys/...    Dolphin's game settings and Wii files
#   cemu/{bin,share/Cemu}/...     Wii U's emulator, its game profiles and resources
#   sm2emu/{bin,share/sm2-emu}/..., sm2emu.catalogue   Model 2's emulator and
#                                 which sets it plays
#   supermodel/{bin,lib,share/supermodel}/..., supermodel.catalogue   Model 3's
#                                 emulator, SDL2_net, and which sets it plays
#   sunshine/Sunshine-*.rpm       Remote Play's streaming host, with our patch (#304)
#
# This starts image_payload/ afresh. Two more folders go in after it, from
# build.yml's later steps (by hand, run them after this): downloads/, the
# pinned outside files (ci/fetch-pinned.sh), and vectrex/, the Vectrex
# screen sheets (tools/fetch-vectrex-overlays.py image_payload/vectrex).
#
# image_payload/ is gitignored, which also keeps `git status -s` clean — the
# Justfile reads that to decide whether to stamp the image with a revision, so
# an untracked staging directory would silently cost the build its labels and
# its git-sha tags.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/image_payload"

if [ $# -ge 1 ]; then
    SRC="$1"
    FRONTEND="$SRC/bin/cabinetos-frontend"
    BRIDGE="$SRC/bin/cabinetos-wii-bridge"
    CORES="$SRC/cores"
    SYSTEM="$SRC/system"
    CEMUSRC="$SRC/cemu"
    SM2SRC="$SRC/sm2emu"
    SUPERMODELSRC="$SRC/supermodel"
    SUNSHINESRC="$SRC/sunshine"
    MAMESRC="$SRC/mame"
else
    FRONTEND="$ROOT/frontend/build/cabinetos-frontend"
    BRIDGE="$ROOT/wiibridge/cabinetos-wii-bridge"
    CORES="$ROOT/cores/build"
    SYSTEM="$ROOT/cores/system"
    CEMUSRC="$ROOT/cores/build/cemu"
    SM2SRC="$ROOT/cores/build/sm2emu"
    SUPERMODELSRC="$ROOT/cores/build/supermodel"
    SUNSHINESRC="$ROOT/cores/build/sunshine"
    MAMESRC="$ROOT/cores/build/mame"
fi

# The cores this console is supposed to have, read off the script that builds
# them rather than written out again here. cores/build-core.sh's case arms are
# the list, and a name that is not one of them is the error it already prints.
mapfile -t CORE_NAMES < <(grep -oE '^[a-z0-9_]+\)' "$ROOT/cores/build-core.sh" | tr -d ')')

# Cross-check on the line above, not a second copy of the list. If somebody
# reshapes that case statement, this says so instead of quietly shipping an
# image with nineteen emulators in it.
# 23 since the 3DS was pinned (docs/PROJECT.md question 50); 22 from
# 2026-09-21, when GameCube was. This number is not decoration:
# it catches a core silently vanishing from build-core.sh's case arms, which is
# where the list is READ from rather than repeated.
EXPECTED=23
if [ "${#CORE_NAMES[@]}" -ne "$EXPECTED" ]; then
    echo "expected $EXPECTED cores in cores/build-core.sh, found ${#CORE_NAMES[@]}" >&2
    printf '  %s\n' "${CORE_NAMES[@]}" >&2
    echo "If a core was genuinely added or removed, change EXPECTED here and say why." >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/cores" "$OUT/system"

# --- The frontend ----------------------------------------------------------

[ -f "$FRONTEND" ] || { echo "no frontend binary at $FRONTEND" >&2; exit 1; }
install -m 0755 "$FRONTEND" "$OUT/bin/cabinetos-frontend"
# The Wii bridge, built beside it (build-frontend.yml). Without it a real Wii
# Remote still pairs, but no game gets a Nunchuk and Cemu sees no Remote.
[ -f "$BRIDGE" ] || { echo "no Wii bridge at $BRIDGE" >&2; exit 1; }
install -m 0755 "$BRIDGE" "$OUT/bin/cabinetos-wii-bridge"

# It has to be an x86-64 executable this machine's kernel will run, not a
# script, an empty file, a build for another processor, or — the one that has
# actually happened in this project — an artifact that downloaded as a
# directory. Without `file` nothing is checked, so it is required.
command -v file >/dev/null 2>&1 || { echo "file is needed to check the binaries" >&2; exit 1; }
for bin in cabinetos-frontend cabinetos-wii-bridge; do
    case "$(file -b "$OUT/bin/$bin")" in
        ELF\ 64-bit\ LSB\ *executable,\ x86-64,*|ELF\ 64-bit\ LSB\ shared\ object,\ x86-64,*) ;;
        *) echo "$bin is not an x86-64 ELF: $(file -b "$OUT/bin/$bin")" >&2
           exit 1 ;;
    esac
done

# --- The cores -------------------------------------------------------------
#
# Named after the core as the MANIFEST knows it, not as upstream's makefile
# names its output — beetle_ngp builds mednafen_ngp_libretro.so. The line
# below is the same rule as cores/build-core.sh, cores/check-options.sh,
# cores/arcade-sets.sh and catalog::coreFileName, and all five must agree:
# the frontend looks the file
# up by this name, and when it does not find one it reports the platform as
# "the core for this system is not built on this console yet". A wrong name
# here is twenty-three working emulators the console says it does not have.
missing=()
for core in "${CORE_NAMES[@]}"; do
    so="${core%_libretro}_libretro.so"
    if [ -f "$CORES/$so" ]; then
        install -m 0644 "$CORES/$so" "$OUT/cores/$so"
    else
        missing+=("$core ($so)")
    fi
done

if [ "${#missing[@]}" -ne 0 ]; then
    echo "these cores are not in $CORES:" >&2
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

# The arcade emulators' catalogues, from which each arcade game's emulator is
# picked by its zip (frontend/src/arcadematch.h). Without one, that emulator
# plays nothing.
for cat in flycast_libretro.catalogue fbneo_libretro.catalogue mame2003_plus_libretro.catalogue; do
    [ -s "$CORES/$cat" ] || {
        echo "$cat is not in $CORES; cores/arcade-sets.sh makes it" >&2
        exit 1
    }
    install -m 0644 "$CORES/$cat" "$OUT/cores/$cat"
done

# Anything in the source directory that is NOT one of the twenty-three. Not an
# error — a stale .so from a rename would be — but it does not go in the image,
# and saying so beats it vanishing silently.
for f in "$CORES"/*.so; do
    [ -e "$f" ] || continue
    # cabinetos-ps2.so is not in the core list because it is not a core. It is
    # installed by the PlayStation 2 section below.
    [ "$(basename "$f")" = "cabinetos-ps2.so" ] && continue
    [ -f "$OUT/cores/$(basename "$f")" ] || echo "not shipped, not in the core list: $(basename "$f")"
done

# --- The one core that brings its own system files -------------------------
#
# PPSSPP's are fonts, VFPU lookup tables and a per-game compatibility list.
# They are not a console's firmware and they do not come from RomM; they ship
# with the emulator, and retro_init warns "Core system files missing, expect
# bugs" without them — in a log line nobody reads, while the game runs and
# renders no text. So their absence is a build failure here.
if [ -d "$SYSTEM" ] && [ -n "$(ls -A "$SYSTEM" 2>/dev/null)" ]; then
    cp -R "$SYSTEM/." "$OUT/system/"
fi
[ -f "$OUT/system/PPSSPP/compat.ini" ] || {
    echo "PPSSPP's system files are not in $SYSTEM — PSP would run with no fonts" >&2
    exit 1
}
# DOLPHIN'S `Sys`, the same shape of failure and a quieter one: GameCube and
# Wii boot without it and lose Dolphin's own per-game settings, which looks
# like a game with a rendering bug rather than a missing file. See the dolphin
# case arm in cores/build-core.sh.
[ -d "$OUT/system/dolphin-emu/Sys/GameSettings" ] || {
    echo "Dolphin's Sys folder is not in $SYSTEM - GameCube and Wii would lose their game settings" >&2
    exit 1
}

# --- PlayStation 2, which is not a libretro core ---------------------------
#
# PCSX2 is a whole emulator with CabinetOS's own host layer compiled into it,
# not a core that implements retro_run, so none of the machinery above applies:
# it is not in build-core.sh's case arms, its file is not <name>_libretro.so,
# and it carries two shared libraries of its own.
#
# IT IS REQUIRED, NOT OPTIONAL, AND THAT IS THE POINT. A missing core does not
# break this console — catalog.cpp reports the platform as "not built on this
# console yet" and the library carries on — so an image that quietly lost
# PlayStation 2 would look completely normal and be missing 71 games. The whole
# reason to check here is that nothing downstream will.
#
# Built by cores/build-pcsx2.sh, whose output IS this payload. Locally that
# lands in cores/build/pcsx2; from a CI artifact it arrives already merged into
# cores/ and system/.
PS2SRC="$CORES/pcsx2"
[ -d "$PS2SRC" ] || PS2SRC="$CORES"

[ -f "$PS2SRC/cabinetos-ps2.so" ] || {
    echo "cabinetos-ps2.so is not in $PS2SRC — the image would lose PlayStation 2" >&2
    echo "Build it with cores/build-pcsx2.sh." >&2
    exit 1
}
install -m 0644 "$PS2SRC/cabinetos-ps2.so" "$OUT/cores/cabinetos-ps2.so"

# The libraries the Bazzite base does not have. THE LIST IS READ, NOT WRITTEN
# DOWN: frontend/ps2/compile.sh works out which libraries the console is
# missing and records what it acted on, so this cannot drift from what the
# emulator was actually linked to expect beside it.
#
# It found an empty manifest on 2026-09-21 — compile.sh skipped RECORDING a
# library it had skipped COPYING because it was already there — so an empty one
# is a build failure and not an absence.
[ -s "$PS2SRC/cabinetos-ps2.bundled" ] || {
    echo "$PS2SRC/cabinetos-ps2.bundled is empty or absent" >&2
    echo "PCSX2 carries libraries the image lacks; without them it cannot dlopen." >&2
    exit 1
}
while read -r soname; do
    [ -n "$soname" ] || continue
    [ -f "$PS2SRC/$soname" ] || {
        echo "the manifest names $soname and it is not in $PS2SRC" >&2
        exit 1
    }
    install -m 0644 "$PS2SRC/$soname" "$OUT/cores/$soname"
done < "$PS2SRC/cabinetos-ps2.bundled"
install -m 0644 "$PS2SRC/cabinetos-ps2.bundled" "$OUT/cores/cabinetos-ps2.bundled"

# PCSX2 REFUSES TO START WITHOUT ITS RESOURCES — its game database, its fonts
# and its GS shaders. Not a warning, not a degraded picture: it does not boot.
# The frontend looks for them at imageAssetsDir() + "/pcsx2/resources", which
# is /usr/share/cabinetos/system/pcsx2/resources in the image.
if [ -d "$PS2SRC/resources" ]; then
    mkdir -p "$OUT/system/pcsx2"
    rm -rf "$OUT/system/pcsx2/resources"
    cp -R "$PS2SRC/resources" "$OUT/system/pcsx2/resources"
fi
[ -f "$OUT/system/pcsx2/resources/GameIndex.yaml" ] || {
    echo "PCSX2's resources are not staged — it would refuse to start" >&2
    echo "Expected $OUT/system/pcsx2/resources/GameIndex.yaml" >&2
    exit 1
}
# And its patches, which install-frontend.sh requires: missing, the image
# build would fail eight minutes in rather than here (#487, SCRIPTS-15).
[ -s "$OUT/system/pcsx2/resources/patches.zip" ] || {
    echo "PCSX2's patches.zip is not staged" >&2
    exit 1
}

# NOT SHIPPED: cabinet-ps2-probe. It is the headless harness for measuring
# without a television, it is 28 MB, and nothing on a console runs it.

# --- MAME 0.289 -------------------------------------------------------------
# libretro's own build of the core, its catalogue and its plugins, from
# cores/fetch-mame.sh (cores/fetch-mame.sh cores/build locally). REQUIRED, for
# PlayStation 2's reason: without it the games only it plays read as not
# installed. build_files/install-mame.sh puts it beside the cores.
for f in mame_libretro.so mame_libretro.catalogue plugins/boot.lua plugins/json/init.lua \
    plugins/hiscore/init.lua VERSION; do
    [ -s "$MAMESRC/$f" ] || {
        echo "MAME 0.289's $f is not in $MAMESRC; cores/fetch-mame.sh makes it" >&2
        exit 1
    }
done
case "$(file -b "$MAMESRC/mame_libretro.so")" in
    ELF\ 64-bit\ LSB\ shared\ object,\ x86-64,*) ;;
    *) echo "mame_libretro.so is not an x86-64 shared object" >&2; exit 1 ;;
esac
rm -rf "$OUT/mame"
cp -R "$MAMESRC" "$OUT/mame"

# --- Wii U ----------------------------------------------------------------
# Cemu, built by cores/build-cemu.sh at its pin: the program and the two
# folders it reads. REQUIRED, for PlayStation 2's reason: without it Wii U
# reads as "not installed" and nothing else would say so.
# build_files/install-cemu.sh puts it at /usr/lib/cabinetos/cemu.
for f in bin/Cemu share/Cemu/resources share/Cemu/gameProfiles/default VERSION; do
    [ -e "$CEMUSRC/$f" ] || {
        echo "Cemu's $f is not in $CEMUSRC — the image would lose Wii U" >&2
        echo "Build it with cores/build-cemu.sh." >&2
        exit 1
    }
done
rm -rf "$OUT/cemu"
cp -R "$CEMUSRC" "$OUT/cemu"

# --- Sega Model 2 -----------------------------------------------------------
# sm2-emu, built by cores/build-sm2emu.sh at its pin, and its catalogue
# (cores/arcade-sets.sh sm2emu, run into the same folder). REQUIRED, for
# PlayStation 2's reason: without it every Model 2 game falls to MAME 0.289,
# which runs few, or greys. build_files/install-sm2emu.sh puts it at
# /usr/lib/cabinetos/sm2emu and the catalogue beside the cores.
for f in bin/sm2-emu share/sm2-emu/games.xml VERSION sm2emu.catalogue; do
    [ -s "$SM2SRC/$f" ] || {
        echo "sm2-emu's $f is not in $SM2SRC, so the image would lose Model 2" >&2
        echo "Build it with cores/build-sm2emu.sh, then cores/arcade-sets.sh sm2emu $SM2SRC." >&2
        exit 1
    }
done
rm -rf "$OUT/sm2emu"
cp -R "$SM2SRC" "$OUT/sm2emu"

# --- Sega Model 3 -----------------------------------------------------------
# Supermodel, built by cores/build-supermodel.sh at its pin, and its catalogue
# (cores/arcade-sets.sh supermodel, run into the same folder). REQUIRED, for
# PlayStation 2's reason: without it every Model 3 game greys, since MAME
# 0.289 runs none. build_files/install-supermodel.sh puts it at
# /usr/lib/cabinetos/supermodel and the catalogue beside the cores.
for f in bin/supermodel lib/libSDL2_net-2.0.so.0 share/supermodel/Games.xml VERSION \
         supermodel.catalogue; do
    [ -s "$SUPERMODELSRC/$f" ] || {
        echo "Supermodel's $f is not in $SUPERMODELSRC, so the image would lose Model 3" >&2
        echo "Build it with cores/build-supermodel.sh, then cores/arcade-sets.sh supermodel $SUPERMODELSRC." >&2
        exit 1
    }
done
rm -rf "$OUT/supermodel"
cp -R "$SUPERMODELSRC" "$OUT/supermodel"

# --- Remote Play ------------------------------------------------------------
# Sunshine, built by cores/build-sunshine.sh at its pin with our patch (#304).
# REQUIRED: build_files/install-sunshine.sh installs this package and nothing
# else, so an image without it would have no Remote Play.
shopt -s nullglob
sunshine_rpms=("$SUNSHINESRC"/Sunshine-*.x86_64.rpm)
shopt -u nullglob
if [ "${#sunshine_rpms[@]}" -ne 1 ] || [ ! -s "$SUNSHINESRC/VERSION" ]; then
    echo "Sunshine's package is not in $SUNSHINESRC (one .rpm and VERSION)" >&2
    echo "— the image would lose Remote Play. Build it with cores/build-sunshine.sh." >&2
    exit 1
fi
rm -rf "$OUT/sunshine"
cp -R "$SUNSHINESRC" "$OUT/sunshine"

# --- What went in ----------------------------------------------------------

echo "staged $OUT"
printf '  frontend  %s\n' "$(du -h "$OUT/bin/cabinetos-frontend" | cut -f1)"
# COUNTED BY NAME. `*.so` counted PlayStation 2 as a twenty-second core, which
# it is not — it is a whole emulator with our host layer compiled into it.
printf '  cores     %s in %s\n' "$(find "$OUT/cores" -name '*_libretro.so' | wc -l | tr -d ' ')" \
       "$(du -sh "$OUT/cores" | cut -f1)"
printf '  ps2       %s emulator, %s libraries, %s resources\n' \
       "$(du -h "$OUT/cores/cabinetos-ps2.so" | cut -f1)" \
       "$(wc -l < "$OUT/cores/cabinetos-ps2.bundled" | tr -d ' ')" \
       "$(du -sh "$OUT/system/pcsx2/resources" | cut -f1)"
printf '  mame      0.289 (%s), %s\n' "$(cut -c1-8 "$OUT/mame/VERSION")" "$(du -sh "$OUT/mame" | cut -f1)"
printf '  wii u     Cemu %s, %s\n' "$(cut -c1-10 "$OUT/cemu/VERSION")" "$(du -sh "$OUT/cemu" | cut -f1)"
printf '  model 2   sm2-emu %s, %s\n' "$(cut -c1-10 "$OUT/sm2emu/VERSION")" "$(du -sh "$OUT/sm2emu" | cut -f1)"
printf '  model 3   Supermodel %s, %s\n' "$(cut -c1-10 "$OUT/supermodel/VERSION")" "$(du -sh "$OUT/supermodel" | cut -f1)"
printf '  sunshine  %s, %s\n' "$(head -1 "$OUT/sunshine/VERSION" | cut -d' ' -f1)" "$(du -sh "$OUT/sunshine" | cut -f1)"
printf '  system    %s, %s entries\n' "$(du -sh "$OUT/system" | cut -f1)" \
       "$(find "$OUT/system" -mindepth 2 -maxdepth 2 | wc -l | tr -d ' ')"
printf '  total     %s\n' "$(du -sh "$OUT" | cut -f1)"
