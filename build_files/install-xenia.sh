#!/usr/bin/env bash
#
# Put Xenia Edge, the Xbox 360 emulator, into the image: its developer's own
# Linux build, unpacked into /usr/lib/cabinetos/xenia.
#
# WHY EDGE. It is a fork of Xenia Canary whose developers build and recommend
# a native Linux version rather than Canary under Wine, and Batocera's default
# for Xbox 360. Measured on the A9 2026-09-29: Forza Horizon 2 and Left 4 Dead
# 2 held their native 30 FPS, the GPU under 40%. docs/PROJECT.md, open
# question 34; issue #192.
#
# WHY NOT FLATHUB: it is not there. In /usr, like RPCS3, so it moves with the
# image and a console that has never reached the internet has Xbox 360 from
# its first boot.
#
# PINNED THE WAY RPCS3 IS. Edge publishes a release for nearly every commit
# (has207/xenia-edge, tagged with the short SHA) and has no stable channel, so
# the URL names one exact build and the checksum makes the build fail rather
# than ship anything else. GitHub's own digest for the asset is the same
# sha256. To move it: pick a release, put its tag and the sha256 of its
# AppImage here, try it on the A9, and say why in the commit. Never patched.
#
# UNPACKED, NOT RUN AS AN APPIMAGE, as RPCS3 is: `usr/bin/xenia_edge` runs
# directly, finds its bundled libraries through its own RUNPATH
# ($ORIGIN/../lib), and is a plain process called `xenia_edge` the console can
# find and close. Measured on the A9 2026-11-01 for 4ad875a: 21 MB
# downloaded, 55 MB unpacked, no library missing against the image, run
# exactly this way.
#
# Its licence is BSD-3-Clause; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly XENIA_VERSION="4ad875a"
readonly XENIA_URL="https://github.com/has207/xenia-edge/releases/download/${XENIA_VERSION}/xenia_edge_linux.AppImage"
readonly XENIA_SHA256="80d562771186f074297f4c15522199e0f4ffad6a6db1ddf7d2f5113f71f10bee"
readonly DEST=/usr/lib/cabinetos/xenia

log "Xenia Edge ${XENIA_VERSION}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

fetch_pinned "${XENIA_URL}" "${XENIA_SHA256}" "${work}/xenia.AppImage" "Xenia Edge"

got="$(sha256sum "${work}/xenia.AppImage" | cut -d' ' -f1)"
if [[ "${got}" != "${XENIA_SHA256}" ]]; then
    log "ERROR: Xenia Edge's checksum is ${got}, expected ${XENIA_SHA256}"
    exit 1
fi

# The AppImage unpacks itself without FUSE, into squashfs-root beside it.
chmod +x "${work}/xenia.AppImage"
(cd "${work}" && ./xenia.AppImage --appimage-extract >/dev/null)
root="${work}/squashfs-root"
[[ -x "${root}/usr/bin/xenia_edge" ]] || { log "ERROR: no usr/bin/xenia_edge in the AppImage"; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}"
# usr/ is the program and its libraries. The AppImage's launcher is not
# needed. Nothing else goes beside the program: Xenia writes its log there
# unless told otherwise, and the console always tells it otherwise
# (standalone_xenia.cpp, `--log_file`), because /usr is read-only.
cp -a "${root}/usr" "${DEST}/"
rm -f "${DEST}/usr/bin/"*.log
printf '%s\n' "${XENIA_VERSION}" > "${DEST}/VERSION"

# READABLE BY EVERYONE, which the AppImage is not. It unpacks with its top
# folder 0700 and `cp -a` kept that, so the first testing image had
# /usr/lib/cabinetos/xenia/usr readable by root alone: the console, which runs
# as `cabinet`, could not see the program and said Xbox 360's emulator was not
# installed (2026-09-30). RPCS3's AppImage does not do this. Every file gets
# its owner's read bit for everyone, and the execute bit where the owner has
# one; then the check below fails the build if anything is still shut.
chmod -R u+rwX,go+rX,go-w "${DEST}"
shut="$(find "${DEST}" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi
[[ -x "${DEST}/usr/bin/xenia_edge" && "$(stat -c %A "${DEST}/usr/bin/xenia_edge")" == *x ]] ||
    { log "ERROR: xenia_edge is not executable by everyone"; exit 1; }

# THE CHECK THAT CAN BE MADE HERE: every library it needs resolves, against
# the image's own. It cannot be started in the build container: it opens its
# window before anything else, and there is no display. The A9 is where it is
# seen to run.
missing="$(ldd "${DEST}/usr/bin/xenia_edge" | grep 'not found' || true)"
if [[ -n "${missing}" ]]; then
    log "ERROR: Xenia Edge is missing libraries:"
    echo "${missing}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1))"
