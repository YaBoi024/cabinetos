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
| `frontend/` | The console's one program, in C++20: `src/` (375 files, one folder; the Makefile compiles these and rcheevos), `data/` (shaders, controller lists, and `vectrex-overlays.json`, which Vectrex game takes which screen sheet), `third_party/rcheevos/` (RetroAchievements, vendored), `covers/` (the stand-in library's covers), `tools/fake-nmcli/` (a stand-in network tool for trying first run) | `/usr/bin/cabinetos-frontend` |
| `frontend/ps2/` | The PlayStation 2 host around PCSX2, compiled by `cores/build-pcsx2.sh` into `cabinetos-ps2.so`, not by the frontend's Makefile (so a pull request to `main` never compiles it) | `/usr/lib/cabinetos/cores/` |
| `wiibridge/` | `cabinetos-wii-bridge`, a stand-in for each real Wii Remote; GPL, its own program | `/usr/libexec/cabinetos-wii-bridge` |
| `linkup/` | `cabinetos-linkup`, LinkUp's own program, so a firewall can tell it from the frontend and off can mean off (#654): `conductor.cpp` (what it does: the frontend's commands, the server's events, one game with a friend), `noise.cpp` (Noise and the slow hash on OpenSSL; `noise-test.cpp` holds it to `noise-vectors.txt` and `proof-vectors.txt`, and `friend_sound.cpp` to a host's clock), `server_link.cpp` and `pairing.cpp` (every request to the LinkUp server), `keys.cpp` (what it keeps, in `/var/lib/cabinetos/linkup`), `box.cpp` (what two consoles seal for each other), `rtc.cpp` (webrtcbin, its ports, the links it gathers on: the cable when one is connected, never a tunnel; and its data channels), `capture.cpp`, `stream_host.cpp`, `stream_friend.cpp`, `friend_sound.cpp` (the friend's sound: a queue of its own, played through PipeWire at the host's pace), `bitrate.cpp`, `portmap.cpp`, `local_socket.cpp`, `hexutil.h` and `jsonutil.h` (hex, base64 and a few words over json-c), and `probe.cpp` (a console on the command line, for `tools/linkup-interop.sh`, not in the image). What it says to a server is `PROTOCOL.md` in MMagTech/cabinet-linkup, whose one tested release is pinned in `ci/linkup-server.txt` | `/usr/libexec/cabinetos-linkup` |
| `cores/` | How every emulator the image carries is built: `build-core.sh` (23 libretro cores, each pinned by commit in its `case` arm), `fetch-mame.sh` (the 24th, MAME 0.289, libretro's own build fetched and checked at its pins, with its plugins and catalogue), `build-pcsx2.sh`, `build-cemu.sh`, `build-sm2emu.sh` (sm2-emu, Sega Model 2's emulator, a program of its own), `build-supermodel.sh` (Supermodel, Sega Model 3's, a program of its own too, with SDL2_net beside it), `build-sunshine.sh` and their builder containers; `options/<core>.txt`, every option each core declares at its pin; `arcade-sets.sh`, each arcade emulator's catalogue from its own list at its pin (CI) | `/usr/lib/cabinetos/cores/` and the emulators' folders |
| `build_files/` | The image build's scripts: `build.sh` and the scripts it calls (remove the desktop, the storefronts and the help pages, set up the session, the branding), `install-*.sh` (one per piece the image carries; `install-linkup.sh` adds the one package LinkUp needs and fails the build if a GStreamer element it builds from is not there; `install-mame.sh` puts MAME 0.289 beside the cores, in a layer of its own; `install-sm2emu.sh` puts sm2-emu in `/usr/lib/cabinetos/sm2emu` and its catalogue beside the cores, `install-supermodel.sh` Supermodel in `/usr/lib/cabinetos/supermodel` and its catalogue the same way, `install-rpcs3.sh` RPCS3 in `/usr/lib/cabinetos/rpcs3` with `rpcs3-config-database.json` beside it as `config_database.dat`: the snapshot of the RPCS3 team's per-game settings, which `tools/fetch-rpcs3-config-database.py` refreshes when the pin moves, with the pin it was taken against in `rpcs3-config-database.pin`), `require-*.sh` (checks that fail the build), `sound-output-rule.sh` (the build fails if a WirePlumber update changes what the sound rule stands on, #655), `sound-backup-output.sh` (pipewire-pulse starts at login, so a backup output catches the sound while the TV's is gone, #228), `wifi-scan-rule.sh` (the build fails if a NetworkManager or wpa_supplicant update changes what the Wi-Fi scan rule replaces or stands on, or the power saving setting is not read, #666) | run once, inside the image build |
| `system_files/` | Files copied over the image as they are: the session script, systemd units, the root helpers in `usr/libexec/`, polkit rules, udev rules, the diagnostic report, which output plays the sound (`usr/share/wireplumber/`, a WirePlumber script and its setting file), Wi-Fi's background scans and power saving (`usr/lib/NetworkManager/`, a dispatcher script and a setting file), and a few files in `etc/` (the SSH settings, the update signing key, `adjtime`: the PC's clock in local time where no installer set it) | `/usr` and `/etc` |
| `ci/` | What the workflows run: collecting the build outputs (`stage-image-payload.sh`), the installer ISO repacked in CabinetOS's look after bootc-image-builder makes it (`brand-installer-iso.sh`, `build-disk.yml`; `tools/installer-vm.sh repack` runs it for test ISOs), the file a tester writes to a USB drive, made from the builder's raw disk and then booted in a VM (`make-portable-image.sh`, `check-portable-boots.sh`, `build-disk.yml`), the downloads put on Cloudflare R2 (`publish-to-r2.sh`, `build-disk.yml`), the key each core's build is cached under (`core-cache-key.sh`, `build-core.yml`), version numbers, promoting the tested image, watching the Bazzite base, whether each Flathub pin can still be downloaded (`check-flatpak-pins.sh`, `flatpak-pins.yml`), the checks every pull request runs (`checks.yml`: `check-romm-paths.sh`, `check-doc-paths.py`, `check-project-index.sh`, `check-long-files.sh`, `check-docs-list.sh`, `check-decisions.sh`, `check-licences.sh`, `check-rpcs3-snapshot.sh`, `check-code-and-docs-agree.py`, which holds what a pull request touches to the comments naming what is there, each header's functions being defined and no private address, and reads the whole tree for the skill of the same name, and `check-linkup-no-network.sh`, that the frontend's LinkUp files hold no internet), the one LinkUp server release the console is tested against (`linkup-server.txt`, by digest; `build-frontend.yml` runs `tools/linkup-interop.sh` against it), and the frontend's checks and reports (`build-frontend.yml`: `check-new-warnings.sh`, `run-self-tests.sh`, `list-arcade-changes.sh`, every arcade game whose emulator a catalogue change would move, `report-unused-code.sh`, which calls `warn-unused-touched.sh`) | GitHub Actions |
| `.github/` | The workflows, the issue form for players, and the pull request template | GitHub |
| `tools/` | Test and maintenance tools, run by hand; they take their machines from the environment (CONTRIBUTING.md, "Running and testing"). One also runs in the image build: `fetch-vectrex-overlays.py`, which `build.yml` runs to fetch the Vectrex screen sheets (#224) into the payload | nowhere |
| `disk_config/` | The installer, test-disk and portable-disk layouts (`iso.toml`, `disk.toml`, `portable.toml`), and the installer's look, which `ci/brand-installer-iso.sh` puts on the ISO: its boot menu (`grub.cfg`, with `grub-theme.txt` and `grub-background.png` under UEFI), Anaconda's settings, stylesheet and logo (`installer.conf`, `installer.css`, `installer-logo.png` and its `@2x`), and its title without Fedora's version (`installer-en.po`), and the line with moving dots while it loads (`installer-wait.sh`, `installer-wait.service`). The pictures are drawn by `tools/make-installer-art.py` | the installer ISO |
| `licences/` | The full licence texts the image ships, and `SOURCES`, where each comes from | `/usr/share/licenses/` |
| `docs/` | The decisions (`decisions/`, one small file each) and the old record they replace (`PROJECT.md`, frozen on 2026-10-23), the lessons, the website (`index.html`, `wiki/`, and `assets/` with its styles and the wiki's search, `search.js`; served by GitHub Pages), and `history/`: docs that describe the project as it was, each saying what replaced it | only `docs/LICENCES.md` |
| `Containerfile` | The image: the Bazzite base pinned by digest, then the build scripts in several layers | |
| `Justfile`, `cabinetos.env` | The build recipes CI calls, and their parameters | |
| `base-manifest.txt` | The base image's package list, for the monthly base-update check | |
| `AGENTS.md`, `CLAUDE.md` | What an AI coding tool reads before it changes anything: `AGENTS.md` for any tool, `CLAUDE.md` for Claude Code, which holds the seven rules for every change that both point to | nowhere |
| `.claude/skills/` | One folder per skill, named for the job it does, each a `SKILL.md` of plain Markdown that any AI tool can read as instructions; the first is `check-emulator-update`, `check-code-and-docs-agree` is the full read-only audit of the code against the docs, for before a release or with the monthly base update, and `check-security-fixes` reads every pin of ours that takes in outside data (Sunshine, rcheevos, the emulators) for publicly known security holes, with the monthly base update, which covers what comes from Bazzite | nowhere |

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
| Drawing | `ui.h`/`ui.cpp` (the primitives), `design.h` (the design system's numbers), `motion.h`, `text.cpp`, `image.cpp` (covers), `imagecodec.cpp`, `card.h`, `panels.h`, `screenparts.cpp` (what more than one screen draws: a cover, the press prompt, and the focused game's line beside a heading that Home, a grid and Search share, which ends with a greyed game's reason, or with the controller it needs and nobody has on, from `App::controllersOn` in `launch.cpp`: *Needs a controller*, *Needs a Wii Remote*), `look.cpp` (the menus' colour), `looks.cpp` (a system's screen looks), `vectrexsheets.cpp` (a Vectrex game's screen sheet over its picture, the picture fitted inside it, and the pause menu's Overlays choice, #224), `backdrop.cpp`, `notice.cpp` and `noticegallery.h` (the short messages), `qr.cpp` and `qrtexture.h`, `timewords.cpp`, `capture.h` (the SIGUSR1 picture), `shaders.inc` |
| The RomM client | `romm.h` (its face and its rules), `romm_types.h` (what it hands back), `romm_internal.h`, `romm_http.cpp` (every request goes out here, and only to the RomM server, #380), `romm_pairing.cpp`, `romm_library.cpp`, `romm_playdata.cpp`, `romm_snapshots.cpp` (RomM 5.5's save channels: a game's channels listed, a channel's history, a snapshot pushed and RomM's answer read, RomM's content hash of a save or a state, and a game's channels written back in RomM's form for the console's copy of them, #346), `romm_codes.cpp`, `romm_racetest.cpp`, `romm_snapshots_test.cpp` (`--romm-snapshots-test`) |
| Library data | `library.cpp` (the store of cards, the shelves, and the Library's tiles: one per platform, and one, Arcade, for every arcade folder, `tileGroups`), `catalog.cpp` and `catalog_internal.h` (which platforms play, on which core), `arcadematch.cpp` (which emulator plays an arcade game, from its zip's name for a grid and its files at Play, against the catalogues `cores/arcade-sets.sh` makes; and reading a zip's or 7z's files and a CHD's SHA1), `covercache.cpp`, `coverfill.cpp`, `gridfill.cpp`, `search.cpp`, `codecache.cpp` and `gametdb.cpp` (game codes and controller lists read off files), `samplelibrary.h` (the stand-in library) |
| A launch and a game in play | `launch.cpp` (the frame thread's half), `launch_routes.cpp` (the worker), `play.cpp` (in play, and leaving), `arcadesettings.cpp` (holding Start in an arcade game opens its own settings), `mamehome.cpp` (MAME 0.289's home folder before each of its games: its plugins, the ini that starts its high scores, and this person's scores), `screenview.cpp` (L3 shows a DS or 3DS game's screens both, top or bottom; the DS's lid on R2), `pace.cpp` (the game frame), `pausemenu.cpp`, `gamesession.h`, `romfile.cpp` (what to hand a core), `ncch.cpp` (whether a 3DS file is one Azahar plays: an encrypted dump or a CIA is refused before the core; and whether a name is a CIA's, the games `catalog.cpp` hides from every list), `disc.cpp`, `discs.cpp`, `rewind.cpp`, `states.cpp`, `rumble.cpp` |
| The libretro host | `core.h`, `core.cpp`, `core_state.h`, `core_*.cpp` (discs, hardware rendering, input, memory, options, picture, PS2), `netblock.cpp` (no emulator in the program reaches the network: the program's own `socket`, name lookups and `pthread_create`, exported for every library, refuse an emulator library's; `core.cpp` and `ps2.cpp` tell it what they load), `vkhost.cpp`, `vkhost_state.h`, `vkbridge.cpp`, `libretro.h` and `libretro_vulkan.h` (the libretro API, the RetroArch team's), and the console's three option tables: `coreoptions.cpp` (set on purpose for every game), `quality.cpp` (Picture quality) and `sysopts.cpp` (a system's pause-menu options); `screenfx.cpp` (screen looks) |
| PlayStation 2 | `ps2.cpp`, `ps2window.cpp`, `overlaywin.cpp`, and `frontend/ps2/` (its `CabinetPS2Bios.cpp` chooses each game's BIOS by the game's region) |
| Emulators run as programs | `standalone.h`, `standalone.cpp`, `standalone_play.cpp` (also what the console draws over the game in play: the dim, and the pointer for a game touched through the console, the Vita's), `standalone_eden.cpp`, `standalone_xemu.cpp`, `standalone_rpcs3.cpp`, `standalone_xenia.cpp`, `standalone_cemu.cpp`, `standalone_vita3k.cpp`, `standalone_sm2emu.cpp` (Sega Model 2: sm2-emu's settings, written whole in the person's folder for the game, with a game's buttons on a fight stick where the stick's rules put them), `standalone_supermodel.cpp` (Sega Model 3: Supermodel's settings, written whole in its home, which is its working folder, with the person's NVRAM folder for the game linked in, and its buttons laid out the same way), `vpad.cpp` (the virtual controllers: the plain uinput ones, and beside each one for RPCS3 a motion device that carries the player's tilt, #683; which kind an emulator gets is a switch on its row), `vdualsense.cpp` (the stand-in DualSense Eden, Cemu and Vita3K are given in place of the plain controller, #669: a `/dev/uhid` device with Sony's id, which the kernel's own PlayStation driver turns into a gamepad, a motion device and a touchpad sharing one uniq, so the emulator's SDL pairs the player's tilt by itself, and for a pad without sensors one whose motion device the emulator cannot open; its report thread, the driver's three questions answered, rumble back, and `--dualsense-test`), `vtouch.cpp` (the virtual touch screen the pointer touches with, and gamescope passing touch through), `xtitles.cpp`, `xboxhdd.cpp`, `x360profile.cpp`, `dolphinprep.cpp` |
| Saves and states | `savetable.cpp` (what each core writes, where, which of its files it writes only as the game closes, whose battery travels with the cartridge's clock or with Saturn's cartridge beside it, and its two tags on RomM: `emulatorTag` for states, `saveTag` for saves; how RomM hands each core another emulator's save, the core's name in Sigil's spelling with the options and profile a request names, `saveForm`, and the check such a save must pass, `SaveCheck`; which systems have states and rewind, `snapshotsAllowed` and `rewindAllowed`), `saves.cpp` (gathering and restoring all three kinds of save: `syncSave`, `syncDirSave` and `syncFileSaves` each write this console's copy when the save changed and send nothing; and, for a start, fetching what RomM holds of the game's save, another emulator's asked of RomM in this console's form and checked, `toOwnForm`, writing the one the start continues from, keeping a save it replaces aside first, and the notes the start leaves), `savepick.cpp` (which save a start continues from, the one rule: RomM's save channels and its saves from before them weighed together, another emulator's in a channel or in none named to be asked for, a save not yet sent winning, a save of the console's own that RomM kept beside a channel's current never losing to an older one, and whether the start says "Continuing from another device"; and which of RomM's saves from before channels a start brings into a channel, #346), `savepush.cpp` (leaving a game: what a session left marked as one owed snapshot, where it is aimed, and RomM's answer settled; a save from before channels brought into a channel by the start that took it, and a save's marker from before them made the game's push; a state marked owed in it as Save state makes it, and the states a game offers and a push names: the three newest under the console's own name, from that push and from wherever they are on RomM; with `--save-push-test` in `savepush_test.cpp`, #346), `savepicture.cpp` (the save's own picture: the game's save looked at once a second while it runs, and a picture of the game taken about a second after it stops changing, the core's frame or, for an emulator run as a program and PlayStation 2 in its own window while it plays, gamescope's, asked from a worker so the main loop that feeds PS2's sound never waits, kept in the person's pending folder until it goes with that save; with `--save-picture-test` in `savepicture_test.cpp`, #346), `dirsave.cpp` and `filesave.cpp` (their helpers), `states.cpp` (Save state and Load latest state, and screenshots), `statefile.cpp` (a state file's layout: the core's bytes alone, or RetroArch's RASTATE blocks with the achievement progress), `savemirror.cpp` (RomM's saves and save channels as last seen, and the snapshot the console holds of each game), `uploader.cpp` (the queue: a game's snapshot, with its save and states, screenshots, time played and favourites), `pending.cpp` (the markers), `offline_sync.cpp`, `playtime.cpp`, `favorites.cpp`; the requests themselves are in `romm_playdata.cpp`, and RomM 5.5's save channels and snapshots in `romm_snapshots.cpp` |
| Storage | `storage.h` (where everything lives), `storage_internal.h`, `locations.cpp`, `cache.cpp`, `drives.cpp`, `drives_fs.cpp`, `udisks.cpp`, `users.cpp`, `accounts.cpp`, `accountremove.cpp`, `fileio.cpp` (every file the console replaces goes through its `writeBytes`: a temporary, fsync, a rename, the folder synced so the name is on the drive too; `syncDrive` waits for one drive, and `keepsSizeWithBytes` says which drives need no care after a power cut, #559), `unsettled.cpp` (the files a launch marked complete that their drive may not have yet, listed on the console's own drive and deleted at that game's next start after a power cut, #559), `prefs.cpp`, `gonegames.cpp` (a game RomM no longer has leaves the drive at a start with the server there, with its record, covers, saves, states and screenshots, #625) |
| Controllers | `players.cpp` (which pad is which player; it refuses to seat the console's own stand-in controllers, told by their name, since the stand-in DualSense carries a real pad's id), `tilt.cpp` (a seated pad's motion sensors, its accelerometer and gyroscope, turned on for a player's seat when a game asks and read in SDL's own units; it knows no core or emulator, and `core_input.cpp` answers libretro's sensor interface from it, for mGBA and Azahar, and `vpad.cpp` writes it into each PS3 player's motion device and each Switch, Wii U and Vita player's stand-in DualSense; a LinkUp friend's seat on the host is a pad with the friend's sensors, read the same way (`linkup_pad.cpp`); its test pads serve `--motion-device-test` and `--dualsense-test` too), `fightstick.cpp` (the table of known fight sticks, and an arcade game's buttons laid out on one), `controls.cpp` (Controls: an arcade game's buttons moved per game, kind of controller and person, kept in `users/<id> - <name>/controls/`; `play.cpp`'s readGameInput reads each pad through it, `launch.cpp`'s startPlaying reads the game's file, and for sm2-emu and Supermodel `standalone_play.cpp`'s startStandalone reads it before `standalone_sm2emu.cpp` and `standalone_supermodel.cpp` write each player's buttons from it into the program's settings), `controlsflow.cpp` (its panel over the pause menu, and the press it listens for, which `controllers.cpp` hands it before anything else sees it), `controllers.cpp`, `bluetooth.cpp`, `btagent.h`, `battery.cpp`, `batteryrow.cpp`, `shortcuts.cpp`, `wiiremote.cpp` (starts the Wii bridge), `wiipair.cpp`, `wiipanel.cpp`, `wiimii.cpp`, `wii.cpp`, `wiiu.cpp` |
| The machine | `net.cpp`, `wifi.cpp`, `power.cpp`, `power_menu.cpp`, `powerprofile.cpp`, `volume.cpp`, `sound.cpp`, `gpu.cpp`, `cpus.cpp`, `idle.cpp`, `proc.cpp` (running a program with no shell), `unit.cpp` (asking systemd for a root job), `timezone.cpp` (the time zone first run asks on a console with none: its regions and cities from the tz database, and setting one through `cabinetos-timezone@.service`), `update.cpp` (System update, the console's side), `files.cpp` (File access, the console's side), `server.cpp` and `serveraddress.cpp` (the RomM server this console belongs to) |
| RetroAchievements | `achievements.cpp` (the sign-in and rcheevos' client; its console table names every system), `achievements_ui.cpp`, `ra_list.cpp`, `ra_net.cpp`, `ra_state.h` |
| Settings features | `settingsflow.cpp` (the rows and their dispatch), `settingsids.h`, `about.cpp`, `credits.h`, `access.cpp` (File access and Developer access), `wikiqrview.cpp` and `wikiqr.h` (the Wiki's and What's new's QR codes, made by `tools/make-wiki-qr.py`; How to and the link screen draw them), and one `*flow.cpp` per feature: `accountsflow.cpp`, `downloadsflow.cpp`, `drivesflow.cpp`, `pinflow.cpp`, `serveraddressflow.cpp`, `remoteplayflow.cpp`, `tailscaleflow.cpp`, `steamflow.cpp`, `updateflow.cpp`, `reportflow.cpp`, `gameeventsflow.cpp`; `choiceflow.cpp` asks a question over any screen |
| LinkUp (#654) | `linkupscreen.cpp` (its top-bar screen, and what a console shows in a session with no game to show), `linkup.cpp` (the App's side: the top bar, inviting, answering an invite, the next game, the panels over a game), `linkupflow.cpp` (the Settings rows), `linkupsession.cpp` (the live side: the program's answers, a session with a friend, the friend's seat and picture, `--linkup-script`), `linkup_link.cpp` (the local socket to the program, the only way the frontend reaches it), `linkup_wire.h` (what the two programs say, which both include), `linkup_pad.cpp` (the friend's controller at both ends: what the friend's console sends, its presses, sticks and tilt, and the host's seat for it, a virtual pad with the motion sensors the friend's controller has; what counts as a press for the idle rule; `--linkup-pad-test`), `linkuprules.cpp` (the rules and words, with no SDL), `linkupgallery.cpp` (`--linkup-gallery`: every screen with made-up friends). All the network is `linkup/` |
| Game events (#716) | `gameevents.h` (its face and its rules), `gameevents.cpp` (the messages, the knock on each network, and the one thread that sends them in order), `gameevents_wire.cpp` (one message on the wire: the name looked up, every address it gives held to the home rule, the POST pinned to those addresses, no redirect, no proxy), `gameevents_internal.h` (what those two and the test share), `gameevents_test.cpp` (`--game-events-test`), and `gameeventsflow.cpp` (the Settings row and the App's side of each message). Who calls it: `launch.cpp` and `standalone_play.cpp` beside `playClock.begin` (started), `play.cpp` and `standalone_play.cpp` beside `playtime::close` (ended, which Sleep, Restart and Power off reach too), `frame.cpp` once a frame (the pause menu over a game) and for Done's check, `achievements_ui.cpp` as each pop-up is taken, `linkupsession.cpp` on a LinkUp friend's console (the host's `starting` and `quit`, and the session's end), `power_menu.cpp` (Sleep waits for what is waiting), `startup.cpp` (the exit) and `main.cpp` (the start) |
| Remote Play, Tailscale, Steam, the report | `remoteplay.cpp`, `sunshinelog.cpp`, `tailscale.cpp` (the `tailscale` command, and the rule for when tailscaled runs, which `cabinetos-saved-services` follows at boot), `steam.cpp`, `report.cpp`, `samenames.cpp` |
| Test instruments | `probes.cpp` (runs the probes and calls most self-tests; `--same-names-test`, `--linkup-test`, `--linkup-pad-test`, `--arcade-tile-test`, `--fightstick-test`, `--tilt-test`, `--motion-device-test`, `--dualsense-test`, `--arcade-settings-test`, `--arcade-match-test`, `--arcade-changes`, `--screen-view-test`, `--controls-test`, `--netblock-test`, `--idle-test`, `--keyboard-test`, `--gone-games-test`, `--save-notes-test`, `--save-pick-test`, `--save-push-test`, `--save-picture-test`, `--game-events-test`, `--romm-race-test` and `--romm-snapshots-test` run from `cli.cpp`), `selftests.cpp`, `coretests.cpp`, `testhooks.cpp`, `discrig.cpp`, `optcheck.cpp` (the option-table check CI runs). Most self-tests live in their feature's own file (`players::test` in `players.cpp`, `tilt::test` in `tilt.cpp`, `vpad::motionTest` in `vpad.cpp`, `vdualsense::test` in `vdualsense.cpp`, `linkuppad::test` in `linkup_pad.cpp`, `gameevents::test` in `gameevents_test.cpp`, `romm::snapshotsTest` in `romm_snapshots_test.cpp`, `savepick::test` in `savepick.cpp`, the table `--save-pick-test` runs before `selftests.cpp`'s restores, `savepush::test` in `savepush_test.cpp`, `savepicture::test` in `savepicture_test.cpp`, and so on). They ship in the program and run only from a flag |

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
- **LinkUp's program owns everything on LinkUp's network, and the frontend none of it.** The frontend's
  LinkUp files hold no internet call (`ci/check-linkup-no-network.sh`), and reach `linkup/` over one
  local socket, polled once a frame. The program never draws a menu and never decides a word on the
  screen; the frontend never holds a key. **Off means off:** the program is not running, nothing listens
  and nothing is contacted, and the firewall lock (`cabinetos-linkup-firewall`, loaded at every boot
  before the network) drops everything from its group and everything to its ports whether or not it is
  running. The program asks the lock before anything else, a name lookup included, and exits if it is
  closed. It is walled off from the home folder and the rest of the console's data by its unit.
- **The libretro host** (`core.h`, `core*.cpp`) runs one core at a time
  (`Core::shared()`); libretro's callbacks are plain C, so its state is
  process-wide on purpose (`core_state.h`).
- **Emulators run as programs** are started, watched by their window titles
  and stopped by `standalone*.cpp`; a game in one of them is a game like any
  other to the rest of the console.
- **No emulator reaches the network** (#557,
  `docs/decisions/2026-10-26-emulators-get-no-network.md`). Inside the
  program, `netblock.cpp` refuses an internet socket or a name lookup that an
  emulator library's code asks for, its threads included, and leaves the
  console's own requests alone; a library is an emulator's because
  `Core::load` or `ps2::load` loaded it. An emulator run as a program is
  started with no network by the three `proc::spawn` calls that start one
  (`standalone.cpp`, `standalone_xemu.cpp`): bwrap for the image's
  (`noNetwork`), `flatpak run --unshare=network` for the Flatpaks. A PS3 or
  Vita install, the one time an emulator runs to do anything but play, goes
  under the same bwrap (`noNetworkInstall`; `rpcs3Headless`, `vitaHeadless`).
  A new core or emulator program is covered by going through those, with
  nothing of its own.
- **Storage** (`storage.h`) decides where everything lives. Eviction only ever
  walks `cache/`; kept games (`roms/`) and save data are never evicted. The
  one other removal is a game RomM no longer has, kept or played, at a start
  with the server there, with its saves, states and screenshots, once the
  upload queue has tried what that start owed (`gonegames.h`).
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
- **Tailscale is its own switch, in Network**
  (`docs/decisions/2026-11-15-tailscale-own-switch.md`). Only two things
  start or stop `tailscaled.service`: `cabinetos-saved-services` starts it
  at boot, and `tailscaleflow.cpp` starts and stops it from its row, its
  sign-in and the app's start. Remote Play and LinkUp never touch it, so a
  console that reaches its RomM server at a Tailscale address keeps it
  whatever Remote Play is. Both read the same rule, `tailscale::runs`.
- **Game events sends to one address and nowhere else**
  (`docs/decisions/2026-11-15-game-events.md`): the one the owner typed, and
  only when every address its name gives is private, local or Tailscale,
  checked at every send, on a network that answered the knock
  (`gameevents_wire.cpp`, `gameevents.cpp`). It never listens. Off means its
  thread is never started and every call returns at once. It sends nothing
  about one console's setup over LinkUp, and nothing on disk waits to go.
- **Running another program goes through `proc.h`, never a shell.**
  Exceptions today: the `--first-run-writes` and `--playtime-test` self-tests
  remove their scratch folders with `std::system`; the Wii bridge is started
  with `fork` and `execl` (`wiiremote.cpp`); and the frontend restarts itself
  with `execv("/proc/self/exe")` (`startup.cpp`, `serverwait.cpp`).
- **The image** (`Containerfile`, `build_files/`) puts what it installs in
  `/usr`, which every update replaces, and a few settings files in `/etc`
  (`system_files/etc/`, and the signing policy written by
  `require-signed-updates.sh`). Nothing goes in `/var`, which an update never
  touches (lessons rule 9). The `require-*.sh` scripts fail the build rather
  than ship an image that cannot work.
- **CI** builds, stages and promotes. The scheduled jobs (the base check
  and Steam's spare, both monthly on the 1st) open pull requests and never
  merge them; the Flathub pins' check, the same day, opens nothing and fails
  while a pin is near the end of what Flathub keeps. The controller list's
  update runs only by hand, for a pad a player reported, and also only opens
  a pull request.

## 3. The main flows, step by step

### Start-up, to Home

`App::run` (`main.cpp`) calls, in this order:

1. `parseCommandLine` (`cli.cpp`). A flag that exits ends the parse there.
2. `resolveCoresAndServer` (`cli.cpp`): where the cores are (`--core-dir`,
   else `cores/build` or `../cores/build` when it exists from the working
   folder, else `/usr/lib/cabinetos/cores`), and the server's address:
   `--romm`, else `CABINETOS_ROMM`, else `/etc/cabinetos/session.env`, else the console's
   `config/server.json` (`serveraddress.cpp`).
3. `earlyProbe` (`probes.cpp`): the tests and probes that need no storage
   and no window run and exit here.
4. `startStorage`, then `storageProbe`: the storage tree, then the probes
   that need it.
5. `startDisplay`: the window and the GL context.
6. `runFirstRun`: first run, if the console is not set up (`firstrun.h` for
   its rules, `setup.cpp` for its screens). On a console with no time zone
   (one running from a USB drive) it opens at the time zone, before the
   network.
7. `startStartupScreen`, then `connectToServer`: the console waits for its
   server; when there is a game on the drive to play, it goes offline after
   15 seconds and tries the server in the background, otherwise it keeps
   waiting (`serverwait.cpp`).
8. `startBrowsing`; `startSync`, which starts the upload queue and sends
   first whatever an earlier run still owed (`uploader.resendOwed`), and
   with the server there starts the backfill (`offline_sync.h`): the games
   RomM no longer has asked about and removed first (`gonegames.h`), then
   what offline needs fetched;
   `recoverPowerCutSaves`, which finishes the saves a crash or a power cut
   left in an emulator run as a program (an Xbox game's drive through
   `recoverDriveSaves`, which that game's launch also runs) or in a core
   whose save is files (`finishInterruptedPlay`, which that game's launch
   also runs); then the rest of the start-up steps.
9. `runFrameLoop` (`frame.cpp`) until the console exits or restarts; then the
   game is left, the emulators closed, the core unloaded, and `shutDown`
   (`startup.cpp`): the uploads are sent and RetroAchievements' unlocks put
   on disk first, then the workers are stopped. That rest is given two
   seconds (#305), except for a restart in place; past them the process ends
   and names what it was waiting on.

### Pressing Play on a game

1. The game's page returns an Action; `App::apply` (`actions.cpp`) asks
   `refuseStartWithoutController` (`launch.cpp`), which stops a Wii Remote's
   press on a game that isn't a Wii game, and any press on a Wii game that
   needs a Remote while none is on, then calls `launchById`, which calls
   `beginLaunch` (`launch.cpp`). On the frame
   thread and with no curtain, `beginLaunch` sets the core's options and
   loads the core (not for an emulator run as a program), records the keep
   for a Download, decides where the game goes, and starts the launch
   worker.
2. The worker (`launch_routes.cpp`) fetches the platform's firmware from RomM
   first, then takes one of four routes, in this order: a game on several
   discs, a game installed before it runs (PS3, Vita), a folder of files, one
   file.
   The bytes go straight to disk: under `cache/` for a game pulled to play,
   under `roms/` for a game kept, on the console's own storage or a drive.
   Nothing a power cut may have left half written is trusted (#559): on an
   exFAT or NTFS drive a download is listed before its `.part` is renamed
   into place (`romm_http.cpp`, `fetchToFile`), and so are an unpacked
   game's files before they are stamped and, on any drive, an install's
   note (`standalone.cpp`, `writeInstalledNote`). The list (`unsettled.cpp`)
   is begun before the worker runs and settled when it ends (`launch.cpp`),
   and a list left from an earlier boot deletes what it names at that
   game's next start, so it is downloaded, unpacked or installed again.
   An archive the core does not open itself is unpacked beside it
   (`romfile.cpp`, `prepareFile`), each file stamped with the archive's own
   time; a later start that finds every file at its size with that time
   skips the unpacking and its room check (`alreadyUnpacked`, #530).
   With the game on the disk, a 3DS file Azahar cannot play (an encrypted
   dump, a CIA) ends the job with its reason (`ncch.cpp`); otherwise it
   fetches what RomM holds of the game's save (`fetchLaunchSaves`;
   `saves.cpp`, `fetchSaves`): its save channels (RomM 5.5, #346), asked by
   the file the game's record remembers they are keyed to, and its saves
   from before channels, both at once; then the save the start continues
   from, by the one rule in `savepick.cpp` (the newest, whichever device
   or app made it; a save this console has not sent yet wins), and that
   save's bytes: another emulator's asked of RomM in this console's form
   and checked (`toOwnForm`), the newest this console can use as stored
   when it fails. The same for the Mii Channel of a Wii game. Then
   it marks the job Ready.
3. Each frame, `pumpLaunch` (`launch.cpp`) reads the job. A stop (Stop on
   the page's filling row, `pressStopDownload` in `downloadsflow.cpp`;
   Escape on a keyboard, `input.cpp`; Eject of the drive it is going to,
   #404) sets the job's cancel, which the worker answers at every step
   (#472); the job ends Failed, and a cancel says nothing. A failed job is
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
   (`restoreBeforeLoad`). Each restore writes the save the worker picked;
   a save of this console's that it replaces with another is copied to
   `saves/<platform>/<romId>/replaced/` first, one copy a game
   (`keepAside` in `saves.cpp`).
6. The core loads the game (`loadLaunchedGame`); `setUpLoadedGame` sets up
   the session; then the battery save is written into the core, and onto
   the console's own copy of it when it came from RomM and differs, with a
   Game Boy cartridge's clock (#346, step 11; `restoreClock`), and the
   start leaves its notes (`noteStart` in `saves.cpp`): the channel and
   snapshot it continued from, in the session, for the push when the game
   is left; and that snapshot as the one this console holds
   (`savemirror::Held`). A start that put RomM's save from before channels
   in place owes the game's push that brings it into a channel, and queues
   it at once (`savepush::markOldSave`, "Saves and states reaching RomM"
   step 3). Then the options the core was given are logged,
   sound and pacing open, and play starts (`startPlaying`), with the pill
   "Continuing from another device" when the console's own note, or RomM's
   when it has none, says the save was made somewhere else, and the game's
   save watched for its picture from there (`savepicture::Watch`, looked at
   once a frame from `frame.cpp`). An emulator run as a program does the
   same in `startStandalone`, its save watched from `watchStandalone`. The restore order and
   its rules are one comment on `pumpLaunch`.

### Leaving a game

`App::finishExit` (`play.cpp`), in order: a save that changed since its
last picture takes one while the game is still on screen
(`savepicture::Watch::leaving`); the time played is closed and
queued for RomM; the battery save is read out of the core and written on the
console (`syncSave`), with a Game Boy cartridge's clock, whose change alone
is no new save; the disc in the drive is remembered for the next Play,
and the drive put back on its first disc; RetroAchievements stops reading
memory; the game unloads and the core shuts down (`Core::unload`:
`retro_unload_game`, then `retro_deinit`, as RetroArch's Close Content
does); then folder saves and file saves are gathered, after both, because
that is when a core flushes them (`syncDirSave`, `syncFileSaves`, all in
`saves.cpp`); libretro MAME writes its NVRAM, settings and high scores only
in `retro_deinit`. Each of the three writes the console's copy only when the
save changed, and sends nothing. The save's picture still being made is
finished (`savepicture::Watch::finish`). Last, what the session left is marked as
one snapshot owed to RomM and queued (`savepush::leave`, `savepush.cpp`),
the save with its picture,
with the states made in it, which Save state marked owed as it made them
(`savepush::markState`, from `states.cpp`): no save changed and no state
made means nothing sent. Powering off or restarting mid-game
leaves through the same function (`leaveGameAtExit`). A game in an emulator
run as a program does the same when the program ends (`finishStandalone` in
`standalone_play.cpp`, and `finishInterruptedSave` in `saves.cpp` after a
power cut, the picture its sitting took read from its playing note); every
close the console asks of the program goes through `stopStandaloneGame`,
which asks gamescope for the save's picture first. A game in a core whose
save is files the core writes while it runs (DS, Dreamcast, PlayStation 2,
PSP, 3DS, GameCube, Wii, and Saturn's backup cartridge) has a playing note
of its own, written once the
start's restores are done (`notePlaying`, from `setUpLoadedGame`) and taken
away here; one a crash or a power cut left is read at the game's next start,
before its restores, and at start-up (`finishInterruptedPlay`): what the
core wrote after the note is gathered as the leave would have, unless a
file fails the save check another app's save gets, and goes up as the
session's snapshot, made when the core wrote it, so the start keeps it
rather than putting RomM's older save over it (#346, step 9).

The snapshot goes up on the upload queue's thread (`savepush::send`), into
the channel the save it was made from is in, naming the snapshot this
console holds as the current it builds on (`savemirror::Held`), or into the
person's channel named "default" when the console holds none. When RomM
answers that the channel moved on, it has already kept the push beside the
channel's current; the console puts it on top with a second push that sends
no bytes only if its save was made later, and never for a session that could
not use the newest save. A push of states alone goes onto the channel's
current as RomM lists it when it is sent, beside another device's newer
save, and the console's note of the save it last had stays its own; not
onto a save of another emulator's, nor from a session that could not use the
newest. A hardcore channel is never saved into: the save
goes into the person's channel named "softcore". The order, the answers and
the reasons are the head of `savepush.h`.

### A game with a friend (LinkUp)

Nothing here is on the frontend's network: each step on the wire is the program's (`linkup/`), and
the frontend asks for it over the local socket (`linkup_wire.h`, `linkupsession.cpp`).

1. **Inviting.** A press on an Online friend, then a game, comes to `linkupPickedGame` (`linkup.cpp`),
   which goes to the waiting screen and sends `invite` to the program. The game list has a system only
   when a friend can play one of its games (`inviteHasSystem` in `gridfill.cpp`, the rule in
   `linkuprules.cpp`); the systems judged game by game are read ahead as the LinkUp tab opens
   (`readAheadForInvite`, from `goToDestination`). The program picks the invite's id,
   seals the game's name for that friend (`box.cpp`) and knocks (`conductor.cpp`). A game not on the drive
   starts downloading (`linkupFetchGame`).
2. **Answering.** The friend's program hears the knock on its long request (`waitLoop`), fetches the
   list, opens the box, and tells its frontend: a pill, and an Invited line. Join sends `join`, and the
   friend's program sends its first sealed word: what it can decode.
3. **Connecting.** Both ask the server for its STUN list and a way through its relay. The host chooses
   the codec, builds its pipeline with black frames going in (`stream_host.cpp`, because webrtcbin makes no
   offer until the encoder's caps reach it) and makes the offer. Offer, answer and candidates pass as
   sealed boxes through the server (`sendSignal`, `onSignalBox`), and from there the game does not go
   through it. A console with a cable connected makes its candidates on the cable alone (`rtc.cpp`).
   `connected` reaches both frontends.
4. **The game starts.** The host's frontend waits for the friend's first pad state, which says what motion
   sensors their controller has (a second at most, `linkupFollowGame`), seats the friend as player two with
   those sensors (`linkupSeatFriend`, a virtual SDL pad, `linkup_pad.cpp`; `players.h`) and launches as for
   any Play. Before each later game the seat is made again if those sensors changed. The friend's frontend hands the television to the
   program's window, as to an emulator run as a program, and waits on top as gamescope's overlay
   (`linkupFriendHandover`).
5. **The picture is off unless the game alone is on the host's screen** (`linkupFollowGame`, before the
   frame draws): `picture` turns the capture off and drops the sound, and the friend's console says Waiting.
   Where the game sits on the host's screen passes over the reliable data channel (`linkupTellPlace`, from
   the picture's draw), and the friend's console draws black bars and the letterbox glow around it, over
   the stream's own (`LinkUpPlayScreen::drawPlace`), because the encoder turns the host's glow into steps.
6. **Presses and tilt.** The friend's frontend reads its player one's pad, tilt included (`linkupSendPad`,
   `linkup_pad.cpp`), sends its whole state to its program, and that goes over the unreliable data channel
   to the host's program and frontend (`linkupFriendPadChanged`), into the virtual pad a core reads, or
   `vpad.h` for an emulator run as a program; `tilt.h` reads the friend's tilt from that pad as from any, so
   a Switch or PS3 game gets it the way it gets a pad's on the host. Only a press keeps the session from
   its idle pause. Rumble comes back the other way.
7. **Ending.** A pause, a leave and the rest pass between the two frontends over the reliable data channel
   (`linkupFromPeer`). A game's end keeps the session (`linkupHostGameEnded`); `end` closes it, and Remote
   Play, which it stopped, comes back (`linkupSessionClosed`).

### Saves and states reaching RomM

1. A save, a state or a screenshot is written on disk, and a marker saying
   what to send is written before the upload is tried (`pending.h`; the
   markers are in `users/<id> - <name>/pending/`). A screenshot has a
   marker of its own. A game's save and its states have one marker per
   person and game, for the snapshot they go up in on RomM 5.5's save
   channels (#346, `cache::OwedPush`, written by `savepush::mark` and
   `savepush::markState`): a newer one replaces it and carries its states,
   and it is cleared only by the push that sent it. It names the save's
   picture (`savepicture.h`), a file beside it that it owns: a newer marker
   naming another deletes it, and so does the marker's clearing.
2. The upload queue (`uploader.h`) sends it on its own thread as the person
   who made it, and removes the marker when RomM has it: a screenshot
   through `romm_playdata.cpp`, a game's snapshot through
   `savepush::send` and `romm_snapshots.cpp`, which also settles what RomM
   answered ("Leaving a game", above). The save's picture goes in the same
   push as RomM's `save_screenshot` part, and RomM keeps it with the save's
   row, which is what its website shows on the channel's tile. A marker left by a crash is sent
   again at the next start (`startSync`). One that failed is sent again
   every five minutes, when the server comes back, when RomM gains save
   channels (step 4) and after a person's next upload works; one RomM refused (an HTTP 4xx) keeps its marker and waits
   for the next start or a new save of it, unless RomM no longer has the
   game: then, once the queue has tried it that start, the file goes with
   the game (`gonegames.h`). A snapshot owed for a game that is being
   played waits until the game is left: its save can be the file the core
   is writing. A save's or a state's marker from before save channels, one
   per file, becomes its game's owed push before anything is sent
   (`savepush::adoptOldSaves`, `adoptOldStates`): a save's files as its
   save, made when the console wrote them; a state as one of its states.
   States go up inside the snapshot, under the console's own name (the
   core's state tag) in slots 1 to 3, so other apps' states in it are
   carried as they are. The game's page, Load latest state and Resume offer
   the three newest under that name wherever they are on RomM (RomM's list
   of the game's states, every snapshot's and the ones from before
   channels), with the ones made here and still owed; offline, the
   console's own files (`savepush::offeredStates`). A push names those same
   three, always with the ones made since, each one RomM holds by its
   content hash, read off the console's copy, the channels' currents or the
   channel's history (`GET /api/snapshots`), since RomM's list carries
   none. The console deletes no state on RomM; RomM's own history keeps the
   older ones, and the console lets go of its copies of the ones the push
   did not name.
3. Each core's saves, where it writes them and the tag they carry on RomM are
   one table, `savetable.cpp`. A battery goes up as it is, or as one zip
   with what travels beside it (#346, step 11; decided 2026-10-12): a Game
   Boy cartridge's clock, Gambatte's own `<game>.rtc` beside `<game>.srm`,
   and Saturn's backup cartridge, Beetle Saturn's `<game>.bcr`, which is
   never sent while it holds no save; a change to the clock alone is no new
   save, and a save that carries no clock or cartridge leaves this
   console's as they are (`savepush.cpp`, gather; `saves.cpp`,
   `restoreClock`). Which save a launch restores is one rule,
   `savepick.cpp`: the newest RomM holds for this person that this console
   can use, among the current snapshots of the game's save channels and
   the saves from before channels under the core's tag (RomM lists a
   snapshot's save among a game's saves too, with its channel named, and
   that one is never taken for an old one); this console's own copy wins
   while it is still owed to RomM, and is used when RomM has none. A
   snapshot's save is dated by the save's own row among the game's saves,
   when RomM first stored those bytes (`savepick::dateSaves`): a snapshot
   that changed only its states carries an older save. ANOTHER EMULATOR'S
   SAVE (#346, step 8), in a channel or another app's in no channel, is
   asked of RomM in this console's form, through the save library RomM
   builds in, Sigil (`fetchSaves`, `toOwnForm`): RomM converts it for the
   core as Sigil names it (`catalog::saveForm`), hands a cartridge
   system's over as stored, and says when it converts nothing for a
   system, whose file then travels as it is; the answer is checked for
   the size, the marks and a save in it (`catalog::SaveCheck`,
   `cab::passesCheck`). One that fails leaves the start to the newest this
   console can use as stored, and the start says "Couldn't use the newest
   save" (the session's `couldNotUseNewest`). Where Sigil names a
   difference RomM does not convert yet (DS, Game Boy Advance, the Mega
   Drive family), a save handed over as stored is used only when this
   core made it, RomM's own `may_load` rule (`savepick::mayLoad`); an
   arcade game takes no other app's save at all (`OwnForm::arcade`). Every
   push names the core in Sigil's spelling beside the tag. What RomM holds and the
   picked save's bytes are fetched on the launch worker (`fetchSaves`) and
   written on the frame thread (`saves.cpp`); a save of the game that lands
   on RomM between the two makes the fetch stale, and the console's copy
   stands. The game's page reads its "Saved" line through the same rule
   (`listSaves`). A start that put RomM's save from before channels in
   place brings it into the channel this console saves into, the first
   time the game is started after the update (decision 2 of 2026-11-15):
   the restores note each row they took, the rule says what goes
   (`savepick::oldSaveToCopy`), and the game's owed push names the row for
   RomM to copy (`copy_of`), no byte sent and the row left as it is for a
   console still on the old version; a save that was several rows (Sega
   CD's two) goes as one zip of them (`savepush::markOldSave`). The game's
   states from before channels go into that first snapshot with it, from
   this console's copies, as their bytes, since RomM holds them in no
   snapshot (answer 5 of 2026-11-17). It is no
   save this console owes, so the next start still takes RomM's newest, and
   it is sent only while RomM still has each row as the start took it.
4. Offline, the console uses its copy of RomM's saves and save channels as
   last seen (`savemirror.h`), so a launch decides as it would online. What
   offline will need is fetched while online: every game on the drive's
   saves, its channels, and the bytes of the save the same rule picks
   (`offline_sync.cpp`); the server is tried in the background while it is
   away, and what is owed waits as markers on disk. Back online it goes at
   once, and the save made later wins (`savepush::send`, step 2 of
   "Leaving a game"): the console's goes on top when its save was made
   after the other device's push reached RomM, else RomM keeps it beside
   the current as a branch, 30 days by RomM's
   `SNAPSHOT_BRANCH_LIFETIME_DAYS`; the save it replaces on top stays in
   the channel's history, which RomM trims to `SNAPSHOT_RETENTION` (10).
   A RomM TOO OLD FOR SAVE CHANNELS (no `SAVE_SYNC` in its heartbeat) is
   neither read nor written for a save or a state (#346, step 9; decision 7
   of 2026-11-15, answer 4 of 2026-11-17; `romm::serverTakesSaves`): a
   start goes by the copy as last seen, as offline, the game's page and
   Load latest state by the console's own states, the offline download
   fetches no save, and a push waits with nothing asked
   (`savepush::Sent::Waits`). Nothing is said on the TV. The heartbeat the
   reconnect thread asks once a minute notices the server gaining channels,
   and what waited goes at once (`App::followServer`). `savemirror.h` also
   keeps one note of the console's own, which forgetting RomM's copy
   leaves: the snapshot it holds of the game, written by a start from one,
   with the content hash of every save of the game it has held. RomM keeps
   a note of what each device holds too, but every CabinetOS console is one
   device there, so only the console's own note can tell a save another
   console made, or tell one of its own that RomM's website put back on top
   (a new snapshot with the same save inside).

### System update

1. The frontend (`updateflow.cpp`, through `update.cpp`) starts
   `cabinetos-update-check.service` with `unit::start`, by hand or weekly
   when that setting is on.
2. The unit runs `/usr/libexec/cabinetos-update check` as root: `bootc
   upgrade --check`, the size of what is missing, and the RomM version the
   new image needs, its `cabinetos.romm-needed` label (the Justfile puts it
   on every image, from `cabinetos.env`, and stops the build when
   `docs/wiki/romm.md` names another). It writes the answer to
   `/run/cabinetos-update/status`; a label it cannot read fails the check.
3. The root script decides: it reports an update whenever bootc finds a
   different image (bootc compares digests, not versions), and "ready" when
   one is already staged. The frontend shows what the file says, and between
   checks still offers a version it found earlier if that is not the one
   running. It holds back an update that needs a newer RomM than the
   server's (`update::rommFor`): "Needs RomM 5.5 or newer." and Download
   greyed out. The server's version is the newest heartbeat's from any
   client this run (`romm::heardServerVersion`), which the frontend writes
   down in `settings.json` for a start with RomM out of reach.
4. Download starts `cabinetos-update-download.service`, which checks again
   and stages the newest image; the next restart or power off applies it. An
   image newer than the one on screen that needs another RomM is not
   fetched: the script reports it as available instead, and the frontend
   decides again.

Where a console takes its updates from is set once, at the first start of a
new console, however the image reached its drive: the installer ISO, or a
disk image (`docs/decisions/2026-11-03-first-time-setup-in-the-os.md`).
`cabinetos-update-source.service` runs `/usr/libexec/cabinetos-update-source`
at the first start of each deployment, alongside systemd-sysusers, which
makes the account (`system_files/usr/lib/sysusers.d/cabinetos.conf`; its
home is `tmpfiles.d/cabinetos.conf`'s). On a console with one deployment
that does not follow a signed CabinetOS image yet, it points it at
`ghcr.io/mmagtech/cabinetos:latest`, signed, offline; every console that
updated into the image it leaves as it is.

How a change gets into that image is in CONTRIBUTING.md, "How a change
reaches a console".

### Running from a USB drive

A portable install is the disk image written to a USB drive and booted on
somebody's PC (`docs/decisions/2026-11-03-running-from-a-usb-drive.md`).
`build-disk.yml`'s `portable` type makes that file: bootc-image-builder makes
a raw disk with no account and no size (`disk_config/portable.toml`),
`ci/make-portable-image.sh` shrinks it, gives it an ISO install's boot line,
does beforehand or delays what a first start would otherwise write
(`docs/decisions/2026-11-04-portable-first-start.md`),
takes out the boot file that would add an entry to the PC's boot list, adds
the key screen a PC with Secure Boot on needs
(`docs/decisions/2026-11-04-portable-secure-boot.md`) and compresses it, and
`ci/check-portable-boots.sh` boots the result in a VM, with Secure Boot on and
with it off, and fails the run unless both reach first run
(`docs/decisions/2026-11-03-portable-disk-image.md`).
What tells a portable apart at run time is one test, `cabinetos-system-disk portable`: the drive
the system is on can be unplugged (the console's own test,
`storage::isExternalDevice`). Then, at each start:

1. udev runs `/usr/libexec/cabinetos-system-disk` on every drive
   (`90-cabinetos-system-disk.rules`). The console's own drive was hidden from
   udisks already (#223); on a portable every drive of the PC that cannot be
   unplugged is too. The console mounts, lists and formats only what udisks
   does not ignore (`drives.cpp`, `consider`), Steam's Storage page leaves it
   out, and Steam is given libraries only on drives the console mounted
   (`cabinetos-steam-libraries`).
2. `cabinetos-grow-root.service`, early, grows the system's partition and
   filesystem into free space directly after it (`cabinetos-grow-root`); after
   the first start it finds none.
3. On a start with no time zone, chrony does not start
   (`chronyd.service.d/50-cabinetos.conf`). First run opens at the time zone;
   the city chosen starts `cabinetos-timezone@<zone>.service`
   (`68-cabinetos-timezone.rules`), which sets the zone and reads the system
   clock again from the PC's clock, which stays in local time
   (`system_files/etc/adjtime`), and leaves that clock alone.

An install from the ISO is on the PC's own drive and has the installer's
zone and clock setting, so none of this happens there.

## 4. Where to look for the most common changes

| To change | Look in |
|---|---|
| Words or layout on a screen | the screen's file (section 1), its numbers in `design.h`; judged on the television |
| A Settings row | `settingsflow.cpp` (the rows and what each does), the feature's `*flow.cpp`, docs/SETTINGS.md |
| What a button does, or where it goes | `input.cpp` (who gets the event), `nav.cpp`, `actions.cpp`; then About, How to, `docs/wiki/navigation.md`, and the printable guide (`docs/wiki/controls.html`, made into `controls.pdf`) |
| Which core plays a platform | `catalog.cpp` (the platform table), `savetable.cpp` (its saves' tag); for an arcade game, `arcadematch.cpp` and `cores/arcade-sets.sh` (`docs/decisions/2026-10-28-arcade-emulator-from-the-zip.md`), for MAME 0.289's pins `cores/fetch-mame.sh`, for sm2-emu's `cores/build-sm2emu.sh`, and for Supermodel's `cores/build-supermodel.sh` |
| An option the console sets on a core | `coreoptions.cpp`, `quality.cpp`, `sysopts.cpp`, `wii.cpp` (Dolphin's rumble for Wii) and `vectrexsheets.cpp` (the Vectrex picture fitted to its sheet): the five sources `launch.cpp` merges, in that order, with a test's `--core-option` laid on top of all five by the core host (`Core::setCommandLineOption`), and `--check-option-tables` checks against `cores/options/` (`optcheck.cpp`); a libretro core in `catalog.cpp`'s table with no list there fails it |
| A new libretro core | `cores/build-core.sh` (a `case` arm, and any patches in an `if [ "$CORE" = <core> ]` block of its own; its name there is the core's name everywhere, and the file is that name without a trailing `_libretro`, plus `_libretro.so`; nothing else in that file changes, or CI rebuilds every core: `ci/core-cache-key.sh`), `build-core.yml`'s list, the two counts of 23 (`ci/stage-image-payload.sh`'s `EXPECTED` and `build_files/install-frontend.sh`), `cores/options/<core>.txt` (made from what `tools/core-options.c` prints for the built core; `cores/check-options.sh` runs it), `catalog.cpp` (the platform's row; the table is positional, read its struct first), `savetable.cpp` (its two tags), `coreoptions.cpp`, `achievements.cpp`'s console table for a new system, `docs/wiki/systems.md`, and the licences (CONTRIBUTING.md, "Duties") |
| Moving a core's pin | its `COMMIT` in `cores/build-core.sh`; in the same commit `cores/options/<core>.txt`, and the same commit in `licences/SOURCES` (then `tools/fetch-licences.py`) and `docs/LICENCES.md` |
| An emulator run as a program | `standalone.h`, its `standalone_<name>.cpp`, its `build_files/install-*.sh` or `system_files/usr/share/cabinetos/flatpaks.list`, its row in `catalog.cpp` |
| The RPCS3 team's settings for a PS3 game | `build_files/rpcs3-config-database.json`, the snapshot of RPCS3's per-game settings database, refreshed by `tools/fetch-rpcs3-config-database.py` only when RPCS3's pin moves (CONTRIBUTING.md, "Duties"), with the pin it was taken against in `build_files/rpcs3-config-database.pin`, which `ci/check-rpcs3-snapshot.sh` and the image build hold to the pinned one; `build_files/install-rpcs3.sh` puts it beside RPCS3, and `standalone_rpcs3.cpp` (`prepareRpcs3`) copies it into RPCS3's config folder before every start; `docs/decisions/2026-11-06-rpcs3-settings-database-snapshot.md` |
| What an emulator may reach on the network | `netblock.cpp` (inside the program), `standalone.cpp` and `standalone_xemu.cpp` (programs: bwrap, `flatpak run --unshare=network`); `docs/decisions/2026-10-26-emulators-get-no-network.md` |
| A RomM request or field | the client (`romm.h`, `romm_*.cpp`, `romm_types.h`); RomM's documentation and behaviour decide; the map of every request is #398's Part 5, section 48 (the audit's comments on #398 each start "Audit, Part N", and sections are numbered across the parts) |
| Saves and states | `savetable.cpp` (what and where), `saves.cpp` (gathering and restoring), `savepick.cpp` (which save a start continues from), `savepush.cpp` (the snapshot a game's save goes up in when it is left, and RomM's answer), `savepicture.cpp` (the save's own picture), `states.cpp`, `statefile.cpp` (what a state file holds), `uploader.cpp` (the queue), `pending.cpp` (the markers), `romm_playdata.cpp` (the requests), `standalone_play.cpp` (emulators run as programs) |
| Something in the image (a package, a unit, a rule) | `build_files/`, `system_files/`; an image build, so it is proven on `testing` |
| Which output plays the sound (the TV, headphones, a speaker or jack that cannot sense a plug, a backup while the TV's is gone) | `system_files/usr/share/wireplumber/scripts/cabinetos-sound-output.lua` (the rule, and why each step exists), `system_files/usr/share/wireplumber/wireplumber.conf.d/60-cabinetos-sound-output.conf` (what else the rule needs, and how it loads), `build_files/sound-output-rule.sh` (the build fails if a WirePlumber update changes what the rule stands on) and `build_files/sound-backup-output.sh` (pipewire-pulse started at login, so PipeWire's Dummy Output catches the sound while the screen sleeps or the TV is off; the build fails if PipeWire moves either); `docs/decisions/2026-11-04-one-sound-rule.md`; `docs/decisions/2026-10-26-sound-backup-output.md` |
| Wi-Fi's background scans and power saving | `system_files/usr/lib/NetworkManager/dispatcher.d/50-cabinetos-wifi-scans` (the scan rule set after each connect, and why), `system_files/usr/lib/NetworkManager/conf.d/50-cabinetos-wifi.conf` (power saving off), `build_files/wifi-scan-rule.sh` (the build fails if NetworkManager or wpa_supplicant moves what either stands on); `docs/decisions/2026-11-08-wifi-scans-and-power-saving.md` |
| Something the console must do as root | a script in `system_files/usr/libexec/`, its unit, a polkit rule naming the unit, `unit::start` from the frontend, and a status file |
| LinkUp | the words and screens: `linkuprules.cpp`, `linkupscreen.cpp`, `linkup.cpp`; a session: `linkupsession.cpp`; anything on the network or a key: `linkup/`, and what it says to a server is `PROTOCOL.md` in MMagTech/cabinet-linkup; the stream's settings: the top of `stream_host.h`, `stream_friend.h` and `friend_sound.h`, and #654's third comment for why each is what it is |
| A Game events message or field | `gameevents.cpp` (the messages: a field is added, never renamed or removed, and a break is `format` 2), `gameevents_test.cpp` (every field is checked there), `docs/decisions/2026-11-15-game-events.md` |
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
  backend model"; `cache.h`), until RomM no longer has the game
  (`docs/decisions/2026-11-01-game-gone-from-romm.md`).
- **gamescope, not Bazzite's gamescope-session-plus** (cabinetos-session's
  header; docs/PROJECT.md, open question 3).
- **The on-disk layout** (docs/PROJECT.md, open question 18; `storage.h`).
- **Vulkan first in the core host** (docs/PROJECT.md, open question 20).
- **The tested image is what ships**; a merge promotes it instead of building
  again (docs/SETTINGS.md, System, "THE TESTED IMAGE IS WHAT SHIPS").
- **Updates are signed, and the console checks** (`build_files/require-signed-updates.sh`).
- **First-time setup is the image's, not the installer's**: the account and
  the update source, so the ISO and a disk image share one copy
  (`docs/decisions/2026-11-03-first-time-setup-in-the-os.md`).
- **A portable install leaves the PC alone**: its drives hidden through
  udisks, its clock in local time, the time zone asked on first run
  (`docs/decisions/2026-11-03-running-from-a-usb-drive.md`), and no boot entry
  written into its firmware (`docs/decisions/2026-11-03-portable-disk-image.md`).
  The one thing that can stay is the key a tester enrols on a PC with Secure
  Boot on (`docs/decisions/2026-11-04-portable-secure-boot.md`).
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
- **CI runs the option tables and the 28 self-tests in
  `ci/run-self-tests.sh`;** the tests it lists as needing a screen, a core, a
  game or a server are run by hand. No Python is tested, and neither are
  `tools/` and `disk_config/`.
  A pull request to `main` never builds the cores, `frontend/ps2`, Cemu,
  sm2-emu, Supermodel, Sunshine or the image, so a fault there first shows at the maintainer's
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
  member order (app.h) and the rows of catalog.cpp's table.
- **Building leaves files in the tree:** the Makefile writes `.o` and `.d`
  files beside the sources, not in `build/`; git ignores them
  (`frontend/.gitignore`).
- **The faults the audit recorded** are on #398, Parts 3 to 5, and become
  pull requests or issues next.

## 7. Where the docs and the code disagree

None is known. The audit (#398, Part 5 sections 44 and 45) found the two
apart in many places. The docs tidy (#427, "Docs tidy: every doc true to the
code, or marked as history (#398)", 2026-10-17) put the docs right, and moved
what describes the project as it was to `docs/history/`. The code comments
it left were put right by 2026-11-03.

`docs/PROJECT.md` is frozen, so an old line there can still differ from the
code; its index gives where each question ended. A disagreement found from
now on is fixed in the same pull request, or listed here until it is.

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
  these rows, when the pin moves. The Flathub ones, Flathub's Mesa among
  them, are put in place on each console by `cabinetos-flatpak-setup`, and
  `ci/check-flatpak-pins.sh` fails on the 1st of each month while one is near
  the end of the few builds Flathub keeps.
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
| gamescope's capture takes a size asked for in its own field | `linkup/capture.cpp` asks for 1080p on a larger screen in gamescope's custom PipeWire property 0x70000 (`pipewire_gamescope.hpp`). Asked any other way, its node goes away until a restart | Not checked. A wrong answer shows as no picture on a friend's console |
| gamescope can crash when a capture stops mid-frame | `linkup/capture.cpp` turns the capture off and waits before it disconnects (0 of 120 crashed). OpenGamingCollective/gamescope#23 has the fix, and is not in our gamescope | `check-upstream-fixes` lists it until it lands |
| GStreamer's Vulkan library cannot draw in an X11 client | The friend's picture is OpenGL on a measurement (#654's fourth comment), because Fedora builds it for Wayland only | `ci/base-watch.txt`, LinkUp: fails the day it links libxcb, which means measure again |
| LinkUp's connection setup (ICE) | `libnice-gstreamer1`, GStreamer's libnice elements, which the base does not carry: `build_files/install-linkup.sh` adds the package and fails the image build when `nicesrc` or `nicesink` is missing | The install script's element check, in the image build |
| gamescope's command line | Every flag and backend `cabinetos-session` and `cabinetos-steam-download` pass | `ci/check-gamescope-flags.sh`, in the base-update pull request itself |
| The screen's sleep, a Vita game's touch screen, Remote Play's picture, the TV's size kept across restarts, screenshots of games run as programs | gamescope's own settings, by name: `drm_sleep_external_screen` (`idle.cpp`, `cabinetos-remoteplay`), `touch_click_mode` 4 and 1 (`vtouch.cpp`), `composite_force`; `GAMESCOPE_PATCHED_EDID_FILE`, `GAMESCOPE_MODE_SAVE_FILE` and `gamescopereaper` (`cabinetos-session`); `gamescopectl screenshot`, which returns before its file is written (`states.cpp`). The console reads gamescopectl's exit as "gamescope took it", which says only that gamescope answered | `log:` (The picture on the TV): the names. Not checked: what 4 and 1 mean, the screenshot's timing |
| The pause menu over a PS2 game or a game run as a program | The `STEAM_OVERLAY`, `STEAM_INPUT_FOCUS` and `GAMESCOPE_NO_FOCUS` properties, gamescope placing a window when it maps (`overlaywin.cpp`), a transparent window, 30 clear frames before it shows (`standalone_play.cpp`) | `tv:` (The picture on the TV): the pause menu over a PS2 game; `tools/gamescope-overlay-test.sh` by hand |
| Smooth frames | gamescope keeping one picture waiting, so every emulator is given vsync (`standalone_*.cpp`, `frontend/ps2/CabinetPS2Host.cpp`); the refresh SDL reports under it (`pace.cpp`); a new emulator window taking the screen by itself; gamescope ending with its program, so the console restarts itself in place (`startup.cpp`) | `tv:` One heavy game. Frame times are measured by hand |
| PS2's own window, and closing an emulator politely | SDL choosing X11 under gamescope and `GAMESCOPE_WAYLAND_DISPLAY` (`ps2window.cpp`); Xwayland listing each emulator's window under the root with `_NET_WM_PID` and `_NET_WM_NAME`, and `WM_DELETE_WINDOW` (`xtitles.cpp`) | Not checked: PS2 falls back to copying frames, and a close to SIGTERM, without a word |
| Which output plays the sound | Three steps between WirePlumber's own (`system_files/usr/share/wireplumber/scripts/cabinetos-sound-output.lua`): the list its default output is picked from, where a program's sound goes with no default, and which profile a card takes; its settings (`system_files/usr/share/wireplumber/wireplumber.conf.d/60-cabinetos-sound-output.conf`); PipeWire making the TV's output again at start, which `volume.cpp` waits for | The image build stops if a file those steps stand on changes (`build_files/sound-output-rule.sh`); `log:` and `tv:` (Sound) |
| The console's sound coming back after the screen sleeps or the TV is off (#228, "Sound can die until a restart") | pipewire-pulse's unit, `/usr/lib/systemd/user/pipewire-pulse.service`, started at login by a link the image build makes beside it in `default.target.wants`; and `/usr/share/pipewire/pipewire-pulse.conf` loading `module-always-sink` by default (its `pulse.cmd` line with the condition `pulse.cmd.always-sink = !false`), which makes Dummy Output whenever no other output exists, so SDL moves the console's sound off the TV's dead output | The image build stops if the unit or the file is gone, if that line is no longer in `pulse.cmd` as at PipeWire 1.6.9, or if anything sets the property or changes `pulse.cmd` (`build_files/sound-backup-output.sh`); `log:` (Sound): pipewire-pulse runs. Not checked: the sound moving to Dummy Output and back, which needs the screen put to sleep and woken |
| The volume keys | `wpctl get-volume`'s "Volume: 0.50" and `set-volume` (`volume.cpp`); every key device readable through systemd's seat rules, after `system_files/usr/lib/udev/rules.d/71-cabinetos-key-devices.rules` | `log:` (Sound; the TV must be awake) and `log:` (Kernel and startup) |
| A PS3 player's tilt reaching RPCS3 (#683) | udev's input_id marking a device with the accelerometer property `ID_INPUT_ACCELEROMETER`, and systemd's seat rules turning the tag that `system_files/usr/lib/udev/rules.d/71-cabinetos-motion-devices.rules` gives each motion device into the console's access, so RPCS3, running as the console's user, can open it (`vpad.cpp`) | `log:` (Kernel and startup): `--motion-device-test` makes two and opens them |
| A Switch, Wii U or Vita player's tilt reaching Eden, Cemu or Vita3K (#669) | The kernel's PlayStation driver, hid-playstation, taking a `/dev/uhid` device with the DualSense Edge's id (its id table), asking it feature reports 0x05, 0x09 and 0x20 in that order, applying 0x05's calibration to every input report 0x01 as its source at 7.2.11 does (`dualsense_get_calibration_data`, `dualsense_parse_report`), and making a gamepad, a "Motion Sensors" device and a touchpad that share one uniq, the address report 0x09 gives, each at the resolutions it declares (8192 a g, 1024 a degree a second); uhid itself; udev's `ID_INPUT_ACCELEROMETER` on the motion device, which `system_files/usr/lib/udev/rules.d/71-cabinetos-motion-devices.rules` turns into the seat's access only for a stand-in whose address says it carries a pad's tilt; and SDL's Linux joystick code pairing a sensor device to a pad by uniq, scaling its axes by the node's resolution, and finding no sensors on a pad whose sensor device it cannot open (`ConfigJoystick`), the same in Eden's 3.2.30, Cemu's 3.4.10, Vita3K's 3.4.16 and the console's 3.4.18 (`vdualsense.cpp`) | `log:` (Kernel and startup): `--dualsense-test` makes two for real, one for a pad without sensors, and reads them back through SDL as the emulators' does. Not checked: each emulator's own SDL, seen as a game's pad lying still |
| Sound controllers kept awake | tuned's post-loaded profile, read only from `/etc`, and its audio plugin (`system_files/usr/lib/tuned/profiles/cabinetos-audio/tuned.conf`) | `log:` (Power) |
| Games on Bazzite's own tuned profile | tuned's `switch_profile` and the names `balanced` and `throughput-performance-bazzite` (`powerprofile.cpp`) | `log:` (Power) |
| Picture quality's Auto | amdgpu's `pp_dpm_sclk`, cpufreq's and the topology's files (`gpu.cpp`, `cpus.cpp`) | `log:` (Kernel and startup) |
| The GPU games draw on, with two (#659) | Mesa giving one GPU the same UUID in GL (`GL_DEVICE_UUID_EXT`, GL_EXT_memory_object) and in Vulkan (`VkPhysicalDeviceIDProperties`), which is how the screen's GPU is matched (`vkbridge.cpp`, `gpu.cpp`); the loader listing devices in one order for every process, which is how Xenia is told its device by number (`standalone_xenia.cpp`) | `--gpu-choice-test` in CI, for the rule; `log:` (Kernel and startup): the `[gpu]` line names the GPU and why. Not checked: a PC with two GPUs, which the reference console is not; seen as the `[gpu]` line saying the screen's GPU is not among the Vulkan devices |
| Controller drivers loaded at boot | The kernel's module names, and its list of pads that wait for their own driver (`system_files/usr/lib/modules-load.d/cabinetos-controllers.conf`) | `log:` No service failed to start (Kernel and startup); `series:` Controllers lists the list's changes, for when the file can go |
| A real Wii Remote kept for the bridge | 92-viia.rules (`ublue-os-udev-rules`) opening every hidraw node before ours closes the Remote's; `/dev/uhid`'s grant; `HID_NAME` in a node's uevent; SDL's Wii driver switched off by its hint (`startup.cpp`) | `log:` and `tv:` (Controllers) |
| A Remote with Nintendo's own id counting once (#549, "A Nintendo Wii Remote counts once, not as a pad and a Remote") | The kernel's Wii Remote driver, hid-wiimote, taking exactly Nintendo's two ids over Bluetooth (057E:0306 and 0330, kept for it in hid-quirks.c), and udev's `ID_INPUT_JOYSTICK` and `LIBINPUT_IGNORE_DEVICE` on its devices, which `system_files/usr/lib/udev/rules.d/72-cabinetos-wii-remote.rules` sets to hide them | `log:` (Kernel and startup): the driver takes exactly those two ids; `series:` Controllers lists its changes on a new kernel series. Not checked: a Remote with Nintendo's id itself, which the A9 does not have (its Remotes are TechKens) |
| Pairing pads and Wii Remotes | bluetoothctl's text (`bluetooth.cpp`, `wiiremote.cpp`); BlueZ wanting an agent for "just works" pairing (`btagent.h`) and its wiimote plugin offering the Wii's PIN (`wiipair.cpp`); btmgmt's `dev_found` and `name` lines (`cabinetos-wii-search`) | `log:` (Controllers): bluetoothctl's lines, btmgmt there; `tv:` a pad, a Wii Remote. Not checked: btmgmt's lines and the agent, seen when a pad is paired |
| Which pad is which, its battery and rumble | SDL's serials, paths and GUIDs (the shortcut button is kept per GUID, `shortcuts.cpp`), `SDL_GetGamepadPowerInfo`, a made-up joystick id on pushed events (`wiiremote.cpp`); the kernel's power_supply and input files (`battery.cpp`, `players.cpp`) | `tv:` A Bluetooth pad (Controllers, Libraries the console's screens use), Rumble. Not checked: the battery row, a kept shortcut button |
| Network settings and first run | nmcli's terse fields and its English error words, "secrets" for a wrong password among them (`net.cpp`, `wifi.cpp`); pkcheck's answer (`probes.cpp`) | `log:` (Network): the fields. Not checked: the error words |
| Remote Play's ports and the report's link | The default firewall zone leaving 1025 to 65535 open (`report.cpp` takes a port the system picks) | `log:` (Network) |
| Wi-Fi scans only on a weak signal | NetworkManager's two background scan rules, `simple:30:-65:300` and `simple:30:-70:86400`, which it does not document and has no setting for, replaced by their text after each connect (`system_files/usr/lib/NetworkManager/dispatcher.d/50-cabinetos-wifi-scans`); NetworkManager using wpa_supplicant and never sending the rule again during a connection; wpa_supplicant 2.11 taking a new rule over D-Bus while connected, with no reconnect | The image build stops if NetworkManager's rules, its Wi-Fi backend or wpa_supplicant's version change, or the script cannot run (`build_files/wifi-scan-rule.sh`); `log:` (Network): the script's rule is the last one set this boot |
| Wi-Fi power saving off | NetworkManager's `wifi.powersave` default in `[connection]` (`system_files/usr/lib/NetworkManager/conf.d/50-cabinetos-wifi.conf`) | The image build stops if NetworkManager does not read it (`build_files/wifi-scan-rule.sh`); `log:` (Network): the card says power saving is off |
| Tailscale, for Remote Play and RomM away from home | `tailscale status --json`'s fields and state words, `tailscale up` refusing unless every setting is named (`tailscale.cpp`, `tailscaleflow.cpp`), `tailscale set`'s flags (`system_files/usr/lib/systemd/system/tailscaled.service.d/50-cabinetos.conf`) | `log:` and `tv:` (Tailscale, for Remote Play and RomM away from home). Not checked: `up`'s rule |
| Every root job | Our polkit rules, reading polkit's subject and action fields (`system_files/usr/share/polkit-1/rules.d/`) | `log:` and `tv:` (What the console may do as root) |
| System update | `bootc status --json`, the text of `bootc upgrade --check` and its progress lines, skopeo and jq, `skopeo inspect --config` giving the image's labels under `.config.Labels`, ostree's names for the layers it holds (`cabinetos-update`); RomM's heartbeat giving its version as numbers and dots, a release's tag after a "-", or "development" for a RomM built from source (`SYSTEM.VERSION`, `romm_pairing.cpp`, read by `update.cpp`); `/proc/cmdline`'s `ostree=`, the deployment's `.origin`, `/usr/share/ublue-os/image-info.json`, which no package owns (`update.cpp`) | `log:` and `tv:` (System update) |
| A new console's account and update source | systemd-sysusers and `cabinetos-update-source.service`'s `ConditionNeedsUpdate=/etc` holding at the first start of each deployment because ostree leaves `/etc/.updated` out of every new one; `bootc switch --mutate-in-place`, a flag bootc hides, rewriting the newest deployment's `.origin` and needing `/sysroot`, read-only on a running console, remounted read-write in a mount namespace of its own; the installer's own `%post` (bootc-image-builder's `osbuild-base.ks`) pointing a new console at the ISO's tag, unsigned; `bazzite-hardware-setup.service` making a second deployment, with the first one's `.origin`, on a new console's first start, after this step (`cabinetos-update-source`) | `log:` (System update): a console that updated was left as it was. Not checked: a new console's first start, which shows only by installing an ISO in the VM (`tools/installer-vm.sh`) |
| Games drives | udisks's object paths (the console works out its own disk's, `udisks.cpp`), its properties, Format's `take-ownership` and `mkfs-args`, "busy" in its errors, mounts under `/run/media/<user>` (`drives.cpp`) | `log:` (Games drives): the A9's drive is mounted. Not checked: Format, Eject, the path naming |
| A portable install hiding the PC's own drives | udev's `UDISKS_IGNORE` becoming udisks's `HintIgnore`, and Steam's Storage page leaving out a drive with it; `lsblk`'s `PKNAME`, `--inverse` and `TYPE`, and sysfs's `removable` (`cabinetos-system-disk`) | Not checked: seen only on a portable with a second drive (`tools/installer-vm.sh portable`) |
| A portable install filling its drive | sfdisk growing the last partition to the drive's end and moving the GPT's backup there as it writes, `partx --update` resizing a partition in use, the filesystems' online resize (`cabinetos-grow-root`) | The image build asserts the tools. Not checked: seen as a portable's drive staying at the image's size, said by `cabinetos-grow-root` in the journal. `ci/check-portable-boots.sh` fails the disk build if it did not grow |
| The portable disk image | bootc-image-builder's raw layout (BIOS boot, vfat EFI, ext4 `/boot`, btrfs root last) and its boot line's `console=tty0 console=ttyS0`; shim on a drive's fallback path running `EFI/BOOT/fb*.efi` when it is there and otherwise `grub*.efi` beside it; Fedora's grub finding its `grub.cfg` in `EFI/fedora` when started from `EFI/BOOT`; bootupd's `grub.cfg` reading `custom.cfg`, grub's `fallback` when the kernel is refused, and shim's `EFI/fedora/mmx64.efi`; the kernel being signed with `/etc/pki/akmods/certs/akmods-ublue.der`; Bazzite's hardware setup adding only `bluetooth.disable_ertm=1` on a normal PC, and skipping its slow job and restart when that is already on the boot line; `bootc switch` writing an `ostree-image-signed:` line in the deployment's `.origin`; the emulators' two units enabled in `/etc`, and `flatpak-add-fedora-repos.service` being what adds Flathub; btrfs shrinking while mounted; sfdisk rewriting a table from its own dump, ids kept (`ci/make-portable-image.sh`). systemd reading a unit from QEMU's SMBIOS table (`ci/check-portable-boots.sh`). bootupd rewrites the boot files it tracks when a new shim or grub arrives, so a later shim brings the fallback back on a drive already written | The script stops the disk build when the layout, the boot line or the boot files change, or the key no longer verifies the kernel; the boot check fails the build if the file does not reach first run with Secure Boot on and off, or the firmware gained an entry. Not checked: real firmware, BitLocker and a real stick, which are a person's test |
| The PC's clock on a portable install, and first run's time zone | The kernel writing the hardware clock in local time only when the first time zone it is told at a start is not UTC; chrony's `rtcsync` and its unit's name (`chronyd.service.d/50-cabinetos.conf`); the kernel's `/sys/class/rtc/rtc0/since_epoch` giving the clock's face as if it were UTC, and `timedatectl set-timezone` writing the hardware clock at once, which `cabinetos-timezone` avoids; systemd's first-boot wizard (`systemd-firstboot.service`, on in the base) skipping a disk image's first start because its `/etc/machine-id` is empty, not "uninitialized": the image has no language, keyboard or zone file, so it would otherwise ask for all three on the text console; tzdata's `zone.tab` and `iso3166.tab` (`timezone.cpp`) | The image build asserts the files and tools, no zone in the image and LOCAL in `/etc/adjtime`; `log:` (Kernel and startup): chrony's `rtcsync` and condition, the zone list. Not checked: the wizard, seen as a disk image's first start stopping at a text prompt, and the kernel's rule, seen as Windows showing a time hours off after a portable's first session |
| Game manuals | pdfinfo's "Page N size" and "rot" lines and pdftoppm's PPM (`manual.cpp`) | `log:` (Game manuals) |
| The type ramp | The font files' names (`startup.cpp`) | `log:` and `tv:` (Libraries the console's screens use) |
| Covers in every format | SDL3_image loading its codecs when first needed (`imagecodec.cpp`) | The image build asserts their sonames (`build_files/require-frontend-libs.sh`); `tv:` Home's covers |
| Downloads | libcurl refusing a raw space in an address, its error codes read as "server away" (`romm_http.cpp`) | `tv:` A download |
| Original Xbox and Switch | Flatpak's system layout, checked to say an emulator is installed (`standalone.cpp`); `flatpak run`'s flags; the program three processes down its sandbox (`proc.cpp`) | `log:` and `tv:` (Original Xbox and Switch games) |
| No emulator reaching the network (#557) | The C library's loader letting the program's own `socket`, name lookups and `pthread_create` answer every library's call (exported by `frontend/Makefile`, `netblock.cpp`); libcurl looking a name up on a thread it starts inside the call that asked (its threaded resolver), which only the thread's mark refuses; systemd-resolved answering a lookup only through its folder `/run/systemd/resolve`, which bwrap hides (`standalone.cpp`) | `--netblock-test` in CI, on the builder's Fedora libcurl; `log:` (Downloads from RomM) runs it on the reference console with the image's; `log:` (Kernel and startup, Original Xbox and Switch games): a name lookup under bwrap fails |
| One shader cache per emulator | Mesa making `mesa_shader_cache` inside `MESA_SHADER_CACHE_DIR` (`standalone.cpp`, `standalone_xemu.cpp`) | Not checked: every game would compile its shaders again |
| The power button | logind's inhibitor, and the power key reaching SDL through gamescope (`input.cpp`, `power.cpp`) | Not checked |
| The desktop kept out, the builders in step | The packages the strip scripts remove; the builders' Fedora | `ci/check-base-update.sh`, in the pull request itself; `log:` (Apps CabinetOS removes) |
| The Steam tile | The `steam` package and Steam's session file | `log:` and `tv:` (Steam) |
| The diagnostic report | The text of many tools (`journalctl`, `nmcli`, `bluetoothctl`, `wpctl`, `tailscale`, `vulkaninfo`, `edid-decode`, `flatpak` and others) read by `system_files/usr/libexec/cabinetos-report`; and the frontend's own lines that mark a game's start and its return to Home, which its game section cuts at | The build asserts the tools are there (`build_files/build.sh`); their text is not checked. A change shows as a missing part of the next report |

### Moved by a pin of ours

| What | Leans on | Checked when the pin moves |
|---|---|---|
| MAME 0.289's saves and high scores | libretro MAME writing NVRAM, settings and high scores only in `retro_deinit` (`retro_finish`), so the console shuts every core down as a game is left (`play.cpp`); its folders split between the save folder (cfg, diff, nvram) and the system folder (home, ini, plugins) by `opt_type` in `retro_init.cpp`; the high-score plugin writing to its homepath's `hiscore` (`plugins/hiscore/init.lua`), which `mamehome.cpp` points at the person's save folder; and the plugin starting only with `boot.lua` and `json` beside it | `cores/fetch-mame.sh` stops when the folders, the plugin's path or the deinit read otherwise at the pin. Not checked: that a MAME which saved earlier would only save earlier |
| melonDS's last save before quitting | A patch: `NDSCart_SRAMManager::FlushSecondaryBuffer()` added to `retro_unload_game` (`cores/build-core.sh`, the melonds block; Cabinet's) | The patch's anchor is asserted, so the build stops. No issue (it predates the rule of 2026-10-20) |
| PPSSPP's shader cache, and its CPU engine named in the log | Two patches: the cache saved in `GPU_GLES::DeviceLost`, a log line in `MIPSState::Init` (`cores/build-core.sh`, the ppsspp block; Cabinet's) | Asserted. No issue (they predate the rule) |
| Each 3DS game its own folders | A patch to Azahar's `SetUserPath` | Asserted; tracked on #534 ("Azahar patch: each game sets its own folders (until Azahar fixes SetUserPath)") |
| PS2's sound through the console | A patch to PCSX2's `pcsx2/Host/AudioStream.cpp`, and `AudioStream`'s own members (`frontend/ps2/CabinetPS2Audio.cpp`) | The anchor is asserted (`cores/build-pcsx2.sh`); a changed member fails the build. Not an upstream fault, so no issue |
| A game set as a slave cabinet, linked to itself (Dirt Devils) | A patch to Supermodel's `Src/Model3/SimNetBoard.cpp`: a machine linked to itself takes the master's path whatever its game is set to (`cores/build-supermodel.sh`, "the patch") | Asserted. Not a wait: Supermodel declined it (trzy/Supermodel#360), so it stays, and its issue, #634 ("Supermodel patch: a machine linked to itself runs as the only cabinet (Dirt Devils)"), is closed (`docs/decisions/2026-11-01-declined-patch-gets-no-issue.md`) |
| The PS2 host (`frontend/ps2/`) | PCSX2's internals, not a library interface: 55 `Host::` functions, `EmuFolders`, `VMManager`'s start order, `PadDualshock2`'s inputs, the settings keys it fills in memory (PCSX2 ignores unknown ones), the patch named "Widescreen 16:9", `GSSaveSnapshotToMemory`, the memory map RetroAchievements reads, and a linker wrap of `InputManager::SetPadVibrationIntensity` by its mangled name | A missing function fails the build (`-z defs`, the probe in `cores/build-pcsx2.sh`). The picture keys: `tools/check-applied.sh` reads the `[ps2] renderer` line. Not checked: the other keys, the rumble wrap (it links and does nothing), the memory map |
| PS2's widescreen patches | `PCSX2/pcsx2_patches` at a pinned commit, packed as PCSX2 does | `cores/build-pcsx2.sh` asserts the commit and that `patches.zip` was made |
| Every option the console sets on a libretro core | The option names and values each core declares | `--check-option-tables` in CI, and `cores/check-options.sh` at each core build (`cores/options/`); Dolphin's and FBNeo's lists by hand (`tools/check-applied.sh --lists`). Not checked: the defaults the console leaves alone and relies on (`coreoptions.cpp`) |
| How each core is driven | Behaviours of single cores the host works around: Flycast's four ports and threaded renderer, PPSSPP's Vulkan 1.0 request and its abort at a raised log level, Dolphin's mid-game geometry and its memory map from the first frame, melonDS's GL context, Azahar's states, Mupen64Plus-Next's frame per state, the queue lock argued from each Vulkan core's source (`core*.cpp`, `vkhost.cpp`, `pace.cpp`) | Not checked: seen only in a game on that core |
| A fight stick's layout in arcade games | The order each arcade core declares a game's buttons in: FBNeo in the game's own order, Flycast a Naomi or Atomiswave board's in the board's (B A Y X R L R2 L2, its `nao_joymap` and `aw_joymap`), MAME 2003-Plus in RetroPad id order with its Classic table (`EMIT_RETROPAD_CLASSIC` in `src/mame2003/mame2003.c`) saying which id is which button, copied into `fightstick.cpp`; MAME 0.289 naming none, so the console declares them from its catalogue's count in its joypad's order, B A Y X L R then L3 and R3 (`button_mapping` and `configure` in `src/osd/modules/input/input_retro.cpp`, `fightstick.cpp`'s `kMame`); sm2-emu hearing no RetroPad at all, so the console lays out the three buttons its list gives a game (`buttons3` in `data/games.xml`) and writes the pad button each landed on into its settings (`pad<n>_button_button<k>`, `standalone_sm2emu.cpp`); Supermodel likewise, its four for a fighting game or Spikeout and three for Virtua Striker (`standalone_supermodel.cpp`, from its catalogue); and FBNeo's own combination buttons, known only by their names, "Buttons AB" and the like (`src/burner/libretro/retro_input.cpp`) | MAME 2003-Plus: the core's build stops when the table, or the plain joypad that selects it, reads otherwise (`cores/build-core.sh`, its mame2003_plus block); and a `[fightstick]` line in the log when a label's own button number ("Button 3", "B3: ...") is not the table's. FBNeo: its build stops when a combination button is named otherwise, the "P1 " is no longer taken off a name, or a game's own button is named "Buttons ..." (its fbneo_libretro block). Flycast: its build stops when either map reads otherwise (its flycast block). MAME 0.289: `cores/fetch-mame.sh` stops when its joypad's order or its 7th and 8th move. Not checked: FBNeo's order, seen only as a stick's buttons in the wrong places; the count in MAME's list, seen as too few or too many rows in Controls |
| Holding Start opens a MAME's own menu, or a Naomi or Atomiswave board's test menu | MAME 2003-Plus opening its menu on Tab or on player 1's ninth button (`IPT_UI_CONFIGURE` in `src/inptport.c`), the left stick click being the ninth button in all four of its layouts (`EMIT_RETROPAD_*` in `src/mame2003/mame2003.c`), and its reading the keyboard on port 0 beside the pad while `mame2003-plus_input_interface` is at its default, "simultaneous" (`osd_is_key_pressed`); the console presses Tab and keeps the click from it (`arcadesettings.cpp`). MAME 0.289 opening its menu on Tab alone, with no button for it, and hearing keys only through its keyboard callback (`retro_keyboard_event`, set with `RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK` in `src/osd/libretro/libretro-internal/libretro.cpp`), which `Core::setKey` tells. FinalBurn Neo's own hold, its `fbneo-diagnostic-input` left at "Hold Start". Flycast's board Test and Service on L3 and R3, which the console keeps from every player and presses Test with | MAME 2003-Plus: the core's build stops when the menu's keys, the click's button or the option's default read otherwise (`cores/build-core.sh`, its mame2003_plus block). Flycast: its build stops when L3 and R3 are no longer Test and Service (its flycast block). MAME 0.289: `cores/fetch-mame.sh` stops when Tab is no longer its key, a button opens its menu, or it no longer gives a keyboard callback. Not checked: FinalBurn Neo's default, seen as a hold that opens nothing |
| A DS or 3DS game's screens on L3, the DS's lid on R2 | melonDS's lid on L3 and its swap on R2 (`src/libretro/input.cpp`), and its `melonds_screen_layout` read again mid-game, the new shape sent; Azahar's Home and swap on L3 (`src/citra_libretro/citra_libretro.cpp`), its `citra_layout_option` and `citra_swap_screen` read again mid-game (`HasUpdatedConfig`), and its HOME doing nothing until the 3DS Home Menu has run (`AppletManager::ButtonUpdateEvent`); the console keeps L3 from both and gives melonDS R2 as its L3 (`screenview.cpp`) | The option values: `--check-option-tables`. Not checked: the buttons and the options read mid-game; a pin that moves them shows as L3 changing nothing, or closing the DS lid |
| Controls' rows and a person's changes | Each arcade core's names for a game's buttons, as it declares them: the rows are its words, MAME 2003-Plus's "B<n>: " taken off (`controls.cpp`, `trimLabel`), MAME 0.289's, sm2-emu's and Supermodel's the console's own "Button 1" on (`fightstick::mameButtons`, `fightstick::programButtons`), and a person's file names each moved button by those words | Not checked: a name a new pin changes leaves that person's line for it unused, said once as `[controls] <game>: no button called ...`, and the button back where it was; a changed prefix shows on the row |
| A Naomi or Atomiswave game's save, and its BIOS | Flycast writing a board's memory as `reicast/<the zip's own name>.nvmem` in the save folder (the one file that reaches RomM; `.eeprom` and Atomiswave's `.nvmem2` stay on the console, #353), and reading a board's BIOS zip from `bios/dc/` by the names in its `naomi_roms.cpp` (`savetable.cpp`, `launch_routes.cpp`) | The core's build stops when the save's folder or name reads otherwise (`cores/build-core.sh`, its flycast block); the BIOS names come from its own list (`cores/arcade-sets.sh`) |
| Neo Geo as the arcade board | FinalBurn Neo's `fbneo-neogeo-mode` and its value `MVS_EUR` (`coreoptions.cpp`), an option it declares only with a Neo Geo game loaded | `--check-option-tables`, against FBNeo's list taken on a Neo Geo game (`tools/check-applied.sh`) |
| Which emulator plays an arcade game | Each emulator's own list, read in CI at the pin: FBNeo's "Arcade only" DAT and its drivers (the BIOS files they mark `BRF_OPT`, the only ones its loader does without; input lists named "P1 Gun X" for its light-gun games, "P1 Kan", "P1 Pon" or "P1 Chi" for a mahjong panel, and the `GBF_CASINO` genre), 2003-Plus's `metadata/mame2003-plus.xml` (its default `biosset`, the BIOS it loads) and the categories of its `metadata/catver.ini` for the games made for anything but a controller, MAME 0.289's `-listxml` (mamedev's release asset; its default `biosset`, its devices' files, which it lists under each device, `ismechanical`, its mahjong, hanafuda and gambling controls, its light guns, and a touchscreen device beside a light gun, with its player 1's button count), the form of Flycast's `naomi_roms.cpp` and `naomi_roms_input.h` (a reload or Atomiswave trigger for a gun game, no player buttons for a game played on its screen), and sm2-emu's `data/games.xml` (its `preliminary` flag and `<lightgun>`, a clone taking its parent's regions it names none of and its flags, as its `src/rom/game_db.cpp` does, a region marked `required="false"` left out, `buttons3` for a three-button game), and Supermodel's `Config/Games.xml` (a clone's files laid over its parent's by offset as its `src/GameLoader.cpp` does, a region marked `required="false"` left out, its input types for the button count) with MAME 0.289's light-gun and not-for-a-controller marks for the same set, its own list giving the mounted guns and the light gun the same control; RomM's `archive_members` for a loose zip and its listing of a folder game's top-level files; a CHD's header layout, versions 3 to 5, for its SHA1 (`arcadematch.cpp`) | `cores/arcade-sets.sh` stops on a list it cannot read, one that comes out short, or a line out of form. Not checked: RomM's fields, and its labels for arcade folders (`arcadematch::isArcadeSlug`), read against a RomM update by hand |
| Where each core keeps saves and firmware | File names and folders each core writes and reads: Flycast's VMU, the Sega CD `.brm`, 3DO's nvram, MAME's and FBNeo's, PPSSPP's `SAVEDATA`, Azahar's `sdmc`, the BIOS names (`savetable.cpp`, `filesave.cpp`) | Not checked: a moved file means saves stop travelling, with no error |
| States with achievement progress | RetroArch's state file layout, version 1 (`statefile.cpp`) | Not checked; a state of another version is refused and logged |
| GameCube and Wii saves, rumble, Miis | Dolphin's `Dolphin.ini` (`SlotA = 8`, a raw value of Dolphin's device list), its save and NAND folders, SYSCONF's `BT.MOT`, the Mii file, its device ids as raw numbers (`dolphinprep.cpp`, `savetable.cpp`, `wii.cpp`, `wiimii.cpp`, `core.cpp`) | Not checked |
| The Wii bridge's stand-in Remote | Dolphin and Cemu finding a Remote by Nintendo's id on hidraw, and Dolphin's extension encryption (`wiibridge/`) | `tv:` A Wii Remote (Controllers), when BlueZ moves. Not checked when Dolphin's or Cemu's pin moves |
| Every emulator run as a program | A Flatpak's process name, found in `/proc` (`eden`, `xemu`; one in the image, sm2-emu and Supermodel among them, is bwrap's one child, whatever it calls itself); exit status 0 read as the player quitting; its log's wording; its window title (Eden's and Xenia's change only while a game runs, `xtitles.cpp` reads them through Xwayland); its honouring of `XDG_*` folders (`standalone.cpp`) | Not checked: a missed match leaves the player on the emulator's own box until the pause menu |
| The virtual pads given to emulators | SDL's GUID layout and button numbering, as each emulator's own SDL computes them, for the plain controllers and for the stand-in DualSense, whose buttons the kernel's driver numbers (`vpad.cpp`, `vdualsense.cpp`) | Not checked: buttons scrambled with no error |
| Switch (Eden, Flathub pin) | Its flags `-f -g`, portable `user/` folder, `qt-config.ini` keys and their `\default` siblings, `Controls` strings, among them `motionleft` and `motionright` bound to the stand-in DualSense by its GUID with `motion:0` (#669), `profiles.dat` layout and the save folder named from it, keys and firmware folders, log phrases and loader error numbers, window title (`standalone_eden.cpp`, `standalone.cpp`, `quality.cpp`) | `tools/check-applied.sh` reads the picture keys from Eden's log. The rest is not checked: a renamed key is a box over the game, a changed `profiles.dat` stops Switch saves travelling with no error |
| PS3 (RPCS3, our pin) | Its flags, the log lines that say an install worked, `config.yml`, `CurrentSettings.ini`, `Default.yml` (evdev names, and each player's `Buddy Device` and `Motion Sensor` keys) and `vfs.yml` keys; its evdev handler finding a player's motion device by its name and the accelerometer property, and reading its axes, 0 to 1023, as the DualShock 3's units, 512 at rest and 113 a g (`standalone_rpcs3.cpp`, `vpad.cpp`); the installed layout, `.rap` in `exdata` (`standalone_rpcs3.cpp`); its per-game settings database as a file, `GuiConfigs/config_database.dat`, the JSON its `read_json` requires of it, and its log lines `Finished reading config database from file` and `Applying database config` (`standalone_rpcs3.cpp`, `build_files/rpcs3-config-database.json`) | `build_files/install-rpcs3.sh` asserts it starts and is the pinned build, that the snapshot is the JSON RPCS3 reads, and that the program's text still holds the file's name, both log lines, and the Buddy Device and four Motion Sensor keys; `tools/check-applied.sh` reads the `Video:` lines and that the database was read. The rest is not checked: a handler that stops matching the motion device logs `add_motion_device in bindPadToDevice failed` and every pad lies still, and changed units show as a tilt too strong, too weak or the wrong way |
| Original Xbox (xemu, Flathub pin) | Its flags, `xemu.toml` keys, QMP's `memsave` and the XBE header to see the running title, its menu's buttons, and its blank drive's qcow2 and FATX layout (`standalone_xemu.cpp`, `xboxhdd.cpp`) | `tools/check-applied.sh` reads `renderer` and `surface_scale`; the drive's headers are checked as it is read; the drive file is pinned by checksum (`build_files/install-xemu-drive.sh`). The rest is not checked |
| Xbox 360 (Xenia Edge, our pin) | About 27 `--` settings (an unknown one makes it print its help and quit, `tools/check-applied.sh`'s header), the `x> ` log prefix, its title, the profile's `Account` file and its encryption (`standalone_xenia.cpp`, `x360profile.cpp`) | `tools/check-applied.sh` sees that a log was written. The rest is not checked |
| Wii U (Cemu, our pin) | `-g -f`, log phrases and its "Error" box, `settings.xml` and controller profile keys, among them `motion`, which turns a stand-in DualSense's tilt on, and its reading that tilt for the GamePad alone (`VPADController::update_motion`, #669); its turning SDL's PlayStation HID driver on in code (`SDLControllerProvider.cpp`), which `SDL_JOYSTICK_HIDAPI_PS5=0` in its environment overrides so the stand-in is read through its event node, the save folder, the GPU's UUID (`standalone_cemu.cpp`); the `.wua` archive layout (`wiiu.cpp`) | `tools/check-applied.sh` reads `api`, `VSync` and `AsyncCompile`. The rest is not checked: a renamed `motion` key leaves the GamePad lying still, and a renamed hint leaves the game with no controller at all |
| Model 2 (sm2-emu, our pin) | Its flags `--config`, `--nvram` and `--game`, the `sm2-emu.ini` keys the console writes (its shortcuts, Guide chords and overlay off, Vulkan, the render scale, the pads), SDL's quit on SIGTERM ending its loop and NVRAM written only after it, its `<set>.nv` and `<set>.eeprom` names, the two lines it prints for a game it cannot open, its `games.xml` found at `../share/sm2-emu` (`standalone_sm2emu.cpp`, `standalone.cpp`) | `cores/build-sm2emu.sh` stops when a flag or key the console uses is not in its source, the quit or the NVRAM write moves, the Vulkan backend is not built or libcurl is linked; `build_files/install-sm2emu.sh` checks its libraries against the image. Not checked: the file names and the two lines, seen as saves not travelling or "The game closed unexpectedly" in place of "Couldn't start this game" |
| Model 3 (Supermodel, our pin) | Its flag `-game-xml-file` and every `Supermodel.ini` setting and input the console writes (every key shortcut, Test and Service off, the net board linked to itself on 1970, the drive board running, its forces reaching a pad as rumble, gamepad input, two sound channels), its folders kept beside a `Config` in its working folder, the crosshair pictures it will not start without, SDL's quit on SIGTERM ending its loop and NVRAM written only after it, its `<set>.nv` name, the two lines it logs for a zip it cannot use, a net board linked to itself running alone (`standalone_supermodel.cpp`, `standalone.cpp`) | `cores/build-supermodel.sh` stops when a flag, setting or input the console writes is not in its source (read from `standalone_supermodel.cpp` itself), the quit, the NVRAM write, the working-folder rule or the self-linked net board moves, or SDL2_net is not taken from its own `lib/`; `build_files/install-supermodel.sh` checks its libraries against the image. Not checked: the file name and the two lines, seen as saves not travelling or "The game closed unexpectedly" in place of "Couldn't start this game" |
| Vita (Vita3K, our pin) | `--frontend sdl`, the AppImage's layout and its Vulkan loader, `config.yml` keys, among them `disable-motion`, off so a stand-in DualSense's tilt reaches the game, and its reading every controller's motion into the one Vita (`motion.cpp`), which is why only player 1's stand-in carries tilt (#669), the log lines that say an install worked, its storage folders, `user.xml` (`standalone_vita3k.cpp`) | `build_files/install-vita3k.sh` asserts the layout, the one loader and the build number. Not in `tools/check-applied.sh`: the rest is not checked; a renamed `disable-motion` leaves the Vita lying still |
| Remote Play (Sunshine, our build) | Our patch (`cores/sunshine-patches/`, #297, "Sunshine: drop our patch when a stable release has it, and Steam's stream composite when Sunshine reads the whole screen"); the lines `cores/build-sunshine.sh` fills in its spec; its controllers' names, its log lines (`sunshinelog.cpp`), `prep-cmd`, `/api/` and `sunshine_state.json` (`remoteplay.cpp`; the state file only until a release with LizardByte/Sunshine#5696, `remoteplay.h`), its shaders' folder | The patch and the spec lines are asserted (the build says when the patch is upstream). The rest is the list in `build_files/install-sunshine.sh`'s header, gone through on the reference console |

### Moved by terra at each image build, or on their own

| What | Leans on | Checked when it moves |
|---|---|---|
| The Steam tile's session | terra's `gamescope-session` and `gamescope-session-steam`: the files the tile runs, upstream's script launching the client through cardwire only when it exists, `CLIENTCMD`, `steamos-session-select` handing over to `/usr/libexec/os-session-select` | Asserted at every image build (`build_files/install-steam-session.sh`) |
| Steam never offering to format the console's drive | The session reading `/etc/gamescope-session-plus/sessions.d/steam` after its own, and Steam honouring `STEAM_ALLOW_DRIVE_ADOPT` and `STEAM_ALLOW_DRIVE_UNMOUNT` | Not checked |
| The hand-over to Steam and back | `cabinetos-session` (`steam_step`) repeats what upstream's `gamescope-session-plus` wrapper sets up before the session, and starts the session itself | Not checked against upstream's wrapper; the tile is a `tv:` check when the `steam` package moves (`ci/base-watch.txt`, Steam) |
| Steam's own files | Steam's `config/config.vdf` (Bluetooth on before its first start, `cabinetos-session`), both `libraryfolders.vdf` (`cabinetos-steam-libraries`), the `steamwebhelper` and `zenity` processes that say its first download ended (`cabinetos-steam-download`), the files it leaves outside its folder (`steam.cpp`, Remove) | Not checked, and cannot be ahead: Steam updates itself. A change shows in Steam |
| Achievements, and offline play's unlocks | rcheevos, our copy at a pinned commit (`frontend/third_party/rcheevos/VERSION`): its low-level `rc_api_*` beside its client, `rc_libretro`'s table of core names, and the form of its requests, which the offline layer reads and answers (`achievements.cpp`, `ra_list.cpp`); RetroAchievements' server answering `"Success":true`; and its MD5 (`src/rhash/md5.h`), which RomM's content hash is worked out with (`romm_snapshots.cpp`) | Not checked. The copy moves only when we move it; the server moves on its own |
| Saves made in other apps (#346, step 8) | RomM converting a stored save for an emulator through Sigil (`GET /api/saves/{id}/content` with `core`, `option` as key:value and `profile`), answering one file raw with its path in the emulator's save folder in `X-Save-Path` or several as a zip of those paths, a cartridge system's save as stored (`NATIVE_SAVE_PLATFORM_SLUGS`), 400 "can't be converted for a core" for a system it converts nothing for, and Sigil's refusals by their codes (`saves.cpp`, `romm_http.cpp`); Sigil's layout names and restore targets at the commit RomM 5.5.0-alpha.2 builds in, `85f88a85` (`savetable.cpp`, `saveForm`), the shapes of a Wii and a 3DS zip it reads (`dirsave.cpp`, `ZipShape`), Sigil reading a zip of Gambatte's `.srm` and `.rtc` and of Beetle Saturn's `.srm` and `.bcr` as saves from before it, each file by its name (step 11; `savepush.cpp`, gather), and RomM's `may_load` rule (`savepick::mayLoad`). The console converts nothing itself | `--save-pick-test` (each system's check of RomM's answer, the zip shapes). Each system asked both ways on the write-test RomM, 2026-11-19, and twelve games started live from other apps' saves on it: the closing notes of #722, step 8; the clock, the cartridge, PS Vita and Sega CD's shared memory on 2026-11-21, step 11's. A newer RomM's Sigil commit is read at its release |
| Save channels on RomM (#346) | RomM's channel and snapshot API as 5.5.0-alpha.2 has it, which RomM's documentation does not describe yet: `GET /api/channels` by `rom_file_id`, `GET /api/snapshots` by `channel_id` (a channel's history, branches with it, each state with its content hash), `POST /api/snapshots` (a `manifest` part, `save` and `state:<core>:<slot>` parts, and the save's picture as `save_screenshot`; or a save named by `copy_of`, a row of the person's that RomM copies in itself with its picture, leaving the row as it is, kept with the save's row when that row has none and drawn by the website on the channel's tile) and its answers (201, 200, 400 `missing`, 409 `branch` or `hardcore_downgrade`), the fields of a channel and a snapshot, `channel_file_id` on a game's own entry, the heartbeat's `SAVE_SYNC` block as the sign a server has channels, and the content hash a snapshot names each save and state by: the MD5 of its bytes, or of a zip's names and their files' MD5s (`romm_snapshots.cpp`, `romm_pairing.cpp`, `romm_library.cpp`). For the save a start continues from (`savepick.cpp`, `saves.cpp`): `GET /api/saves` listing a snapshot's save among a game's saves with its `channel_id`, where a save from before channels has none; a snapshot's `device` (the client that made it) and `held_by`; and the website's "Restore as current" making a new snapshot that carries the older one's save (`restoreManifest` in its `utils/snapshots.ts`), which is why the pill goes by the save's hash. For the snapshot a game's save goes up in (`savepush.cpp`): a push whose `expected_current_id` is not the channel's current being kept as a branch with the current untouched, the 409 naming both; a second push that names that branch as its `parent_snapshot_id` and no save carrying the branch's save and states with no bytes sent, which is the website's own restore; a `label` with no `channel_id` making a channel, and `channel_id` with a `label` making it again when it is gone; a push that names no states carrying its parent's; a core it names replacing that core's slots whole and leaving every other core's as they are; `GET /api/states` by `rom_id` listing every state of the person's on the game, in every snapshot and from before channels, with its `channel_id` and its bank's `core` but no content hash; a state named by its content hash taking the bytes RomM holds in any of the person's snapshots, and `missing` naming `state:<core>:<slot>` for one it holds in none (so a state from before channels, held by no snapshot, goes up as its bytes); RomM adding its own time to the name of every file it stores, ` [YYYY-MM-DD_HH-MM-SS-mmm]` before the extension, which the console takes off to find its own copy (`savepush.cpp`); a save row's `created_at` being when RomM first stored those bytes and staying so while snapshots carry it, where its `updated_at` moves with each (`savepick::dateSaves`); RomM refusing an ordinary save over a hardcore current only when the push would become the current; a current's `created_at` being when RomM took it, since a push carries no time of its own; a save row from before channels written again in place by a console on the old version (`overwrite=true`: its `updated_at` moves, its `created_at` stays) while the copy a channel holds stays as it was, which is how a start knows the row it brings in is still the one it took (`savepush::send`); and a zip's names read as UTF-8 when the zip marks them so and as CP437 when it does not, and a name holding a backslash refused (422): the console's zips mark a name only when it is UTF-8 (`dirsave.cpp`), and its hash reads an unmarked one as CP437 (`romm_snapshots.cpp`) | `--romm-snapshots-test` in CI, against answers recorded from RomM 5.5.0-alpha.2 and a zip hash computed by RomM's own function in its own container, `--save-pick-test` for the rule built on them and `--save-push-test` for a push settled against each answer. The saves list and a restore made the website's way were seen on the write-test RomM on 2026-11-18, and each answer to a push from a console the same day, a save file named in Shift-JIS stored and restored, and one whose name holds a 0x5C refused. States named in their slots, carried by a push that names none, another core's slot left as it was, the stored names' added time and a carried save's `created_at` were seen on the write-test RomM on 2026-11-19, and `copy_of` the same evening: a new row with the same content hash, the row copied untouched, the same copy again answered unchanged, a stale current answered with a branch, and the old row written again in place by an old-style upload with the channel's copy unchanged. A newer RomM is read against these by hand at its release, and the whole row against RomM's documentation once it covers channels |
