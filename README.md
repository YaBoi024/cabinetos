<p align="center"><img src="docs/assets/mark.svg" alt="" width="72"></p>

<h1 align="center">CabinetOS-handheld</h1>

<p align="center"><b>Your whole game library, on a handheld, as a console.</b><br>
35 systems, from the Atari 2600 to the Switch, played from your own
<a href="https://github.com/rommapp/romm">RomM</a> server, up to four players at once locally.</p>

<p align="center">
<a href="https://cabinetos.mmagtech.com/"><b>See it in action</b></a> ·
<a href="https://cabinetos.mmagtech.com/wiki/">Wiki</a> ·
<a href="docs/wiki/install.md">Install</a> ·
<a href="docs/wiki/systems.md">Systems and BIOS</a>
</p>

CabinetOS-handheld turns a handheld like a Steam Deck into a games console. It starts straight into its own menus, and from there a controller does everything. Games, covers, BIOS and
saves all live on your RomM server, so the console is only ever a screen away
from your whole library.

- **Pick up where you left off.** Saves and save states go to RomM by
  themselves, and every game page shows your last three states with a picture.
- **Every system tuned.** An emulator chosen and set up for each of 35 systems,
  rendering up to 4K, with one pause menu in every game.
- **Make it look right.** CRT and LCD looks per system, and sixteen colours for
  the menus, bright ones included.
- **Everyone their own.** Each person signs in with a QR code and gets their own
  saves, favorites, play time, achievements and color. A PIN guards the rest.
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

## What you need

- **A handheld** Currently testing on Steam Deck
- **A RomM server** 5.1 or newer, with your games, BIOS and firmware.

## Thanks

CabinetOS stands on other people's work.

- **[RomM](https://github.com/rommapp/romm)** keeps the library: every game,
  cover, BIOS and save the console shows comes from it. Without RomM there is
  no CabinetOS.
- **[CabinetOS](https://github.com/MMagTech/cabinetos)** made this idea possible
  and did all the hard work
- **[Bazzite](https://github.com/ublue-os/bazzite)** and
- **[Universal Blue](https://universal-blue.org)** are the system underneath:
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

CabinetOS-handheld ships no games, no BIOS and no keys. All of them come from your own
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
- [`docs/PROJECT.md`](docs/PROJECT.md): the specification.
- [`docs/ROADMAP.md`](docs/ROADMAP.md): the order of work.
- [`docs/lessons/`](docs/lessons/README.md): what cost hours before.

CabinetOS-handheld is a [bootc](https://) image on Bazzite-handheld.
**Nothing builds on a Mac**: every image is built by GitHub Actions, and the
console's own frontend is in [`frontend/`](frontend/).

The showcase page and the wiki are in [`docs/`](docs/) (`index.html` and
`wiki/`), served by GitHub Pages from `main`. Changes under `docs/` never build
an image, except `docs/LICENCES.md`.
