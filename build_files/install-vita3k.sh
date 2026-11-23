#!/usr/bin/env bash
#
# Put Vita3K, the PlayStation Vita emulator, into the image: its developers'
# own Linux build, unpacked into /usr/lib/cabinetos/vita3k.
#
# WHY VITA3K. The one Vita emulator there is, and Batocera's. The console runs
# it through its plain SDL window (`--frontend sdl`), never its own interface,
# so no Vita3K screen is ever shown (standalone_vita3k.cpp). docs/PROJECT.md,
# open question 49; issue #198.
#
# WHY NOT FLATHUB: it is not there. In /usr, like Xenia, so it moves with the
# image and a console that has never reached the internet has the Vita from
# its first boot.
#
# PINNED THE WAY XENIA IS. Vita3K's own release is a moving `continuous`, but
# every build of its master branch is also a numbered release in
# Vita3K/Vita3K-builds with the same x86-64 AppImage, so the URL names one
# numbered build and the checksum makes the build fail rather than ship
# anything else. GitHub's own digest for the asset is the same sha256. To move
# it: pick a numbered build, put its number, the commit it names and the
# sha256 of its AppImage here and in docs/LICENCES.md and licences/SOURCES,
# try it on the A9, and say why in the commit. Never patched.
#
# UNPACKED, NOT RUN AS AN APPIMAGE. It is a "sharun" AppImage: `bin/Vita3K`
# runs `shared/bin/Vita3K` with the glibc and libraries in `lib/` (Qt, SDL,
# its own Mesa Vulkan drivers, which it always uses before the system's), so
# the program is a process called `Vita3K` the console can find and close.
# Its launcher (AppRun) is left out, and with it the launcher's hooks, one of
# which updates the AppImage itself from the internet. What the launcher
# would have set, the console sets (standalone_vita3k.cpp, vitaEnv).
# Measured on the A9 2026-10-20: 78.7 MB downloaded, 258 MB unpacked, run
# exactly this way.
#
# Its licence is GPL-2.0; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly VITA3K_VERSION="4210"
readonly VITA3K_URL="https://github.com/Vita3K/Vita3K-builds/releases/download/${VITA3K_VERSION}/Vita3K-x86_64.AppImage"
readonly VITA3K_SHA256="43fdc878d2ca18f02a093cd7250c226768907a7d26c45d339e255a14218ae3f2"
readonly DEST=/usr/lib/cabinetos/vita3k

log "Vita3K build ${VITA3K_VERSION}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

fetch_pinned "${VITA3K_URL}" "${VITA3K_SHA256}" "${work}/vita3k.AppImage" "Vita3K"

got="$(sha256sum "${work}/vita3k.AppImage" | cut -d' ' -f1)"
if [[ "${got}" != "${VITA3K_SHA256}" ]]; then
    log "ERROR: Vita3K's checksum is ${got}, expected ${VITA3K_SHA256}"
    exit 1
fi

# The AppImage unpacks itself without FUSE, into squashfs-root beside it.
chmod +x "${work}/vita3k.AppImage"
(cd "${work}" && ./vita3k.AppImage --appimage-extract >/dev/null)
root="${work}/squashfs-root"
[[ -x "${root}/bin/Vita3K" && -x "${root}/shared/bin/Vita3K" ]] ||
    { log "ERROR: no bin/Vita3K and shared/bin/Vita3K in the AppImage"; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}"
# Everything but the launcher and its hooks (above).
cp -a "${root}/." "${DEST}/"
rm -f "${DEST}/AppRun" "${DEST}/AppRun.sh" "${DEST}/AppRun.lib" "${DEST}/bin/"*.hook
[[ -z "$(find "${DEST}" -name '*.hook' -o -name 'AppRun*' | head -1)" ]] ||
    { log "ERROR: the AppImage's launcher or a hook is still in ${DEST}"; exit 1; }

# ITS OWN VULKAN LOADER UNDER THE NAME SDL ASKS FOR. The AppImage carries the
# loader only as libvulkan.so.1.<version>, so the SDL inside Vita3K, asking
# for libvulkan.so.1, got the system's while Vita3K had made its instance with
# its own, and every game crashed at its first frame on Vulkan (measured on
# the A9 2026-10-20). The console names this link to SDL
# (SDL_VULKAN_LIBRARY, standalone_vita3k.cpp). Exactly one loader, or a
# moved pin has changed what this fixes and the build says so.
loaders=("${DEST}"/lib/libvulkan.so.1.*)
if [[ ${#loaders[@]} -ne 1 || ! -f "${loaders[0]}" ]]; then
    log "ERROR: expected one libvulkan.so.1.* in ${DEST}/lib, found: ${loaders[*]}"
    exit 1
fi
ln -sfn "$(basename "${loaders[0]}")" "${DEST}/lib/libvulkan.so.1"
printf '%s\n' "${VITA3K_VERSION}" > "${DEST}/VERSION"

# READABLE BY EVERYONE, as for Xenia, whose AppImage unpacked with its top
# folder 0700 and hid the program from the console's user (2026-09-30).
chmod -R u+rwX,go+rX,go-w "${DEST}"
shut="$(find "${DEST}" ! -type l ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi

# THE CHECK THAT CAN BE MADE HERE: it starts, with its own libraries, and
# says which build it is. `--version` opens no window. Its folders go to a
# scratch home, never the image's.
scratch="$(mktemp -d)"
said="$(env -i PATH=/usr/bin:/bin HOME="${scratch}" XDG_CONFIG_HOME="${scratch}" \
    XDG_CACHE_HOME="${scratch}" XDG_DATA_HOME="${scratch}" APPDIR="${DEST}" \
    "${DEST}/bin/Vita3K" --version 2>&1 | tail -1)"
rm -rf "${scratch}"
if [[ "${said}" != *" ${VITA3K_VERSION}-"* ]]; then
    log "ERROR: Vita3K did not start, or is not build ${VITA3K_VERSION}: ${said}"
    exit 1
fi
log "  ${said}, installed in ${DEST} ($(du -sh "${DEST}" | cut -f1))"
