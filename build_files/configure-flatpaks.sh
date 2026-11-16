#!/usr/bin/env bash
#
# Wires up the emulator flatpaks.
#
# NOTHING IS INSTALLED HERE, and that is the whole point of the file.
#
# The obvious thing — `flatpak install` in the Containerfile — does run. It was
# tried in a container on 2026-09-18 and `flatpak remote-add --system` succeeded
# and wrote /var/lib/flatpak. The problem is what bootc does with it afterwards:
# /var in a container image behaves like a Docker VOLUME, unpacked ONLY from the
# initial image, and bootc's own documentation says "subsequent changes to /var
# in a container image are not automatically applied".
#
# So a baked-in emulator would install once on a brand new machine, never move
# again however many images shipped after it, and do nothing whatsoever on a
# machine that already exists — including every machine anyone is already using.
# The build would be green the entire time. That is the failure this file exists
# to avoid, and it is recorded in docs/PROJECT.md as open question 21.
#
# What happens instead: the image carries the LIST and the pinned revisions, a
# oneshot service installs them on the machine, and a timer retries if Flathub
# was unreachable. The version is still decided by the image, which is the
# property that mattered.

set -euo pipefail

source /ctx/lib.sh

group_start "Emulator flatpaks"

MANIFEST=/usr/share/cabinetos/flatpaks.list
SETUP=/usr/libexec/cabinetos-flatpak-setup

# The overlay in build.sh put these here. Asserted rather than assumed, for the
# reason build.sh gives at its own overlay check: a copy that silently does
# nothing leaves a green build. This repository has been bitten by exactly that.
for expected in "${MANIFEST}" "${SETUP}" \
    /usr/lib/systemd/system/cabinetos-flatpak-setup.service \
    /usr/lib/systemd/system/cabinetos-flatpak-setup.timer
do
    if [[ -s "${expected}" ]]; then
        log "  ok: ${expected}"
    else
        log "  ERROR: ${expected} is missing or empty"
        exit 1
    fi
done

if [[ ! -x "${SETUP}" ]]; then
    log "  ERROR: ${SETUP} is not executable"
    exit 1
fi

# flatpak itself has to survive the strip. It is not removed by any list in this
# repository, but `plasma-discover-flatpak` is, and a future removal that
# cascaded into flatpak would leave this whole mechanism silently inert — the
# service would run, log "flatpak is not installed", exit 0, and no emulator
# would ever appear. Fail here instead.
if ! command -v flatpak >/dev/null 2>&1; then
    log "  ERROR: flatpak is not in the image; the setup service cannot work"
    exit 1
fi
log "  ok: flatpak ($(flatpak --version))"

# ---------------------------------------------------------------------------
# The manifest has to parse, and its commits have to look like commits.
# ---------------------------------------------------------------------------
#
# A typo in a 64-character hex string is not visible by eye and would only show
# up as a failed pin on a real machine, hours later, in a log nobody reads.
log "  checking manifest"
python3 - "${MANIFEST}" <<'PY'
import re, sys

path = sys.argv[1]
seen, apps, runtimes, bad = set(), 0, 0, 0

for n, raw in enumerate(open(path), 1):
    if raw.lstrip().startswith('#'):
        continue
    # The console reads each line with `read -r kind ref branch commit` and
    # skips only a line that starts with #, so a comment after the fields would
    # land in the commit: the pin fails on every console, every 6 hours.
    if '#' in raw:
        print(f"    ERROR line {n}: a comment after the fields; the console would read it into the commit"); bad += 1; continue
    line = raw.strip()
    if not line:
        continue
    parts = line.split()
    if len(parts) != 4:
        print(f"    ERROR line {n}: expected 4 fields, got {len(parts)}"); bad += 1; continue
    kind, ref, branch, commit = parts
    if kind not in ("app", "runtime"):
        print(f"    ERROR line {n}: kind must be app or runtime, got '{kind}'"); bad += 1
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*", ref) or ref.count(".") < 2:
        print(f"    ERROR line {n}: '{ref}' is not a flatpak application id"); bad += 1
    if not re.fullmatch(r"[0-9a-f]{64}", commit):
        print(f"    ERROR line {n}: '{commit}' is not a 64-character commit"); bad += 1
    # Once per branch: the graphics driver is pinned on two (25.08 and
    # 25.08-extra), and the console names every ref with its branch.
    if (ref, branch) in seen:
        print(f"    ERROR line {n}: '{ref}//{branch}' listed twice"); bad += 1
    seen.add((ref, branch))
    if kind == "app":
        apps += 1
    else:
        runtimes += 1

if bad:
    sys.exit(f"    manifest has {bad} problem(s)")
if apps == 0:
    sys.exit("    manifest lists no applications")
if runtimes == 0:
    # Pinning apps without their runtimes was considered and rejected: two
    # machines installing the same image a month apart would get different
    # libraries under the same emulator. See the manifest's header.
    sys.exit("    manifest pins no runtimes; see the header for why that matters")

print(f"    ok: {apps} application(s), {runtimes} runtime(s), all pinned")
PY

# ---------------------------------------------------------------------------
# Enable the units.
# ---------------------------------------------------------------------------
#
# /usr is read-only at runtime, so this has to happen now: a unit not enabled
# here cannot be enabled on the machine.
systemctl enable cabinetos-flatpak-setup.service
systemctl enable cabinetos-flatpak-setup.timer

for unit in cabinetos-flatpak-setup.service cabinetos-flatpak-setup.timer; do
    if systemctl is-enabled "${unit}" >/dev/null 2>&1; then
        log "  enabled: ${unit}"
    else
        log "  ERROR: ${unit} did not enable"
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# The masked timers this design depends on.
# ---------------------------------------------------------------------------
#
# strip-desktop.sh masks these so nothing updates a flatpak, or the image that
# names the pins, on its own schedule. That is not a tidiness measure here — it
# is what makes a pinned commit STAY pinned, and what keeps the console from
# restarting into an update nobody asked for. If a future change or a base bump
# turned one on, this file's promise would quietly stop being true.
#
# `systemctl is-enabled` EXITS NON-ZERO FOR A MASKED UNIT while still printing
# "masked", so the obvious `|| echo absent` appends a second line and the
# comparison never matches. That is what failed this build the first time it
# ran. Capture the output, ignore the status, and take the first line.
for unit in flatpak-system-update.timer uupd.timer rpm-ostreed-automatic.timer \
    bootc-fetch-apply-updates.timer
do
    state=$(systemctl is-enabled "${unit}" 2>/dev/null | head -n1) || true
    state="${state:-not-found}"

    case "${state}" in
        masked|masked-runtime)
            log "  ok: ${unit} is ${state}, so nothing moves a pin behind us"
            ;;
        not-found)
            # Nothing to drift. Bazzite could drop the unit entirely and that
            # would be fine; what matters is that it cannot run.
            log "  ok: ${unit} is not present at all"
            ;;
        enabled|enabled-runtime|alias|indirect)
            log "  ERROR: ${unit} is '${state}' — it would update the console on"
            log "         its own schedule, apart from the player and the pins."
            log "         See docs/PROJECT.md open question 21, and strip-desktop.sh."
            exit 1
            ;;
        *)
            # disabled, static, generated and friends cannot start on a timer of
            # their own, so they are not the hazard this guards against. Logged
            # rather than failed, because failing here on a harmless state would
            # break the build every time Bazzite changed how it ships the unit.
            log "  ok: ${unit} is '${state}' — cannot run unattended"
            ;;
    esac
done

group_end
