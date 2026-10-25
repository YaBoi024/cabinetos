# The CabinetOS frontend

The C++20 program that draws every screen of CabinetOS and hosts its emulators.
Why it is built this way: [docs/PROJECT.md](../docs/PROJECT.md), "The design
system", "How Cabinet hosts cores" and "The frontend toolkit".

## Building

**In the `cabinetos-builder` container, never directly on a Mac or a
console** (the image carries no compiler): on any Linux x86-64 machine with
podman, a CabinetOS console over Developer access included, or in CI. From
this folder:

```bash
podman build -t cabinetos-builder -f Containerfile .
podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make
```

The result is `build/cabinetos-frontend`. `Containerfile` pins Fedora 44, which
is what Bazzite 44 is built from, so glibc and every runtime library match the
image the binary runs on. **If the base image's Fedora release moves, move that
with it.**

## Where it puts things

Everything the console holds lives under one root, the first of these that
applies: `--storage-root <path>`, then `$CABINETOS_STORAGE`, then
`/var/lib/cabinetos` if it can be created and written (a console), then the
folder the program was started in. The one chosen is printed at start-up as
`[storage] root <path>`.

```
<root>/
├── roms/<platform>/<romId> - <name>     kept games
├── cache/<platform>/<romId> - <name>    pulled games, the only thing eviction touches
├── bios/                                firmware from RomM, and the cores' system directory
├── games/                               what the console knows of each game on its drives
├── covers/<server>/                     cover art, kept so it is not fetched at every start
├── emulators/<name>/                    each separate emulator's own files, and Dolphin's cache
├── wii-remote-miis/                     the Wii bridge's Miis
├── users/<id> - <name>/
│   ├── saves/<platform>/<romId>/<core>/
│   ├── states/<platform>/<romId>/<core>/
│   ├── screenshots/<romId>/
│   ├── keeps/                           which games this person kept
│   ├── pending/                         what has not reached RomM yet
│   ├── controls/<romId> - <name>.json   an arcade game's buttons as this person moved them (Controls)
│   └── server-saves/<romId>/            RomM's saves as last seen, for play offline
├── config/                              settings, the server address, the accounts list
├── logs/                                a separate emulator's output, when it keeps no log of its own
└── reports/                             the last five diagnostic reports
```

A games drive gets its own `roms/` and `cache/`, in a `CabinetOS/` folder on
the drive. `<platform>` is RomM's `fs_slug` (`Sony Playstation`, `Game Boy`,
`FBNEO`), the spelling RomM uses for its own folders. A game is a file when it
is one file, and a folder when its archive unpacked into several.
`users/0 - local/` holds the saves of a game started with `--core`, which has
no person. Not under the root: the sign-in tokens and the PIN
(`~/.config/cabinetos/`, `src/accounts.cpp`) and Remote Play's files
(`/var/lib/cabinetos/remoteplay/`, `src/remoteplay.cpp`). The layout is
`src/storage.h`; the decision is docs/PROJECT.md, open question 18.

Running, testing and the flags are in [CONTRIBUTING.md](../CONTRIBUTING.md),
"Running and testing"; the rest of this file as it stood in September 2026 is
[docs/history/frontend-readme-2026-09.md](../docs/history/frontend-readme-2026-09.md).
