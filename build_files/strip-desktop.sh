#!/usr/bin/env bash
#
# Remove the desktop: the display manager, the desktop applications, and every
# obvious route to a terminal, a file browser, an app store or a web browser.
#
# READ THIS BEFORE ADDING TO THE LISTS BELOW.
#
# This script does NOT uninstall KDE Plasma itself. That is deliberate and it is
# open question 1 in docs/PROJECT.md. Short version:
#
#   - Bazzite versionlocks `plasma-*` and `qt6-*` on this base, which means it
#     treats them as load-bearing.
#   - Bazzite installs `plasma-foreground-booster-dmemcg` as part of its
#     process-priority handling for games.
#   - `xdg-desktop-portal-kde` is the portal implementation here.
#   - A cascading Plasma removal can produce an image that builds fine and does
#     not boot, and right now we have no VM test to catch that.
#
# What this script does instead is make the desktop unreachable: the default
# systemd target becomes multi-user.target, the display manager is removed and
# masked, and the desktop applications are gone. That satisfies "the user never
# sees a desktop" without betting the image on a large dependency cascade.
#
# Plasma stays installed: docs/PROJECT.md open question 1.

# ===========================================================================
#  DO NOT REMOVE — HDMI-CEC
# ===========================================================================
#
# These look like desktop or HTPC cruft and are not:
#
#   libcec, v4l-utils, linux-cec, linuxconsoletools
#   60-cec-*.rules, 60-inputattach-cec.rules, 99-cec-bluetooth.rules
#   cec-onboot/onsleep/onpoweroff/poweroff-tv/active-source units
#   pulse8-cec-inputattach@, rainshadow-cec-inputattach@
#
# Kept installed for HDMI-CEC, which is shelved (2026-09-24) but may come
# back. v4l-utils reads as a webcam package, linuxconsoletools as a joystick
# utility. build_files/require-cec.sh fails the build if any of them go
# missing: see it, and docs/PROJECT.md open question 31.

source /ctx/lib.sh

group_start "Removing the display manager and graphical login"

# The display manager is the thing that would otherwise put a login screen in
# front of the user. CabinetOS autologins into its own session (Phase 2).
#
# `sddm` is the traditional Kinoite display manager; `plasma-login-manager` is
# its replacement in newer Plasma. Both are listed because Bazzite has been
# moving between them and remove_pkgs skips whichever is absent.
remove_pkgs "no graphical login, CabinetOS autologins into its own session" \
    sddm \
    sddm-breeze \
    sddm-wayland-plasma \
    sddm-x11 \
    plasma-login-manager \
    gdm

# Masked as well as removed, so that nothing can pull a display manager back in
# as a dependency and so graphical.target cannot start one.
disable_units "no graphical login" \
    sddm.service \
    plasma-login-manager.service \
    gdm.service

# The default target. This is what makes the Phase 1 image boot to a console
# prompt rather than to a desktop, and it is the single most effective line in
# this file. Phase 2 replaces this with a CabinetOS session unit.
log "setting default systemd target to multi-user.target"
rm -f /usr/lib/systemd/system/default.target
ln -sf /usr/lib/systemd/system/multi-user.target /usr/lib/systemd/system/default.target

group_end

group_start "Removing desktop applications"

# App store. Explicit non-goal: "No app store."
remove_pkgs "no app store" \
    plasma-discover \
    plasma-discover-flatpak \
    plasma-discover-libs \
    plasma-discover-notifier \
    plasma-discover-packagekit \
    plasma-discover-rpm-ostree \
    gnome-software

# Web browser. Explicit non-goal: "No browser."
remove_pkgs "no browser" \
    firefox \
    firefox-langpacks \
    falkon \
    epiphany

# Terminal emulators. A terminal is a route to a Linux error message, which is
# a bug by the definition in docs/PROJECT.md.
remove_pkgs "no terminal" \
    konsole \
    konsole5 \
    yakuake \
    ptyxis \
    gnome-terminal \
    xterm

# File browser. Explicit non-goal.
remove_pkgs "no file browser" \
    dolphin \
    dolphin-plugins \
    nautilus \
    krusader

# Text editors, document viewers, media players, archive tools and the rest of
# the stock desktop application set. None of these have a place on a console,
# and all of them are leaves.
remove_pkgs "desktop applications, not console software" \
    kate \
    kwrite \
    okular \
    gwenview \
    ark \
    elisa \
    kcalc \
    kcharselect \
    kmines \
    kpat \
    kmahjongg \
    kolourpaint \
    spectacle \
    haruna \
    dragon \
    kmail \
    kontact \
    korganizer \
    kaddressbook \
    akregator \
    kwalletmanager5 \
    krfb \
    krdc \
    kdeconnectd \
    kde-connect \
    kde-connect-libs \
    neochat \
    thunderbird \
    libreoffice-core

# System administration GUIs. Anything that presents partitions, snapshots or
# services to a user is a computer, not a console.
#
# NOTE: `snapper` itself is kept — Bazzite uses it for the snapshot layer. Only
# the graphical front end goes.
remove_pkgs "no system administration UI" \
    btrfs-assistant \
    plasma-systemmonitor \
    ksystemlog \
    filelight \
    partitionmanager

# Cockpit is a web-based server admin console. Useful on a server, exactly the
# wrong idea on a console. Phase 6 adds an SSH developer mode instead.
remove_pkgs "no web admin console" \
    cockpit \
    cockpit-ws \
    cockpit-bridge \
    cockpit-networkmanager \
    cockpit-podman \
    cockpit-selinux \
    cockpit-system \
    cockpit-files \
    cockpit-storaged

# Waydroid is an Android runtime. Out of scope, large, and its service is
# already disabled by Bazzite.
remove_pkgs "no Android runtime" \
    waydroid \
    waydroid-selinux

# Bazzite's own first-run and welcome UI. CabinetOS has its own first run setup
# (Phase 8) and must not show Bazzite's.
remove_pkgs "CabinetOS owns first run, not Bazzite" \
    bazzite-portal \
    ublue-os-just \
    yafti

group_end

group_start "Disabling the automatic updaters"

# The update model is one version number for the whole system (docs/PROJECT.md),
# applied only when the player asks: /usr/libexec/cabinetos-update runs
# `bootc upgrade` itself and never starts any of these. `uupd` updates packages
# and Flatpaks independently of the image, which would create exactly the
# partial-version state that model exists to prevent; rpm-ostreed-automatic
# stages updates by its own policy (none today); flatpak-system-update would
# move the pinned emulators. bootc-fetch-apply-updates.timer is bootc's
# own: `bootc upgrade --apply` an hour after boot and every 8 hours, which
# restarts into the new image, possibly mid-game. Bazzite ships it disabled;
# masked here so a base that turned it on changes nothing.
# configure-flatpaks.sh fails the build if any of the four is enabled.
#
# Disabled rather than removed: `uupd` is a Bazzite integration point and
# removing it may have consequences we have not mapped. The timer is what does
# the damage.
disable_units "the player applies updates; no partial version state" \
    uupd.timer \
    rpm-ostreed-automatic.timer \
    flatpak-system-update.timer \
    bootc-fetch-apply-updates.timer

group_end

group_start "Removing Bazzite's drive automounters"

# THE CONSOLE IS THE ONLY THING THAT MOUNTS A DRIVE (frontend/src/drives.cpp,
# #236). Bazzite ships three automounters for desktops and handhelds, and each
# would take a drive away from the console or race it (read on the A9,
# 2026-10-04):
#
#   - ublue-os-media-automount.service mounts every labelled INTERNAL ext4 or
#     btrfs partition at boot, as root, under /run/media/system/. The console
#     looks in /run/media/cabinet/, so an internal ext4 drive it formatted
#     would be "already mounted by something else": not in Storage, no games.
#   - 99-steamos-automount.rules (SD cards) runs `fsck -y` on a card and
#     mounts it itself, rewriting udisks' mount options file while it does,
#     at the moment the console mounts the same card.
#   - 99-framework-steam-automount.rules does the same for a drive labelled
#     "steamgames".
#
# The cost: a card or drive plugged in while Steam is open is mounted when
# the console comes back, not inside Steam.
disable_units "the console mounts every drive itself" \
    ublue-os-media-automount.service
rm -f /usr/lib/udev/rules.d/99-steamos-automount.rules \
      /usr/lib/udev/rules.d/99-framework-steam-automount.rules

group_end

group_start "Removing the help pages"

# NO MANUAL PAGES (docs/decisions/2026-11-04-no-help-pages.md). They are for
# someone typing commands on a desktop, and nothing on a console reads them.
# The base rebuilds their index at every start,
# fedora-atomic-desktop-mandb-update.service, which is not in any package. It
# takes about a second on an NVMe drive and took 1 to 3.5 minutes on a USB
# stick, where a user manager started during it once ran out of systemd's 45 s
# (measured 2026-11-04). With no user manager, no sound server starts, so the
# console has no sound for that run. The man-db package goes as well, so a job
# renamed by a later base has nothing to run, and the base-update check's
# failed-service line shows it.
remove_pkgs "nothing on a console reads the help pages" \
    man-db \
    man-pages
disable_units "nothing on a console reads the help pages" \
    fedora-atomic-desktop-mandb-update.service
find /usr/share/man -mindepth 1 -delete

group_end

# A full Plasma removal was considered and not done: docs/PROJECT.md open
# question 1 (packages that never run cost nothing).
