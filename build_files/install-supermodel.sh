#!/usr/bin/env bash
#
# Put Supermodel, the Sega Model 3 emulator, into the image, as
# cores/build-supermodel.sh built it at the pinned commit (the payload's
# supermodel/, from build-supermodel.yml), with its catalogue beside the cores:
#
#   /usr/lib/cabinetos/supermodel/bin/supermodel       the program
#   /usr/lib/cabinetos/supermodel/lib/                 SDL2_net, which the
#                                                      base lacks, found from
#                                                      the program's run path
#   /usr/lib/cabinetos/supermodel/share/supermodel/    its game list and the
#                                                      crosshair pictures it
#                                                      will not start without
#   /usr/lib/cabinetos/cores/supermodel.catalogue      which sets it plays
#                                                      (frontend/src/arcadematch.h)
#
# WHY Supermodel, BUILT HERE: docs/decisions/2026-10-28-model-2-and-model-3.md;
# the reasons are in cores/build-supermodel.sh. In /usr, like sm2-emu, so it
# moves with the image and a console that has never reached the internet has
# Model 3 from its first boot. Its settings and saves go where the console
# tells it (standalone_supermodel.cpp), because /usr is read-only.
#
# Its licence is GPL v3; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly SRC=/ctx/payload/supermodel
readonly DEST=/usr/lib/cabinetos/supermodel
readonly CORES=/usr/lib/cabinetos/cores

log "Supermodel $(cut -c1-10 "${SRC}/VERSION")"
# A FILE, NOT AN EXECUTABLE, IN THE PAYLOAD: GitHub's artifact upload does not
# keep permissions (install-cemu.sh), so the execute bit is set below.
for f in bin/supermodel lib/libSDL2_net-2.0.so.0 share/supermodel/Games.xml \
         share/supermodel/Assets/p1crosshair.bmp supermodel.catalogue VERSION; do
    [[ -s "${SRC}/${f}" ]] || { log "ERROR: no ${f} in the payload at ${SRC}"; exit 1; }
done

rm -rf "${DEST}"
mkdir -p "${DEST}" "${CORES}"
cp -R "${SRC}/bin" "${SRC}/lib" "${SRC}/share" "${SRC}/VERSION" "${DEST}/"
install -m 0644 "${SRC}/supermodel.catalogue" "${CORES}/"

# READABLE BY EVERYONE: the console runs as `cabinet` (install-cemu.sh).
chmod -R u+rwX,go+rX,go-w "${DEST}"
chmod 0755 "${DEST}/bin/supermodel"
shut="$(find "${DEST}" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi

# EVERY LIBRARY IT NEEDS RESOLVES against the image itself, SDL2_net from its
# own lib/ (build-supermodel.sh checked the same in its builder). It cannot be
# started here: there is no display. The A9 is where it is seen to run.
libs="$(ldd "${DEST}/bin/supermodel")"
if grep -q 'not found' <<<"${libs}"; then
    log "ERROR: Supermodel is missing libraries:"
    grep 'not found' <<<"${libs}"
    exit 1
fi
grep -qE "libSDL2_net-2.0.so.0 => ${DEST}/(bin/\.\./)?lib/" <<<"${libs}" ||
    { log "ERROR: Supermodel does not take SDL2_net from ${DEST}/lib"; exit 1; }
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1)), $(grep -vc '^#' "${CORES}/supermodel.catalogue") sets"
