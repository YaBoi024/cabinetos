# Fork changes

A fork of [CabinetOS](https://github.com/MMagTech/cabinetos) by MMagTech,
adapted for the Steam Deck. This lists every deliberate difference from
upstream. When a sync conflicts, use it to decide which side to keep.

## Base image
- `Containerfile` builds on `ghcr.io/ublue-os/bazzite-deck`, pinned by tag
  and digest, for Deck hardware support (speaker tuning, built-in controls,
  power controls, OLED panel correction).

## Workflows
- Added `.github/workflows/watch-upstream.yml` and `.github/upstream-watch.txt`
  (daily check of upstream; opens an issue when watched files change).
- `base-update.yml`: [pointed at bazzite-deck]

## Updates
- Created /system_files/usr/lib/systemd/system/cabinetos-update-download.service.d/10-selinux.conf to adjust SELinux for successful updates.
- Created system_files/usr/lib/systemd/system/cabinetos-session.service.d/10-audio.conf, it allows audio to swap from dock (tested on HDMI) to internal speaker whenever going from docked to handheld.
- players.cpp: added: a connected external pad takes player 1 ahead of the Deck’s built-in controls
- Upstream safety: more watch paths, drop-in target check for deck related things against the main CabinetOS

## Tested on
- Steam Deck OLED: tested.
- Steam Deck LCD: not tested.

## Known issues
- First boot after install: Eden installs in about two minutes and needs
  internet; audio and Eden appear after the first reboot. (May be fixed, need to verify)
- Touchscreen does not work in CabinetOS menus.

## To do
- Gyro working
- Check back buttons, trackpads, rumble, sleep, battery
- Touchscreen in CabinetOS menus (low priority)
- Test on a Steam Deck LCD
- No multiplayer yet (wishful thinking) by me (on emulators that support it)
