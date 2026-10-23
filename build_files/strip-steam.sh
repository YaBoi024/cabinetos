#!/usr/bin/env bash
#
# Remove the PC-gaming storefront layer, keeping Steam itself for its one entry.
#
# CabinetOS is the emulation console; the library comes from RomM and games are
# launched by emulators we bundle. Since 2026-10-03 (issue #223) it also has
# ONE Steam entry that hands over to Steam's own Big Picture session and comes
# back (docs/PROJECT.md, Non goals, narrowed). So the `steam` package stays: it
# is a 20 MB bootstrapper that downloads and updates the real client into
# Steam's own slice of the disk the first time someone picks it, and does
# nothing at all for anyone who never does. The 32-bit libraries it needs stay
# with it (#201 closed as keep). Its session (gamescope-session and
# gamescope-session-steam) is a bazzite-deck package, not in our base, and is
# added by install-steam-session.sh.
#
# Lutris, the Windows-compatibility layer around it and Bazzite's own Steam
# wrappers still go: Steam brings its own Proton, and nothing else here
# launches Windows games.
#
# What is deliberately KEPT from Bazzite's gaming stack, and why:
#
#   steam                         The Steam entry (#223), above.
#   gamescope (terra-gamescope*)  The micro-compositor. Phase 2 runs the
#                                 frontend inside it, and Phase 5 runs emulators
#                                 as its children. This is the single most
#                                 important thing Bazzite gives us.
#   mangohud, vkBasalt            Vulkan layers. Useful for Phase 8 performance
#                                 work on PS2/GameCube. Small. Steam's session
#                                 also runs mangoapp for its overlay.
#   mesa (Valve-patched)          The graphics stack. Non-negotiable.
#   kmod-xone, kmod-gcadapter,    Controller drivers. xone is Xbox wireless;
#   kmod-new-lg4ff, kmod-hid-*    gcadapter is the GameCube adapter, which is
#                                 directly relevant to this project.
#   tuned / power-profiles-daemon Power and thermal handling.
#   ds-inhibit                    Stops controllers being treated as keyboards
#                                 for idle purposes.

source /ctx/lib.sh

group_start "Removing the PC gaming storefront layer (Steam itself stays, #223)"

# Bazzite's wrappers and desktop presets around Steam, not Steam itself (kept
# for #223, above). `steam-devices`, if it exists as a separate package,
# provides gamepad udev rules and must survive: see docs/PROJECT.md open
# question 2. This is why every removal uses --no-autoremove.
#
# gamescope-session-steam is NOT in this list any more: it is the session the
# Steam entry runs, installed after this by install-steam-session.sh. The
# OpenGamepadUI one is still removed: it is an overlay on top of Steam with
# its own input daemon, which the entry does not want.
remove_pkgs "Bazzite's Steam wrappers and deck presets" \
    steam-devices-non-free \
    bazzite-steam \
    steam-notif-daemon \
    gamescope-session-ogui-steam \
    steamdeck-backgrounds \
    steamdeck-gnome-presets \
    steamdeck-kde-presets \
    steamdeck-kde-presets-desktop

# cardwire picks which of two GPUs runs an app. Its daemon is masked
# (configure-session.sh: 6.8 s of boot), so it can do nothing here, and
# Steam's session launches Steam THROUGH it whenever /usr/bin/cardwire exists:
# `cardwire launch steam ...` then fails every time with "Cannot communicate
# with cardwired" and Steam never starts (audit, #223, question 2). Without
# it, upstream's script runs Steam directly, unmodified. A machine with two
# GPUs gets the one the system picks by default, as the emulators already do;
# if that is ever wrong it is fixed for the whole console (MMagTech,
# 2026-10-03). Nothing requires either package.
remove_pkgs "Steam's session would launch through it, and its daemon is off" \
    cardwire-gui \
    cardwire

# Other PC game launchers. Same reasoning: the library is RomM's.
remove_pkgs "no third-party game launchers" \
    lutris \
    heroic-games-launcher-bin \
    bottles

# Windows compatibility. CabinetOS runs emulators, not Windows games. umu is
# Valve's Proton launcher wrapper; winetricks is a Wine configuration helper.
remove_pkgs "no Windows compatibility layer" \
    umu-launcher \
    umu-wrapper

# Bazzite installs winetricks as a loose script rather than an RPM.
if [[ -f /usr/bin/winetricks ]]; then
    log "removing /usr/bin/winetricks (loose script, not an RPM)"
    rm -f /usr/bin/winetricks
fi

# Bazzite's loose Steam scripts: not owned by any package, and not used. The
# entry runs upstream's session directly; `bazzite-steam` calls cardwire and
# expects Bazzite's image info, and `switcherooctl` is a stub that prints a
# deprecation notice, which upstream's session would otherwise try as its
# second way to launch Steam. The two just files are inert (ujust is gone)
# and describe recipes for a desktop.
log "removing Bazzite's loose Steam scripts"
rm -f /usr/bin/bazzite-steam \
      /usr/bin/bazzite-steam-bpm \
      /usr/bin/bazzite-steam-brand \
      /usr/bin/bazzite-steam-firstrun \
      /usr/bin/protontricks \
      /usr/bin/protontricks-launch \
      /usr/bin/switcherooctl \
      /usr/share/ublue-os/just/94-bazzite-protonplus.just \
      /usr/share/ublue-os/just/95-bazzite-deck-session.just

# Steam's bootstrap tarball, shipped by bazzite-deck for gamescope-session.
# Never in our base; the `steam` package carries its own bootstrap, which
# unpacks into Steam's slice on first run.
if [[ -f /usr/share/gamescope-session-plus/bootstrap_steam.tar.gz ]]; then
    log "removing Steam bootstrap tarball"
    rm -f /usr/share/gamescope-session-plus/bootstrap_steam.tar.gz
fi

# Desktop entries for anything we just removed, Steam's own (nothing here
# reads a .desktop file; the entry is the console's tile), and Bazzite's
# autostart entry that launches Steam silently at login, which must never
# exist: Steam starts only from its tile (#223).
log "removing Steam/Lutris desktop entries and autostart"
rm -f /usr/share/applications/steam.desktop \
      /usr/share/applications/bazzite-steam-bpm.desktop \
      /usr/share/applications/net.lutris.Lutris.desktop \
      /etc/skel/.config/autostart/steam.desktop \
      /etc/xdg/autostart/steam.desktop

# ---------------------------------------------------------------------------
# Record what controller udev rules survived.
# ---------------------------------------------------------------------------
#
# This is the evidence for open question 2. If gamepads do not enumerate in the
# Phase 1 VM test, this listing in the CI log is where to look first.
group_end
group_start "Controller udev rules present after Steam removal"
find /usr/lib/udev/rules.d /etc/udev/rules.d -type f 2>/dev/null \
    | grep -iE "steam|joystick|gamepad|input|uaccess|xpad|60-|70-" \
    | sort || true
group_end

group_start "Controller kernel modules present after Steam removal"
rpm -qa 'kmod-*' | sort || true
group_end
