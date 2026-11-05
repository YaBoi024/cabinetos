#!/usr/bin/env bash
#
# THE SOUND RULE STILL FITS WIREPLUMBER (#655, "Sound: a line-out jack that
# cannot sense a plug takes the sound from the screen (Beelink SER, UCM
# profile)"). The rule is in system_files/usr/share/wireplumber/scripts/
# cabinetos-sound-output.lua, which says what each of its three steps does.
#
# WHY A CHECK. The rule runs between steps of WirePlumber's own and reads what
# they leave for it. A rule that stops fitting does not stop the sound:
# WirePlumber goes on with its own choice (measured 2026-11-04, with a typing
# mistake in the script and with a fault while it ran). So the sound goes to a
# speaker or an empty jack on some PCs, and the A9, which has neither, shows
# nothing. The build stops instead. The base image is pinned by digest, so this
# can only stop the pull request that moves the base.
#
# Each file is read with comments taken out and spaces collapsed, so only a
# change of meaning stops the build.
#
# IF THE BUILD FAILS HERE, a WirePlumber update changed a file the rule stands
# on, or added a step beside them. Read what changed against the rule's three
# steps, fix the script if it needs it, and run them on a stand-in card
# (docs/lessons/testing.md, "Testing where the sound goes with nothing to hear
# it"). Then put the sums the error prints below. Do not delete the check.

set -euo pipefail

source /ctx/lib.sh

CONF=/usr/share/wireplumber/wireplumber.conf

group_start "The sound rule still fits WirePlumber (#655)"

python3 -I - "${CONF}" <<'EOF'
import hashlib, os, re, sys

conf = sys.argv[1]
ROOT = "/usr/share/wireplumber/scripts"
SUMS = {
    # Step 1, the default output: who makes the list, who settles a choice
    # somebody made, and who picks from the list after the rule has been at it.
    "default-nodes/rescan.lua": "ddf26e8f42915918",
    "default-nodes/find-selected-default-node.lua": "ceed3aab0be1c12d",
    "default-nodes/state-default-nodes.lua": "9825954aa1c068dc",
    "default-nodes/find-best-default-node.lua": "bf81f839f88cd057",
    "default-nodes/apply-default-node.lua": "78c86bb621f33398",
    # Step 2, the silent output: where a program's sound goes with no default.
    "linking/find-default-target.lua": "83781e0d3cc0879b",
    "linking/find-best-target.lua": "932567d771ce70e7",
    "lib/linking-utils.lua": "b0f4c773f73cc2cd",
    # Step 3, the card's profile.
    "device/select-profile.lua": "98daedc179315b3a",
    "device/state-profile.lua": "4c5322bdb8ecdbb0",
    "device/find-preferred-profile.lua": "3531d242504434ad",
    "device/find-best-profile.lua": "ac42e46038f5bafb",
    "device/apply-profile.lua": "d23f9f01f9b98c53",
    "lib/profile-utils.lua": "7e41af2de9bbb99a",
    # How the rule reads a card's outputs.
    "lib/common-utils.lua": "50c076c1b34732a1",
}
# A new file here may be a new step between these.
FOLDERS = {
    "default-nodes": ["apply-default-node.lua", "find-best-default-node.lua",
                      "find-selected-default-node.lua", "rescan.lua",
                      "state-default-nodes.lua"],
    "device": ["apply-profile.lua", "apply-routes.lua", "automute-alsa-routes.lua",
               "autoswitch-bluetooth-profile.lua", "find-best-profile.lua",
               "find-best-routes.lua", "find-preferred-profile.lua",
               "find-voice-call-profile.lua", "select-profile.lua",
               "select-routes.lua", "state-profile.lua", "state-routes.lua"],
}

def meaning(path, comment):
    with open(path) as f:
        lines = [l for l in f if not l.lstrip().startswith(comment)]
    return re.sub(r"\s+", " ", " ".join(lines)).strip()

faults = []
for name, want in SUMS.items():
    path = os.path.join(ROOT, name)
    if not os.path.isfile(path):
        faults.append(f"{path} is not in this image")
        continue
    got = hashlib.sha256(meaning(path, "--").encode()).hexdigest()[:16]
    if got != want:
        faults.append(f"{path} changed: its sum is now {got}")
for folder, want in FOLDERS.items():
    path = os.path.join(ROOT, folder)
    got = sorted(os.listdir(path)) if os.path.isdir(path) else []
    if got != want:
        faults.append(f"{path} now holds {got}")
# The rule's own config loads it before this part, in this profile.
text = meaning(conf, "#")
for needed in ("provides = support.standard-event-source", "main = {"):
    if needed not in text:
        faults.append(f"{conf} no longer has '{needed}'")

if faults:
    for fault in faults:
        print("  ERROR: " + fault)
    sys.exit("  The sound rule may no longer fit: read the comment at the top of build_files/sound-output-rule.sh")
print(f"  {len(SUMS)} of WirePlumber's files read as the rule was tested on")
EOF

log "the sound rule fits $(rpm -qf "${CONF}")"

group_end
