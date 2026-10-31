#!/usr/bin/env bash
#
# MAME 0.289 for the image: libretro's own build of its core, the three
# plugins its high scores need, and its catalogue, ready to go into the image
# beside the cores (build_files/install-mame.sh).
#
# THE ONE CORE NOT BUILT FROM SOURCE (docs/decisions/
# 2026-10-28-prebuilt-mame-core.md): libretro's build server made it from
# libretro/mame at MAME_COMMIT, our own build of it would take hours, and we
# change nothing in it. It is never patched; a change we needed would mean
# building it ourselves. cores/build-core.sh builds every other core.
#
# EVERY PIN IS HERE, and each is checked:
#   MAME_URL, MAME_SHA256   the core's zip, an asset of a release of this
#                           repository, kept there because libretro's
#                           "latest" moves on
#   MAME_SO_SHA256          the core inside it
#   MAME_COMMIT             the libretro/mame commit it reports, "0.289
#                           (5f5a0774)": the plugins come from here, and what
#                           the console leans on in its source is read here
#   LISTXML_URL, LISTXML_SHA256
#                           mamedev's -listxml of 0.289, the release asset its
#                           SHA256SUMS names, which cores/arcade-sets.sh reads
#                           for the catalogue
# To move it: a new zip in a release, its two checksums and the commit it
# reports, that version's -listxml, a run on the A9, and the questions in
# docs/decisions/2026-10-23-when-an-emulator-pin-moves.md asked first: every
# MAME update is taken to break saves and states
# (docs/decisions/2026-10-28-emulator-updates-and-saves.md).
#
# WHAT IT ASSERTS, beside the checksums: the core loads and reports
# MAME_COMMIT (tools/core-info.c); its options are cores/options/
# mame_libretro.txt (cores/check-options.sh); and, in its source at the
# commit, what the console leans on (docs/ARCHITECTURE.md, "What leans on
# other projects"): where it keeps its folders, where the high-score plugin
# writes, its joypad's buttons, that only Tab opens its menu, and that it
# writes its saves in retro_deinit.
#
# A FOLDER OF DOWNLOADS (the first argument after the output) keeps the zips
# between runs; CI keeps it in its cache. A zip already there with its
# checksum is not fetched again, so a copy put there by hand works too.
#
# Usage: cores/fetch-mame.sh <out dir> [downloads dir]
#   writes <out>/mame/{mame_libretro.so, mame_libretro.catalogue, plugins/,
#   VERSION}, in $CABINETOS_BUILDER (cabinetos-builder) for the checks

set -euo pipefail

MAME_URL=https://github.com/MMagTech/cabinetos/releases/download/prebuilt-mame-0.289/mame_libretro.so.zip
MAME_SHA256=caf8ed4f3d400b1637112947df53c80ea6e4cb67132baa5443f4bb97eee1ceb5
MAME_SO_SHA256=cc890599a17f7af71aa85c3ecf27dd7d5f1e3dd5b4fff7c62aff52a6b9afcd6a
MAME_COMMIT=5f5a07748ce5c239c4118463ce8e6473743d4859
LISTXML_URL=https://github.com/mamedev/mame/releases/download/mame0289/mame0289lx.zip
LISTXML_SHA256=d5d8f40223aef8afddbb846c003402d4e348d07d3a867e6686267895ddbd2817

OUT="${1:?usage: cores/fetch-mame.sh <out dir> [downloads dir]}"
# Absolute: podman takes a relative path in -v for a volume's name.
mkdir -p "${OUT}"
OUT="$(cd "${OUT}" && pwd)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDER="${CABINETOS_BUILDER:-cabinetos-builder}"
tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT
DL="${2:-${tmp}/downloads}"
mkdir -p "${DL}"
DEST="${OUT}/mame"
rm -rf "${DEST}"
mkdir -p "${DEST}"

# <url> <sha256> -> <downloads>/<file name>, fetched unless already there.
fetch() {
    local url="$1" sha="$2" file
    file="${DL}/$(basename "${url}")"
    if [[ -f "${file}" && "$(sha256sum "${file}" | cut -d' ' -f1)" == "${sha}" ]]; then
        echo "$(basename "${file}"): from the downloads folder" >&2
    else
        curl -fsSL --retry 3 -o "${file}.part" "${url}"
        local got
        got=$(sha256sum "${file}.part" | cut -d' ' -f1)
        if [[ "${got}" != "${sha}" ]]; then
            echo "${url} has sha256 ${got}, the pin says ${sha}" >&2
            rm -f "${file}.part"
            exit 1
        fi
        mv "${file}.part" "${file}"
    fi
    echo "${file}"
}

# --- the core --------------------------------------------------------------
zip=$(fetch "${MAME_URL}" "${MAME_SHA256}")
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extract("mame_libretro.so", sys.argv[2])' \
    "${zip}" "${DEST}"
got=$(sha256sum "${DEST}/mame_libretro.so" | cut -d' ' -f1)
if [[ "${got}" != "${MAME_SO_SHA256}" ]]; then
    echo "the core in the zip has sha256 ${got}, the pin says ${MAME_SO_SHA256}" >&2
    exit 1
fi
chmod 0644 "${DEST}/mame_libretro.so"
echo "core        mame_libretro.so ($(du -h "${DEST}/mame_libretro.so" | cut -f1)), sha256 as pinned"

# --- its source at the commit: the plugins, and what the console leans on ---
src="${tmp}/src"
raw() {
    mkdir -p "${src}/$(dirname "$1")"
    curl -fsSL --retry 3 -o "${src}/$1" \
        "https://raw.githubusercontent.com/libretro/mame/${MAME_COMMIT}/$1"
}
PLUGINS=(plugins/boot.lua
    plugins/json/init.lua plugins/json/plugin.json plugins/json/LICENSE
    plugins/hiscore/init.lua plugins/hiscore/plugin.json plugins/hiscore/hiscore.dat)
for f in "${PLUGINS[@]}" \
    src/osd/libretro/libretro-internal/retro_init.cpp \
    src/osd/libretro/libretro-internal/libretro.cpp \
    src/osd/modules/input/input_retro.cpp; do
    raw "${f}"
done

# Its folders (Set_Path_Option): cfg, diff, nvram, input, states and snaps in
# the save folder, the rest, its home among them, in the system folder
# (frontend/src/mamehome.h).
init="${src}/src/osd/libretro/libretro-internal/retro_init.cpp"
dirs=$(sed -n '/^static const char \*dir_name\[NB_OPTPATH\] =/,/^};/p' "${init}" | grep -o '"[a-z]*"' | tr -d '"' | tr '\n' ' ')
types=$(sed -n '/^int opt_type\[NB_OPTPATH\] =/,/^};/p' "${init}" | grep -oE '^ *[01](,[01])*' | tr -d ' \n')
if [[ "${dirs}" != "cfg diff nvram input states snaps artwork cheat hash ini plugins samples crosshair  " ||
    "${types}" != "0,0,0,0,0,01,1,1,1,1,1,1,1" ]]; then
    echo "mame: its folders are no longer split as mamehome.h says: '${dirs}' / '${types}'" >&2
    exit 1
fi
grep -qF "return manager.machine.options.entries.homepath:value():match('([^;]+)') .. '/hiscore'" \
    "${src}/plugins/hiscore/init.lua" || {
    echo "mame: the high-score plugin no longer writes to its homepath's hiscore; check mamehome.h" >&2
    exit 1
}
# Its joypad (fightstick.cpp, kMame): buttons 1 to 6 on B A Y X L R with its
# per-game profiles off, 7 and 8 on L3 and R3, and no button that opens its
# menu, which only Tab does (arcadesettings.h).
input="${src}/src/osd/modules/input/input_retro.cpp"
order=$(sed -n '/^int button_mapping\[\] =/,/^};/p' "${input}" | grep -o 'RETROPAD_[A-Z0-9]*' | tr '\n' ' ')
if [[ "${order}" != "RETROPAD_B RETROPAD_A RETROPAD_Y RETROPAD_X RETROPAD_L RETROPAD_R " ]] ||
    ! grep -qF 'add_button_assignment(assignments, IPT_BUTTON7, { switch_ids[SWITCH_L3] });' "${input}" ||
    ! grep -qF 'add_button_assignment(assignments, IPT_BUTTON8, { switch_ids[SWITCH_R3] });' "${input}" ||
    grep -q 'IPT_UI_MENU' "${input}" ||
    ! grep -qF '{"TAB",       RETROK_TAB,          ITEM_ID_TAB},' "${input}"; then
    echo "mame: its joypad's buttons or its menu key moved; check fightstick.cpp and arcadesettings.h" >&2
    exit 1
fi
# It hears keys through its keyboard callback, and writes its saves in
# retro_deinit (play.cpp, finishExit).
lr="${src}/src/osd/libretro/libretro-internal/libretro.cpp"
if ! grep -qF 'environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard_callback);' "${lr}" ||
    ! sed -n '/^void retro_deinit(void)/,/^}/p' "${lr}" | grep -q 'retro_finish();'; then
    echo "mame: it no longer takes keys by its callback, or no longer finishes in retro_deinit" >&2
    exit 1
fi
echo "checked     its folders, its high scores' folder, its joypad and menu key, its deinit"

mkdir -p "${DEST}/plugins"
cp -R "${src}/plugins/." "${DEST}/plugins/"
[[ -s "${DEST}/plugins/hiscore/hiscore.dat" ]] || { echo "mame: no hiscore.dat" >&2; exit 1; }
echo "plugins     $(find "${DEST}/plugins" -type f | wc -l) files, $(du -sh "${DEST}/plugins" | cut -f1)"

# --- in the builder: it loads, reports its commit, and its options ---------
podman run --rm -v "${ROOT}":/repo:Z -v "${DEST}":/out:Z -w /repo "${BUILDER}" \
    sh -c 'gcc -O2 -Wall -Wextra -o /tmp/core-info tools/core-info.c -ldl &&
           exec /tmp/core-info /out/mame_libretro.so "$1"' _ "${MAME_COMMIT}"
CABINETOS_CORE_OUT="${DEST}" CABINETOS_BUILDER="${BUILDER}" \
    "${ROOT}/cores/check-options.sh" mame_libretro

# --- its catalogue ---------------------------------------------------------
CABINETOS_LISTXML="$(fetch "${LISTXML_URL}" "${LISTXML_SHA256}")" \
    "${ROOT}/cores/arcade-sets.sh" mame_libretro "${DEST}"

printf '%s\n' "${MAME_COMMIT}" > "${DEST}/VERSION"
echo "wrote       ${DEST} ($(du -sh "${DEST}" | cut -f1))"
