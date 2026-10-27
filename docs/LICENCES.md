# Licences

CabinetOS's own source is MIT. **Almost nothing else in the image is**, and this
page lists what is in there, where it came from, the exact commit it was built
from, and under what terms.

Modelled on Cabinet's `docs/licenses.md`, which had already solved this. The
commits below are **CabinetOS's**, not Cabinet's: eleven cores are pinned to a
different revision than Cabinet's iOS build shipped, and the pins here are the
ones `cores/build-core.sh` asserts at build time.

**CabinetOS ships no games and no BIOS files, with one exception: the Vectrex
system ROM** (its built-in game, Mine Storm, included), which the vecx core has
compiled into it and cannot load from anywhere else. Smith Engineering, which
owns the Vectrex's software, allowed it to be shared freely for non-commercial
use in 1992; the same non-commercial constraint as the six cores below covers
it. Everything else comes from the person's own RomM server at runtime and is
not redistributed here.

**The Vectrex sheets are GCE's too.** Every retail cartridge shipped with
a translucent printed sheet that sat in front of the screen; the console
draws each game's sheet over its picture (#224) from community-drawn
copies of them. The drawings are MIT; the designs are Smith Engineering's,
allowed for non-commercial use with the games, as above. They are not
public domain, and the permission is Jay Smith's to withdraw.

---

## The constraint this project is under, said plainly

**Six of the twenty-three cores are free for non-commercial use only.** Not GPL —
a different thing, and a stricter one. They cover Arcade, SNES, Genesis, Sega
CD, Master System, Game Gear, 32X and 3DO, which is a large slice of the
library.

> **CabinetOS is free, is not sold, and takes no donations. That is what keeps
> those cores legitimate in this build, and it is a deliberate constraint rather
> than an oversight.** Selling a machine with this image on it, charging for the
> image, or adding a sponsor button would break it.

Cabinet wrote the same paragraph about itself and it is worth repeating here
because **the risk is sharper for an operating system than for an app**: an OS
is something you can plausibly put on a box and sell, and this project has
already discussed changing hardware. The day money changes hands for a machine
carrying this image, six cores have to come out or be relicensed.

## GPL, and what it actually requires

Most of the rest is GPL, and GPL is not a restriction on use. Anyone may run it,
study it, modify it privately, and redistribute it — including commercially. The
obligation lands only on **distribution**: pass on the same freedoms, and make
the corresponding source available.

**The cores are `dlopen`ed rather than statically linked**, which is a narrower
coupling than Cabinet's build had, but the conservative reading is the same one
Cabinet stated about itself: the distributed image is a combined work under
those terms. The corresponding source for every GPL component is the upstream
commit named below, unmodified except where `cores/build-core.sh` says
otherwise, and that script is in this repository so anyone can reproduce the
binaries exactly. Two cores have been shown to build byte-identically on two
different machines.

**Source patches are part of this.** `cores/build-core.sh` applies four
in-flight patches: melonDS's unload flush, PPSSPP's shader-cache save and
CPU-engine log, and Azahar's, which gives games no network. They are visible in that script, they are applied at build time
rather than vendored, and they are the modifications this project makes to GPL
source.

---

## CabinetOS

| | |
|---|---|
| Licence | MIT |
| Source | This repository |

Covers `frontend/`, `cores/`, `build_files/`, `ci/` and `tools/`. Our
`Justfile`, `.github/workflows/` and `disk_config/` derive from
[ublue-os/image-template](https://github.com/ublue-os/image-template), which is
Apache-2.0.

## The base image

CabinetOS is built on [Bazzite](https://github.com/ublue-os/bazzite), itself
built on Fedora. Every package in the image keeps the licence it ships under
from those projects, and their terms are satisfied by that provenance and by the
credit given here and in the README. The base is pinned by digest in
`Containerfile`.

Two things for the Steam entry (#223) come from Bazzite's own package
sources rather than its base image: `gamescope-session` and
`gamescope-session-steam`, from terra, both MIT (OpenGamingCollective,
formerly ChimeraOS). The `steam` package itself is Valve's bootstrapper,
redistributed by Bazzite and Fedora under Valve's terms; the Steam client it
downloads, and everything Steam installs, are under the Steam Subscriber
Agreement and are never part of this image.

## Emulator cores

Each is built from upstream at the commit below by `cores/build-core.sh`, which
checks out that exact revision and asserts it, then reads the revision back out
of the finished `.so` and fails the build if it does not match.

| Core | Systems | Licence | Upstream | Commit |
|---|---|---|---|---|
| FinalBurn Neo | Arcade | **Non-commercial**, plus MAME's terms | [libretro/FBNeo](https://github.com/libretro/FBNeo) | `2444fbe3ddab` |
| MAME 2003-Plus | Arcade | **MAME 0.78 non-commercial** | [libretro/mame2003-plus-libretro](https://github.com/libretro/mame2003-plus-libretro) | `21256d24120b` |
| Snes9x | SNES | **Non-commercial** | [libretro/snes9x](https://github.com/libretro/snes9x) | `890b5d445538` |
| Genesis Plus GX | Genesis, Sega CD, Master System, Game Gear | **Non-commercial** | [libretro/Genesis-Plus-GX](https://github.com/libretro/Genesis-Plus-GX) | `a7985a9c4278` |
| PicoDrive | Sega 32X | **Non-commercial** | [libretro/picodrive](https://github.com/libretro/picodrive) | `733c711a477a` |
| Opera | 3DO | **Modified LGPL, non-commercial** (FreeDO terms; the LGPL's version is not stated, and the terms are only in the source headers) | [libretro/opera-libretro](https://github.com/libretro/opera-libretro) | `a501a278d057` |
| Flycast | Dreamcast, Naomi | GPL v2 | [flyinghead/flycast](https://github.com/flyinghead/flycast) | `a172e0001351` |
| PPSSPP | PSP | GPL v2 or later | [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp) | `c989c2553e10` |
| Dolphin | GameCube (and Wii, from the same core) | GPL v2 or later | [libretro/dolphin](https://github.com/libretro/dolphin) | `1a0f97270b70` |
| mupen64plus-libretro-nx (bundles GLideN64) | Nintendo 64 | GPL v2 | [libretro/mupen64plus-libretro-nx](https://github.com/libretro/mupen64plus-libretro-nx) | `f275caf4b2bf` |
| PCSX ReARMed | PlayStation | GPL v2 or later | [libretro/pcsx_rearmed](https://github.com/libretro/pcsx_rearmed) | `ba61a4fdee1f` |
| Beetle Saturn | Saturn | GPL v2 or later | [libretro/beetle-saturn-libretro](https://github.com/libretro/beetle-saturn-libretro) | `ed549bdac0e1` |
| Beetle PCE Fast | TurboGrafx-16, TurboGrafx-CD | GPL v2 or later | [libretro/beetle-pce-fast-libretro](https://github.com/libretro/beetle-pce-fast-libretro) | `2f623abd0332` |
| Beetle NeoPop | Neo Geo Pocket Color | GPL v2 or later | [libretro/beetle-ngp-libretro](https://github.com/libretro/beetle-ngp-libretro) | `a50d5ac288a8` |
| Beetle VB | Virtual Boy | GPL v2 or later | [libretro/beetle-vb-libretro](https://github.com/libretro/beetle-vb-libretro) | `83ed42608601` |
| FCEUmm | NES | GPL v2 or later | [libretro/libretro-fceumm](https://github.com/libretro/libretro-fceumm) | `236ccdfc911e` |
| Gambatte | Game Boy, Game Boy Color | GPL v2 | [libretro/gambatte-libretro](https://github.com/libretro/gambatte-libretro) | `d9d6cd06382d` |
| ProSystem | Atari 7800 | GPL v2 or later | [libretro/prosystem-libretro](https://github.com/libretro/prosystem-libretro) | `8a88014287c7` |
| Stella 2014 | Atari 2600 | GPL v2 | [libretro/stella2014-libretro](https://github.com/libretro/stella2014-libretro) | `4a7da82595d2` |
| melonDS | Nintendo DS | GPL v3 or later | [libretro/melonDS](https://github.com/libretro/melonDS) | `66b5d2634cd0` |
| DraStic FreeBIOS | DS BIOS replacement inside melonDS | BSD 2-clause | bundled in the fork above | `66b5d2634cd0` |
| vecx | Vectrex | GPL v3; **the Vectrex system ROM inside it**, Smith Engineering's non-commercial permission (above) | [libretro/libretro-vecx](https://github.com/libretro/libretro-vecx) | `8f671cc9d737` |
| mGBA | Game Boy Advance | MPL 2.0 | [libretro/mgba](https://github.com/libretro/mgba) | `e31759b24e7a` |
| Azahar | Nintendo 3DS | GPL v3: most of its files are GPL v2 or later, and some it is built from are GPL v3 or later (`src/core/core.cpp` among them), though the repository's licence file is the GPL v2's. **Modified**: each game sets its own folders (the patch is in `cores/build-core.sh`), and built without Azahar's compiled-in blob of the 3DS's keys | [azahar-emu/azahar](https://github.com/azahar-emu/azahar) | `219d743a1e69` (release 2126.2) |

**Not carried from Cabinet:** `gw-libretro` (Game & Watch) and `vemulator`
(Dreamcast VMU minigames) were iOS-only in Cabinet by decision and are not built
here. See `frontend/src/catalog.cpp`.

## Files shipped inside the image that are not cores

| What | Where it comes from | Licence |
|---|---|---|
| **The screen looks** (#122), `/usr/share/cabinetos/shaders/` | RetroArch's GLSL shaders from [libretro/glsl-shaders](https://github.com/libretro/glsl-shaders) at `435612fe4f10`, unmodified, read at run time and never compiled into the frontend; `frontend/data/shaders/`, fetched by `tools/fetch-shaders.py` | Each file's own: **public domain** for crt-lottes (Timothy Lottes), lcd3x (Gigaherz) and sharp-bilinear-simple; **GPL** for crt-easymode and its halation version (EasyMode), crt-geom (cgwg, Themaister, DOLLS), zfast-crt and zfast-lcd (Greg Hogan), crt-aperture (EasyMode), crt-guest-dr-venom (guest.r), the Game Boy dot-matrix (Harlequin) and its colour version (LuigiRa, GPL v3 or later); crt-guest-dr-venom's colour tables (LUTs) are torridgristle's, with no licence stated. lcd-grid-v2 (cgwg) and some helper passes of halation and guest carry no header; cgwg distributes his shaders under the GPL (his note in crt-geom), and the helpers are parts of GPL works, so they are treated as GPL |
| **The Vectrex screen sheets** (#224), `/usr/share/cabinetos/vectrex/` | the `Game Overlay` folder of [libretro/overlay-borders](https://github.com/libretro/overlay-borders) (`1080 GCE Vectrex`) at `385490fa8`, 38 PNG files, unmodified, fetched at build time by `tools/fetch-vectrex-overlays.py` (as the shaders are); which sheet belongs to which game is this project's own table, `frontend/data/vectrex-overlays.json`, after Cabinet's | **MIT** for the drawings, which are vector recreations by THK and Gigapig of the HyperSpin community, cut for RetroArch by thatman84 (credited in each file's `.cfg`, and the attribution libretro asked for in its issue #6). **The designs underneath are GCE's, 1982 to 1984, and belong to Smith Engineering**, under the same non-commercial permission as the system ROM (above); ten sheets carry the titles and marks of the games' licensors (Paramount, Namco, Atari, Konami, Cinematronics) as the printed sheets did |
| **PPSSPP system files** — fonts, VFPU tables, `compat.ini`, the atlases | PPSSPP's own `assets/`, installed by `cores/build-core.sh` | GPL v2 or later, as PPSSPP (the PSP fonts carry no licence of their own; the Japanese and Latin ones were replaced in 2020 by a PPSSPP contributor's) |
| **FFmpeg**, statically linked inside PPSSPP | the prebuilt `ffmpeg/linux/x86_64` in PPSSPP's own tree | LGPL v2.1 or later |
| **Noto Sans** and Noto Sans CJK, the interface type | already in the Bazzite base; nothing is bundled | SIL Open Font License 1.1 |
| **The Wii bridge**, `/usr/libexec/cabinetos-wii-bridge` | this repository's `wiibridge/`, a program of its own that the console app only starts; it carries Dolphin's extension encryption (`encryption.cpp`/`.h`) from `libretro/dolphin` at `1a0f97270b70`, credited in each file | GPL v2 or later, as Dolphin |
| **PCSX2**, linked into `cabinetos-ps2.so` | upstream `PCSX2/pcsx2` at v2.8.2, built by `cores/build-pcsx2.sh` | GPL v3 or later |
| **PCSX2's resources** — `GameIndex.yaml`, the Redump database, fonts, GS shaders | PCSX2's own `bin/resources` | GPL v3 or later, as PCSX2, except its fonts and flags: Roboto (Apache 2.0), Twemoji (Apache 2.0 code, CC BY 4.0 artwork, credited in its licence file), promptfont (OFL 1.1), the flag icons (MIT); `fa-solid-900.ttf` (Font Awesome) carries no licence file at that version |
| **PCSX2's game patches**, `patches.zip` | built by `cores/build-pcsx2.sh` from `PCSX2/pcsx2_patches` at `9f82a4d2b8a2`, as PCSX2's own releases carry it | none stated: the repository has no licence file. Community-written patches, distributed by PCSX2 with every release |
| **rapidyaml** and **c4core**, carried beside the emulator | Fedora's packages, copied because the Bazzite base lacks them | MIT |
| **RPCS3**, in `/usr/lib/cabinetos/rpcs3` | the RPCS3 team's official Linux build `0.0.42-20076-1707d7fc` (release `build-1707d7fc…` of `RPCS3/rpcs3-binaries-linux`), unmodified, pinned by checksum in `build_files/install-rpcs3.sh`; source is `RPCS3/rpcs3` at commit `1707d7fc883ef48ff21bdcbb0141a3211ae09cb2` | GPL v2 |
| **The libraries RPCS3's build carries** (Qt 6, FFmpeg, SDL 3, OpenCV, OpenAL and others, in its `usr/lib`) | bundled by the RPCS3 team in the same build, unchanged | each under its own licence, as RPCS3 ships them (Qt LGPL v3, FFmpeg LGPL v2.1 or later, SDL zlib) |
| **xemu's blank Xbox hard drive**, `/usr/share/cabinetos/xemu/xbox_hdd.qcow2` | `xbox_hdd.qcow2` from `xemu-project/xemu-dashboard` release `v20260516-0955`, unmodified, pinned by checksum in `build_files/install-xemu-drive.sh`. It holds the open-source xemu-dashboard and nothing of Microsoft's | MIT, with the libraries the dashboard is built from (nxdk and others) under their own licences, as xemu-dashboard ships them |
| **The libraries compiled into Azahar's core** (Boost, dynarmic, xbyak, fmt, Crypto++, LibreSSL, zstd, teakra, faad2, SoundTouch, glslang, SPIRV-Tools, sirit, Vulkan Memory Allocator, xxHash, inih and others), and its open-source stand-ins for the 3DS's system archives (the shared font and the bad-word list among them, generated by B3n30/citra_system_archives) | Azahar's own submodules and `externals/` at the core's pinned commit, linked statically by its own CMake, unchanged | each under its own licence, as Azahar's own builds carry them |
| **Dolphin's `Sys` folder** — per-game settings, the Wii's `shared2` files, shaders, the cheat code handler | Dolphin's own `Data/Sys` at the core's pinned commit, installed by `cores/build-core.sh` | GPL v2 or later, as Dolphin, except the GameCube fonts (Droid Sans, Apache 2.0, `GC/font-licenses.txt`) |
| **The Xbox EEPROM**, `/usr/share/cabinetos/xemu/eeprom.bin` | made once by this project's `tools/xbox-eeprom.py`, the way xemu makes one | MIT, as CabinetOS |
| **Xenia Edge**, in `/usr/lib/cabinetos/xenia` | its developer's official Linux build, release `a7c39fa` of `has207/xenia-edge` (`xenia_edge_linux.AppImage`), unmodified, pinned by checksum in `build_files/install-xenia.sh`; source is `has207/xenia-edge` at commit `a7c39fa7d9c54d83022e431da62c1c65bf3e6196` | BSD 3-Clause |
| **The libraries Xenia Edge's build carries** (GTK 3, GLib, cairo, SDL 3, X11 libraries and others, in its `usr/lib`) | bundled by Edge's own AppImage build, unchanged | each under its own licence, as Edge ships them (GTK, GLib and cairo LGPL, SDL zlib, the X11 libraries MIT) |
| **Vita3K**, in `/usr/lib/cabinetos/vita3k` | its developers' official Linux build, numbered build `4210` of `Vita3K/Vita3K-builds` (`Vita3K-x86_64.AppImage`), unpacked without its launcher and unmodified apart from one link naming its own Vulkan loader, pinned by checksum in `build_files/install-vita3k.sh`; source is `Vita3K/Vita3K` at commit `649d7d69b2da2eb55f17233376f861a3a32cf12a` | GPL v2 or later |
| **The libraries Vita3K's build carries** (glibc, Qt 6, FFmpeg with x264 and Xvid, Mesa's Vulkan drivers, the X11 and Wayland libraries and others, in its `lib`; SDL 3 is linked into the program) | bundled by Vita3K's own AppImage build, unchanged | each under its own licence, as Vita3K ships them (glibc LGPL v2.1 or later, Qt LGPL v3, FFmpeg LGPL v2.1 or later, x264 and Xvid GPL v2 or later, Mesa MIT, SDL zlib, the X11 libraries MIT) |
| **Cemu**, in `/usr/lib/cabinetos/cemu` | built from source by `cores/build-cemu.sh`, unmodified, `cemu-project/Cemu` at commit `4e3c824faa00f6b85782db019f20f29f063f3a2a`, with Cemu's own game profiles and resources from the same commit | MPL 2.0, except its shared fonts (Noto Sans CJK, OFL 1.1) |
| **The libraries compiled into Cemu** (wxWidgets, Boost, fmt, SDL 3, OpenSSL, curl, glslang, pugixml, libzip, zstd and others) | built by Cemu's own vcpkg recipe at the versions its pinned commit names, linked statically, unchanged | each under its own licence (wxWidgets licence, Boost licence, fmt MIT, SDL zlib, OpenSSL Apache 2.0, curl MIT-style, glslang BSD-style, pugixml MIT, libzip BSD 3-Clause, zstd BSD) |
| **Sunshine** (Remote Play), `/usr/bin/sunshine` and `/usr/share/sunshine` | built from source by `cores/build-sunshine.sh` with LizardByte's own Fedora 44 recipe, `LizardByte/Sunshine` at tag `v2026.914.233613`, **modified**: our patches in `cores/sunshine-patches/` (#304); `build_files/install-sunshine.sh` removes only the file capability the package sets and its own service and launchers | GPL v3 |
| **The libraries compiled into Sunshine** (FFmpeg, and x264 for its CPU encoder, which the console does not use) | linked statically by LizardByte's build recipe, unchanged | each under its own licence (FFmpeg LGPL 2.1 or later / GPL, x264 GPL v2 or later) |
| **Tailscale** (Remote Play away from home), `/usr/bin/tailscale` and `/usr/sbin/tailscaled` | already in the Bazzite base (1.102.4); nothing is bundled, only its service given CabinetOS's settings | BSD 3-Clause |
| **miniupnpc**, Sunshine's one library the base lacks | Fedora 44's package, installed as Sunshine's dependency | BSD 3-Clause |
| **The Remote Play tile**, `/usr/share/cabinetos/remoteplay/cabinetos.png` | drawn by this project's `tools/make-remoteplay-tile.py` (the boot logo's cabinet, redrawn), the name set in Noto Sans | MIT, as CabinetOS; Noto Sans is OFL 1.1, which places no condition on a picture drawn with it |

PSP is the only platform whose "firmware" ships with the emulator rather than
coming from RomM. Every other system's BIOS is fetched at runtime from the
person's own server and none is redistributed.

**PLAYSTATION 2 IS NOT A LIBRETRO CORE and is listed here rather than in the
table above.** It is the whole PCSX2 emulator built as a static library, with
CabinetOS's own host layer compiled against it and the two linked into one
shared object. That makes `cabinetos-ps2.so` a combined work under the GPL v3,
and this project's own sources are offered under the same terms — see
*CabinetOS* above. **There is one patch**, three lines in
`AudioStream::CreateStream` to stop PCSX2 opening a sound device of its own; it
lives in `cores/build-pcsx2.sh` where anyone can read it, which is what GPL v3
section 5 asks of a modified version.

**PLAYSTATION 3 IS A WHOLE PROGRAM, NOT A CORE**, run beside the frontend
rather than inside it. It is the RPCS3 team's own build, shipped exactly as
they publish it, and only unpacked from its AppImage; nothing in it is patched.
**XBOX 360 IS A WHOLE PROGRAM TOO**, Xenia Edge's own build, shipped as its
developer publishes it and only unpacked; nothing is patched. The profile file
the console writes for it (`frontend/src/x360profile.cpp`) is encrypted the
way Edge encrypts its own, with the key Edge publishes in its BSD-licensed
source; no game, BIOS or firmware is involved, and Xbox 360 needs none.
**PLAYSTATION VITA IS A WHOLE PROGRAM TOO**, Vita3K's own numbered build,
shipped as its developers publish it and only unpacked, with a link added so
the SDL inside it finds the Vulkan loader it carries; nothing is patched. No
firmware ships: the Vita's system software and fonts come from the person's
RomM, and Vita3K installs them on the console.
**WII U IS A WHOLE PROGRAM TOO**, Cemu built here from its own source at a
pinned commit by its own recipe, with nothing patched; MPL 2.0 asks that its
source be available, and it is, at that commit. The console reads a `.wua`'s
table of contents itself (`frontend/src/wiiu.cpp`) to find a game's product
code; that is this project's own code, written from the format's description
in Exzap/ZArchive (MIT No Attribution). No game, firmware or key is involved.
**REMOTE PLAY IS A WHOLE PROGRAM TOO**, Sunshine, built by LizardByte's own
Fedora recipe at the pinned tag, with our changes (#304). GPL v3 asks that its
source be available, modified as shipped: it is LizardByte's tag plus the
patches in `cores/sunshine-patches/`, both public, and `cores/build-sunshine.sh`
builds exactly that. Each change is offered upstream. The settings and the
one entry the console writes for it (`/usr/libexec/cabinetos-remoteplay`)
are configuration, not code of Sunshine's.
Switch (Eden) and Xbox (xemu) are not in the image at all: each console
installs them from Flathub, which distributes them (open question 21). Only
xemu's blank drive and the EEPROM are in the image, in the table above; the
Xbox BIOS and boot ROM come from the person's own RomM, like every other
system's.

## Libraries the frontend links

All are present in the Bazzite base rather than vendored here, so the image
carries them under the terms Fedora ships them with.

| Library | Used for | Licence |
|---|---|---|
| SDL3 | Window, input, audio | zlib licence |
| Mesa (EGL, GLES) | The graphics context | MIT |
| FreeType | Text rasterising | FTL or GPL v2 |
| libjpeg-turbo, libpng | Cover art | BSD-style, libpng licence |
| libcurl | The RomM client | curl licence (MIT-style) |
| json-c | Parsing RomM's responses | MIT |
| libarchive | ROM archives, and PSP save zips | BSD 2-clause |
| zlib | Compression under several of the above | zlib licence |

**rcheevos** (github.com/RetroAchievements/rcheevos, MIT) is compiled into
the frontend for RetroAchievements (#74): the copy in
`frontend/third_party/rcheevos/`, unmodified, at tag v12.5.0 (commit
`1433173220a7`), with its `LICENSE` beside it and the files left out listed in
its `VERSION`. It is the library RetroArch, Dolphin and PCSX2 use.

**Signing in to RetroAchievements sends data to RetroAchievements**, and only
then: it is opt-in, per person, in Settings. The username and password go to
retroachievements.org once; afterwards the console sends the token it was
given, which game is being played (its RetroAchievements fingerprint, from
RomM), each achievement as it is earned, and a periodic "still playing" note,
as every RetroAchievements client does. Its User-Agent names CabinetOS, its
version and the emulator. Nothing is sent for anybody who has not signed in.

**SDL_GameControllerDB** (github.com/mdqinc/SDL_GameControllerDB, zlib
licence) is carried as data, not linked: `gamecontrollerdb.txt` in
`/usr/share/cabinetos/`, unmodified. Pinned in this repository
(`frontend/data/`, installed with the frontend so an update ships one file) and
refreshed by a weekly pull request (`.github/workflows/pad-db-update.yml`),
which names the upstream commit; first pinned at `c6d6e7ecca57`
(2026-09-24). It is the community's list of controllers SDL does not know by
itself (issue #66).

**GameTDB's Wii database** (www.gametdb.com, community-kept since 2009) is
carried as data: `/usr/share/cabinetos/wii-controls.txt`, an extract of which
controllers each Wii game accepts, made by `tools/wii-controls.py` from the
full `wiitdb.xml` and pinned in `frontend/data/` with the database version in
its first line. GameTDB's file offers it "for anyone to use in any Wii-related
project" and asks for permission only for use on a website. It decides which
Wii games play on an ordinary pad (docs/PROJECT.md open question 35).

**GameTDB's Wii U database** is carried the same way:
`/usr/share/cabinetos/wiiu-controls.txt`, made by `tools/wiiu-controls.py` from
the full `wiiutdb.xml`, its version in its first line. The Wii U file carries
no statement of its own; GameTDB's FAQ describes all its databases as "for
anyone to use in any game-related project". It decides which Wii U games play
on an ordinary pad (open question 36).

## Not bundled

**Games, BIOS and firmware.** All come from the person's own RomM server.

**RomM's own web player** runs emulators inside a page served by that server.
CabinetOS does not bundle them and they are RomM's attribution to make.

---

## Where this lives on a running machine

**`/usr/share/licenses/cabinetos/`**, installed by `build_files/build.sh` and
asserted there rather than assumed: this page, CabinetOS's own licence, and
**`texts/`, the full licence text of everything above with a pinned source**,
one file each, fetched at the exact revision the image is built from by
`tools/fetch-licences.py` (the list, with each file's path, is
`licences/SOURCES`). The base image's packages keep theirs where Fedora puts
them, in the same folder. **Not on the television** (MMagTech, 2026-09-25):
Settings, About, Credits and licenses names each project and its licence in
one line. The person most likely to redistribute this
image is the one who pulls it and never sees this repository, so the terms
travel with the binaries instead of sitting beside them.

## Checked

**Every line above was checked against the source tree it names, at the
pinned revision, on 2026-10-10 (#120)**: each licence file read, not taken
from Cabinet's list or the repository's GitHub label. What that changed: seven
"GPL v2" cores and melonDS are "or later" (their headers say so), the Vectrex
system ROM inside vecx, the fonts inside PCSX2, Dolphin and Cemu that have
their own licences, PCSX2's game patches, and two shader credits. The
libraries bundled inside RPCS3's, Xenia Edge's and Sunshine's own builds, and
Cemu's vcpkg libraries, are credited as those builds ship them and were not
checked one by one.
