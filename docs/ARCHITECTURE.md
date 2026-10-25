# CabinetOS architecture

How the code is laid out, what each part owns and must never do, and what
happens, step by step, in the flows that matter most. Written from the code
at the restructure of #398 ("Audit the code and restructure it for
contributors (main.cpp is 20,000 lines)"), October 2026. Start with
[CONTRIBUTING.md](../CONTRIBUTING.md) if you have not.

**When you change the layout, change this file in the same pull request**:
a new file, a moved job, a renamed part, or a change to who calls whom.

## 1. The map

### The repository

| Folder or file | What is in it | Where it ends up |
|---|---|---|
| `frontend/` | The console's one program, in C++20: `src/` (324 files, one folder; the Makefile compiles these and rcheevos), `data/` (shaders, controller lists, and `vectrex-overlays.json`, which Vectrex game takes which screen sheet), `third_party/rcheevos/` (RetroAchievements, vendored), `covers/` (the stand-in library's covers), `tools/fake-nmcli/` (a stand-in network tool for trying first run) | `/usr/bin/cabinetos-frontend` |
| `frontend/ps2/` | The PlayStation 2 host around PCSX2, compiled by `cores/build-pcsx2.sh` into `cabinetos-ps2.so`, not by the frontend's Makefile (so a pull request to `main` never compiles it) | `/usr/lib/cabinetos/cores/` |
| `wiibridge/` | `cabinetos-wii-bridge`, a stand-in for each real Wii Remote; GPL, its own program | `/usr/libexec/cabinetos-wii-bridge` |
| `cores/` | How every emulator the image carries is built: `build-core.sh` (the 23 libretro cores, each pinned by commit in its `case` arm), `build-pcsx2.sh`, `build-cemu.sh`, `build-sunshine.sh` and their builder containers; `options/<core>.txt`, every option each core declares at its pin | `/usr/lib/cabinetos/cores/` and the emulators' folders |
| `build_files/` | The image build's scripts: `build.sh` and the scripts it calls (remove the desktop and the storefronts, set up the session, the branding), `install-*.sh` (one per piece the image carries), `require-*.sh` (checks that fail the build), `built-in-speaker.sh` (a speaker inside the case never takes the sound, #547) | run once, inside the image build |
| `system_files/` | Files copied over the image as they are: the session script, systemd units, the root helpers in `usr/libexec/`, polkit rules, udev rules, the diagnostic report, which output plays the sound (`usr/share/wireplumber/`, a WirePlumber setting file), and a few files in `etc/` (the SSH settings, the update signing key) | `/usr` and `/etc` |
| `ci/` | What the workflows run: collecting the build outputs (`stage-image-payload.sh`), the key each core's build is cached under (`core-cache-key.sh`, `build-core.yml`), version numbers, promoting the tested image, watching the Bazzite base, the checks every pull request runs (`checks.yml`: `check-romm-paths.sh`, `check-doc-paths.py`, `check-project-index.sh`, `check-long-files.sh`, `check-docs-list.sh`, `check-decisions.sh`), and the frontend's checks and reports (`build-frontend.yml`: `check-new-warnings.sh`, `run-self-tests.sh`, `report-unused-code.sh`, which calls `warn-unused-touched.sh`) | GitHub Actions |
| `.github/` | The workflows, the issue form for players, and the pull request template | GitHub |
| `tools/` | Test and maintenance tools, run by hand; they take their machines from the environment (CONTRIBUTING.md, "Running and testing"). One also runs in the image build: `fetch-vectrex-overlays.py`, which `build.yml` runs to fetch the Vectrex screen sheets (#224) into the payload | nowhere |
| `disk_config/` | The installer and test-disk layouts | the installer ISO |
| `licences/` | The full licence texts the image ships, and `SOURCES`, where each comes from | `/usr/share/licenses/` |
| `docs/` | The decisions (`decisions/`, one small file each) and the old record they replace (`PROJECT.md`, frozen on 2026-10-23), the lessons, the website (`index.html`, `wiki/`, and `assets/` with its styles and the wiki's search, `search.js`; served by GitHub Pages), and `history/`: docs that describe the project as it was, each saying what replaced it | only `docs/LICENCES.md` |
| `Containerfile` | The image: the Bazzite base pinned by digest, then the build scripts in several layers | |
| `Justfile`, `cabinetos.env` | The build recipes CI calls, and their parameters | |
| `base-manifest.txt` | The base image's package list, for the monthly base-update check | |
| `AGENTS.md`, `CLAUDE.md` | What an AI coding tool reads before it changes anything: `AGENTS.md` for any tool, `CLAUDE.md` for Claude Code, which holds the seven rules for every change that both point to | nowhere |
| `.claude/skills/` | One folder per skill, named for the job it does, each a `SKILL.md` of plain Markdown that any AI tool can read as instructions; the first is `check-emulator-update` | nowhere |

### frontend/src

Each file opens with a comment saying what it holds; read that first.
Where a screen and the App's side of the same feature would share a name,
the screen's file says so: `libraryscreen.cpp`, `searchscreen.cpp` and
`detailscreen.cpp` are screens, and `library.cpp`, `search.cpp` and
`detail.cpp` are the App's side. A `*flow.cpp` file is the App's side of
one feature, most of them reached from Settings. Every App member function
is in the file named for its feature, and
`grep -n 'App::name(' frontend/src/*.cpp` finds it.

| Area | Files |
|---|---|
| Start and the frame | `main.cpp` (main() and App::run, the start-up in order), `cli.cpp` (every flag but `--check-option-tables`, which `main()` answers before the App exists), `startup.cpp` (start-up and shutdown steps), `frame.cpp` (the frame loop; its order is the behaviour), `input.cpp` (every event, in the order that decides who gets it), `nav.cpp`, `bar.cpp` (the top bar), `curtain.cpp` |
| The App | `app.h` (everything the program holds, as one struct), `actions.cpp` (the one place a screen's request is answered) |
| Screens | `screens.h` (what they share), `libraryscreen.cpp`, `grid.cpp`, `searchscreen.cpp`, `detailscreen.cpp` (a game's page), `settings.cpp`, `accountpanel.cpp`, `qrlink.cpp`, `choice.cpp`, `pin.cpp`, `keyboard.cpp`, `downloads.cpp`, `manualview.cpp`, `achlist.cpp`, `startscreen.cpp` |
| App code that draws its own screen | `home.cpp` (Home), `setup.cpp` (first run; its rules are `firstrun.cpp`, with `firstrun_internal.h`), `serverwait.cpp` (the wait for the server at start). These act as well as draw: first run pairs and joins networks, the wait talks to the server |
| Drawing | `ui.h`/`ui.cpp` (the primitives), `design.h` (the design system's numbers), `motion.h`, `text.cpp`, `image.cpp` (covers), `imagecodec.cpp`, `card.h`, `panels.h`, `screenparts.cpp`, `look.cpp` (the menus' colour), `looks.cpp` (a system's screen looks), `vectrexsheets.cpp` (a Vectrex game's screen sheet over its picture, the picture fitted inside it, and the pause menu's Overlays choice, #224), `backdrop.cpp`, `notice.cpp` and `noticegallery.h` (the short messages), `qr.cpp` and `qrtexture.h`, `timewords.cpp`, `capture.h` (the SIGUSR1 picture), `shaders.inc` |
| The RomM client | `romm.h` (its face and its rules), `romm_types.h` (what it hands back), `romm_internal.h`, `romm_http.cpp` (every request goes out here, and only to the RomM server, #380), `romm_pairing.cpp`, `romm_library.cpp`, `romm_playdata.cpp`, `romm_codes.cpp`, `romm_racetest.cpp` |
| Library data | `library.cpp` (the store of cards, the shelves), `catalog.cpp` and `catalog_internal.h` (which platforms play, on which core), `covercache.cpp`, `coverfill.cpp`, `gridfill.cpp`, `search.cpp`, `codecache.cpp` and `gametdb.cpp` (game codes and controller lists read off files), `samplelibrary.h` (the stand-in library) |
| A launch and a game in play | `launch.cpp` (the frame thread's half), `launch_routes.cpp` (the worker), `play.cpp` (in play, and leaving), `arcadesettings.cpp` (holding Start in an arcade game opens its own settings), `pace.cpp` (the game frame), `pausemenu.cpp`, `gamesession.h`, `romfile.cpp` (what to hand a core), `ncch.cpp` (whether a 3DS file is one Azahar plays: an encrypted dump or a CIA is refused before the core; and whether a name is a CIA's, the games `catalog.cpp` hides from every list), `disc.cpp`, `discs.cpp`, `rewind.cpp`, `states.cpp`, `rumble.cpp` |
| The libretro host | `core.h`, `core.cpp`, `core_state.h`, `core_*.cpp` (discs, hardware rendering, input, memory, options, picture, PS2), `vkhost.cpp`, `vkhost_state.h`, `vkbridge.cpp`, `libretro.h` and `libretro_vulkan.h` (the libretro API, the RetroArch team's), and the console's three option tables: `coreoptions.cpp` (set on purpose for every game), `quality.cpp` (Picture quality) and `sysopts.cpp` (a system's pause-menu options); `screenfx.cpp` (screen looks) |
| PlayStation 2 | `ps2.cpp`, `ps2window.cpp`, `overlaywin.cpp`, and `frontend/ps2/` (its `CabinetPS2Bios.cpp` chooses each game's BIOS by the game's region) |
| Emulators run as programs | `standalone.h`, `standalone.cpp`, `standalone_play.cpp` (also the pointer over a game touched through the console, the Vita's), `standalone_eden.cpp`, `standalone_xemu.cpp`, `standalone_rpcs3.cpp`, `standalone_xenia.cpp`, `standalone_cemu.cpp`, `standalone_vita3k.cpp`, `vpad.cpp`, `vtouch.cpp` (the virtual touch screen the pointer touches with, and gamescope passing touch through), `xtitles.cpp`, `xboxhdd.cpp`, `x360profile.cpp`, `dolphinprep.cpp` |
| Saves and states | `savetable.cpp` (what each core writes, where, and its two tags on RomM: `emulatorTag` for states, `saveTag` for saves; which systems have states and rewind, `snapshotsAllowed` and `rewindAllowed`), `saves.cpp` (sending and restoring all three kinds of save: `syncSave`, `syncDirSave`, `syncFileSaves`), `dirsave.cpp` and `filesave.cpp` (their helpers), `states.cpp`, `statefile.cpp` (a state file's layout: the core's bytes alone, or RetroArch's RASTATE blocks with the achievement progress), `savemirror.cpp`, `uploader.cpp` (the queue), `pending.cpp` (the markers), `offline_sync.cpp`, `playtime.cpp`, `favorites.cpp`; the requests themselves are in `romm_playdata.cpp` |
| Storage | `storage.h` (where everything lives), `storage_internal.h`, `locations.cpp`, `cache.cpp`, `drives.cpp`, `drives_fs.cpp`, `udisks.cpp`, `users.cpp`, `accounts.cpp`, `accountremove.cpp`, `fileio.cpp` (every file the console replaces goes through its `writeBytes`: a temporary, fsync, a rename), `prefs.cpp` |
| Controllers | `players.cpp`, `fightstick.cpp` (the table of known fight sticks, and an arcade game's buttons laid out on one), `controllers.cpp`, `bluetooth.cpp`, `btagent.h`, `battery.cpp`, `batteryrow.cpp`, `shortcuts.cpp`, `wiiremote.cpp` (starts the Wii bridge), `wiipair.cpp`, `wiipanel.cpp`, `wiimii.cpp`, `wii.cpp`, `wiiu.cpp` |
| The machine | `net.cpp`, `wifi.cpp`, `power.cpp`, `power_menu.cpp`, `powerprofile.cpp`, `volume.cpp`, `sound.cpp`, `gpu.cpp`, `cpus.cpp`, `idle.cpp`, `proc.cpp` (running a program with no shell), `unit.cpp` (asking systemd for a root job), `update.cpp` (System update, the console's side), `files.cpp` (File access, the console's side), `server.cpp` and `serveraddress.cpp` (the RomM server this console belongs to) |
| RetroAchievements | `achievements.cpp` (the sign-in and rcheevos' client; its console table names every system), `achievements_ui.cpp`, `ra_list.cpp`, `ra_net.cpp`, `ra_state.h` |
| Settings features | `settingsflow.cpp` (the rows and their dispatch), `settingsids.h`, `about.cpp`, `credits.h`, `access.cpp` (File access and Developer access), `wikiqrview.cpp` and `wikiqr.h` (the Wiki's and What's new's QR codes, made by `tools/make-wiki-qr.py`; How to and the link screen draw them), and one `*flow.cpp` per feature: `accountsflow.cpp`, `downloadsflow.cpp`, `drivesflow.cpp`, `pinflow.cpp`, `serveraddressflow.cpp`, `remoteplayflow.cpp`, `tailscaleflow.cpp`, `steamflow.cpp`, `updateflow.cpp`, `reportflow.cpp`; `choiceflow.cpp` asks a question over any screen |
| Remote Play, Steam, the report | `remoteplay.cpp`, `sunshinelog.cpp`, `tailscale.cpp`, `steam.cpp`, `report.cpp`, `samenames.cpp` |
| Test instruments | `probes.cpp` (runs the probes and calls most self-tests; `--same-names-test`, `--fightstick-test`, `--arcade-settings-test` and `--romm-race-test` run from `cli.cpp`), `selftests.cpp`, `coretests.cpp`, `testhooks.cpp`, `discrig.cpp`, `optcheck.cpp` (the option-table check CI runs). Most self-tests live in their feature's own file (`players::test` in `players.cpp`, and so on). They ship in the program and run only from a flag |

### Where it starts

1. systemd starts `cabinetos-session.service` on the first virtual console,
   as the console's own user (`system_files/usr/lib/systemd/system/`).
2. `cabinetos-session` (`system_files/usr/bin/`) reads
   `/etc/cabinetos/session.env`, then starts gamescope on the screen with the
   frontend inside it. When the machine has no Vulkan device gamescope
   accepts, it falls back to cage (software rendered), and last to a headless
   gamescope so the session stays alive.
3. The frontend's `main()` (`frontend/src/main.cpp`) builds the App and calls
   `App::run`.

## 2. What each part owns, and must never do

- **A screen draws, owns its own focus and scroll, and returns an Action.**
  It never touches the network, the disk or a core. (This is the rule for
  the screens past Home in screens.h; Home, first run and the server wait
  are App code, section 1.) `App::apply`
  (`actions.cpp`) is the one place a request is answered, so a screen cannot
  start a download by accident and can be drawn in a test (screens.h's
  header).
- **The App** (`app.h`) owns everything a screen may not: the RomM client,
  the core, the workers, the cache, the navigation. Its member order is its
  destruction order, members point at other members, and threads hold
  `this`, so it is never copied or moved, and a new member goes where its
  dependencies allow (app.h's header).
- **The frame thread must never wait on the network** (romm.h: "IT MUST
  NEVER BLOCK THE FRAME LOOP"). Downloads, uploads, cover loads and most RomM
  requests run on workers; the frame loop reads their state once a frame
  (`job.h`, `launch.h`). **Not yet true everywhere:** a launch loads the core
  on the frame thread at the press (10 to 80 ms measured, #481, "The picture
  freezes while the console waits on the network or a helper"); the game's
  saves are fetched by the launch worker and only written behind the curtain
  (section 6).
- **The RomM client is the only code that talks to RomM.** It is synchronous
  and owns no thread; callers run it on workers. No password ever reaches it
  (RomM's device pairing gives it a token). Plain HTTP must work. It is
  called from many threads at once: the address and the token are read and
  written only under its lock, and a request takes both together (romm.h's
  header). Known exceptions are listed under "Known gaps".
- **The libretro host** (`core.h`, `core*.cpp`) runs one core at a time
  (`Core::shared()`); libretro's callbacks are plain C, so its state is
  process-wide on purpose (`core_state.h`).
- **Emulators run as programs** are started, watched by their window titles
  and stopped by `standalone*.cpp`; a game in one of them is a game like any
  other to the rest of the console.
- **Storage** (`storage.h`) decides where everything lives. Eviction only ever
  walks `cache/`; kept games (`roms/`) and save data are never evicted.
  A save, a state or a screenshot owed to RomM is a marker on disk before
  the upload is tried (`pending.h`), so a crash or a power cut cannot lose
  it; time played and favourites are owed in their own files
  (`playtime.json`, `favorites-owed.json`).
- **The frontend never runs anything as root.** It runs as the console's
  user. For a root job it starts one named systemd unit (`unit.h`) whose
  script in `system_files/usr/libexec/` answers in a status file; otherwise
  it asks a system service (NetworkManager, udisks) directly. Either way a
  polkit rule in `system_files/usr/share/polkit-1/rules.d/` names what the
  console's user may do, and anything else is refused.
- **Running another program goes through `proc.h`, never a shell.**
  Exceptions today: the `--first-run-writes` and `--playtime-test` self-tests
  remove their scratch folders with `std::system`; the Wii bridge is started
  with `fork` and `execl` (`wiiremote.cpp`); and the frontend restarts itself
  with `execv("/proc/self/exe")` (`startup.cpp`).
- **The image** (`Containerfile`, `build_files/`) puts what it installs in
  `/usr`, which every update replaces, and a few settings files in `/etc`
  (`system_files/etc/`, and the signing policy written by
  `require-signed-updates.sh`). Nothing goes in `/var`, which an update never
  touches (lessons rule 9). The `require-*.sh` scripts fail the build rather
  than ship an image that cannot work.
- **CI** builds, stages and promotes. The scheduled jobs (the base check
  monthly, the controller list and Steam's spare weekly) open pull requests
  and never merge them.

## 3. The main flows, step by step

### Start-up, to Home

`App::run` (`main.cpp`) calls, in this order:

1. `parseCommandLine` (`cli.cpp`). A flag that exits ends the parse there.
2. `resolveCoresAndServer` (`cli.cpp`): where the cores are (`--core-dir`,
   else `cores/build` when it exists in the working folder, else
   `/usr/lib/cabinetos/cores`), and the server's address: `--romm`, else
   `CABINETOS_ROMM`, else `/etc/cabinetos/session.env`, else the console's
   `config/server.json` (`serveraddress.cpp`).
3. `earlyProbe` (`probes.cpp`): the tests and probes that need no storage
   and no window run and exit here.
4. `startStorage`, then `storageProbe`: the storage tree, then the probes
   that need it.
5. `startDisplay`: the window and the GL context.
6. `runFirstRun`: first run, if the console is not set up (`firstrun.h` for
   its rules, `setup.cpp` for its screens).
7. `startStartupScreen`, then `connectToServer`: the console waits for its
   server; when there is a game on the drive to play, it goes offline after
   15 seconds and tries the server in the background, otherwise it keeps
   waiting (`serverwait.cpp`).
8. `startBrowsing`; `startSync`, which starts the upload queue and sends
   first whatever an earlier run still owed (`uploader.resendOwed`);
   `recoverPowerCutSaves`, which finishes the saves a power cut left in an
   emulator run as a program (an Xbox game's drive through
   `recoverDriveSaves`, which that game's launch also runs); then the rest
   of the start-up steps.
9. `runFrameLoop` (`frame.cpp`) until the console exits or restarts; then the
   game is left, the emulators closed, the core unloaded, and `shutDown`
   (`startup.cpp`): the uploads are sent and RetroAchievements' unlocks put
   on disk first, then the workers are stopped. That rest is given two
   seconds (#305), except for a restart in place; past them the process ends
   and names what it was waiting on.

### Pressing Play on a game

1. The game's page returns an Action; `App::apply` (`actions.cpp`) asks
   `refuseRemoteStart` (`launch.cpp`), which stops a Wii Remote's press on a
   game that isn't a Wii game, then calls `launchById`, which calls
   `beginLaunch` (`launch.cpp`). On the frame
   thread and with no curtain, `beginLaunch` sets the core's options and
   loads the core (not for an emulator run as a program), records the keep
   for a Download, decides where the game goes, and starts the launch
   worker.
2. The worker (`launch_routes.cpp`) fetches the platform's firmware from RomM
   first, then takes one of four routes, in this order: a game on several
   discs, a game installed before it runs (PS3), a folder of files, one file.
   The bytes go straight to disk: under `cache/` for a game pulled to play,
   under `roms/` for a game kept, on the console's own storage or a drive.
   An archive the core does not open itself is unpacked beside it
   (`romfile.cpp`, `prepareFile`), each file stamped with the archive's own
   time; a later start that finds every file at its size with that time
   skips the unpacking and its room check (`alreadyUnpacked`, #530).
   With the game on the disk, a 3DS file Azahar cannot play (an encrypted
   dump, a CIA) ends the job with its reason (`ncch.cpp`); otherwise it
   fetches the game's saves from RomM, the rows
   and the bytes a restore may pick (`fetchLaunchSaves`; `saves.cpp`,
   `fetchSaves`), and the Mii Channel's for a Wii game, then marks the job
   Ready.
3. Each frame, `pumpLaunch` (`launch.cpp`) reads the job. A failed job is
   undone (the keep record comes out; the bytes stay, to be evicted). A
   Download that was not a Play stops when the game is on disk. A game the
   person walked away from finishes quietly and does not start. A launch
   that loads no game here (these three, a core that refuses the game, an
   emulator run as a program) forgets what its press asked for: a picked
   state, Home's Resume, a start disc (`forgetLaunchAsks`).
4. The curtain comes down before the rest of the work on the frame thread
   (writing the fetched saves and loading the game), so the person sees a
   curtain, not a frozen screen; nothing behind it asks the network. A game for an emulator run as a program starts it here
   (`standalone_play.cpp`) and ends this flow; an Xbox game whose last saves
   are still on its drive has them taken off first (`recoverDriveSaves`,
   `startup.cpp`): to this person's folder and the server, or to the folder
   of the person they belong to, who sends them at their next start. Only a
   drive that will not give them up stops the game.
5. For a core: this person's save folder for this game and core is made, a
   folder save is written from the fetch and the starting disc chosen
   (`prepareSaveDirAndDiscs`); then the file saves (most saves) are written
   and Dolphin's settings and the Miis, all before the load
   (`restoreBeforeLoad`).
6. The core loads the game (`loadLaunchedGame`); `setUpLoadedGame` sets up
   the session; then the battery save is written into the core, the options
   the core was given are logged, sound and pacing open, and play starts
   (`startPlaying`). The restore order and its rules are one comment on
   `pumpLaunch`.

### Leaving a game

`App::finishExit` (`play.cpp`), in order: the time played is closed and
queued for RomM; the battery save is sent (`syncSave`); the disc in the drive
is remembered for the next Play, and the drive put back on its first disc;
RetroAchievements stops reading memory; the core unloads; then folder saves
and file saves are sent, after the unload, because that is when a core
flushes them (`syncDirSave`, `syncFileSaves`, all in `saves.cpp`). A game in
an emulator run as a program sends its saves when the program ends
(`standalone_play.cpp`, and `finishInterruptedSave` in `saves.cpp` after a
power cut).

### Saves and states reaching RomM

1. A save, a state or a screenshot is written on disk, and a marker saying
   what to send is written before the upload is tried (`pending.h`; the
   markers are in `users/<id> - <name>/pending/`).
2. The upload queue (`uploader.h`) sends it on its own thread as the person
   who made it (`romm_playdata.cpp` makes the request), and removes the
   marker when RomM has it. A marker left by a crash is sent again at the
   next start (`startSync`). One that failed is sent again every five
   minutes, when the server comes back and after a person's next upload
   works; one RomM refused (an HTTP 4xx) keeps its marker and waits for the
   next start or a new save of it. States: the newest three per game and
   emulator tag are kept on RomM.
3. Each core's saves, where it writes them and the tag they carry on RomM are
   one table, `savetable.cpp`. A launch restores the newest row under its
   tag, except that this console's own copy wins while it is still owed to
   RomM, and is used when RomM has none (`saves.cpp`). The rows and bytes
   are fetched on the launch worker (`fetchSaves`) and written on the frame
   thread; a save of the game that lands on RomM between the two makes the
   fetch stale, and the console's copy stands.
4. Offline, the console uses its copy of RomM's rows as last seen
   (`savemirror.h`), so a launch decides as it would online. What offline
   will need is fetched while online, and the server is tried in the
   background while it is away (`offline_sync.h`); what is owed waits as
   markers on disk.

### System update

1. The frontend (`updateflow.cpp`, through `update.cpp`) starts
   `cabinetos-update-check.service` with `unit::start`, by hand or weekly
   when that setting is on.
2. The unit runs `/usr/libexec/cabinetos-update check` as root: `bootc
   upgrade --check` and the size of what is missing. It writes the answer to
   `/run/cabinetos-update/status`.
3. The root script decides: it reports an update whenever bootc finds a
   different image (bootc compares digests, not versions), and "ready" when
   one is already staged. The frontend shows what the file says, and between
   checks still offers a version it found earlier if that is not the one
   running.
4. Download starts `cabinetos-update-download.service`, which stages the new
   image; the next restart or power off applies it.

How a change gets into that image is in CONTRIBUTING.md, "How a change
reaches a console".

## 4. Where to look for the most common changes

| To change | Look in |
|---|---|
| Words or layout on a screen | the screen's file (section 1), its numbers in `design.h`; judged on the television |
| A Settings row | `settingsflow.cpp` (the rows and what each does), the feature's `*flow.cpp`, docs/SETTINGS.md |
| What a button does, or where it goes | `input.cpp` (who gets the event), `nav.cpp`, `actions.cpp`; then About, How to, and `docs/wiki/navigation.md` |
| Which core plays a platform | `catalog.cpp` (the platform table), `savetable.cpp` (its saves' tag) |
| An option the console sets on a core | `coreoptions.cpp`, `quality.cpp`, `sysopts.cpp`, `wii.cpp` (Dolphin's rumble for Wii) and `vectrexsheets.cpp` (the Vectrex picture fitted to its sheet): the five sources `launch.cpp` merges, in that order, with a test's `--core-option` laid on top of all five by the core host (`Core::setCommandLineOption`), and `--check-option-tables` checks against `cores/options/` (`optcheck.cpp`); a libretro core in `catalog.cpp`'s table with no list there fails it |
| A new libretro core | `cores/build-core.sh` (a `case` arm, and any patches in an `if [ "$CORE" = <core> ]` block of its own; its name there is the core's name everywhere, and the file is that name without a trailing `_libretro`, plus `_libretro.so`; nothing else in that file changes, or CI rebuilds every core: `ci/core-cache-key.sh`), `build-core.yml`'s list, the two counts of 23 (`ci/stage-image-payload.sh`'s `EXPECTED` and `build_files/install-frontend.sh`), `cores/options/<core>.txt` (made from what `tools/core-options.c` prints for the built core; `cores/check-options.sh` runs it), `catalog.cpp` (the platform's row; the table is positional, read its struct first), `savetable.cpp` (its two tags), `coreoptions.cpp`, `achievements.cpp`'s console table for a new system, `docs/wiki/systems.md`, and the licences (CONTRIBUTING.md, "Duties") |
| Moving a core's pin | its `COMMIT` in `cores/build-core.sh`; in the same commit `cores/options/<core>.txt`, and the same commit in `licences/SOURCES` (then `tools/fetch-licences.py`) and `docs/LICENCES.md` |
| An emulator run as a program | `standalone.h`, its `standalone_<name>.cpp`, its `build_files/install-*.sh` or `system_files/usr/share/cabinetos/flatpaks.list`, its row in `catalog.cpp` |
| A RomM request or field | the client (`romm.h`, `romm_*.cpp`, `romm_types.h`); RomM's documentation and behaviour decide; the map of every request is #398's Part 5, section 48 (the audit's comments on #398 each start "Audit, Part N", and sections are numbered across the parts) |
| Saves and states | `savetable.cpp` (what and where), `saves.cpp` (sending and restoring), `states.cpp`, `statefile.cpp` (what a state file holds), `uploader.cpp` (the queue), `pending.cpp` (the markers), `romm_playdata.cpp` (the requests), `standalone_play.cpp` (emulators run as programs) |
| Something in the image (a package, a unit, a rule) | `build_files/`, `system_files/`; an image build, so it is proven on `testing` |
| Which output plays the sound (the TV, headphones, a built-in speaker) | `system_files/usr/share/wireplumber/wireplumber.conf.d/60-cabinetos-sound-output.conf` (the rule, and why each part exists) and `build_files/built-in-speaker.sh` (a built-in speaker counts as not there: one PipeWire file edited at build, and the build fails if PipeWire changes it); docs/PROJECT.md, decision 46 |
| Something the console must do as root | a script in `system_files/usr/libexec/`, its unit, a polkit rule naming the unit, `unit::start` from the frontend, and a status file |
| A command-line flag or a self-test | `cli.cpp` (the flag), `probes.cpp` (where a probe or test is called), the feature's own file for its test (`players.cpp`'s `players::test`), `selftests.cpp`, `testhooks.cpp`. There is no test framework: a test is a flag that exits with 0 when it passes |

## 5. Decisions, and why

Each with where it is recorded.

- **Screens return Actions; the App acts.** So a screen cannot start a
  download by accident (main.cpp's and screens.h's headers).
- **One program, which owns the frame and hosts the cores itself**, instead
  of a skin on RetroArch or another frontend (docs/PROJECT.md, "Constraints
  and principles", 4; "How Cabinet hosts cores").
- **Big emulators run as programs, played as games** (docs/PROJECT.md, open
  question 32).
- **Pulled games are a cache; kept games stay** (docs/PROJECT.md, "The
  backend model"; `cache.h`).
- **gamescope, not Bazzite's gamescope-session-plus** (cabinetos-session's
  header; docs/PROJECT.md, open question 3).
- **The on-disk layout** (docs/PROJECT.md, open question 18; `storage.h`).
- **Vulkan first in the core host** (docs/PROJECT.md, open question 20).
- **The tested image is what ships**; a merge promotes it instead of building
  again (docs/SETTINGS.md, System, "THE TESTED IMAGE IS WHAT SHIPS").
- **Updates are signed, and the console checks** (`build_files/require-signed-updates.sh`).
- **No emulator settings page; three picture levels** (docs/SETTINGS.md, "The
  screen"; `quality.h`).
- **Settings, as built** (docs/PROJECT.md, open question 31; docs/SETTINGS.md
  is the final state).

## 6. Known gaps

- **The frame thread loads the core at the press** (`beginLaunch`, 10 to
  80 ms measured) and loads the game behind the curtain (`pumpLaunch`). The
  game's saves have come from the launch worker since #481; nothing behind
  the curtain asks the network (`launch.cpp`, `saves.cpp`).
- **RomM paths spelled outside the client:** the firmware and one-file game
  downloads (`launch_routes.cpp`), `probes.cpp`, the manual (`manual.cpp`),
  avatars (`startup.cpp`), small and big covers (`image.cpp`) and the cover
  cache's key (`covercache.cpp`). They should move into the client.
  `ci/check-romm-paths.sh` lists them and fails a pull request that adds
  another.
- **CI runs one self-test** (`--check-option-tables`); the others are run by
  hand. No Python is tested, and neither are `tools/` and `disk_config/`.
  A pull request to `main` never builds the cores, `frontend/ps2`, Cemu,
  Sunshine or the image, so a fault there first shows at the maintainer's
  testing push, including a core's options list that no longer matches
  (`cores/check-options.sh` runs inside the core build).
- **No test framework and no single local check command:** a test is a flag
  in the shipped program. The compiler's warnings are listed, not counted
  against a baseline.
- **Some test tools lean on state the maintainer set up by hand** on his
  console: `tools/ui-loop.sh` (a folder of cores and the builder image),
  `tools/check-applied.sh` (the builder image) and the disc rig
  (`tools/disc-rig/README.md`). Their machines come from the environment
  (CONTRIBUTING.md, "Running and testing").
- **Some of the maintainer's own details are still in the code:** the empty server
  address field on first run shows his server's address as its example
  (`setup.cpp`), and comments carry his controllers' Bluetooth addresses
  (`bluetooth.cpp`, `bluetooth.h`, `players.cpp`, `players.h`).
- **A fork's image carries the project's key and image name until the fork
  edits them:** they are written into tracked files (CONTRIBUTING.md,
  "Generate a signing key", lists them).
- **A test stand-in is not in the repository:** the frame-time tool many
  comments cite as `frames.py`. (`racemock.py`, which `--romm-race-test`
  needs, is `tools/racemock.py` since #488.)
- **Not built yet:** choosing a different core for a game, and exporting
  (`detailscreen.cpp`'s note).
- **The test instruments ship in the program**, run only by a flag.
- **A scratch `--storage-root` does not isolate a run on a console:** the
  sign-in tokens and the PIN live in `~/.config/cabinetos/` and Remote
  Play's files in `/var/lib/cabinetos`, whatever the storage root
  (`accounts.cpp`, `remoteplay.cpp`), so a headless test on a signed-in
  console acts as that console's person. A scratch `HOME` and
  `CABINETOS_DRIVES` separate the sign-in and the drives
  (CONTRIBUTING.md, "Running and testing"); Remote Play's files stay.
- **No documented way to run your own build on your own console**, and the
  environment variables a run off a console needs (`CABINETOS_SHADERS`,
  `CABINETOS_PAD_DB`, `CABINETOS_ASSETS`, `CABINETOS_STORAGE`,
  `CABINETOS_DRIVES` and others; `grep -n 'getenv("CABINETOS_' frontend/src/*.cpp`)
  are written down nowhere.
- **Positional data that an ordinary edit can break silently:** the App's
  member order (app.h), `Uploader::Job`'s field order (uploader.h), and the
  rows of catalog.cpp's table.
- **Building leaves files in the tree:** the Makefile writes `.o` and `.d`
  files beside the sources, and the `.d` files are not ignored by git.
- **The faults the audit recorded** are on #398, Parts 3 to 5, and become
  pull requests or issues next.

## 7. Where the docs and the code disagree

The audit (#398, Part 5 sections 44 and 45) found the docs and the code
apart in many places. The docs were brought into line in the docs tidy
(#427, "Docs tidy: every doc true to the code, or marked as history
(#398)", 2026-10-17): frontend/README.md cut down to the build; the
2026-09-20 snapshot, the three history files and the old frontend README in
`docs/history/`; the stale lines fixed where they stand, and in
docs/PROJECT.md noted under the stale line with a date, since its rule then
was to record a reversal rather than edit history; an index at the top of
PROJECT.md giving where each question ended. What is left is in code
comments, which the faults step fixes. Until then, trust the code:

- **Code comments:** 59 point at the wrong place, most saying "in main.cpp"
  for code the restructure moved (app.h's own header among them). Several
  scripts say twenty-one cores (`Containerfile`, `.gitignore`,
  `cores/build-pcsx2.sh`, which also says it applies no patches and applies
  one); `build_files/build.sh` and `build_files/require-cec.sh` call HDMI-CEC
  a hard requirement, though it was shelved on 2026-09-24; several workflow
  headers say pull requests to `main` build the image, which they do not.
  Others: `frontend/Containerfile` cites an `archive.h` that `frontend/src/`
  does not have; `selftests.cpp` says `--first-run-rules` runs in CI (it does
  not); `samplelibrary.h` says main.cpp is its only user (startup.cpp is);
  `cores/check-options.sh` names catalog.cpp for what is now
  `coreoptions.cpp`; `cores/build-core.sh` says core pins follow Cabinet's
  manifest and "are not to be updated here", which predates both Cabinet
  ceasing to be a constraint (2026-10-11) and the stability rule for moving
  pins (`docs/decisions/2026-10-23-when-an-emulator-pin-moves.md`; first set
  2026-10-14), which is the rule now.
- **More code comments the docs tidy found:** Developer access placed in
  "Settings, System" (`system_files/etc/ssh/sshd_config.d/30-cabinetos.conf`,
  `cabinetos-developer.service`, `enable-ssh.sh`, `app.h`; it is the last row
  of About); `frontend/Makefile` points to README.md for the loop, which is
  CONTRIBUTING.md, "Running and testing"; `frontend/ps2/CabinetPS2Host.cpp`
  and `.h` cite docs/PCSX2-HOST-SURFACE.md, now in `docs/history/` (a stub
  keeps the old path); `storage.h`'s layout comment, which
  frontend/README.md's tree corrects; Flycast said to have no shared tag
  (`gamesession.h`, `cores/build-core.sh`; `savetable.cpp` gives it one);
  `savetable.cpp`'s own claim that only cores built as Cabinet builds them
  are listed; "twenty-one cores" in the workflows too (`build.yml`,
  `build-core.yml`, `build-pcsx2.yml`, `build-frontend.yml`).

## 8. What leans on other projects

Everything CabinetOS has built that works only because another project
behaves a way it does not promise: a patch to its source, a settings key or
file it does not document, its output or its log read as text, the name of
one of its settings, a file laid over one of its own. Public, documented
interfaces (libretro, SDL's API, D-Bus services, RomM's API, a program's
documented settings) are not listed, and neither is a package or library
simply being there, which the image build asserts (`build_files/require-*.sh`,
`build_files/build.sh`, the `ldd` checks in each `install-*.sh`). Started
2026-10-21 (MMagTech asked for one place to read when an upstream moves);
the rule for adding a row is CONTRIBUTING.md, "Duties that come with some
changes".

Each row says where it is checked when that project moves, and what moves
it:

- **The Bazzite base** moves by the monthly base-update pull request.
  `ci/check-base-update.sh` checks some things itself (gamescope's flags,
  the builders' Fedora, the strip lists); the rest are lines in
  `ci/base-watch.txt`, which the pull request lists for the areas it
  changed: `log:` lines `tools/base-checks.sh` runs on the reference
  console, and `tv:` lines for MMagTech.
- **A pin of ours** (a core, PCSX2, an emulator, Sunshine, a Flatpak)
  moves only when somebody edits it. Patches are asserted, so a pin move
  that breaks one stops the build; the rest is gone through by hand, from
  these rows, when the pin moves.
- **Terra's Steam session** floats: every image build installs terra's
  latest (`build_files/install-steam-session.sh`).
- **Steam's client** updates itself, whenever it likes, and
  **RetroAchievements' server** moves on its own. Nothing can check either
  ahead.

"Not checked" means a change shows first on a television or in a player's
report.

### Moved by the Bazzite base

The area named is the one in `ci/base-watch.txt` whose checks a change asks for.

| What | Leans on | Checked when the base moves |
|---|---|---|
| gamescope's command line | Every flag and backend `cabinetos-session` and `cabinetos-steam-download` pass | `ci/check-gamescope-flags.sh`, in the base-update pull request itself |
| The screen's sleep, a Vita game's touch screen, Remote Play's picture, the TV's size kept across restarts, screenshots of games run as programs | gamescope's own settings, by name: `drm_sleep_external_screen` (`idle.cpp`, `cabinetos-remoteplay`), `touch_click_mode` 4 and 1 (`vtouch.cpp`), `composite_force`; `GAMESCOPE_PATCHED_EDID_FILE`, `GAMESCOPE_MODE_SAVE_FILE` and `gamescopereaper` (`cabinetos-session`); `gamescopectl screenshot`, which returns before its file is written (`states.cpp`). The console reads gamescopectl's exit as "gamescope took it", which says only that gamescope answered | `log:` (The picture on the TV): the names. Not checked: what 4 and 1 mean, the screenshot's timing |
| The pause menu over a PS2 game or a game run as a program | The `STEAM_OVERLAY`, `STEAM_INPUT_FOCUS` and `GAMESCOPE_NO_FOCUS` properties, gamescope placing a window when it maps (`overlaywin.cpp`), a transparent window, 30 clear frames before it shows (`standalone_play.cpp`) | `tv:` (The picture on the TV): the pause menu over a PS2 game; `tools/gamescope-overlay-test.sh` by hand |
| Smooth frames | gamescope keeping one picture waiting, so every emulator is given vsync (`standalone_*.cpp`, `frontend/ps2/CabinetPS2Host.cpp`); the refresh SDL reports under it (`pace.cpp`); a new emulator window taking the screen by itself; gamescope ending with its program, so the console restarts itself in place (`startup.cpp`) | `tv:` One heavy game. Frame times are measured by hand |
| PS2's own window, and closing an emulator politely | SDL choosing X11 under gamescope and `GAMESCOPE_WAYLAND_DISPLAY` (`ps2window.cpp`); Xwayland listing each emulator's window under the root with `_NET_WM_PID` and `_NET_WM_NAME`, and `WM_DELETE_WINDOW` (`xtitles.cpp`) | Not checked: PS2 falls back to copying frames, and a close to SIGTERM, without a word |
| Which output plays the sound | WirePlumber's documented settings (`system_files/usr/share/wireplumber/wireplumber.conf.d/60-cabinetos-sound-output.conf`); PipeWire making the TV's output again at start, which `volume.cpp` waits for | `log:` and `tv:` (Sound) |
| A built-in speaker never taking the TV's sound (#550, "Sound: a built-in speaker never takes the sound from the TV") | PipeWire's `/usr/share/alsa-card-profile/mixer/paths/analog-output-speaker.conf`, edited at image build: its `[Jack Speaker Phantom]` and `[Jack Speaker Front Phantom]` sections say "no" where they said "unknown", so the speaker's output counts as unplugged; and PipeWire's rule that a plugged jack's "no" outweighs everything else (`calc_port_state`, read at 1.6.9) | The image build stops if the file is gone or either section does not read as at PipeWire 1.6.9 (`build_files/built-in-speaker.sh`). Not checked: that the rule still works, which needs a machine with a built-in speaker; the A9 has none |
| The volume keys | `wpctl get-volume`'s "Volume: 0.50" and `set-volume` (`volume.cpp`); every key device readable through systemd's seat rules, after `system_files/usr/lib/udev/rules.d/71-cabinetos-key-devices.rules` | `log:` (Sound; the TV must be awake) and `log:` (Kernel and startup) |
| Sound controllers kept awake | tuned's post-loaded profile, read only from `/etc`, and its audio plugin (`system_files/usr/lib/tuned/profiles/cabinetos-audio/tuned.conf`) | `log:` (Power) |
| Games on Bazzite's own tuned profile | tuned's `switch_profile` and the names `balanced` and `throughput-performance-bazzite` (`powerprofile.cpp`) | `log:` (Power) |
| Picture quality's Auto | amdgpu's `pp_dpm_sclk`, cpufreq's and the topology's files (`gpu.cpp`, `cpus.cpp`) | `log:` (Kernel and startup) |
| Controller drivers loaded at boot | The kernel's module names, and its list of pads that wait for their own driver (`system_files/usr/lib/modules-load.d/cabinetos-controllers.conf`) | `log:` No service failed to start (Kernel and startup); `series:` Controllers lists the list's changes, for when the file can go |
| A real Wii Remote kept for the bridge | 92-viia.rules (`ublue-os-udev-rules`) opening every hidraw node before ours closes the Remote's; `/dev/uhid`'s grant; `HID_NAME` in a node's uevent; SDL's Wii driver switched off by its hint (`startup.cpp`) | `log:` and `tv:` (Controllers) |
| A Remote with Nintendo's own id counting once (#549, "A Nintendo Wii Remote counts once, not as a pad and a Remote") | The kernel's Wii Remote driver, hid-wiimote, taking exactly Nintendo's two ids over Bluetooth (057E:0306 and 0330, kept for it in hid-quirks.c), and udev's `ID_INPUT_JOYSTICK` and `LIBINPUT_IGNORE_DEVICE` on its devices, which `system_files/usr/lib/udev/rules.d/72-cabinetos-wii-remote.rules` sets to hide them | `log:` (Kernel and startup): the driver takes exactly those two ids; `series:` Controllers lists its changes on a new kernel series. Not checked: a Remote with Nintendo's id itself, which the A9 does not have (its Remotes are TechKens) |
| Pairing pads and Wii Remotes | bluetoothctl's text (`bluetooth.cpp`, `wiiremote.cpp`); BlueZ wanting an agent for "just works" pairing (`btagent.h`) and its wiimote plugin offering the Wii's PIN (`wiipair.cpp`); btmgmt's `dev_found` and `name` lines (`cabinetos-wii-search`) | `log:` (Controllers): bluetoothctl's lines, btmgmt there; `tv:` a pad, a Wii Remote. Not checked: btmgmt's lines and the agent, seen when a pad is paired |
| Which pad is which, its battery and rumble | SDL's serials, paths and GUIDs (the shortcut button is kept per GUID, `shortcuts.cpp`), `SDL_GetGamepadPowerInfo`, a made-up joystick id on pushed events (`wiiremote.cpp`); the kernel's power_supply and input files (`battery.cpp`, `players.cpp`) | `tv:` A Bluetooth pad (Controllers, Libraries the console's screens use), Rumble. Not checked: the battery row, a kept shortcut button |
| Network settings and first run | nmcli's terse fields and its English error words, "secrets" for a wrong password among them (`net.cpp`, `wifi.cpp`); pkcheck's answer (`probes.cpp`) | `log:` (Network): the fields. Not checked: the error words |
| Remote Play's ports and the report's link | The default firewall zone leaving 1025 to 65535 open (`report.cpp` takes a port the system picks) | `log:` (Network) |
| Remote Play away from home | `tailscale status --json`'s fields and state words, `tailscale up` refusing unless every setting is named (`tailscale.cpp`, `tailscaleflow.cpp`), `tailscale set`'s flags (`system_files/usr/lib/systemd/system/tailscaled.service.d/50-cabinetos.conf`) | `log:` and `tv:` (Remote Play away from home). Not checked: `up`'s rule |
| Every root job | Our polkit rules, reading polkit's subject and action fields (`system_files/usr/share/polkit-1/rules.d/`) | `log:` and `tv:` (What the console may do as root) |
| System update | `bootc status --json`, the text of `bootc upgrade --check` and its progress lines, skopeo and jq, ostree's names for the layers it holds (`cabinetos-update`); `/proc/cmdline`'s `ostree=`, the deployment's `.origin`, `/usr/share/ublue-os/image-info.json`, which no package owns (`update.cpp`) | `log:` and `tv:` (System update) |
| Games drives | udisks's object paths (the console works out its own disk's, `udisks.cpp`), its properties, Format's `take-ownership` and `mkfs-args`, "busy" in its errors, mounts under `/run/media/<user>` (`drives.cpp`) | `log:` (Games drives): the A9's drive is mounted. Not checked: Format, Eject, the path naming |
| Game manuals | pdfinfo's "Page N size" and "rot" lines and pdftoppm's PPM (`manual.cpp`) | `log:` (Game manuals) |
| The type ramp | The font files' names (`startup.cpp`) | `log:` and `tv:` (Libraries the console's screens use) |
| Covers in every format | SDL3_image loading its codecs when first needed (`imagecodec.cpp`) | The image build asserts their sonames (`build_files/require-frontend-libs.sh`); `tv:` Home's covers |
| Downloads | libcurl refusing a raw space in an address, its error codes read as "server away" (`romm_http.cpp`) | `tv:` A download |
| Original Xbox and Switch | Flatpak's system layout, checked to say an emulator is installed (`standalone.cpp`); `flatpak run`'s flags; the program three processes down its sandbox (`proc.cpp`) | `log:` and `tv:` (Original Xbox and Switch games) |
| One shader cache per emulator | Mesa making `mesa_shader_cache` inside `MESA_SHADER_CACHE_DIR` (`standalone.cpp`, `standalone_xemu.cpp`) | Not checked: every game would compile its shaders again |
| The power button | logind's inhibitor, and the power key reaching SDL through gamescope (`input.cpp`, `power.cpp`) | Not checked |
| The desktop kept out, the builders in step | The packages the strip scripts remove; the builders' Fedora | `ci/check-base-update.sh`, in the pull request itself; `log:` (Apps CabinetOS removes) |
| The Steam tile | The `steam` package and Steam's session file | `log:` and `tv:` (Steam) |
| The diagnostic report | The text of many tools (`journalctl`, `nmcli`, `bluetoothctl`, `wpctl`, `tailscale`, `vulkaninfo`, `edid-decode` and others) read by `system_files/usr/libexec/cabinetos-report` | The build asserts the tools are there (`build_files/build.sh`); their text is not checked. A change shows as a missing part of the next report |

### Moved by a pin of ours

| What | Leans on | Checked when the pin moves |
|---|---|---|
| melonDS's last save before quitting | A patch: `NDSCart_SRAMManager::FlushSecondaryBuffer()` added to `retro_unload_game` (`cores/build-core.sh`, the melonds block; Cabinet's) | The patch's anchor is asserted, so the build stops. No issue (it predates the rule of 2026-10-20) |
| PPSSPP's shader cache, and its CPU engine named in the log | Two patches: the cache saved in `GPU_GLES::DeviceLost`, a log line in `MIPSState::Init` (`cores/build-core.sh`, the ppsspp block; Cabinet's) | Asserted. No issue (they predate the rule) |
| 3DS games get no network | A patch to Azahar's `ac`, `soc_u` and `http_c` services (`cores/build-core.sh`, the azahar block) | Asserted, each replacement exactly once; tracked on #537 ("Azahar patch: games get no network (until Azahar has an option)") |
| Each 3DS game its own folders | A patch to Azahar's `SetUserPath` | Asserted; tracked on #534 ("Azahar patch: each game sets its own folders (until Azahar fixes SetUserPath)") |
| PS2's sound through the console | A patch to PCSX2's `pcsx2/Host/AudioStream.cpp`, and `AudioStream`'s own members (`frontend/ps2/CabinetPS2Audio.cpp`) | The anchor is asserted (`cores/build-pcsx2.sh`); a changed member fails the build. Not an upstream fault, so no issue |
| The PS2 host (`frontend/ps2/`) | PCSX2's internals, not a library interface: 55 `Host::` functions, `EmuFolders`, `VMManager`'s start order, `PadDualshock2`'s inputs, the settings keys it fills in memory (PCSX2 ignores unknown ones), the patch named "Widescreen 16:9", `GSSaveSnapshotToMemory`, the memory map RetroAchievements reads, and a linker wrap of `InputManager::SetPadVibrationIntensity` by its mangled name | A missing function fails the build (`-z defs`, the probe in `cores/build-pcsx2.sh`). The picture keys: `tools/check-applied.sh` reads the `[ps2] renderer` line. Not checked: the other keys, the rumble wrap (it links and does nothing), the memory map |
| PS2's widescreen patches | `PCSX2/pcsx2_patches` at a pinned commit, packed as PCSX2 does | `cores/build-pcsx2.sh` asserts the commit and that `patches.zip` was made |
| Every option the console sets on a libretro core | The option names and values each core declares | `--check-option-tables` in CI, and `cores/check-options.sh` at each core build (`cores/options/`); Dolphin's and FBNeo's lists by hand (`tools/check-applied.sh --lists`). Not checked: the defaults the console leaves alone and relies on (`coreoptions.cpp`) |
| How each core is driven | Behaviours of single cores the host works around: Flycast's four ports and threaded renderer, PPSSPP's Vulkan 1.0 request and its abort at a raised log level, Dolphin's mid-game geometry and its memory map from the first frame, melonDS's GL context, Azahar's states, Mupen64Plus-Next's frame per state, the queue lock argued from each Vulkan core's source (`core*.cpp`, `vkhost.cpp`, `pace.cpp`) | Not checked: seen only in a game on that core |
| A fight stick's layout in arcade games | The order each arcade core declares a game's buttons in: FBNeo in the game's own order, MAME 2003-Plus in RetroPad id order with its Classic table (`EMIT_RETROPAD_CLASSIC` in `src/mame2003/mame2003.c`) saying which id is which button, copied into `fightstick.cpp`; and FBNeo's own combination buttons, known only by their names, "Buttons AB" and the like (`src/burner/libretro/retro_input.cpp`) | MAME 2003-Plus: the core's build stops when the table, or the plain joypad that selects it, reads otherwise (`cores/build-core.sh`, its mame2003_plus block); and a `[fightstick]` line in the log when a label's own button number ("Button 3", "B3: ...") is not the table's. FBNeo: its build stops when a combination button is named otherwise, the "P1 " is no longer taken off a name, or a game's own button is named "Buttons ..." (its fbneo_libretro block). Not checked: FBNeo's order, seen only as a stick's buttons in the wrong places |
| Holding Start opens MAME 2003-Plus's own menu | MAME 2003-Plus opening its menu on Tab or on player 1's ninth button (`IPT_UI_CONFIGURE` in `src/inptport.c`), the left stick click being the ninth button in all four of its layouts (`EMIT_RETROPAD_*` in `src/mame2003/mame2003.c`), and its reading the keyboard on port 0 beside the pad while `mame2003-plus_input_interface` is at its default, "simultaneous" (`osd_is_key_pressed`); the console presses Tab and keeps the click from it (`arcadesettings.cpp`). FinalBurn Neo's own hold, its `fbneo-diagnostic-input` left at "Hold Start" | MAME 2003-Plus: the core's build stops when the menu's keys, the click's button or the option's default read otherwise (`cores/build-core.sh`, its mame2003_plus block). Not checked: FinalBurn Neo's default, seen as a hold that opens nothing |
| Where each core keeps saves and firmware | File names and folders each core writes and reads: Flycast's VMU, the Sega CD `.brm`, 3DO's nvram, MAME's and FBNeo's, PPSSPP's `SAVEDATA`, Azahar's `sdmc`, the BIOS names (`savetable.cpp`, `filesave.cpp`) | Not checked: a moved file means saves stop travelling, with no error |
| States with achievement progress | RetroArch's state file layout, version 1 (`statefile.cpp`) | Not checked; a state of another version is refused and logged |
| GameCube and Wii saves, rumble, Miis | Dolphin's `Dolphin.ini` (`SlotA = 8`, a raw value of Dolphin's device list), its save and NAND folders, SYSCONF's `BT.MOT`, the Mii file, its device ids as raw numbers (`dolphinprep.cpp`, `savetable.cpp`, `wii.cpp`, `wiimii.cpp`, `core.cpp`) | Not checked |
| The Wii bridge's stand-in Remote | Dolphin and Cemu finding a Remote by Nintendo's id on hidraw, and Dolphin's extension encryption (`wiibridge/`) | `tv:` A Wii Remote (Controllers), when BlueZ moves. Not checked when Dolphin's or Cemu's pin moves |
| Every emulator run as a program | Its process name, found in `/proc` (`eden`, `rpcs3`, `xemu`, `xenia_edge`, `Cemu`, `Vita3K`); exit status 0 read as the player quitting; its log's wording; its window title (Eden's and Xenia's change only while a game runs, `xtitles.cpp` reads them through Xwayland); its honouring of `XDG_*` folders (`standalone.cpp`) | Not checked: a missed match leaves the player on the emulator's own box until the pause menu |
| The virtual pads given to emulators | SDL's GUID layout and button numbering, as each emulator's own SDL computes them (`vpad.cpp`) | Not checked: buttons scrambled with no error |
| Switch (Eden, Flathub pin) | Its flags `-f -g`, portable `user/` folder, `qt-config.ini` keys and their `\default` siblings, `Controls` strings, `profiles.dat` layout and the save folder named from it, keys and firmware folders, log phrases and loader error numbers, window title (`standalone_eden.cpp`, `standalone.cpp`, `quality.cpp`) | `tools/check-applied.sh` reads the picture keys from Eden's log. The rest is not checked: a renamed key is a box over the game, a changed `profiles.dat` stops Switch saves travelling with no error |
| PS3 (RPCS3, our pin) | Its flags, the log lines that say an install worked, `config.yml`, `CurrentSettings.ini`, `Default.yml` (evdev names) and `vfs.yml` keys, the installed layout, `.rap` in `exdata` (`standalone_rpcs3.cpp`) | `build_files/install-rpcs3.sh` asserts it starts and is the pinned build; `tools/check-applied.sh` reads the `Video:` lines. The rest is not checked |
| Original Xbox (xemu, Flathub pin) | Its flags, `xemu.toml` keys, QMP's `memsave` and the XBE header to see the running title, its menu's buttons, and its blank drive's qcow2 and FATX layout (`standalone_xemu.cpp`, `xboxhdd.cpp`) | `tools/check-applied.sh` reads `renderer` and `surface_scale`; the drive's headers are checked as it is read; the drive file is pinned by checksum (`build_files/install-xemu-drive.sh`). The rest is not checked |
| Xbox 360 (Xenia Edge, our pin) | About 25 `--` settings (an unknown one makes it print its help and quit, `tools/check-applied.sh`'s header), the `x> ` log prefix, its title, the profile's `Account` file and its encryption (`standalone_xenia.cpp`, `x360profile.cpp`) | `tools/check-applied.sh` sees that a log was written. The rest is not checked |
| Wii U (Cemu, our pin) | `-g -f`, log phrases and its "Error" box, `settings.xml` and controller profile keys, the save folder, the GPU's UUID (`standalone_cemu.cpp`); the `.wua` archive layout (`wiiu.cpp`) | `tools/check-applied.sh` reads `api`, `VSync` and `AsyncCompile`. The rest is not checked |
| Vita (Vita3K, our pin) | `--frontend sdl`, the AppImage's layout and its Vulkan loader, `config.yml` keys, the log lines that say an install worked, its storage folders, `user.xml` (`standalone_vita3k.cpp`) | `build_files/install-vita3k.sh` asserts the layout, the one loader and the build number. Not in `tools/check-applied.sh`: the rest is not checked |
| Remote Play (Sunshine, our build) | Our patch (`cores/sunshine-patches/`, #297, "Sunshine: drop our patch when a stable release has it, and Steam's stream composite when Sunshine reads the whole screen"); the lines `cores/build-sunshine.sh` fills in its spec; its controllers' names, its log lines (`sunshinelog.cpp`), `prep-cmd`, `/api/` and `sunshine_state.json` (`remoteplay.cpp`; the state file only until a release with LizardByte/Sunshine#5696, `remoteplay.h`), its shaders' folder | The patch and the spec lines are asserted (the build says when the patch is upstream). The rest is the list in `build_files/install-sunshine.sh`'s header, gone through on the reference console |

### Moved by terra at each image build, or on their own

| What | Leans on | Checked when it moves |
|---|---|---|
| The Steam tile's session | terra's `gamescope-session` and `gamescope-session-steam`: the files the tile runs, upstream's script launching the client through cardwire only when it exists, `CLIENTCMD`, `steamos-session-select` handing over to `/usr/libexec/os-session-select` | Asserted at every image build (`build_files/install-steam-session.sh`) |
| Steam never offering to format the console's drive | The session reading `/etc/gamescope-session-plus/sessions.d/steam` after its own, and Steam honouring `STEAM_ALLOW_DRIVE_ADOPT` and `STEAM_ALLOW_DRIVE_UNMOUNT` | Not checked |
| The hand-over to Steam and back | `cabinetos-session` (`steam_step`) repeats what upstream's `gamescope-session-plus` wrapper sets up before the session, and starts the session itself | Not checked against upstream's wrapper; the tile is a `tv:` check when the `steam` package moves (`ci/base-watch.txt`, Steam) |
| Steam's own files | Steam's `config/config.vdf` (Bluetooth on before its first start, `cabinetos-session`), both `libraryfolders.vdf` (`cabinetos-steam-libraries`), the `steamwebhelper` and `zenity` processes that say its first download ended (`cabinetos-steam-download`), the files it leaves outside its folder (`steam.cpp`, Remove) | Not checked, and cannot be ahead: Steam updates itself. A change shows in Steam |
| Achievements, and offline play's unlocks | rcheevos, our copy at a pinned commit (`frontend/third_party/rcheevos/VERSION`): its low-level `rc_api_*` beside its client, `rc_libretro`'s table of core names, and the form of its requests, which the offline layer reads and answers (`achievements.cpp`, `ra_list.cpp`); RetroAchievements' server answering `"Success":true` | Not checked. The copy moves only when we move it; the server moves on its own |
