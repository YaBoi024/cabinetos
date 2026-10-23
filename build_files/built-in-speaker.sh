#!/usr/bin/env bash
#
# A SPEAKER BUILT INTO THE CASE NEVER TAKES THE SOUND FROM THE TV (#547,
# "Sounds plays only from onboard speakers (Beelink ser9)"). The rest of
# MMagTech's sound rule is in system_files/usr/share/wireplumber/
# wireplumber.conf.d/60-cabinetos-sound-output.conf: the TV first, a jack that
# senses the plug takes over, nothing else.
#
# WHY. The kernel gives a built-in speaker a "Speaker Phantom Jack" that always
# reads as plugged in, and PipeWire's description of the speaker output says
# what that means: `state.plugged = unknown`. An output whose connector is
# "unknown" counts as usable, and the onboard analog output outranks HDMI, so a
# mini PC with a speaker in its case played everything there (a Beelink SER9
# in #547; an HP EliteDesk 705 G4 in the report on #416). The speaker and the
# headphone jack are one output, so no priority can put the speaker below the
# TV and the headphones above it.
#
# THE FIX is the file format's own option: the phantom jack's state becomes
# `no`. In PipeWire, a plugged-in jack that says "no" overrides everything
# else (calc_port_state in spa/plugins/alsa/acp/acp.c, read at 1.6.9). A
# built-in speaker then counts as an empty headphone jack does: the analog
# output exists only while headphones are in, as on a machine with no speaker
# (the A9). Headphones, line out and HDMI are other sections and other files,
# untouched. A card with its own UCM profile (the Steam Deck's, AMD/acp5x)
# never reads this file.
#
# IF THE BUILD FAILS HERE, a PipeWire update changed the file. Read its new
# speaker sections and redo the edit to match; do not delete the check. Each
# section is changed only if it reads exactly as it did at 1.6.9.

set -euo pipefail

source /ctx/lib.sh

FILE=/usr/share/alsa-card-profile/mixer/paths/analog-output-speaker.conf

group_start "A built-in speaker counts as not there (#547)"

if [[ ! -f "${FILE}" ]]; then
    log "  ERROR: ${FILE} is not in this image"
    exit 1
fi

python3 -I - "${FILE}" <<'EOF'
import sys

path = sys.argv[1]
with open(path) as f:
    text = f.read()
for jack in ("Speaker Phantom", "Speaker Front Phantom"):
    before = f"[Jack {jack}]\nrequired-any = any\nstate.plugged = unknown\nstate.unplugged = unknown\n"
    after = f"[Jack {jack}]\nrequired-any = any\nstate.plugged = no\nstate.unplugged = no\n"
    if text.count(before) != 1:
        sys.exit(f"  ERROR: [Jack {jack}] in {path} does not read as it did at PipeWire 1.6.9")
    text = text.replace(before, after)
with open(path, "w") as f:
    f.write(text)
EOF

log "both speaker phantom jacks now say no in ${FILE} ($(rpm -qf "${FILE}"))"

group_end
