#!/usr/bin/env bash
#
# Put RPCS3, the PlayStation 3 emulator, into the image: the RPCS3 team's own
# Linux build, unpacked into /usr/lib/cabinetos/rpcs3.
#
# WHY NOT FLATHUB, like Eden and xemu. Since 2026-08-13 RPCS3 shows an
# "Unofficial Build Warning" box on every start when it runs as a Flatpak
# (rpcs3qt/gui_application.cpp, `FLATPAK_ID`), with No as the default and No
# quitting. There is no setting for it. The Flathub package is community-made
# and the RPCS3 team does not support it. MMagTech, 2026-09-28: take the
# official build instead. docs/PROJECT.md, open questions 19 and 21.
#
# WHY THIS IS NOT THE /var TRAP of open question 21. That trap is a Flatpak's:
# it installs into /var, which bootc unpacks only on the first install. This
# lands in /usr, which every update replaces whole, so RPCS3 moves with the
# image exactly as the libretro cores do, and a console that has never reached
# the internet has PS3 from its first boot.
#
# PINNED THE WAY A CORE IS. The official builds are one GitHub release per
# commit (RPCS3/rpcs3-binaries-linux), kept back to 2019, so the URL names an
# exact build and the checksum makes the build fail rather than ship anything
# else. To move it: pick a release, put its URL and the sha256 of its
# AppImage here, and say why in the commit.
#
# UNPACKED, NOT RUN AS AN APPIMAGE. An AppImage mounts itself with FUSE when it
# starts; unpacked, `usr/bin/rpcs3` runs directly, finds its bundled libraries
# through its own RUNPATH ($ORIGIN/../lib), and is a plain process called
# `rpcs3` that the console can signal. Measured on the A9 2026-09-28: 94 MB
# downloaded, 338 MB unpacked, no library missing against the image, and the
# AppImage's own launcher adds nothing on this base (its libstdc++ check picks
# the system's, which is newer).
#
# Its licence is GPL-2.0; see docs/LICENCES.md.

set -euo pipefail
source /ctx/lib.sh

readonly RPCS3_VERSION="0.0.42-20076-1707d7fc"
readonly RPCS3_URL="https://github.com/RPCS3/rpcs3-binaries-linux/releases/download/build-1707d7fc883ef48ff21bdcbb0141a3211ae09cb2/rpcs3-v0.0.42-20076-1707d7fc_linux64.AppImage"
readonly RPCS3_SHA256="e3e29063410ca40fc7b5c66aaaec7c14c41144135faa09f23f5797cf15194a55"
readonly DEST=/usr/lib/cabinetos/rpcs3

log "RPCS3 ${RPCS3_VERSION}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

fetch_pinned "${RPCS3_URL}" "${RPCS3_SHA256}" "${work}/rpcs3.AppImage" "RPCS3"

got="$(sha256sum "${work}/rpcs3.AppImage" | cut -d' ' -f1)"
if [[ "${got}" != "${RPCS3_SHA256}" ]]; then
    log "ERROR: RPCS3's checksum is ${got}, expected ${RPCS3_SHA256}"
    exit 1
fi

# The AppImage unpacks itself without FUSE, into squashfs-root beside it. It
# still prints "failed to utilize FUSE during startup!" in a container; the
# unpack is unaffected, and the check below is what says whether it worked.
chmod +x "${work}/rpcs3.AppImage"
(cd "${work}" && ./rpcs3.AppImage --appimage-extract >/dev/null)
root="${work}/squashfs-root"
[[ -x "${root}/usr/bin/rpcs3" ]] || { log "ERROR: no usr/bin/rpcs3 in the AppImage"; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}"
# usr/ is the program, its libraries and its own data (fonts, icons, the
# game-compatibility patches). The AppImage's launcher and checkrt are not
# needed; see above.
cp -a "${root}/usr" "${DEST}/"
printf '%s\n' "${RPCS3_VERSION}" > "${DEST}/VERSION"

# THE RPCS3 TEAM'S PER-GAME SETTINGS, beside the program. RPCS3's own window
# downloads them (https://api.rpcs3.net/config/?api=v1) into
# GuiConfigs/config_database.dat and lays a game's entry over the global
# settings at every boot, `--no-gui` included; the console fetches nothing
# from there, so the image carries a snapshot of that file,
# /ctx/rpcs3-config-database.json (build_files/, refreshed by
# tools/fetch-rpcs3-config-database.py when the pin above moves), and the
# console copies it into RPCS3's config folder before every start
# (frontend/src/standalone_rpcs3.cpp, prepareRpcs3).
# docs/decisions/2026-11-06-rpcs3-settings-database-snapshot.md.
#
# TAKEN AGAINST THIS BUILD, or the build stops: the tool writes the pin it
# ran against as the first word of /ctx/rpcs3-config-database.pin, and a
# snapshot from an older build drifts from what this one needs (the decision
# says how). ci/check-rpcs3-snapshot.sh says the same on the pull request.
#
# CHECKED AS RPCS3 CHECKS IT (rpcs3qt/config_database.cpp, read_json): JSON,
# an object, `return_code` not negative, `games` an object, every game a
# `config` string. An entry the pinned build cannot read is RPCS3's to skip,
# one by one, at every start. The file's name and the two log lines the
# console reads for it (standalone_rpcs3.cpp, tools/check-applied.sh) are in
# the program's own text, and a build that renames them fails here rather
# than silently giving no game its entry.
readonly DATABASE=/ctx/rpcs3-config-database.json
taken="$(head -1 /ctx/rpcs3-config-database.pin | cut -d' ' -f1)"
if [[ "${taken}" != "${RPCS3_VERSION}" ]]; then
    log "ERROR: the settings snapshot was taken against RPCS3 ${taken}, this build is ${RPCS3_VERSION}: run tools/fetch-rpcs3-config-database.py"
    exit 1
fi
games="$(python3 -I - "${DATABASE}" <<'PY'
import json, sys
with open(sys.argv[1], "rb") as f:
    data = json.load(f)
if not isinstance(data, dict) or not isinstance(data.get("return_code"), int) or data["return_code"] < 0:
    sys.exit("no non-negative return_code")
games = data.get("games")
if not isinstance(games, dict) or not games:
    sys.exit("no games object")
for serial, game in games.items():
    if not isinstance(game, dict) or not isinstance(game.get("config"), str):
        sys.exit(f"{serial} has no config string")
print(len(games))
PY
)" || { log "ERROR: ${DATABASE} is not RPCS3's settings database"; exit 1; }
for phrase in "config_database.dat" "Finished reading config database from file" \
              "Applying database config"; do
    grep -q -a -F "${phrase}" "${root}/usr/bin/rpcs3" ||
        { log "ERROR: RPCS3 no longer has '${phrase}': the per-game database moved"; exit 1; }
done
install -m 0644 "${DATABASE}" "${DEST}/config_database.dat"
log "  per-game settings database: ${games} games"

# READABLE BY EVERYONE: the console runs as `cabinet`, and the checks below run
# as root, so they would pass on a folder only root can read. Xenia's AppImage
# unpacks that way and its first testing image shipped it (install-xenia.sh).
# RPCS3's does not today; a new build could. Done and checked the same way.
chmod -R u+rwX,go+rX,go-w "${DEST}"
shut="$(find "${DEST}" ! -perm -o+r | head -5)"
if [[ -n "${shut}" ]]; then
    log "ERROR: files in ${DEST} that the console's user cannot read:"
    echo "${shut}"
    exit 1
fi
[[ -x "${DEST}/usr/bin/rpcs3" && "$(stat -c %A "${DEST}/usr/bin/rpcs3")" == *x ]] ||
    { log "ERROR: rpcs3 is not executable by everyone"; exit 1; }

# THE CHECK THAT MATTERS: it starts, from where it now lives, against the
# image's own libraries, and it is the build this file pins. `--headless` as
# well as `--version`: without it RPCS3 builds its Qt window layer first, and
# the build container has no display for that (the first testing build died
# here, 2026-09-29, "no Qt platform plugin could be initialized"). Headless it
# makes a plain QCoreApplication (rpcs3.cpp, create_application).
if ! XDG_CONFIG_HOME="${work}/cfg" XDG_CACHE_HOME="${work}/cache" \
        "${DEST}/usr/bin/rpcs3" --headless --version >"${work}/version.out" 2>&1 ||
   ! grep -q "RPCS3 ${RPCS3_VERSION}" "${work}/version.out"; then
    cat "${work}/version.out"
    log "ERROR: RPCS3 does not start in the image, or is not ${RPCS3_VERSION}"
    exit 1
fi
missing="$(ldd "${DEST}/usr/bin/rpcs3" | grep 'not found' || true)"
if [[ -n "${missing}" ]]; then
    log "ERROR: RPCS3 is missing libraries:"
    echo "${missing}"
    exit 1
fi
log "  installed in ${DEST} ($(du -sh "${DEST}" | cut -f1))"
