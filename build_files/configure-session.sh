#!/usr/bin/env bash
#
# Phase 2: make the machine boot into the CabinetOS session instead of a console.
#
# The session units themselves are in system_files/ and were copied into place
# by build.sh before this runs. This script enables them and closes the routes
# to a desktop that the strip pass left behind.

source /ctx/lib.sh

group_start "Configuring the CabinetOS session"

# Takes tty1 via Conflicts=getty@tty1.service, so there is no login prompt to
# fall back to. That is deliberate: a console that drops to a shell has failed.
log "enabling cabinetos-session.service"
systemctl enable cabinetos-session.service

# File access, Developer access, Remote Play and Tailscale, started at boot
# from Settings, so a console booted with no TV answering can still be
# reached while the session waits for one (#268).
log "enabling cabinetos-saved-services.service"
systemctl enable cabinetos-saved-services.service

# FORK (handheld): the bazzite-deck base ships tuned MASKED (seen on an OLED:
# `systemctl is-enabled tuned` says masked, and the console's every profile
# switch failed with "The name is not activatable"). Without it the profile
# switch in frontend/src/powerprofile.cpp never takes effect and the battery
# profiles Bazzite ships are never used. Unmask and enable it.
log "unmasking and enabling tuned.service"
systemctl unmask tuned.service
systemctl enable tuned.service

group_end

group_start "Closing the remaining routes to a desktop"

# Plasma's out-of-box wizard. It is inert today only because nothing starts a
# graphical session — the moment one existed, it would run. CabinetOS owns first
# run (Phase 8), so this must not be here.
remove_pkgs "CabinetOS owns first run, not Plasma" \
    plasma-setup

# Belt and braces: the unit gates itself on a plasma-setup-done flag file, so if
# some dependency drags the package back in, the flag stops the wizard anyway.
mkdir -p /etc/plasma-setup
touch /etc/plasma-setup/plasma-setup-done

# The only Wayland session the image offers is Plasma's. Removing the desktop
# file means nothing can select it, even if something reintroduced a display
# manager.
if [[ -f /usr/share/wayland-sessions/plasma.desktop ]]; then
    log "removing the plasma wayland session"
    rm -f /usr/share/wayland-sessions/plasma.desktop
fi

group_end

group_start "Disabling services a console has no use for"

# Measured on the first booted image: stopping these recovered 91 MB of 629 MB
# idle, 14%, and cardwired alone was 6.8s of the 14s userspace boot. See
# docs/PROJECT.md, "Measured behaviour".
#
# Deliberately NOT here: tuned (power and thermal management the project needs),
# firewalld (a security decision, not a performance one), uresourced and
# dmemcg-booster (Bazzite's game process-priority layer), ds-inhibit (stops
# controllers being treated as keyboards for idle purposes).
disable_units "84 MB; CabinetOS owns controller mapping in Phase 5" \
    input-remapper.service

# cardwire itself is removed since #223 (strip-steam.sh): Steam's session
# launched through it. The mask stays, for a base that brings it back.
disable_units "38 MB and 6.8s of boot" \
    cardwired.service

disable_units "no cellular modems on a console" \
    ModemManager.service

disable_units "no USB display adapters" \
    displaylink.service

disable_units "no Kerberos or NFS credentials" \
    gssproxy.service

disable_units "no portable home directories" \
    systemd-homed.service

disable_units "mains powered; nothing to monitor" \
    upower.service

group_end
