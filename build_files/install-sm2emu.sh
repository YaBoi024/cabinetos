#!/usr/bin/env bash
#
# Put sm2-emu, the Sega Model 2 emulator, into the image, as
# cores/build-sm2emu.sh built it at the pinned commit (the payload's sm2emu/,
# from build-sm2emu.yml), with its catalogue beside the cores:
#
#   /usr/lib/cabinetos/sm2emu/bin/sm2-emu             the program
#   /usr/lib/cabinetos/sm2emu/share/sm2-emu/          its game list and
#                                                     translations, where it
#                                                     looks from where it is
#   /usr/lib/cabinetos/cores/sm2emu.catalogue         which sets it plays
#                                                     (frontend/src/arcadematch.h)
#
# WHY sm2-emu, BUILT HERE: docs/decisions/2026-10-28-model-2-and-model-3.md;
# the reasons are in cores/build-sm2emu.sh. In /usr, like Cemu, so it moves
# with the image and a console that has never reached the internet has Model
# 2 from its first boot. Its settings and saves go where the console tells it
# (standalone_sm2emu.cpp), because /usr is read-only.
#
# Its licence is GPL-3.0-or-later with additional terms under its section 7;
# see docs/LICENCES.md and its NOTICE in licences/.

set -euo pipefail
source /ctx/lib.sh

readonly SRC=/ctx/payload/sm2emu
readonly DEST=/usr/lib/cabinetos/sm2emu
readonly CORES=/usr/lib/cabinetos/cores

log "sm2-emu $(cut -c1-10 "${SRC}/VERSION")"
# A FILE, NOT AN EXECUTABLE, IN THE PAYLOAD: GitHub's artifact upload does not
# keep permissions (install-cemu.sh), so the execute bit is set below.
for f in bin/sm2-emu share/sm2-emu/games.xml sm2emu.catalogue VERSION; do
    [[ -s "${SRC}/${f}" ]] || { log "ERROR: no ${f} in the payload at ${SRC}"; exit 1; }
done

rm -rf "${DEST}"
mkdir -p "${DEST}" "${CORES}"
cp -R "${SRC}/bin" "${SRC}/share" "${SRC}/VERSION" "${DEST}/"
install -m 0644 "${SRC}/sm2emu.catalogue" "${CORES}/"

# READABLE BY EVERYONE: the console runs as `cabinet` (install-cemu.sh).
chmod -R u+rwX,go+rX,go-w "${DEST}"
chmod 0755 "${DEST}/bin/sm2-emu"
shut="$(find "${DEST}" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi

# EVERY LIBRARY IT NEEDS RESOLVES against the image itself, and libcurl is
# not among them (build-sm2emu.sh checked the same in its builder). It cannot
# be started here: there is no display. The A9 is where it is seen to run.
libs="$(ldd "${DEST}/bin/sm2-emu")"
if grep -q 'not found' <<<"${libs}"; then
    log "ERROR: sm2-emu is missing libraries:"
    grep 'not found' <<<"${libs}"
    exit 1
fi
if grep -q 'libcurl' <<<"${libs}"; then
    log "ERROR: sm2-emu links libcurl; its artwork downloader is in"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1)), $(grep -vc '^#' "${CORES}/sm2emu.catalogue") sets"
