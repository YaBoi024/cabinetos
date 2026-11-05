<p align="center"><img src="docs/assets/mark.svg" alt="" width="72"></p>

<h1 align="center">CabinetOS</h1>

<p align="center"><b>Your whole game library, on the TV, as a console.</b><br>
35 systems, from the Atari 2600 to the Switch, played from your own
<a href="https://github.com/rommapp/romm">RomM</a> server, up to four players at once.</p>

<p align="center">
<a href="https://cabinetos.mmagtech.com/"><b>See it in action</b></a> ·
<a href="https://cabinetos.mmagtech.com/wiki/">Wiki</a> ·
<a href="docs/wiki/install.md">Install</a> ·
<a href="docs/wiki/systems.md">Systems and BIOS</a>
</p>

[![CabinetOS Home on the TV](docs/media/home.webp)](https://cabinetos.mmagtech.com/)

CabinetOS turns a small PC into a games console. It starts straight into its
own menus, and from there a controller does everything. Games, covers, BIOS and
saves all live on your RomM server, so the console is only ever a screen away
from your whole library.

- **Pick up where you left off.** Saves and save states go to RomM by
  themselves, and every game page shows your last three states with a picture.
- **Every system tuned.** An emulator chosen and set up for each of 35 systems,
  rendering up to 4K, with one pause menu in every game.
- **Make it look right.** CRT and LCD looks per system, and sixteen colours for
  the menus, bright ones included.
- **Everyone their own.** Each person signs in with a QR code and gets their own
  saves, favourites, play time, achievements and colour. A PIN guards the rest.
- **Four players, real Wii Remotes**, and your Miis in every Wii game.
- **Play from your phone** with Moonlight, at home or away over Tailscale.
- **Your Steam games too.** One press hands the TV to Steam's Big Picture, and
  quitting Steam brings you back to Home. Remote Play streams it to your phone.
- **Take it with you.** Away from your server it plays every game you
  downloaded and the ones you played recently; saves wait and go up when you
  are back.
- **Install once.** From then on it updates itself in one press, the whole
  system at once, checked against CabinetOS's signature, with the version
  before kept on the console.

| | | |
|---|---|---|
| ![The Library](docs/media/library.webp) | ![A game's page with three save states](docs/media/game.webp) | ![The pause menu](docs/media/pause-menu.webp) |
| ![Zero Gunner 2 on Dreamcast](docs/media/play-dreamcast.webp) | ![Deathsmiles in the arcade, CRT look](docs/media/play-arcade.webp) | ![Six of the sixteen colours](docs/media/colours-grid.webp) |

Every picture here was captured from a running console. The
[showcase page](https://cabinetos.mmagtech.com/) has the videos.

## Why CabinetOS exists

Gaming, the way I remember it, was a couch, a friend, and a controller each.
Resident Evil for the first time with the lights off. Talking trash. Punching
each other in the arm when somebody messed up. That's the feeling I keep
chasing.

The games were never the problem. I had the ROMs. I had the emulators. I just
spent more time getting them to work than playing them: BIOS files in the
wrong folder, a controller that wouldn't map, a desktop in the way of
everything. The part I loved kept getting buried under setup.

So I ground through all of it once, so it never gets in the way again. That's
CabinetOS. Turn it on, pick a game, play. No desktop, no terminal, no mapping
buttons, no hunting for files. Your library lives on your own RomM server,
your saves follow you, and every screen works from the couch with a
controller.

These days it's my kids on that couch. Same feeling.

*MMagTech*

## What you need

- **A small PC** with AMD Radeon graphics. CabinetOS is built and tested on the
  **GEEKOM A9 Max**.
- **A RomM server**, 5.1 or newer, with your games, BIOS and firmware.
- **A controller**, Bluetooth or USB, and a USB keyboard for the first run.
- **A TV.**

**Handhelds are not supported.** Bazzite has a separate image for handhelds;
CabinetOS builds on its desktop image and is a TV console, with a classic
controller layout. It may run on a handheld, but nothing handheld-specific is
tested or planned. The one exception is a Steam Deck inside Steam, which reads
the Deck's controls itself; even there, no fix is promised. Forks are welcome.

## Get started

The [wiki](https://cabinetos.mmagtech.com/wiki/) goes from a blank PC to
playing: [install](docs/wiki/install.md), [first run](docs/wiki/first-run.md),
[your RomM server](docs/wiki/romm.md), then everything else.

**[Download the installer](https://download.mmagtech.com/cabinetos.iso)**:
one file, `cabinetos.iso`, always the newest release
([what's new](https://cabinetos.mmagtech.com/wiki/changelog.html)).

**Try it without installing:** [run it from a USB drive](docs/wiki/portable.md).
It is an alpha, and your PC's own drives stay as they are.

## Thanks

CabinetOS stands on other people's work.

- **[RomM](https://github.com/rommapp/romm)** keeps the library: every game,
  cover, BIOS and save the console shows comes from it. Without RomM there is
  no CabinetOS.
- **[Bazzite](https://github.com/ublue-os/bazzite)** and
  **[Universal Blue](https://universal-blue.org)** are the system underneath:
  the kernel and drivers, gamescope, and the signed image updates CabinetOS is
  built from.
- **The emulator projects** (the libretro cores, PCSX2, Dolphin, PPSSPP, Cemu,
  Xenia and the rest) play the games. Each one is listed with its licence in
  [`docs/LICENCES.md`](docs/LICENCES.md).

Thank you to everyone who works on them.

## Credits and licence

CabinetOS's own source is MIT licensed. See [`LICENSE`](LICENSE).

**The emulators it ships are not, and some are free for non-commercial use
only.** [`docs/LICENCES.md`](docs/LICENCES.md) lists every one, its licence,
its source and the exact version it was built from. **CabinetOS is free, is not
sold, and takes no donations**: that is what keeps those emulators legitimate in
this build, and it is deliberate.

CabinetOS ships no games, no BIOS and no keys. All of them come from your own
RomM server.

CabinetOS's look comes from [Cabinet](https://github.com/MMagTech/cabinet),
the retired Apple TV app it began as. The `Containerfile`, `Justfile`, `disk_config/` and `.github/workflows/` are derived
from the [Universal Blue image template](https://github.com/ublue-os/image-template)
(Apache-2.0).

---

## Working on CabinetOS

- [`CONTRIBUTING.md`](CONTRIBUTING.md): start here. Building, testing, what
  CI checks, how a change reaches a console, and the rules.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): the map of the code and
  how its parts fit.
- [`docs/WORKING.md`](docs/WORKING.md): how the maintainer works: the loop,
  the testing branch, merges.
- [`docs/decisions/`](docs/decisions/README.md): what the project decided and
  why, one small file each.
- [`docs/PROJECT.md`](docs/PROJECT.md): the older record of decisions, frozen
  on 2026-10-23.
- [`docs/ROADMAP.md`](docs/ROADMAP.md): the order of work.
- [`docs/lessons/`](docs/lessons/README.md): what cost hours before.

CabinetOS is a [bootc](https://bootc-dev.github.io/bootc/) image on Bazzite.
**Nothing builds on a Mac**: every image is built by GitHub Actions, and the
console's own frontend is in [`frontend/`](frontend/).

The showcase page and the wiki are in [`docs/`](docs/) (`index.html` and
`wiki/`), served by GitHub Pages from `main`. Changes under `docs/` never build
an image, except `docs/LICENCES.md`.
