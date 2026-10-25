# Contributing to CabinetOS

This is for people who can program and want to find their way around
CabinetOS, build it, test it, and change one thing safely. A small fix needs
no asking; anything that decides something new is raised on an issue first
("Pull requests and issues", below). It is written from what the code does today. Where an older doc
describes the code differently, the code wins; docs/decisions/, and
docs/PROJECT.md before it, stay the record of what was decided. The known
disagreements are listed at the end of
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

**Read in this order:**

1. This file: what CabinetOS is, how to build and test it, what CI checks,
   how a change reaches a console, and the rules.
2. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the map of the code, what
   each part owns and must never do, the main flows step by step, and where
   to look for the most common changes.
3. [docs/lessons/README.md](docs/lessons/README.md): ten rules that each cost
   hours to learn, and the lessons file for the area you are working in.

[docs/WORKING.md](docs/WORKING.md) is how the maintainer, MMagTech, works on
his own machines, and [docs/NEXT-SESSION.md](docs/NEXT-SESSION.md) is the note
on what is being worked on right now. Neither is needed to get started,
though a few of the rules below are cited from them.

## What CabinetOS is

A console operating system for people who keep their games on a
[RomM](https://romm.app) server. It is a [bootc](https://bootc-dev.github.io/bootc/)
image built on Bazzite, so a console updates the way a phone does: it
downloads a whole new image and boots into it.

- **It boots into one program.** systemd starts `cabinetos-session` on the
  first virtual console, which runs gamescope on the screen, which runs
  `/usr/bin/cabinetos-frontend`. There is no desktop and no login screen on
  the television. (The other virtual consoles keep their text login, which is
  how a console is recovered by hand: docs/WORKING.md, "Machines".)
- **The frontend is one C++20 program** (SDL3, EGL with OpenGL ES 3, and a UI
  layer of its own, no toolkit). It draws every screen, and it hosts the
  emulators: 23 libretro cores and PCSX2 (PlayStation 2, built as a library)
  run inside its process.
- **Six emulators run as separate programs**, started and stopped by the
  frontend like a game: Eden (Switch) and xemu (Xbox) from Flathub, and RPCS3
  (PlayStation 3), Xenia Edge (Xbox 360), Cemu (Wii U) and Vita3K
  (PlayStation Vita) built into the image. Steam's Big Picture is a seventh,
  an optional entry that hands the screen over and comes back.
- **Everything comes from RomM:** games, covers, firmware, saves and save
  states. The console keeps a cache of downloaded games and its own
  settings, and nothing else the server does not know. Updates come from the
  project's image on GitHub's registry.

The same summary, longer, is in [README.md](README.md) (for players) and at
the top of `frontend/src/main.cpp`.

## Building

**Nothing builds on a Mac, and the console has no compiler.** Everything is
built in podman containers on Linux x86-64, or by GitHub Actions. Any Linux
machine with podman and git will do, and so will a CabinetOS console over
Developer access, which has podman (the maintainer's loop builds that way).
The project's own builds run in CI
(docs/PROJECT.md, "Constraints and principles", 6); building locally is how
you check a change before you push it, and it is optional: CI compiles every
pull request to `main`, so without Linux you can open the pull request and
read the "Compile" job of `build-frontend.yml`.

**The frontend**, from the `frontend/` folder (the same recipe is in
`frontend/README.md`, "Building"):

```bash
podman build -t cabinetos-builder -f Containerfile .
podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make
```

The result is `frontend/build/cabinetos-frontend`. The builder pins Fedora 44
because that is what Bazzite 44 is built from, so every library matches the
console. **If Bazzite moves to a new Fedora release, `frontend/Containerfile`
moves with it.** The Makefile picks up every `.cpp` in `frontend/src` by
itself; a new source file needs no Makefile change.

**The Wii bridge** (`wiibridge/`, its own program, see Licences below), with
the same builder, from the `wiibridge/` folder:

```bash
podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make
```

Unlike the frontend's, its Makefile lists its source files by hand: a new
file there must be added to `wiibridge/Makefile`. `make test` there builds
and runs its one test, `wiibridge/pacing-test.cpp` (#480): two Remotes on
socket pairs through the bridge's own loop, no hardware.

**A libretro core:** `cores/build-core.sh <core>` from the repository root,
where `<core>` is the name of one of its `case` arms (the same 23 names as
`build-core.yml`'s list). Git runs on the host and the compile runs in
`cabinetos-builder`, so build that first. Each arm holds the core's
upstream, pinned commit and make arguments. The result lands in
`cores/build/`, and a frontend started from the repository root uses that
folder by itself (`--core-dir` points it anywhere else; with neither, it
uses the image's `/usr/lib/cabinetos/cores`). The script then checks the
core's options against `cores/options/<core>.txt` and prints any
difference; for a new core, that printed list (from `tools/core-options.c`)
is what the file is made from. **PCSX2, Cemu and Sunshine** (Remote Play's
streaming host) each have a script beside it, `cores/build-pcsx2.sh`,
`cores/build-cemu.sh` and `cores/build-sunshine.sh`, which builds its own
container from `cores/<name>-builder/` the first time. Each script's header
says how to run it.

**The whole image** is normally built only by CI (`build.yml`). On a Linux
box it is `ci/stage-image-payload.sh` with no arguments, which collects and
checks your local builds into `image_payload/`, then `just build`. It needs
every piece built first: the frontend, the Wii bridge, all 23 cores,
PCSX2, Cemu and Sunshine under `cores/build/`, the two arcade cores' set
lists (`cores/arcade-sets.sh`) and PPSSPP's and Dolphin's files in
`cores/system/`. That path is not walked
through here; read the Justfile's `build` recipe first.

## Running and testing

**The frontend runs without a screen.** With `SDL_VIDEODRIVER=offscreen` it
needs no compositor, no controller and no window, and can photograph what it
drew. It needs the libraries it was built against, so run it on a CabinetOS
console (over Developer access, below), where they match. Inside the
builder container the self-tests below run (CI runs them there); a run that
draws may work there too, but nobody has tried it, and off a console it
needs environment variables written down nowhere yet
(docs/ARCHITECTURE.md, "Known gaps"). Always give it a scratch
`--storage-root`: otherwise it writes into `/var/lib/cabinetos` when it
can, or into the folder it was started in. On a console a scratch root
does not separate the sign-in or the drives: the tokens and the PIN are
in `~/.config/cabinetos/`, so the run acts as that console's signed-in
person, and every mounted games drive is searched, so a run can play, keep
or remove games on it. A scratch `HOME` and `CABINETOS_DRIVES` (the folder
searched for drives) at an empty folder separate both. Run it from
`frontend/` (the stand-in library's covers are found from there, and the
cores from `../cores/build` when you have built some, else from the
image):

```bash
mkdir -p /tmp/scratch/home /tmp/scratch/drives
HOME=/tmp/scratch/home CABINETOS_DRIVES=/tmp/scratch/drives \
SDL_VIDEODRIVER=offscreen ./build/cabinetos-frontend --no-setup \
  --storage-root /tmp/scratch/root \
  --screenshot /tmp/home.bmp --render-size 1920x1080 --frames 60
```

Headless frames are not paced: 1,500 of them are about a second, so a
test that waits for a game to print something needs 20,000 or more.
`--nav <presses>` walks the screens after start-up through the same door a
pad uses; it skips the start-up screen's lift as a capture does, or its
presses would land on a Home nobody can see yet and be dropped. It does not
reach first run's screens, which run before it. Its words are `up`, `down`,
`left`, `right`, `a`, `b`, `l1`, `r1` and a PIN digit; `key:<name>` (a key
by SDL's name, `key:]`, `key:F5`), `held:<name>` (the system's repeat of a
key already down) and `start` (the pad's Start) go through the event
handler, `input.cpp`, as SDL's own events do.

`--no-setup` skips first run; on a machine with no server address it shows
a stand-in library of six made-up games. The server is the first of
`--romm <address>`, `$CABINETOS_ROMM`, `/etc/cabinetos/session.env` and
`config/server.json` under the storage root (`serveraddress.cpp`).
`--screenshot <file>` saves a BMP once `--frames N` frames are drawn (30 if
not given) and quits. `--screen library`, `--screen grid --tile N` and `--screen
detail --game <id>` (a game's RomM id on your own server) open a screen by
walking the route a person would, as do `--screen settings --tile N`,
`--screen search --query <text>` and `--screen howto` (`testhooks.cpp`);
`--focus N` starts with a card focused; `--render-size` renders any
television size, so a small machine can check a 4K layout. `--setup` shows
first run on a console that is already set up, and saves no server address,
sign-in or finish marker (`setup.h`, `dryRun`), though its pairing step asks
the server for a real code, which expires by itself: `--setup` walked to
the pairing step, or `--setup --setup-step pair`, reaches your real server,
so point it at the read-only relay, which refuses that request (a POST), unless
a real code is what you are checking. `--setup-step <name>`
opens it at `network`, `server`, `pair`, `controller` or `done`; Wi-Fi is
part of `network`.

**Reports that print and quit before a window opens**, so over SSH they
leave a console's screen alone:

| Flag | What it prints |
|---|---|
| `--first-run` | whether first run is needed, where the server address came from, and the step where first run would stop (`--first-run-step <name>` starts at one step; `--first-run-check-server` also asks the server) |
| `--network` | the link, the Wi-Fi radio, and whether this session may save a Wi-Fi network (`--network-scan` adds the networks in range) |
| `--storage` | the storage root, each drive's free space, floors and evictable games, and who kept what |
| `--core-options` | every option every core in the core folder declares, and what the console answers it |

`--launch <id>` and `--download <id>` press Play and Download for a game by
its RomM id once Home is up, and `--unkeep <id>` removes its download
without asking (`testhooks.cpp`).

**Self-tests that need no network, server, controller or screen.** Each exits
with 0 when it passes, and CI runs every one ("What CI checks"):

| Flag | What it checks |
|---|---|
| `--first-run-rules` | all 144 combinations of facts first run can be handed, asserting what it must refuse |
| `--first-run-writes` | that finishing first run actually sticks |
| `--accounts-test` | the rules for the people on a console and switching between them |
| `--players-test` | the rules for which controller is which player |
| `--fightstick-test` | which fight sticks are known, and where a game's buttons go on one: every count of buttons in both arcade cores' orders, and no layout for any other pad |
| `--arcade-settings-test` | holding Start in an arcade game: a hold of about a second presses MAME 2003-Plus's menu key once, a tap or a shorter hold presses nothing, a second hold closes it, player 1's left stick click never reaches MAME, and nothing changes on FinalBurn Neo or any other core |
| `--controls-test` | an arcade game's Controls: every state a person can reach by every press, from the default, on a pad and on the fight stick (each press a swap or a move, two buttons never on one, nothing lost, every refused press changing nothing), which buttons are rows and what they are called, a trigger's pull following its button, and the file written, read back after a hand edit, and deleted by Reset |
| `--battery-test` | how a controller's battery reading becomes the row in the top bar |
| `--volume-test` | the volume keys' rules |
| `--playtime-test` | time played: the clock and what is reported to RomM |
| `--disc-test` | the rules for games on several discs |
| `--same-names-test` | the diagnostic report's list of games with the same name |
| `--files-test` | that a full disk leaves the file it was replacing whole, credentials stay 0600, a cut save's marker is not counted as owed, two settings changed at once both stick, the cover sweeps spare the tile map, a cover being written and what a link points at, and a game's archive keeps its folders and refuses a name outside them or one that comes twice, and is not unpacked again at a later start, but is when a file that came out of it was changed or cut short, its unpacking was cancelled, or it was downloaded again |
| `--vectrex-sheets-test` | which Vectrex game takes which screen sheet: RomM's md5 first, then the longest name token, and the sheets that never shipped drawing nothing, against `frontend/data/vectrex-overlays.json` |
| `--check-option-tables cores/options` | that every option the console sets (`coreoptions.cpp`, `quality.cpp`, `sysopts.cpp`, the Wii's rumble in `wii.cpp` and the Vectrex sheets' fit in `vectrexsheets.cpp`, with every answer that depends on the machine or a setting) exists in the core it is set on, and that every libretro core in `catalog.cpp`'s table has its list in `cores/options/` (CI runs it as a step of its own) |

`--romm-race-test <address> <seconds>` checks the RomM client's thread rule
under load, against the stand-in server `tools/racemock.py <port>` only
(start it, then `--romm-race-test 127.0.0.1:<port> 30`; it exits 0 when
nothing went out wrong); do not point it at a real RomM.

**Every flag is in `frontend/src/cli.cpp`**, 109 of them plus
`--check-option-tables` in `main.cpp`; there is no `--help`.
`frontend/README.md` holds the build recipe and the storage tree. What it
said about running and the flags in September 2026 is history, in
docs/history/frontend-readme-2026-09.md.

**Against a RomM server: your own, never someone else's.** The console
writes to RomM as the signed-in person: saves, states, screenshots, time
played, favourites. Some test flags write too: `--sync-test` saves a state,
sends the battery save and sends the file saves when it quits.
`tools/romm-readonly-relay.py <port>` forwards reads only and answers every
write with 403, which is the safe way to test against a real library; point
the console at it with `CABINETOS_ROMM=127.0.0.1:<port>` and a scratch
`--storage-root`. Pairing needs writes, so the relay cannot sign anybody
in: run it on a console already signed in to your server, with the real
`HOME` on purpose (the tokens are there, above), copy the console's
`config/*.json` into the scratch root's `config/`, and set
`CABINETOS_DRIVES` to an empty folder, or downloads go to the real games
drive (docs/lessons/testing.md, the bullet on the read-only relay). **Its upstream server comes from
`CABINETOS_RELAY_UP`** (`http://<your server>:<port>`), and so does
`tools/offline-relay.py`'s.

**A RomM server for write tests.** The project has a RomM server of its
own, on the RomM version the maintainer's server runs, holding only free
homebrew from GitHub releases and test accounts. **Writes are allowed
there, and only there:** pairing, saves, states, time played, favourites
and the test flags that write. Its address comes from
`CABINETOS_TEST_ROMM` (`http://<server>:<port>`); its login is in the
maintainer's local notes, never here. Point a run at it with
`CABINETOS_ROMM=<server>:<port>` and the scratch `HOME`, `CABINETOS_DRIVES`
and `--storage-root` above, with nothing copied from a real console. Pair
as a console does: `--romm-probe --romm-pair` prints a code, and a user of
that server approves it, on its pairing page or with `POST
/api/auth/device/approve`; the token lands where the console keeps it. Then
`--no-setup --launch <id>` plays one of its games, and SIGTERM leaves the
game the way Exit to Home does, so its saves and time played are sent (time
under a minute is not, and neither is time held on "Press A to continue",
where `--sync-test` leaves a game after it loads the state back). A run
that signs out restarts itself with the same flags; under `--no-setup` that
signs out again and again, because first run is what finishes a sign-out,
so stop it and start it once without `--no-setup`. Without access to that
server, run a RomM of your own the same way (RomM's documentation, "Quick
Start").

**The tools in `tools/` take their machines from the environment;** none
has an address written in. `CABINETOS_A9` is the test console, as
`cabinet@<address>` (`ui-loop.sh`, `base-checks.sh`, `check-applied.sh`,
`installer-vm.sh`); `CABINETOS_KEY` is the SSH key (`~/.ssh/cabinetos`
when unset); `CABINETOS_VM` is the build machine for
`ui-loop.sh --via-vm`; `CABINETOS_RELAY_UP` is the relays' server;
`CABINETOS_TEST_ROMM` is the server for write tests (above), which no tool
reads yet. A tool
stops at once, naming the variable, when one it needs is unset.
CLAUDE.local.example.md has the lines to fill in.

**What only a television can judge:** anything you see or hear. An
offscreen capture has looked right while the television showed nothing
(lessons rule 1). The reference console is the maintainer's; he judges every
change there before it ships (next section). Say in your pull request what
you checked and how, and what you could not.

**On a CabinetOS console of your own:** in Settings, About, press Version
seven times and a Developer access row appears. Switching it on starts SSH
on port 2222 and shows the password on the television; it needs the
console's PIN (docs/SETTINGS.md, "Developer access"). The image itself is
read-only. `tools/ui-loop.sh` is how the maintainer runs a test build on his
console without an image build; it leans on state he set up on his console
by hand (a folder of cores and the builder image), so read it for the steps
rather than run it. A signed image of your own needs a fork with its own key and image name
("Generate a signing key", below); running your own build on your own
console is not written down yet (docs/ARCHITECTURE.md, "Known gaps").

## What CI checks on your pull request

**A pull request to `main`** runs these (the first two only when it changes
something besides `.md` files and `docs/`):

- **Lint**: shellcheck over every `.sh` in `build_files/`, `ci/` and
  `cores/`, over every shell script in `system_files/` (picked by its
  first line), and over the installer's `%pre` and `%post` in
  `disk_config/iso.toml`; and every Python file, the ones that ship
  included, is parsed. `just lint` runs the first two commands.
- **The frontend** (`build-frontend.yml`): it compiles the frontend and the
  Wii bridge in the builder, runs the bridge's `make test` (report only
  until the date below), `--check-option-tables` and the self-tests,
  summarises the compiler's warnings, fails a new one, and lists the code
  nothing calls (below).
- **The changelog check** (`changelog.yml`): see "Pull requests and issues".

**No new warnings in the files you touch** (`ci/check-new-warnings.sh`,
after the Warnings step of `build-frontend.yml`, on every pull request it
runs on): the warnings in the files the pull request changes are compared
with the same files compiled at the base, by their text across all those
files, so code moved within a file or from one changed file to another is
not new. Vendored files (`frontend/third_party/`, `frontend/src/libretro.h`)
are not checked. Only what can put a warning in a touched file is compiled
again at the base: one touched `.cpp` took 6 seconds, and `app.h`, which 53
files include, 38 seconds against 80 for the whole compile in the same run
(2026-10-18). The run's summary lists each touched file's count at the base
and now, and every new warning. Report only for its first week, like the
checks below.

**The self-tests** (`ci/run-self-tests.sh`, after the option tables, on
every run of `build-frontend.yml`, pushes included): each one in the table
in "Running and testing", in the builder container, with a scratch storage
root, a scratch `HOME` and `CABINETOS_DRIVES` at an empty folder, so no run
can reach a real sign-in or games drive. The run's summary has each test's
result and time. `--romm-race-test` and the tests that need a screen, a core
or a server do not run; the script's header names them. Report only for
its first week, like the checks below.

**The unused code** (`ci/report-unused-code.sh`, the job's last step, on
pull requests): every function, and every variable that can change, that
nothing reachable from `main` uses, by file, in the run's summary. It comes
from a second build of the frontend in a scratch folder at `-O0`, with a
section for each function and variable, linked with `--gc-sections
--print-gc-sections`; a name is listed only when no copy of it is left in
the linked program, so a header's function that one file never calls is
not. Constants are not listed (a `constexpr` is folded where it is used),
nor the C++ library or vendored rcheevos, and an inline function nobody
calls is never compiled, so the linker never sees it. The binary that ships
is not that one. A report only: it never fails the job. On a pull request
the names in the files it touches are also a warning on each file and a list
at the end of the report (`ci/warn-unused-touched.sh`, with the names held on
purpose, none today): each is removed, or kept with the reason in the pull
request (CLAUDE.md, rule 6).

**A pull request to `main` never builds the image**, the cores, PCSX2 (and
so `frontend/ps2`), Cemu or Sunshine; a mistake there first shows when the
maintainer pushes to `testing`. That includes the check of each core's
options against `cores/options/` (`cores/check-options.sh`, which
`cores/build-core.sh` runs on every core it builds or takes from the
cache): a pull request to `main` that moves a core's pin passes, and a
difference in its options shows at the testing push. Dolphin's and FBNeo's
lists need a game loaded, so `tools/check-applied.sh` compares them on the
reference console. A pull request into any other branch runs
`build-frontend.yml`, `build-core.yml`, `build-pcsx2.yml`, `build-cemu.yml`
and `build-sunshine.yml` when it touches their paths (each workflow's
`paths:` list).

**Every pull request**, whatever it is aimed at and whatever it changes,
runs `checks.yml`, a few seconds each:

- **RomM paths** (`ci/check-romm-paths.sh`): a line the pull request adds
  that spells a RomM path in code (`/api/`, `/assets/`, `/resources/roms/`,
  `/cover/`) outside the RomM client's files (`romm.h`, `romm_*`) fails. Ask
  the client for it instead. Comments, `.md` files, `docs/` and `tools/` are
  not read, nor is Sunshine's client (`remoteplay.cpp`), whose `/api/` is
  Sunshine's. The paths spelled outside the client today are listed in the
  script (`KNOWN`), so editing one of those lines does not fail; moving one
  into the client takes it off the list.
- **Doc paths** (`ci/check-doc-paths.py`): a `.md` file that names a file
  or folder of this repository that is not there fails, so a pull request
  that moves or deletes a file also puts right the docs that name it. It
  reads every link, and every path in backticks that starts with one of the
  repository's top-level folders (`frontend/src/app.h`); not code blocks,
  `docs/history/` or `frontend/third_party/`. A path `.gitignore` covers (a
  build output) is there, and so is a file of Cabinet's own repository on
  the script's list.
- **PROJECT.md index** (`ci/check-project-index.sh`), report only, never a
  failure: a numbered question in `docs/PROJECT.md` (`### 32.`) with no line
  in its index, an index line with no question, or the index's count of
  questions gone wrong is a warning on the pull request. The file is frozen
  (CLAUDE.md, rule 7), so no question is added to it.
- **Long files** (`ci/check-long-files.sh`), report only, never a failure:
  every source file over 1,500 lines in `frontend/`, `wiibridge/`,
  `build_files/`, `ci/`, `cores/`, `system_files/` and `tools/` is listed in
  the run's summary, and one the pull request makes longer is a warning on
  it, so a file growing into another 20,000-line main.cpp is seen early.
  Source is code (`.c`, `.cpp`, `.h`, `.sh`, `.py`, or a script starting
  `#!`); vendored files (`frontend/third_party/`, `frontend/src/libretro.h`)
  are on the script's list and left out.
- **Docs list** (`ci/check-docs-list.sh`): a `.md` file directly under
  `docs/`, or a folder under `docs/` that holds `.md` files, that is not named
  in "The docs, each with its one job" (above) fails, so a new doc has to say
  what its one job is (CLAUDE.md, rule 2). What is inside a folder is not
  read here.
- **Decisions** (`ci/check-decisions.sh`): a file in `docs/decisions/` that
  is not named `YYYY-MM-DD-short-name.md`, is over 60 lines, or does not have
  exactly one line in the index of `docs/decisions/README.md` fails, as does
  an index line with no file; and a pull request that makes `docs/PROJECT.md`
  longer by more than 5 lines net fails, since the file is frozen. A line a
  pull request adds to a decision file or to PROJECT.md that says "known gap",
  "not fixed", "still open" or "left for later" is a warning on the pull
  request and never a failure: a decision file holds no fault (CLAUDE.md,
  rule 7).

**A check's first week is report only.** Until the end of the date in
`checks.yml`'s `REPORT_ONLY_UNTIL` (2026-10-25, UTC; `build-frontend.yml`
has its own, the same date, for its checks), a check that can fail marks
what it finds as warnings on the pull request and passes; from the next
day it fails. A new check that can fail starts the same way, with that
date set a week ahead in its pull request: the Docs list and Decisions
checks have their own, 2026-10-31, in their jobs in `checks.yml`.

**Not checked by CI:** the frontend's tests that need a screen, a core or a
server, the shell scripts under `tools/`, what any Python does (Lint only
parses it, so the diagnostic report's privacy scrubber is never run), and
the workflows themselves. Run what your change touches yourself and say so.

**The proof of a change is its testing image**, judged on the reference
console. That step is the maintainer's.

## How a change reaches a console

1. **Your pull request** passes the checks above.
2. **The maintainer pushes its final commit to the `testing` branch.**
   `build.yml` builds every piece, builds the image, signs it and publishes it
   as `ghcr.io/mmagtech/cabinetos:testing`.
3. **The reference console follows `testing`.** It takes the image through
   Settings, System update, and he judges the change on the television.
4. **He merges.** If `main`'s files are exactly the ones `testing` was built
   from, nothing is built again: `ci/promote-tested.sh` tags the tested image
   `latest`, same digest, same version. Documentation does not count: `.md`
   files and `docs/` are not in the image, except `docs/LICENCES.md`, which
   is. Anything else committed after the testing push makes `main` build a
   new image instead.
5. **Every console on `latest`** is offered it by its next update check, and
   runs it after a restart.

**Versions** are the UTC date, `2026.10.17`, then `.2`, `.3` for later
images that day, numbered from the tags already published
(`ci/next-version.sh`). Every image keeps its version tag, and the console
reads its own from `/usr/share/cabinetos/version`.

**Signed updates.** Each build that publishes pushes the image under every
tag but the channel's (the version, the sha, and on `main` the date), signs
it by its digest with cosign (`build.yml`, "Sign image"), checks that
signature against
`system_files/etc/pki/containers/cabinetos.pub` the way a console does, and
only then moves `testing` or `latest` to it. A run that stops before that
last step leaves the channel where it was; its version tag stays up,
perhaps unsigned, and nothing follows it. Without the `SIGNING_SECRET` repository
secret, a push to `main` or `testing` fails before anything is published.
The image carries the
public key and a policy that refuses an unsigned update, which bites on any
console installed with the installer (`build_files/require-signed-updates.sh`).
A fork signs its own images with a key of its own: "Generate a signing key",
below.

**The worked example, a change to the Wii bridge** (#398, Part 4 section 34):
`wiibridge/Makefile` builds it; `build-frontend.yml` compiles it and puts it
in the `frontend-linux-x86_64` artifact; `ci/stage-image-payload.sh` refuses
to go on without it and checks it is an x86-64 Linux program;
`build_files/install-frontend.sh` installs it at
`/usr/libexec/cabinetos-wii-bridge` and checks its libraries; the frontend
starts it from there (`frontend/src/wiiremote.cpp`). Each file names the next
only in a comment, which is why the path is written out here.

### Generate a signing key

This is for a fork that publishes its own image. The project's images are
signed with MMagTech's key, whose secret half is never in the repository
(`.gitignore` lists `cosign.key`).

1. **Make a key pair** with cosign (`build.yml` installs v3.1.2):
   `COSIGN_PASSWORD="" cosign generate-key-pair` writes `cosign.key` and
   `cosign.pub`. The password is empty because "Sign image" gives cosign
   none.
2. **Put all of `cosign.key` in the fork's `SIGNING_SECRET` repository
   secret** (`gh secret set SIGNING_SECRET < cosign.key`). `build.yml` then
   signs every image it publishes from `main` or `testing`; without the
   secret a push there fails and publishes nothing.
3. **Replace `system_files/etc/pki/containers/cabinetos.pub`** with
   `cosign.pub`, in the same push: `build.yml` checks each signature against
   that file and will not move `testing` or `latest` to an image it does not
   match.
4. **Change `ghcr.io/mmagtech/cabinetos` to the fork's image** in the three
   files that use it for updates: `build_files/require-signed-updates.sh`
   (`REPO`, the policy's rule),
   `system_files/etc/containers/registries.d/cabinetos.yaml` (where the
   signatures are found) and `disk_config/iso.toml` (the image the installer
   points a console at for its updates). The diagnostic report names it too,
   in `cabinetos-report`'s `PUBLIC` list of words it never scrubs.

The image name itself needs no edit: `build.yml` publishes to
`ghcr.io/<owner>/cabinetos`, from the repository's owner in lower case and
`IMAGE_NAME` in `cabinetos.env`. The key and the image name in the four
files above are written into tracked files today, so a fork edits them.
Until it does, its image carries MMagTech's key and a rule for his image,
and its installer points every console it installs at his image.

### Going back to an earlier version

Every published image stays on the registry under its version tag.

- **The normal way, no token:** revert the merge on `main`. `main` then
  differs from the testing image, so `build.yml` builds the next version, and
  consoles take it as an ordinary update.
- **Fast, with a token that can write packages:** point `latest` back at a
  version. Consoles take it as an update, because the check compares
  digests, not "newer", and the signature is on the digest:
  ```bash
  skopeo copy --preserve-digests --dest-creds "USER:TOKEN" \
    docker://ghcr.io/mmagtech/cabinetos:<version> docker://ghcr.io/mmagtech/cabinetos:latest
  ```
  `testing` still points at the bad image until it moves too.
- **One console**, over Developer access: `sudo bootc rollback && sudo
  systemctl reboot` boots the image it ran before its last update; `sudo
  bootc switch ghcr.io/mmagtech/cabinetos:<version>` pins it to a version
  until it is switched back to `:testing` or `:latest`. A console that rolled
  back is offered the bad image again by its own update check until `latest`
  moves.

## The rules

These are the project's rules. Most are written elsewhere and are cited
here, not reworded; follow the link for the full text.

### Already written

- **Free, never sold, no donations; no games, BIOS or keys.** Some of the
  emulators it ships are licensed for non-commercial use only, and this is
  what keeps them legitimate (README.md, "Credits and licence";
  docs/PROJECT.md, "Licensing, and the constraint it puts on the project").
- **RomM is the single source of truth for the library** (docs/PROJECT.md,
  "The backend model"). For anything on RomM's data: "RomM's documentation
  together with RomM's behaviour is the source of truth" (MMagTech; it
  stands in docs/NEXT-SESSION.md's first paragraph, and is quoted here so it
  stays when that file is rewritten).
- **The controller is the input the design assumes.** A keyboard is the
  guaranteed input at first run, and a mouse "moves focus and clicks the
  focused thing, and nothing else" (docs/PROJECT.md, open question 16,
  answered 2026-09-19).
- **One rule for every game: no per-game fixes, and Eden as it ships**
  (docs/PROJECT.md, "ONE RULE FOR EVERY GAME, AND EDEN AS IT SHIPS").
- **Fixes are general**: "choose for a TV console everywhere else; say for
  every fix what it does on hardware that is not the A9; and when
  RetroArch, Batocera or EmuDeck play something out of the box, find the
  setting or behaviour we are missing rather than invent a fix"
  (docs/PROJECT.md, "Dreamcast, PSP and N64 draw right", 2026-09-27). The A9
  is the reference console. The same rule opens with a Cabinet clause that
  the next point overtakes.
- **Cabinet, the retired Apple TV app CabinetOS came from, is a reference,
  not a constraint.** Since 2026-10-11 "Cabinet on Apple platforms is no
  longer a constraint" (docs/PROJECT.md, open question 12b, the GameCube save
  decision, #344, "GameCube: a save restored from RomM never reaches Dolphin
  (card.raw vs card.USA.raw)"); it is newer than the rule above. One thing
  stays: the save tags already on RomM (`savetable.cpp`, many in Cabinet's
  spelling) are what existing saves are filed under, and Cabinet Go, its
  unfinished pocket version, will read them, so changing one hides those
  saves. docs/CABINET.md says what Cabinet is now; docs/history/CABINET.md
  is worth reading for how a problem was solved before.
- **Emulators favour stability over the newest version**: a pin moves only
  for one of five named reasons, with its evidence, and the question is
  asked out loud on the issue or pull request
  (`docs/decisions/2026-10-23-when-an-emulator-pin-moves.md`). This is the
  rule for moving a core's pin; the older comment in
  `cores/build-core.sh` saying pins follow Cabinet's manifest is out of date
  (docs/ARCHITECTURE.md, section 7).
- **Fewer settings; no emulator settings page** (docs/SETTINGS.md, "The screen";
  "fewer settings is the rule", docs/PROJECT.md, the glow decision of
  2026-10-04).
- **Not a handheld OS, not a Steam machine, not a desktop** (docs/PROJECT.md,
  "Non goals").
- **Stay at the application layer; the Bazzite base is pinned and moved only
  on purpose; builds happen in CI; one branch at a time, aimed at `main`**
  (docs/PROJECT.md, "Constraints and principles", 1, 2, 6 and 7).
- **Words on screen assume a competent adult**: say the constraint and stop
  (docs/lessons/frontend.md, "Words on screen").
- **No start-up or shut-down sound, ever** (docs/PROJECT.md, open question
  47).
- **Eden stays on Flathub** for a legal reason: the project never
  distributes a Switch emulator itself (docs/PROJECT.md, open question 32,
  "A legal point").
- **The `MMagTech` handle, never a personal name**: the repository is public
  (docs/WORKING.md, "How MMagTech wants this done").
- **The ten rules** in docs/lessons/README.md. Two are build rules for
  everyone: what the image writes to `/var` reaches only fresh installs, so
  put it in `/usr` (rule 9); every scripted edit must assert its anchor
  (rule 10).

### Written here first (MMagTech, 2026-10-17)

- **The console connects only to fixed places the code chooses:** the
  player's RomM server, GitHub for updates, and RetroAchievements, Flathub,
  Tailscale or Steam when the player uses them. Never to an address found in
  the data, such as a cover's web address.
- **No em dashes in anything the console shows on screen.** Use a full stop,
  a comma or two short sentences.
- **No CabinetOS fix may need Steam.** Steam is optional, so a fix never asks
  a player to install, open or change anything in Steam.
- **Every emulator uses Vulkan when it has a Vulkan renderer**, even where
  upstream defaults to OpenGL. OpenGL only where there is no Vulkan renderer,
  or where a measurement on real hardware shows it is better.
- **A patch to an emulator's source is proposed and agreed first (MMagTech,
  2026-10-20),** and tracked on an issue linked to its upstream report until
  upstream fixes it (docs/WORKING.md, "Patches to an emulator's code").
- **Made for a PC attached to a TV (MMagTech, 2026-10-21).** CabinetOS is
  built for a computer connected to a television: a mini PC, or a larger
  desktop PC. A computer monitor with built-in speakers counts as a TV. A
  fix never risks that experience for another kind of machine. Handhelds (a
  Steam Deck) and laptops are not targets. Since 2026-10-22, a fault is not
  looked at if it shows only on a handheld PC, a laptop, or a computer that
  is not attached to a TV, and not on a mini PC or desktop attached to a TV.
  Remote Play with no screen connected (#381) is the one exception.

### Duties that come with some changes

- **Moving a core's pin:** in the same commit, refresh
  `cores/options/<core>.txt` (`cores/build-core.sh` prints the difference
  after it builds the core; Dolphin's and FBNeo's lists need a real game and
  come from a console, `tools/check-applied.sh`), and move the same commit in
  `licences/SOURCES` (then run `tools/fetch-licences.py`) and
  `docs/LICENCES.md`. A stale options list fails the core's build, which a
  pull request to `main` does not run: it fails at the testing push. Then
  check the console's own option tables still fit (`--check-option-tables`),
  and ask the stability question above. Moving any emulator's or Sunshine's
  pin, go through its rows in docs/ARCHITECTURE.md, "What leans on other
  projects", too.
- **Leaning on another project's internals** (asked for by MMagTech,
  2026-10-21): anything that works only because another project behaves a
  way it does not promise (a patch to its source, a settings key or file it
  does not document, its output or its log read as text, a setting's name, a
  file laid over one of its own) gets, in the same pull request, a check in
  the place that is read when that project moves, and a row in
  docs/ARCHITECTURE.md, "What leans on other projects". For a package of the
  Bazzite base, that place is its area in `ci/base-watch.txt`: a `log:` line,
  run on the reference console as `tools/base-checks.sh` runs it and seen to
  fail when its answer is wrong, or a `tv:` line when only eyes can tell. For
  something pinned here (a core, an emulator, Sunshine), an assertion in its
  build or install script that stops the build. Where nothing can check it,
  the row says so, and what would show it broken.
- **Changing the controls:** change About, How to, and
  `docs/wiki/navigation.md` together (docs/SETTINGS.md, About).
- **A new emulator or library in the image:** `docs/LICENCES.md`,
  `licences/SOURCES` (its text fetched by `tools/fetch-licences.py`) and the
  credits in `frontend/src/credits.h` change together (credits.h's header).
- **Adding to the App** (`frontend/src/app.h`): member order is destruction
  order, and threads hold `this`; read app.h's header first. The RomM
  client is called from many threads at once (romm.h's header). Two more
  things are positional: `Uploader::Job`'s fields (uploader.h's header) and
  the rows of catalog.cpp's table (its struct's comment).
- **What counts as "in the image"** is written four times and all four
  must stay the same: `build.yml`'s `paths`, once for pull requests and once
  for pushes; `ci/promote-tested.sh`'s `in_image`; and `changelog.yml`'s
  filter.
- **A Settings feature:** its pull request flips its status in
  docs/SETTINGS.md to Built (docs/SETTINGS.md, "Tracking").

## Pull requests and issues

- **Ask first for anything that decides something new.** A fix of a few lines
  that puts back what was already meant to work goes straight to a pull
  request: a typo on screen, a label cut off, a crash from a missing check. A
  feature, a setting, a changed default, a new emulator or system, a pin move,
  a new file or folder, a new download or checksum, a change to the build or
  CI, or a refactor across files needs an issue the maintainer has agreed to
  before you write it, linked from the pull request. A fix for one game is
  never taken. If you are unsure which this is, ask on an issue. A pin moves
  only for one of the five reasons in
  `docs/decisions/2026-10-23-when-an-emulator-pin-moves.md`, with its evidence.
- **AI-assisted pull requests are welcome; CabinetOS is built with AI
  itself.** AI tools read `AGENTS.md`. An AI may help write an issue or a pull
  request; a person reads it, sends it and answers for it, and the pull request
  says which tool helped and which checks were run. The maintainer reads every
  pull request, and CI does not run on one from a fork until he approves it.
- **Skills:** a procedure for one job lives in
  `.claude/skills/<job>/SKILL.md`, plain Markdown that any AI tool can read as
  instructions. Name a skill for the job it does: verb first, plain words, no
  project jargon (`check-emulator-update`, not `pin-check`). One job per skill,
  and it points at the docs for the rule instead of repeating it.
- **One branch, one pull request, aimed at `main`.** Its body says first what
  the change does and why it exists, in plain words, then the detail, and
  "Closes #N" for an issue it finishes (docs/WORKING.md, "The loop").
- **The changelog:** a change that reaches consoles adds an entry for
  players to `docs/wiki/changelog.md` in the same pull request; one a player
  cannot notice says `Changelog: none` on its own line in the body. The rule
  is docs/WORKING.md, "Every change that reaches consoles goes in the
  changelog", and `changelog.yml` enforces it. The rule files entries under
  a heading named for the testing image's version, which exists only after
  the maintainer's testing push: put yours at the top under a placeholder
  heading such as `## Next`, and when that image is built he gives it the
  version heading.
- **Name an issue or pull request by its title as well as its number**, every
  time (MMagTech; it stands in docs/NEXT-SESSION.md's first paragraph).
- **The maintainer merges**, after judging the testing image.
- **Bug reports** come through the issue form, which asks for the console's
  diagnostic report (Settings, System, Diagnostic report).
- **Security problems** are reported privately, never in a public issue:
  [SECURITY.md](SECURITY.md).

## The docs, each with its one job

Before proposing a change, look for the decision that already covers it: the
index in docs/decisions/, then docs/PROJECT.md for anything before
2026-10-23.

- **docs/decisions/**: what the project decided and why, one small file
  each, with an index. Since 2026-10-23.
- **docs/PROJECT.md**: the old record of decisions and why, up to
  2026-10-23, frozen. Code cites its questions by number ("open question
  32"), so the numbers never change. It is too large for GitHub to render,
  so it shows as plain text; its index, at the top, lists every section and
  question with where it ended. Search it by question number (`### 32.`) or
  keyword.
- **docs/SETTINGS.md**: every Settings decision, as the screen is.
- **docs/ROADMAP.md**: how the release was reached, the Later milestone, and
  what was decided against, kept so nobody proposes it again.
- **docs/ARCHITECTURE.md**: how the code is laid out, what each part owns and
  must never do, and the main flows.
- **docs/NEXT-SESSION.md**: what the next session works on, and what state
  things are in.
- **docs/WORKING.md**: how MMagTech works: the loop, the testing branch,
  merges.
- **docs/lessons/**: what cost hours, with the reasons.
- **docs/LICENCES.md**: every emulator and library in the image, with its
  licence, its source and the exact version built.
- **docs/CABINET.md**: what CabinetOS shares with Cabinet Go, the pocket app:
  the save and state names on RomM, which must not change. It points to the
  history of the retired Apple apps.
- **docs/wiki/**: the pages for players, with the changelog.
- **docs/history/**: what is kept as history, not current.
  docs/CORE-OPTIONS-AUDIT.md, docs/PCSX2-HOST-SURFACE.md and
  docs/SETTINGS-INVENTORY.md are one-line pointers to files that moved
  there.
- **GitHub issues**: one per piece of work; the audit and restructure of the
  code are #398 ("Audit the code and restructure it for contributors
  (main.cpp is 20,000 lines)"). Its audit comments each start "Audit, Part
  N", and their sections are numbered across the parts, so "#398, Part 5
  section 48" is section 48, in Part 5's comments.

## Licences

- CabinetOS's own code is MIT ([LICENSE](LICENSE)).
- The emulators it ships keep their own licences, and some are for
  non-commercial use only: [docs/LICENCES.md](docs/LICENCES.md) lists every
  one, its source and the exact version built.
- The Wii bridge is GPL-2.0-or-later and its own program, because it carries
  Dolphin's extension encryption; the frontend stays MIT and only starts it
  (`wiibridge/bridge.cpp`'s header).
- The `Containerfile`, `Justfile`, `disk_config/` and `.github/workflows/`
  derive from the Universal Blue image template (Apache-2.0).
