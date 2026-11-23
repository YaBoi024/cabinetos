#!/usr/bin/env bash
#
# THE WI-FI SCAN RULE AND POWER SAVING STILL FIT NETWORKMANAGER (#666). The
# rule is system_files/usr/lib/NetworkManager/dispatcher.d/50-cabinetos-wifi-scans,
# which says what it does, and power saving is off in
# system_files/usr/lib/NetworkManager/conf.d/50-cabinetos-wifi.conf; the
# decision is docs/decisions/2026-11-08-wifi-scans-and-power-saving.md.
#
# WHY A CHECK. The script replaces NetworkManager's two background scan rules,
# by their exact text, in wpa_supplicant. If NetworkManager changes its rules,
# hands Wi-Fi to iwd, or the script stops running, nothing fails: a console on
# Wi-Fi goes back to stuttering ten seconds every five minutes in a streamed
# game, which shows only to a player. So the build stops instead. The base
# image is pinned by digest, so this can only stop the pull request that moves
# the base.
#
# IF THE BUILD FAILS HERE, read what changed:
# - NetworkManager's rules changed: read nm_supplicant_config_add_bgscan() in
#   its src/core/supplicant/nm-supplicant-config.c, put the new rules in the
#   script's case line and below.
# - NetworkManager has a setting of its own for the rule (wifi.bgscan, its
#   merge request !2593): put the rule in
#   system_files/usr/lib/NetworkManager/conf.d/50-cabinetos-wifi.conf, delete
#   the script and this check, and update the decision.
# - The Wi-Fi backend is iwd: the script does nothing there; iwd has its own
#   setting (DisablePeriodicScan). Measure again before choosing.
# - wpa_supplicant moved past 2.11: its bgscan module and its D-Bus setter
#   are what the rule stands on. Read wpa_supplicant/bgscan_simple.c and
#   set_network_properties() in dbus/dbus_new_handlers.c, then put the version
#   below.
# - NetworkManager does not read wifi.powersave=2: read the "Connection
#   section" of its NetworkManager.conf manual for the setting's new name.
# Then run the script on a console on Wi-Fi: its journal line says the rule it
# set, and `iw event -t` shows no scan for 15 minutes. Do not delete the check.

set -euo pipefail

source /ctx/lib.sh

NM=/usr/sbin/NetworkManager
SCRIPT=/usr/lib/NetworkManager/dispatcher.d/50-cabinetos-wifi-scans

group_start "The Wi-Fi scan rule and power saving still fit NetworkManager (#666)"

python3 -I - "${NM}" <<'EOF'
import re, sys
data = open(sys.argv[1], "rb").read()
rules = sorted(set(m.decode() for m in re.findall(rb"simple:-?\d+:-?\d+:-?\d+", data)))
faults = []
if rules != ["simple:30:-65:300", "simple:30:-70:86400"]:
    faults.append(f"NetworkManager's background scan rules are now {rules}")
if b"wifi.bgscan" in data:
    faults.append("NetworkManager now has a setting of its own, wifi.bgscan")
for fault in faults:
    print("  ERROR: " + fault)
if faults:
    sys.exit("  The Wi-Fi scan rule may no longer fit: read the comment at the top of build_files/wifi-scan-rule.sh")
print("  NetworkManager's rules are the two the script replaces")
EOF

# NetworkManager's settings as it reads them from every conf.d file. The
# backend is the one it uses when nothing names one, and nothing does.
config=$(NetworkManager --print-config)
backend=$(sed -n 's/^#\{0,1\} *wifi\.backend=//p' <<<"${config}")
if [[ ${backend} != wpa_supplicant ]]; then
    log "  ERROR: NetworkManager's Wi-Fi backend is '${backend}', not wpa_supplicant"
    exit 1
fi

if ! grep -qx 'wifi.powersave=2' <<<"${config}"; then
    log "  ERROR: NetworkManager does not read wifi.powersave=2 from 50-cabinetos-wifi.conf"
    exit 1
fi

wpa=$(rpm -q --qf '%{VERSION}' wpa_supplicant)
if [[ ${wpa} != 2.11 ]]; then
    log "  ERROR: wpa_supplicant is ${wpa}; the rule was read against 2.11"
    exit 1
fi

if [[ ! -x ${SCRIPT} || $(stat -c '%U %a' "${SCRIPT}") != "root 755" ]]; then
    log "  ERROR: ${SCRIPT} is missing, or NetworkManager would refuse it (it runs only root's, not writable by others)"
    exit 1
fi

log "the Wi-Fi scan rule and power saving fit $(rpm -q NetworkManager) and $(rpm -q wpa_supplicant)"

group_end
