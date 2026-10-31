#!/usr/bin/env bash
#
# Put the console into the image: the frontend, the twenty-three libretro cores,
# and the system files that ship with one of them.
#
# ===========================================================================
#  THIS IS WHAT MAKES AN INSTALLED MACHINE A CONSOLE RATHER THAN A BLACK
#  SCREEN.
# ===========================================================================
#
# Before this existed the image carried the OS half and nothing else. Merge to
# main, the image rebuilt, bootc pulled it, and packages, system files and the
# session service all travelled — but the frontend and the cores did not,
# because they were never in it. `cabinetos-session` ran `sleep infinity`
# inside gamescope, so a freshly installed machine came up, took tty1, started
# a compositor, and drew nothing at all. Every emulator this console can run
# lived on one development VM and was compiled there by hand.
#
# WHERE EACH PART GOES, AND WHY THERE
#
#   /usr/bin/cabinetos-frontend        A program. Where programs go, and on the
#                                      PATH the session script uses.
#
#   /usr/lib/cabinetos/cores/          Architecture-specific shared objects, so
#                                      /usr/lib and not /usr/share. The
#                                      frontend falls back to this path when
#                                      there is no cores/build beside it — see
#                                      resolveCoresAndServer in cli.cpp — and
#                                      --core-dir still overrides both.
#
#   /usr/share/cabinetos/system/       PPSSPP's fonts, VFPU tables and per-game
#                                      compatibility list. DECIDED in
#                                      docs/PROJECT.md open question 18 and
#                                      unbuilt until now: storage::ensureTree
#                                      already symlinks whatever it finds here
#                                      into the console's system directory at
#                                      startup, leaving alone any name that is
#                                      already a real file — which is what
#                                      keeps a development machine working,
#                                      since there the same files sit in bios/
#                                      where the core build left them.
#
#                                      `/system` AND NOT /usr/share/cabinetos
#                                      ITSELF. That directory also holds a
#                                      DEVELOPMENT-IMAGE marker (until 2026-10-07) and two package
#                                      inventories, and an earlier version of
#                                      the link step put all three where a core
#                                      goes looking for its fonts.
#
# All three are under /usr, which on a bootc machine is replaced wholesale by
# every update. That is the entire point of doing this: it is what makes the
# frontend and the cores travel with the image the way the session service
# already does. Nothing here may go in /var — see
# system_files/usr/lib/tmpfiles.d/cabinetos.conf for what that trap looks like.

# THREE CALLS, ONE PER LAYER, 2026-09-23. The Containerfile runs this once
# for each of `cores`, `system` and `frontend`, in that order, so each lands in
# its own image layer and a console updating after a frontend change downloads
# the frontend and nothing else. See the Containerfile and open question 27.
#
# The checks that need all three present, the library sweep and the final
# list, run in the last call, `frontend`.

set -euo pipefail

source /ctx/lib.sh

PAYLOAD=/ctx/payload
PART="${1:-}"

if [[ ! -d "${PAYLOAD}" ]]; then
    log "ERROR: ${PAYLOAD} is missing — ci/stage-image-payload.sh was not run"
    exit 1
fi

install_cores() {
group_start "Installing the cores"

mkdir -p /usr/lib/cabinetos/cores
install -m 0644 "${PAYLOAD}"/cores/*.so /usr/lib/cabinetos/cores/

# PCSX2's own shared libraries, which the Bazzite base does not carry.
#
# A SEPARATE LINE BECAUSE THE GLOB ABOVE DOES NOT MATCH THEM. They are
# libryml.so.0.10.0 and libc4core.so.0.2.8 — `*.so` matches neither, so they
# would have been left behind in the payload while everything looked fine, and
# the emulator would have failed to dlopen at the moment somebody started a
# game. Found while wiring this up rather than on a television.
shopt -s nullglob
ps2libs=("${PAYLOAD}"/cores/*.so.*)
shopt -u nullglob
if [[ ${#ps2libs[@]} -gt 0 ]]; then
    install -m 0644 "${ps2libs[@]}" /usr/lib/cabinetos/cores/
    log "installed ${#ps2libs[@]} bundled libraries beside the emulators"
fi

# COUNTED BY NAME, NOT BY EXTENSION. This was `-name '*.so'`, which counted
# every shared object in the directory — correct while all of them were cores,
# and wrong the moment cabinetos-ps2.so arrived, because PlayStation 2 is a
# whole emulator rather than a libretro core. It would have read 22 and failed
# a build that was entirely correct.
cores=$(find /usr/lib/cabinetos/cores -name '*_libretro.so' | wc -l)
log "installed ${cores} cores into /usr/lib/cabinetos/cores ($(du -sh /usr/lib/cabinetos/cores | cut -f1))"

# The count is checked again here, having already been checked against
# cores/build-core.sh before the image build started. Not redundant: what that
# script proved is that the staging directory was complete, and what this
# proves is that the contents of it reached the image. A copy that silently
# does nothing leaves a green build, which is the exact failure the
# system_files overlay in build.sh was written to catch after it happened.
if [[ "${cores}" -ne 23 ]]; then
    log "ERROR: expected 23 cores in the image, found ${cores}"
    exit 1
fi

# The arcade emulators' catalogues: each set's files, discs and working
# status, which pick an arcade game's emulator from its zip. Read by
# arcadematch.cpp from beside the cores.
for cat in flycast_libretro.catalogue fbneo_libretro.catalogue mame2003_plus_libretro.catalogue; do
    if [[ ! -s "${PAYLOAD}/cores/${cat}" ]]; then
        log "ERROR: ${cat} is not in the payload"
        exit 1
    fi
    install -m 0644 "${PAYLOAD}/cores/${cat}" /usr/lib/cabinetos/cores/
    log "  ${cat}: $(grep -vc '^[#!]' "/usr/lib/cabinetos/cores/${cat}") sets"
done

# --- PlayStation 2 ---------------------------------------------------------
#
# Named individually, because each absence is silent in a different way and
# none of them stops the image building. The `frontend` call checks all of
# them again once every layer is down.
if [[ ! -f /usr/lib/cabinetos/cores/cabinetos-ps2.so ]]; then
    log "ERROR: cabinetos-ps2.so did not land — the image would lose PlayStation 2"
    exit 1
fi

# The emulator's own libraries, read from the manifest compile.sh wrote rather
# than from a list kept here — see ci/stage-image-payload.sh for why that list
# is computed and never written down.
if [[ -f "${PAYLOAD}/cores/cabinetos-ps2.bundled" ]]; then
    while read -r soname; do
        [[ -n "${soname}" ]] || continue
        if [[ ! -f "/usr/lib/cabinetos/cores/${soname}" ]]; then
            log "ERROR: PCSX2 expects ${soname} beside it and it did not land"
            exit 1
        fi
    done < "${PAYLOAD}/cores/cabinetos-ps2.bundled"
    install -m 0644 "${PAYLOAD}/cores/cabinetos-ps2.bundled" \
        /usr/lib/cabinetos/cores/cabinetos-ps2.bundled
fi

group_end
}

install_system() {
group_start "Installing the core system files"

# --- PPSSPP's system files -------------------------------------------------

mkdir -p /usr/share/cabinetos/system
cp -R "${PAYLOAD}/system/." /usr/share/cabinetos/system/
log "installed the core system files ($(du -sh /usr/share/cabinetos/system | cut -f1))"

# Named, not counted. Without compat.ini the core logs "Core system files
# missing, expect bugs" at a level nothing reads and then runs a game that
# renders no text — which is a working build, a green check, and a broken
# console.
if [[ ! -f /usr/share/cabinetos/system/PPSSPP/compat.ini ]]; then
    log "ERROR: PPSSPP's system files did not land — PSP would run with no fonts"
    exit 1
fi

# PCSX2 refuses to start without its resources. Not a warning, not a
# degraded picture: it does not boot.
if [[ ! -f /usr/share/cabinetos/system/pcsx2/resources/GameIndex.yaml ]]; then
    log "ERROR: PCSX2's resources did not land — the image would lose PlayStation 2"
    exit 1
fi
# And its game patches (#217): without the file PCSX2 says so on every start
# and no widescreen or no-interlacing patch exists.
if [[ ! -s /usr/share/cabinetos/system/pcsx2/resources/patches.zip ]]; then
    log "ERROR: PCSX2's patches.zip did not land — PS2 would lose its widescreen patches"
    exit 1
fi
log "installed PlayStation 2's resources ($(du -sh /usr/share/cabinetos/system/pcsx2 | cut -f1))"

group_end
}

install_frontend() {
group_start "Installing the frontend"

install -D -m 0755 "${PAYLOAD}/bin/cabinetos-frontend" /usr/bin/cabinetos-frontend
log "installed /usr/bin/cabinetos-frontend ($(du -h /usr/bin/cabinetos-frontend | cut -f1))"
# The Wii bridge (#200, wiibridge/), where the frontend looks for it
# (wiiremote.cpp). Its own program because it is GPL; the frontend only starts it.
install -D -m 0755 "${PAYLOAD}/bin/cabinetos-wii-bridge" /usr/libexec/cabinetos-wii-bridge
log "installed /usr/libexec/cabinetos-wii-bridge ($(du -h /usr/libexec/cabinetos-wii-bridge | cut -f1))"
# The controller list, beside the binary that reads it (players::loadMappings)
# and in the same small layer, so its weekly update ships one file.
install -D -m 0644 /ctx/frontend-data/gamecontrollerdb.txt /usr/share/cabinetos/gamecontrollerdb.txt
log "installed the controller list ($(grep -c 'platform:Linux' /usr/share/cabinetos/gamecontrollerdb.txt) Linux entries)"
# Which controllers each Wii game accepts, from GameTDB (tools/wii-controls.py):
# a Wii game plays on a pad only when it takes a Classic Controller or a
# GameCube pad (wii::padDevice, frontend/src/wii.cpp; the rule is
# catalog::coverageFor; docs/PROJECT.md open question 35).
install -D -m 0644 /ctx/frontend-data/wii-controls.txt /usr/share/cabinetos/wii-controls.txt
log "installed the Wii controller list ($(grep -vc '^#' /usr/share/cabinetos/wii-controls.txt) games)"
# And each Wii U game (tools/wiiu-controls.py): it plays on a pad only when it
# takes a Pro or a Classic Controller (wiiu.h; docs/PROJECT.md open question 36).
install -D -m 0644 /ctx/frontend-data/wiiu-controls.txt /usr/share/cabinetos/wiiu-controls.txt
log "installed the Wii U controller list ($(grep -vc '^#' /usr/share/cabinetos/wiiu-controls.txt) games)"
# The screen looks (#122): RetroArch's own GLSL shaders, unchanged, where
# screenfx::shaderDir looks for them. Read at run time, never compiled in.
# Checked by the preset each default names, so a list that lost one fails here
# rather than drawing a plain picture on every console.
mkdir -p /usr/share/cabinetos/shaders
cp -R /ctx/frontend-data/shaders/. /usr/share/cabinetos/shaders/
find /usr/share/cabinetos/shaders -type d -exec chmod 0755 {} +
find /usr/share/cabinetos/shaders -type f -exec chmod 0644 {} +
for preset in crt/crt-easymode.glslp handheld/lcd3x.glslp; do
    if [[ ! -s "/usr/share/cabinetos/shaders/${preset}" ]]; then
        log "ERROR: screen look ${preset} did not land — every game would draw plainly"
        exit 1
    fi
done
log "installed the screen looks ($(find /usr/share/cabinetos/shaders -name '*.glslp' | wc -l) presets, $(du -sh /usr/share/cabinetos/shaders | cut -f1))"
# The Vectrex games' screen sheets (#224), where vectrexsheets::folder looks:
# the 38 PNG files tools/fetch-vectrex-overlays.py put in the payload (build.yml
# runs it; a build by hand runs it into image_payload/vectrex first), and the
# table saying which game takes which, ours, from frontend/data. Checked by
# count and by every sheet the table names, so a payload that lost one fails
# here rather than leaving a game with its bare screen on every console.
if [[ ! -d "${PAYLOAD}/vectrex" ]]; then
    log "ERROR: ${PAYLOAD}/vectrex is missing; run tools/fetch-vectrex-overlays.py image_payload/vectrex"
    exit 1
fi
mkdir -p /usr/share/cabinetos/vectrex
install -m 0644 "${PAYLOAD}"/vectrex/*.png /usr/share/cabinetos/vectrex/
install -m 0644 /ctx/frontend-data/vectrex-overlays.json /usr/share/cabinetos/vectrex/vectrex-overlays.json
sheets=$(find /usr/share/cabinetos/vectrex -name '*.png' | wc -l)
if [[ "${sheets}" -ne 38 ]]; then
    log "ERROR: ${sheets} Vectrex sheets installed, expected 38"
    exit 1
fi
# Read whole first, so a table this cannot read is a count of 0 and fails,
# rather than a loop that checks nothing.
named="$(grep -o '"file": *"[^"]*"' /usr/share/cabinetos/vectrex/vectrex-overlays.json |
    sed 's/.*"\([^"]*\)"$/\1/')"
if [[ "$(wc -l <<<"${named}")" -ne 38 ]]; then
    log "ERROR: the Vectrex table does not name 38 sheets"
    exit 1
fi
while read -r sheet; do
    if [[ ! -s "/usr/share/cabinetos/vectrex/${sheet}.png" ]]; then
        log "ERROR: the Vectrex table names ${sheet}.png, which did not land"
        exit 1
    fi
done <<<"${named}"
log "installed the Vectrex sheets (${sheets} sheets, $(du -sh /usr/share/cabinetos/vectrex | cut -f1))"

group_end

# ---------------------------------------------------------------------------
# Every library, resolved against THIS image.
# ---------------------------------------------------------------------------
#
# The one check that is worth more than all the others here, because it is the
# only one that asks the question the console actually depends on: can the
# binary, and each of the twenty-three cores, find everything it links against in
# the image it is about to ship in?
#
# The frontend is compiled in a Fedora 44 container to match Bazzite 44, so the
# two SHOULD agree. They agree by construction and not by declaration, though:
# build_files/require-frontend-libs.sh names the libraries that are in the
# base incidentally, with nothing depending on them, and a Bazzite bump is free
# to drop any of them. This catches that, and it catches the same thing for the
# cores — whose dependencies nobody has ever written down. Flycast alone wants
# zlib, libzip, alsa and udev off the system.
#
# `ldd` on a missing dependency prints "=> not found" and still exits 0, which
# is why this reads the output rather than the status.
group_start "Resolving every library against the image"

unresolved=0
check_links() {
    local what="$1"
    local out
    out=$(ldd "${what}" 2>/dev/null | grep 'not found' || true)
    if [[ -n "${out}" ]]; then
        log "  UNRESOLVED  $(basename "${what}")"
        printf '%s\n' "${out}" | sed 's/^/      /'
        unresolved=1
    fi
}

check_links /usr/bin/cabinetos-frontend
check_links /usr/libexec/cabinetos-wii-bridge
for so in /usr/lib/cabinetos/cores/*.so; do
    check_links "${so}"
done

if [[ "${unresolved}" -ne 0 ]]; then
    log "ERROR: something in the image cannot find a library it links against."
    log "Install the package in a script build.sh runs and add it to"
    log "build_files/require-frontend-libs.sh with the reason. Do not delete"
    log "this check: a console that cannot start its own frontend is not a"
    log "console."
    exit 1
fi

log "every library resolves: the frontend and all $(find /usr/lib/cabinetos/cores -name '*_libretro.so' | wc -l) cores"
group_end

# ---------------------------------------------------------------------------
# The console, all of it, now that all three layers are down.
# ---------------------------------------------------------------------------
#
# These were in build.sh's sanity block until the payload moved into layers of
# its own. That block runs before any of it is installed now, so the list moved
# here, to the one place that runs after all of it.
group_start "The console is in the image"
missing=0
for expected in \
    /usr/bin/cabinetos-frontend \
    /usr/libexec/cabinetos-wii-bridge \
    /usr/lib/cabinetos/cores/cabinetos-ps2.so \
    /usr/share/cabinetos/system/PPSSPP/compat.ini \
    /usr/share/cabinetos/system/pcsx2/resources/GameIndex.yaml
do
    if [[ -e "${expected}" ]]; then
        log "  ok: ${expected}"
    else
        log "  MISSING: ${expected}"
        missing=1
    fi
done
group_end
if [[ "${missing}" -ne 0 ]]; then
    log "ERROR: the image is missing part of the console"
    exit 1
fi
}

case "${PART}" in
    cores)    install_cores ;;
    system)   install_system ;;
    frontend) install_frontend ;;
    *)
        log "ERROR: say which layer: cores, system or frontend (got '${PART}')"
        exit 1
        ;;
esac
