#!/usr/bin/env bash
#
# A BACKUP SOUND OUTPUT WHILE THE TV'S IS GONE (#228, "Sound can die until a
# restart"). pipewire-pulse starts at every login of the console's user, so
# PipeWire's own Dummy Output is there whenever no other output is. The rest
# of the sound rule is in system_files/usr/share/wireplumber/
# wireplumber.conf.d/60-cabinetos-sound-output.conf.
#
# WHY. SDL 3.4.18, which plays all of the console's own sound, leaves its
# stream on a dead device when the TV's output disappears (the screen asleep,
# the TV off) and no other output exists. When the TV comes back under the
# same PipeWire id, SDL can find the dead device instead of the new one
# (SDL_FindPhysicalAudioDeviceByHandle, read at 3.4.18) and nothing moves: the
# console plays into nothing until it is restarted. Upstream has not fixed it
# (libsdl-org/SDL#16133, milestone 3.6.0). With any other output there, SDL
# moves the stream to it when the TV goes and lets the dead device go, so the
# returning TV has nothing to collide with.
#
# THE FIX is PipeWire's own backup: pipewire-pulse loads module-always-sink by
# default, which makes a "Dummy Output" whenever no other output exists. Until
# now pipewire-pulse started only when a program asked for PulseAudio (its
# socket), and on most boots nothing did. The link below starts it with the
# user's session, where its own unit's [Install] section would put it
# (WantedBy=default.target). It is in /usr, which every update replaces whole,
# so consoles that upgrade get it as well as new installs
# (docs/lessons/image-and-ci.md, "Everything the image installs goes in /usr").
# `systemctl --user is-enabled` says "disabled" for a link in /usr, as it does
# for the base's own unity-gtk-module.service beside it, which starts at every
# login all the same: `is-active` is the check on a console.
#
# MEASURED on the A9 (testing image 2026.10.26, the screen's own sleep and
# wake): as shipped, silent on the 2nd wake; with pipewire-pulse running, 20 of
# 20 wakes had sound, the stream moving to Dummy Output within 2 s of each
# sleep. Remote Play is not changed by it: pipewire-pulse already runs during
# every stream. Nothing needs undoing when SDL is fixed.
#
# IF THE BUILD FAILS HERE, a PipeWire update moved what this leans on: the
# unit's name or place, or pipewire-pulse.conf no longer loading
# module-always-sink by default. Read the new files and redo the link or the
# check to match; do not delete the check. Without a backup output the
# console's sound can die again after the TV sleeps.

set -euo pipefail

source /ctx/lib.sh

UNIT=/usr/lib/systemd/user/pipewire-pulse.service
LINK=/usr/lib/systemd/user/default.target.wants/pipewire-pulse.service
CONF=/usr/share/pipewire/pipewire-pulse.conf

group_start "pipewire-pulse starts at login, so a backup output catches the sound (#228)"

for needed in "${UNIT}" "${CONF}"; do
    if [[ ! -f "${needed}" ]]; then
        log "  ERROR: ${needed} is not in this image"
        exit 1
    fi
done

# The backup output is loaded by default: the always-sink line is in
# pulse.cmd, with the condition that holds while nothing sets the property,
# and nothing sets it. Read with comments taken out and spaces collapsed, so
# only a change of meaning stops the build. A drop-in can add to pulse.cmd,
# replace it or set the property, so none may name either.
python3 -I - "${CONF}" <<'EOF'
import glob, os, re, sys

path = sys.argv[1]
def meaning(p):
    with open(p) as f:
        lines = [l for l in f if not l.lstrip().startswith("#")]
    return re.sub(r"\s+", " ", " ".join(lines))

text = meaning(path)
start = text.find("pulse.cmd = [")
if start < 0:
    sys.exit(f"  ERROR: {path} has no pulse.cmd list")
depth, end = 0, start
for end in range(text.index("[", start), len(text)):
    depth += {"[": 1, "]": -1}.get(text[end], 0)
    if depth == 0:
        break
entry = ('{ cmd = "load-module" args = "module-always-sink" flags = [ ] '
         'condition = [ { pulse.cmd.always-sink = !false } ] }')
if entry not in text[start:end + 1]:
    sys.exit(f"  ERROR: {path}'s pulse.cmd no longer loads module-always-sink by default")
if text.count("pulse.cmd.always-sink") != 1:
    sys.exit(f"  ERROR: {path} sets pulse.cmd.always-sink, which may turn the backup output off")
if os.path.exists("/etc/pipewire/pipewire-pulse.conf"):
    sys.exit("  ERROR: /etc/pipewire/pipewire-pulse.conf would be read instead of " + path)
for dropin in sorted(glob.glob("/usr/share/pipewire/pipewire-pulse.conf.d/*.conf") +
                     glob.glob("/etc/pipewire/pipewire-pulse.conf.d/*.conf")):
    if "pulse.cmd" in meaning(dropin):
        sys.exit(f"  ERROR: {dropin} changes pulse.cmd, which loads the backup output")
EOF
log "${CONF} loads module-always-sink by default ($(rpm -qf "${CONF}"))"

mkdir -p "$(dirname "${LINK}")"
ln -sfn ../pipewire-pulse.service "${LINK}"
if [[ "$(readlink -f "${LINK}")" != "${UNIT}" ]]; then
    log "  ERROR: ${LINK} does not lead to ${UNIT}"
    exit 1
fi
log "${LINK} -> $(readlink "${LINK}") ($(rpm -qf "${UNIT}"))"

group_end
