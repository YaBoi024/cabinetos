<p align="center"><img src="docs/assets/mark.svg" alt="" width="72"></p>

<h1 align="center">CabinetOS Handheld</h1>

<p align="center"><b>Your whole game library, on a handheld, as a console.</b><br>
35 systems, from the Atari 2600 to the Switch, played from your own
<a href="https://github.com/rommapp/romm">RomM</a> server, up to four players at once locally.</p>

<p align="center">
<a href="docs/wiki/README.md"><b>Wiki</b></a> ·
<a href="docs/wiki/install.md">Install</a> ·
<a href="docs/wiki/systems.md">Systems and BIOS</a> ·
<a href="https://cabinetos.mmagtech.com/">The original CabinetOS in action</a>
</p>

CabinetOS Handheld turns a handheld like a Steam Deck into a games console. It starts straight into its own menus, and from there a controller does everything. Games, covers, BIOS and
saves all live on your RomM server, so the console is only ever a screen away
from your whole library.

It is a fork of [CabinetOS](https://github.com/MMagTech/cabinetos) by MMagTech,
adapted for handhelds (tested on Steam Deck OLEDs for now).

- **Pick up where you left off.** Saves and save states go to RomM by
  themselves, and every game page shows your last three states with a picture.
- **Every system tuned.** An emulator chosen and set up for each of 35 systems,
  rendering up to 4K, with one pause menu in every game.
- **Make it look right.** CRT and LCD looks per system, and sixteen colors for
  the menus, bright ones included.
- **Everyone their own.** Each person signs in with a QR code and gets their own
  saves, favorites, play time, achievements and color. A PIN guards the rest.
- **Four players, real Wii Remotes**, and your Miis in every Wii game.
- **Play from your phone** with Moonlight, at home or away over Tailscale.
- **Your Steam games too.** One press hands the TV to Steam's Big Picture, and
  quitting Steam brings you back to CabinetOS' Home. Remote Play streams it to
  your phone or other devices with Moonlight.
- **Take it with you.** Away from your server it plays every game you
  downloaded and the ones you played recently; saves wait and go up when you
  are back.
- **Install once.** From then on it updates itself in one press, the whole
  system at once, checked against CabinetOS Handheld's signature.

## What you need

- **A handheld.** Currently testing on Steam Deck OLEDs.
- **A RomM server** 5.1 or newer, with your games, BIOS and firmware.

## Install

There is no installer for CabinetOS Handheld yet. For now, install the original
CabinetOS, then switch it to this image:

1. Install [CabinetOS](https://github.com/MMagTech/cabinetos) by MMagTech on
   the handheld. This erases the drive.
2. SSH into the handheld:

   ```bash
   sudo bootc switch ghcr.io/yaboi024/cabinetos:latest
   ```

3. Restart the handheld. It now runs CabinetOS Handheld and updates from this
   repository's image.

The full steps are in [`docs/wiki/install.md`](docs/wiki/install.md). A
bootable installer ISO is planned.

## Thanks

CabinetOS Handheld stands on other people's work.

- **[RomM](https://github.com/rommapp/romm)** keeps the library: every game,
  cover, BIOS and save the console shows comes from it. Without RomM there is
  no CabinetOS Handheld.
- **[CabinetOS](https://github.com/MMagTech/cabinetos)** by MMagTech, the
  project this is forked from, made this idea possible and did all the hard
  work.
- **[Bazzite](https://github.com/ublue-os/bazzite)** and
  **[Universal Blue](https://universal-blue.org)** are the system underneath:
  the kernel and drivers, gamescope, and the signed image updates CabinetOS
  Handheld is built from.
- **The emulator projects** (the libretro cores, PCSX2, Dolphin, PPSSPP, Cemu,
  Xenia and the rest) play the games. Each one is listed with its licence in
  [`docs/LICENCES.md`](docs/LICENCES.md).

Thank you to everyone who works on them.

## Credits and licence

CabinetOS Handheld is MIT licensed, like the CabinetOS source it comes from.
See [`LICENSE`](LICENSE).

**The emulators it ships are not, and some are free for non-commercial use
only.** [`docs/LICENCES.md`](docs/LICENCES.md) lists every one, its license,
its source and the exact version it was built from.

CabinetOS Handheld ships no games, no BIOS and no keys. All of them come from
your own RomM server.

The look comes from [CabinetOS](https://github.com/MMagTech/cabinetos), which
began as [Cabinet](https://github.com/MMagTech/cabinet), a retired Apple TV app.
The `Containerfile`, `Justfile`, `disk_config/` and `.github/workflows/` are
derived from the [Universal Blue image template](https://github.com/ublue-os/image-template)
(Apache-2.0).

---

## Tested hardware

- **Steam Deck OLED:** Working: audio, touchscreen, gyro and more.
  Not worked on yet: touchpads, and the L4, R4, L5 and R5 buttons.

## Non-Tested hardware
- **Steam Deck LCD:** not tested yet, testing soon. [Reports welcome.]
  (https://github.com/YaBoi024/cabinetos/issues/new/choose)
- **Other handhelds:** untested. CabinetOS Handheld is made for handhelds and
  more are planned, but there is no other hardware to develop and test on yet.
  [Reports welcome.](https://github.com/YaBoi024/cabinetos/issues/new/choose)

---

## Contributors

- **[YaBoi024](https://github.com/YaBoi024)**: creator and maintainer of
  CabinetOS Handheld.
- **[Claude](https://claude.com)** (Anthropic): AI assistant that helped write
  and test parts of the code and documentation.
- **[MMagTech](https://github.com/MMagTech)**: author of the original
  [CabinetOS](https://github.com/MMagTech/cabinetos) this is forked from.

---

## Working on CabinetOS Handheld

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): the map of the code and
  how its parts fit.
- [`FORK-CHANGES.md`](FORK-CHANGES.md): every deliberate difference from
  CabinetOS.

CabinetOS Handheld is a [bootc](https://containers.github.io/bootc/) image on
Bazzite (the `bazzite-deck` variant). **Nothing builds on a Mac**: every image
is built by GitHub Actions, and the console's own frontend is in
[`frontend/`](frontend/).

The showcase page and the wiki are in [`docs/`](docs/) (`index.html` and
`wiki/`), and can be published with GitHub Pages from `main`. Changes under
`docs/` never build an image, except `docs/LICENCES.md`.
