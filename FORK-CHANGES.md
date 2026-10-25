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

## Tested on
- Steam Deck OLED: tested.
- Steam Deck LCD: not tested.

## Known issues
- The console's update screen fails under SELinux (`mac_admin` denied to
  the update service). Workaround: `sudo bootc upgrade` over SSH, then reboot.
- First boot after install: Eden installs in about two minutes and needs
  internet; audio and Eden appear after the first reboot.
- Touchscreen does not work in CabinetOS menus.
- No multiplayer yet (wishful thinking) by me.

## To do
- Test an SELinux fix for the update service when the next update is pending.
