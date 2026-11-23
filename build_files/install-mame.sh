#!/usr/bin/env bash
#
# Put MAME 0.289 into the image, beside the cores: libretro's own build of
# its core, its catalogue and the plugins its high scores need, all from the
# payload, where cores/fetch-mame.sh put them after checking every pin
# (docs/decisions/2026-10-28-prebuilt-mame-core.md).
#
#   /usr/lib/cabinetos/cores/mame_libretro.so         the core
#   /usr/lib/cabinetos/cores/mame_libretro.catalogue  which sets it plays
#                                                     (frontend/src/arcadematch.h)
#   /usr/lib/cabinetos/cores/mame/plugins/            boot.lua, json and
#                                                     hiscore, linked into its
#                                                     home at each game
#                                                     (frontend/src/mamehome.h)
#
# A LAYER OF ITS OWN (the Containerfile), since the core is 416 MB, four
# times all the other cores together, and moves as rarely as they do: a
# change to another core does not ship it again. Its libraries are checked
# with every core's in the `frontend` call of install-frontend.sh.
#
# Its licence is GPL-2.0-or-later; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly SRC=/ctx/payload/mame
readonly CORES=/usr/lib/cabinetos/cores

log "MAME 0.289 (libretro/mame $(cut -c1-12 "${SRC}/VERSION"))"
for f in mame_libretro.so mame_libretro.catalogue plugins/boot.lua plugins/json/init.lua \
    plugins/hiscore/init.lua plugins/hiscore/hiscore.dat; do
    [[ -s "${SRC}/${f}" ]] || { log "ERROR: no ${f} in the payload at ${SRC}"; exit 1; }
done

mkdir -p "${CORES}"
install -m 0644 "${SRC}/mame_libretro.so" "${SRC}/mame_libretro.catalogue" "${CORES}/"
rm -rf "${CORES}/mame"
mkdir -p "${CORES}/mame"
cp -R "${SRC}/plugins" "${CORES}/mame/plugins"
chmod -R u+rwX,go+rX,go-w "${CORES}/mame"
shut="$(find "${CORES}/mame" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${CORES}/mame that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi

missing="$(ldd "${CORES}/mame_libretro.so" | grep 'not found' || true)"
if [[ -n "${missing}" ]]; then
    log "ERROR: MAME's core is missing libraries:"
    echo "${missing}"
    exit 1
fi
log "  core $(du -h "${CORES}/mame_libretro.so" | cut -f1), $(grep -vc '^[#!]' "${CORES}/mame_libretro.catalogue") sets, plugins $(du -sh "${CORES}/mame/plugins" | cut -f1)"
