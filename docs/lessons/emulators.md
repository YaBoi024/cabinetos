# Lessons: emulators and cores

Read before working on a core, an emulator, input, saves or states.

## Before believing a core works

- **Build a core, then RUN it.** Five for five — and the fifth was the worst,
  because nothing was newly built at all: every one of the 223 arcade games
  turned out to be unable to start, and had been for a day. **A green build and
  a passing screenshot say nothing about whether a game runs.**

- **A dark first capture is usually a slow boot.** PlayStation needs about
  6000 frames to clear the Sony logo on this VM, Saturn 2600, Sega 32X 2000,
  Neo Geo Pocket 1200. **And a Saturn capture is not repeatable** — the same
  2000-frame run gave max=209 and then max=8.

- **A core will happily "load" something that is not a game.** Genesis Plus GX
  accepted an HTML error page, reported correct Master System geometry, ran,
  and drew black for three thousand frames.

- **A silent fallback is worse than a failure.** Flycast substitutes its own
  boot ROM when it cannot find the real one and says nothing at any log level.

- **A fact carried across is a fact nobody has checked.** Three of the eight
  rows in a save-path table taken from a working implementation were wrong on
  this console. All three failed silently. Two minutes of `find` after a launch
  caught all three.

- **`av_info` is a narrow probe.** Geometry, frame rate, sample rate.

- **An unanswered libretro core option is NOT the default.** The core skips the
  case and the C global keeps its zero value. **This is a demonstration, not an
  argument**: run `--core-options-off` and launch a PSP game and it ends at
  *"the core needs a render target this context cannot build"*.

- **A core that declares no options is the suspicious case, not the clean one.**
  FBNeo and MAME declare theirs per driver, so the table does not exist until a
  game is loaded.

- **Ask the CORE, never the platform**, whether an archive should be opened.

- **Never dispatch on a file extension.** Thirty-two files in the reference
  library have none. Sniff the magic bytes.

## The questions for every core

Every core meets the console at the same places: each person's and each
game's folders, saves on RomM, caches and states. Each new core has broken
one of them unnoticed: Azahar wrote every 3DS game after the first into the
first game's folder (#534, "Azahar patch: each game sets its own folders
(until Azahar fixes SetUserPath)"). Ask all of these for any core, emulator,
save or cache work, answer them in the report without being asked, and say
which were checked on the reference console and which come from reading the
code.

1. **Two games in a row**, with no restart, and with another core in
   between: does each game use its own folders? A core can stay in memory
   after `dlclose` when it has GNU unique symbols, so its statics survive.
2. **Two people:** switch person without a restart (`users.cpp`,
   `setCurrentUser`): each keeps their own saves, states and settings, and
   nothing is read from or uploaded to the other person's account.
3. **The save round trip:** on RomM under this game and this person after
   quitting; restored on the next start; restored on a console that never
   had it.
4. **Everything else it writes** (shader caches, NAND, settings, logs):
   where it lands, whether it survives a quit, a restart and an image
   update, per person or shared, bounded in size, never uploaded by mistake.
5. **States:** save, load, after quitting, after a restart, per person.
6. **The first start against later ones:** what is downloaded, unpacked or
   compiled again every time.
7. **No network, or the server down:** it starts, and the save uploads later.
8. **Quit paths:** quitting from the menu, power off, a crash: is the save
   written?
9. **Parity with the other cores:** controllers, picture levels, sound,
   RetroAchievements, the pause menu, time played.
10. **The rules:** the emulator reaches no network, never needs Steam, gets
    no fix for one game, and uses Vulkan (CONTRIBUTING.md, "The rules").

## Input

- **A GAME FACT FROM MEMORY IS A GUESS: CHECK REDUMP.** "Snatcher, two discs"
  sent MMagTech after a single-disc game (2026-10-10). libretro-database's
  `metadat/redump/*.dat` lists every disc of every set; grep it before
  suggesting a game, a disc count or a region.

- **`RETRO_DEVICE_INDEX_ANALOG_BUTTON` IS NOT A STICK.** It is libretro's third
  analogue index and its `id` is a joypad button id — L2 is 12, R2 is 13. Any
  code that tests for the LEFT index and treats "everything else" as the right
  stick answers "how far is the trigger pressed" with the right stick's Y axis.
  That is what this console did until 2026-09-19.

- **AND ANSWERING IT WRONGLY IS WORSE THAN NOT ANSWERING IT.** Flycast reads
  the analogue trigger first and falls back to the digital L2/R2 bit **only
  when that value is exactly zero**. Cabinet returned a plain 0 here and
  therefore worked by taking the fallback; a real pad's right stick rests a few
  hundred counts off centre, which is not zero, so the fallback never ran. On
  Dreamcast those triggers are the accelerator and the brake.

- **A BUTTON THAT DOES NOTHING IS USUALLY CORRECT.** A RetroPad has sixteen
  inputs and a real machine has fewer. The Dreamcast pad has no shoulder
  BUTTONS at all — its L and R are analogue triggers, so on a Switch Pro
  Controller the top shoulders are meant to be silent and ZL/ZR are the
  triggers. The console prints this per game now:
  `[input] port 0 does nothing in this game: ...`, which is the half that
  answers the question somebody actually asks.

- **ONE PAD SEATED TWICE NEEDED THE FIRST CONNECTION AFTER A BOOT (#318, "One
  controller seated twice: presses count double (the pause menu opens and
  closes)").**
  The kernel's `hid_have_special_driver` list makes only PS4 and PS5 pads
  wait for their own driver. An Xbox pad over Bluetooth meets `hid-generic`
  first, `hid_microsoft` loads and takes it over 50 to 80 ms later, and SDL
  seats it in that window through the plain joystick ("Xbox One Controller"
  in `[players]`, two kernel `input:` lines). When anything makes SDL look
  again, its Xbox HIDAPI driver seats the same pad as "Xbox One S
  Controller", and every press counts twice. PCSX2 is what made it look (its
  `SDLInputSource` starts although `InputSources/SDL` is false). Every later
  connection in the boot is fine, because the driver is loaded by then, so
  **a test from a console that has seen the pad once this boot proves
  nothing**: restart, check the driver is not in `lsmod`, then connect. A
  Remote Play pad loads `hid_microsoft` too. The fix loads the drivers at
  boot (`modules-load.d`).

- **Telling a core about SOME of its ports is the same as telling it about
  none.** Flycast returns early from `retro_set_controller_port_device` while
  any of its four ports is unset.

## Picture and rotation

- **THE CONSOLE RUNS ON X11, NOT WAYLAND.** gamescope embeds an Xwayland
  server and SDL picks the `x11` driver, so the GL context is GLX and **there
  is no EGL display in the process at all**. A picture handed over as an
  EGLImage fails with `EGL_NOT_INITIALIZED` on the television while working
  perfectly under `SDL_VIDEODRIVER=offscreen`, where SDL does use EGL. It cost
  a PlayStation 2 game that played with sound and a black screen.

- **THE COPY BETWEEN VULKAN AND GL COSTS TWELVE MICROSECONDS.** Everything else
  in that path is the frontend waiting for the EMULATOR to finish drawing,
  because the core's work is queued ahead of ours. Both obvious optimisations
  — an optimally-tiled destination through `VK_EXT_image_drm_format_modifier`,
  and an exported semaphore instead of the fence — were reasoned about, one was
  BUILT and measured, and neither is worth anything. The tables are in
  `vkbridge.cpp`'s `createShared`. **Do not rebuild either without a number that
  contradicts them.**

- **A core reports TWO geometries and neither is wrong.** MAME 2003-Plus
  declares 224x256 in `av_info` for Arkanoid — the picture as SHOWN, already
  turned — and hands back 256x224 from `video_refresh` every frame. A layout
  must use the second. The `[core] WxH` line at load prints the first.

- **THE SHAPE AT LOAD IS NOT THE SHAPE OF THE GAME.** Genesis Plus GX
  declares 256x192 and aspect 1.5238 at load, then asks for 320x224 and
  1.3061 with SET_GEOMETRY on its first frames, once the game picks its
  mode. Until #220 ("A core's mid-game picture shape change is ignored",
  2026-10-18) the console refused the ask and drew every 320-wide Genesis
  game 17% too wide (a 1646x1080 quad on a 1080 screen, measured headless
  on the A9: Comix Zone, Earthion). Nothing looked broken in a capture; it
  was found by reading the `[core] now` line the fix added. Dolphin asks
  the same way when a GameCube game turns widescreen on.

- **A TURNED BOARD'S DECLARED ASPECT IS ALREADY TURNED.** FBNeo says 0.75 for
  DoDonPachi while handing back 448x224. Inverting it turns the picture twice
  and stretches it, which Cabinet shipped once and wrote down.

- **A core calling `SET_ROTATION(0)` is ordinary, not a no-op to ignore.**
  Flycast does it explicitly, which silently overwrote a rotation forced in for
  a test. Anything per-game must be cleared in `loadGame`, not in `load`.

- **Only ARCADE cores ever rotate.** MMagTech, 2026-09-19: a console was built
  to put its picture on a television the right way up. It is what bounds the
  rule above to boards, where it is safe.

- **Rotation does not come from a MAME DAT and never did.** Checked in
  Cabinet's own tree because it was raised as a likely memory: the three
  MAME-derived JSON files it shipped hold `rotary`, `dial`, `trackball`,
  `pedals`, `lightgun` and `paddle` — control panels, not screens.

- **MEASURE THE GLITCH BEFORE BELIEVING THE ARITHMETIC.** #221 ("Built-in
  cores: no audio rate control, a frame repeats or drops every 10 to 17 s")
  predicted one
  dropped frame every 10 s on SNES from the 60.10 against 60.00 Hz beat. The
  TV showed 4 to 11 skipped and up to 9 repeated every 10 s, because the
  clock pacing ran 2 frames or 0 whenever the stopwatch wobbled near a
  frame boundary. The beat was the smallest cause.

- **A STATE SAVE CAN RUN THE GAME.** Mupen64Plus-Next's `retro_serialize`
  runs the machine to its next vertical interrupt before saving, so each of
  rewind's snapshots was an N64 frame nobody counted: its sound filled the
  speaker, which held the picture twice a second, and for three weeks that
  was put down to the core's timing (#254, "N64 makes 3.4% more sound per
  frame than it promises", 2026-10-18). Found only by counting `retro_run`
  calls against VIs inside a measuring build of the core. **When a core's
  numbers are off by a round fraction, look for what the console does on
  that period** (one frame in thirty is 3.3%; snapshots every 30 frames).

- **A CORE CAN ASK FOR ITS GL CONTEXT TWICE, AND THE SECOND ASK MUST KEEP
  THE FIRST.** melonDS calls SET_HW_RENDER in retro_load_game and again
  from its first retro_run. The host made a fresh desktop context on the
  second ask, mid-frame, which threw away every object melonDS had and left
  the UI's context current under it; the console crashed about 25 s into
  Mario Kart DS (#219, 2026-10-18, headless on the A9). RetroArch only
  stores a repeated ask. **Run a hardware core for a minute, not a few
  frames, before believing its context.**

- **melonDS'S OPENGL PICTURE IS NEVER SMALLER THAN 4x.** Whatever internal
  resolution it is asked for, it builds its output at 4x the DS's size or
  more (src/libretro/screenlayout.cpp), so 2x or 3x only draws the 3D
  smaller and stretches it. Found by reading the render target's size in
  the log (1024x1536 for "3x"), not the option's name.

- **SDL'S SCREEN RATE IS CLOSE, NOT EXACT.** It says 59.980 for a TV whose
  EDID says 60.000, and 59.91 for a mode that measures 59.95. Anything locked
  to the screen needs a rate control to take up the difference; never
  compute timing from that number alone.

## Cores, naming and coverage

- **`catalog::coverageFor` answers FOUR different questions.** No core exists, a
  core exists and Cabinet did not ship it, this console has not built it, and
  it is built and cannot be driven. Collapsing any two hides work.

- **The core-file naming rule now lives in THREE places** —
  `catalog::coreFileName`, `cores/build-core.sh` and
  `ci/stage-image-payload.sh` — and the comment is on all three. A manifest
  name already ending in `_libretro` does not get a second one. Getting it
  wrong cost every arcade game on this console for a day.

## Games on several discs (#162)

#162 is "Multi-disc games: Change disc in the pause menu, one save per game
(Saturn first)".

- **A CORE THE CONSOLE "UNLOADS" CAN STAY IN MEMORY, AND ITS STATIC STATE
  WITH IT.** Flycast's code was still mapped with the console on Home, and its
  static `disk_index` survived into the next game: `retro_unload_game` never
  resets it and `read_m3u` reads no line unless it is 0, so after Insert disc
  2 every later Dreamcast launch in that run was refused ("[BOOT] Unknown disk
  format": Flycast opened the `.m3u` itself as a disc). It also keeps the
  last start disc it was told. Found only because the rig played a second
  game in the same run (rule 3). The console now puts the drive back on the
  list's first entry before every unload, and states the start disc on every
  launch. Dolphin's port does the same kind of thing: its disc list grows by
  every game loaded and is never cleared (4 discs reported for a 2-disc
  game on the second launch).

- **Read the core's source before believing its disc index.** Dolphin's
  `retro_get_image_index` only moves when the frontend sets it; Dolphin's own
  AutoDiscChange never touches it, so the console cannot know which GameCube
  disc is in, and does not pretend to.

- **A DISC THE GAME IS NOT ASKING FOR CAN KILL THE WHOLE APP ON PS1.** Metal
  Gear Solid read disc 2 as if it were disc 1 (once mid-load, once when its
  attract demo started) and Lightrec segfaulted inside its own translation
  cache: the PC was in recompiled code, nothing of ours on the stack. On
  hardware the game would only hang. Swap at the game's own prompt, and test
  a swap at a quiet moment before believing a crash is ours.

- **"It drops to the BIOS" is the hardware, not a bug.** Opening the lid on
  Saturn or Dreamcast during a game that is not asking for a disc goes to the
  console's own menu, as the real console does; Saturn then waits on *Start
  Application* (Start boots the new disc; RetroPad A is Saturn's B, which
  does nothing there). Panzer Dragoon Saga asks for exactly this.

- **A dump can be the fault.** RomM's Night Trap boots to the Sega CD's CD
  player even from one file, and its sizes match no Redump Night Trap; RomM
  could not hash it either (no `ra_hash`). A single-disc game (Mad Dog
  McCree) booted the same way was the control. Check Redump before blaming
  the feature.
  And the file can be another console's game: RomM's PlayStation "Primal
  Rage (USA).chd" (#422, "Primal Rage (PS1) will not start", 2026-10-18)
  has a Mode 1 data track of 55,251 sectors, 129,950,352 bytes, exactly
  Redump's **Saturn** Primal Rage (USA) Track 01 (the PlayStation one is
  222,614,448 bytes and Mode 2, as every PlayStation disc is), and the
  console's Saturn core plays it. PCSX-ReARMed's "unsupported/invalid CD
  image" was right. Read the CHD's track list (its CHT2 metadata) and the
  track sizes against Redump before reading the core.

## PS2 and GameCube

- **`dolphin_renderer` IS NOT AN API SELECTOR.** It takes "Hardware" and
  nothing else in a release build; setting it to "Vulkan" silently turns
  hardware rendering OFF. The API comes from what the frontend advertises in
  `GET_PREFERRED_HW_RENDER`.

- **DOLPHIN DECLARES ZERO CORE OPTIONS UNTIL A GAME IS LOADED**, so
  `--core-options` reports none for it. That is the suspicious case, not the
  clean one — the same shape as FBNeo and MAME.

- **DOLPHIN'S USER DIRECTORY IS UNDER THE SAVE DIRECTORY, NOT THE SYSTEM ONE.**
  `Boot.cpp` prefers `<saveDir>/User` when the frontend gives it a save
  directory, and only falls back to `<system>/dolphin-emu/User`. An hour went
  on a `Dolphin.ini` written in the second place and read from the first.

- **`pcsx2_shared_memory_cards` DEFAULTS TO ON** and puts every game's save in
  one `Mcd001.ps2` in the system directory — a card that belongs to no rom and
  therefore cannot be synced at all. **`pcsx2_analog_mode1` DEFAULTS TO OFF**,
  which is the DualShock's analogue mode disabled and reads as dead sticks.
  That was the libretro core. The console's own PCSX2 host reads no core
  options: it is handed one card per game and sets the analogue mode itself
  (`frontend/ps2/CabinetPS2Host.cpp`), so the overrides went (#492, 2026-10-19).

- **ROMM MATCHES A SAVE ROW BY FILENAME ALONE, AND THE MAC'S SPELLING IS
  DIFFERENT FROM THIS CONSOLE'S.** `cabinet-604.ps2` against
  `Burnout 3 Takedown (Cabinet).srm`. Four separate things had to agree before
  one card could travel — the format, the name, the region extension and the
  emulator tag — and three of them were wrong. See open question 12b.

- **DOLPHIN PUTS THE REGION *AND THE CARD SIZE* IN THE FILENAME.** Ask for
  `card.raw` and it opens `card.USA.raw`, or `card.USA.251.raw` for a 2 MB
  card. For three weeks the console restored GameCube cards to `card.raw`,
  which Dolphin never opened, and the capture, which tried the predicted name
  first, then read that same restored copy back after every game (#344,
  "GameCube: a save restored from RomM never reaches Dolphin (card.raw vs
  card.USA.raw)").
  Nothing was lost only because no new save had been made since a restore.
  **A restore must write the name the emulator opens, and a capture must read
  the file the emulator wrote, never the one we put down.** GameCube now uses
  Dolphin's default `.gci` folder, which has no such name.

- **DOLPHIN REFUSES `MemoryCardSize` FROM `Dolphin.ini`**
  (`ConfigLoaders/BaseConfigLoader.cpp`, `s_setting_disallowed`, since
  2022). The `MemoryCardSize = 2` written there for weeks did nothing; every
  card was 2043 blocks. **A setting written is not a setting applied: read
  the emulator's loader before trusting a key.**

- **A FRESHNESS RULE IS NOT OPTIONAL FOR A PLATFORM WHOSE CORE CREATES ITS OWN
  CARD.** Six of the seven PS2 and GameCube rows on the reference server held
  nothing: three PS2 cards were 8,650,752 bytes of `0xFF` with no format header
  at all, and three GameCube cards had nothing in either copy of their
  directory. Deleted 2026-09-20 with MMagTech's say-so, each re-verified empty
  immediately beforehand.

- **VSYNC OFF UNDER GAMESCOPE IS NOT "FASTER", IT IS A ONE-PICTURE
  MAILBOX.** gamescope never tears, so PCSX2 with vsync off got mailbox
  presentation ("Immediate not supported ... using mailbox" in its log), and
  at 59.94 against the TV's 60 a newer picture replaced a waiting one: 56 of
  60 reached the screen (#226, "PS2: show the picture without copying every
  frame off the graphics chip"). Vsync on alone is mailbox again in PCSX2
  unless it times itself to the screen; `DisableMailboxPresentation` is what
  makes it a queue. **Read which present mode the emulator actually got.**

- **REMOVING A COPY DOES NOT PROVE THE COPY WAS THE LIMIT.** Quality was held
  at 3x "until the copy goes"; with the copy gone, 5x still ran at 68 to 77%
  speed, because the GPU was at 99% (`/sys/class/drm/card1/device/
  gpu_busy_percent`). Read the GPU's load beside the frame times before
  blaming the path.

- **frames.py MEASURES THE WINDOW THAT PRESENTS, NOT THE GAME.** (frames.py
  is a frame-time script kept on the reference console, not in this
  repository; it reads gamescope's frame times.) On the copy
  path it timed the console redrawing at 60 whether PCSX2 had a new frame or
  not. With PCSX2 presenting itself it times PCSX2's own pictures, which is
  the honest number; the console's `[ps2] N fps, speed N%` line every ten
  seconds is PCSX2's own reading.

## Building a whole emulator (PCSX2)

- **A LIBRARY THAT BUILDS TELLS YOU ALMOST NOTHING. A LIBRARY THAT LINKS TELLS
  YOU EVERYTHING.** `libpcsx2.a` built on the first real attempt and that result
  was nearly worthless on its own: a static archive resolves no symbols, so it
  cannot report a single missing host function. Cabinet's own comment on
  `CabinetPS2Smoke.cpp` says exactly this and it is why that file exists.

- **THE CHEAPEST LINK TEST WAS ALREADY IN THE TREE.** `pcsx2-gsrunner` is
  upstream's own Qt-free frontend, one file, 1332 lines, implementing the whole
  `Host` contract. Building it proved linkability in 2.2 seconds and needed no
  code from us. **Look for upstream's second frontend before writing a smoke
  test** — PPSSPP, Dolphin and RPCS3 all have one too.

- **A SHARED OBJECT LINKS HAPPILY WITH UNDEFINED SYMBOLS**, then fails at
  `dlopen` naming only the FIRST one. That is the worst possible instrument for
  sizing a job: it says "you are missing `g_host_hotkeys`" whether you are
  missing one symbol or two hundred. **`-Wl,-z,defs` makes the linker refuse and
  name them all**, which turned "some unknown amount of host layer" into 57.

- **AND THE FIRST ONE IT NAMES IS A VARIABLE, NOT A FUNCTION.**
  `g_host_hotkeys` is a global the frontend must define. Anybody grepping the
  `Host::` namespace for it will not find it.

- **`find_package(X11)` SUCCEEDS WITHOUT `libXi-devel`** and then the build
  fails at CMake GENERATE time, after "Configuring done", on a missing
  `X11::Xi` target. It reads like a CMake bug. It is a missing package.

- **FEDORA SUPPLIES WHAT CATALYST COULD NOT.** Cabinet hand-cross-compiled ten
  dependencies with pinned tarballs and SHA sums; Fedora 44 met every version
  constraint PCSX2 states, with one exception (`libbacktrace`, which is an
  option). **Check the distribution before believing a port is hard** — the
  difficulty recorded in a reference implementation is usually the reference
  platform's, not the problem's.

- **PCSX2 REFUSES TO START WITHOUT ITS `bin/resources` FOLDER** — game database,
  fonts, GS shaders. It does not degrade, it says "Resources directory is
  missing" and stops. The same shape as PPSSPP's 13 MB of system files.

- **UPSTREAM'S SECOND FRONTEND IS A LINK TEST, NOT A SHORTCUT TO A RUNNING
  GAME.** `pcsx2-gsrunner` looks like a headless PCSX2 and is not one: it
  replays GS dumps and refuses anything else at
  `VMManager::IsGSDumpFileName`. It proved the library links and it is the best
  `Host` reference there is; it will not boot a disc. **Check what upstream's
  harness is FOR before planning a measurement around it.**

- **A TOOL THAT PRINTS FORTY LINES AND THEN EXITS 1 HAS NOT NECESSARILY GOT
  FAR.** gsrunner's `LoadStartupSettings()` resets the console log level from
  empty settings at the end of config init, so every `Console.Error` after that
  point reaches nobody — including the one naming the actual problem. The
  directory listing that precedes it is the last thing you see and it looks
  like progress. **When a program goes quiet at exactly the same place every
  time, suspect the logger before the logic.**

- **A SEPARATE BUILDER CONTAINER WAS THE RIGHT CALL.** PCSX2 needs about thirty
  packages the frontend does not. Putting them in `frontend/Containerfile` would
  have slowed every one of the 22 core builds to serve one thing.

- **PCSX2'S DEFAULT BUILD IS FOR THE BUILD MACHINE'S PROCESSOR ONLY**
  (`-march=native` unless `DISABLE_ADVANCE_SIMD=ON`). Every image until
  2026-10-04 carried a PCSX2 built that way on a CI runner, and played only
  because the runners happened to suit the A9. The rebuild for #226 landed
  on an Intel runner and used `vmovw`, an AVX-512 FP16 instruction the A9's
  Zen 5 lacks: SIGILL in `ReverbDownsample_avx` the moment a game made a
  sound. build-pcsx2.sh now builds multi-ISA, as PCSX2's own releases do,
  and refuses `-march=native`. **Check the release build flags of anything
  upstream ships, not only its default build.**

- **TEST THE MODULE THE IMAGE WILL CARRY, NOT THE ONE BUILT BY HAND.** A
  PS2 library built on the A9 always suits the A9, so every test of it
  passed while CI's copy crashed. Download the CI artifact, or upgrade, and
  launch a game from that before asking anyone to look.

## PS3 (parked)

- **RPCS3 will not install a PKG or firmware unless you say `--headless`**, and
  it **exits 134 after a successful install** — SIGABRT in a static destructor,
  *after* logging `Successfully installed`. **Read the log line, not the exit
  code.**

- **An interrupted PS3 install leaves the partial tree behind**, and nothing
  cleans it up. Delete the title directory before retrying.

- **`timeout` does not kill RPCS3 under flatpak.** It signals the `flatpak run`
  wrapper. Follow it with `pkill -x rpcs3` — and mind that a `pkill` aimed at a
  stuck process will also kill an install you started in the same breath.

- **Flathub stalls from this network, silently.** A retry loop fixes it, because
  ostree resumes:
  `for i in $(seq 1 30); do timeout 240 flatpak install -y --user ... && break; done`.

## Xbox (xemu)

- **xemu's menu reads EVERY gamepad SDL shows it, bound to a port or not**
  (ui/xui/input-manager.cc, "Combine all controller states"). Binding the
  ports to the virtual controllers was not enough: a real pad's Guide, or
  Back and Start together, opened xemu's menu over FlatOut. The fix is SDL's
  own hint, `SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT` with the virtual pads'
  ID, so xemu never sees a real pad. **Check what an emulator's own UI reads,
  not only what its game ports are bound to.**

- **A keyboard reaches xemu's hotkeys.** The backtick opens its debug
  monitor over the game. Useful for tests (eject the disc and `system_reset`
  to reach the dashboard); a known gap for anyone with a keyboard plugged in.

- **xemu's window title never names the game** (ui/xemu.c), so Eden's
  title rule cannot tell a game from the dashboard. The Xbox's running
  program is read over QMP instead: `memsave` of 0x10000, the XBE header,
  whose certificate holds the title ID.

- **A RESTORE CAN UNDO A SAVE THAT IS STILL ON ITS WAY UP.** Found through
  Xbox, true of every folder save: a save queued after a power cut, the game
  started again at once, and the server's older zip came down over it. The
  restore now skips while `cache::isPending` says the console owes that zip.
  **Test a recovery with an immediate relaunch, not only a clean one.**


## Xbox 360 (Xenia Edge)

- **SDL CAN SWALLOW SIGTERM FOR A PROGRAM THAT NEVER ASKED.** Edge ignored
  SIGTERM on the A9, which looked like Edge's own choice. It is SDL's:
  initialising SDL's events installs a handler that turns SIGTERM into a quit
  event, and Edge only pumps SDL for controllers and never reads it. Check
  what a library installs before concluding a program handles a signal.

- **A COMPATIBILITY LABEL IS A FACT CARRIED ACROSS.** "XNA games never run"
  came from the original Xenia's WONTFIX labels; Edge had added what they
  need ten days earlier, and both played. Launch it, or read the pinned
  emulator's own log, before saying never.

- **A SAVE UPLOADED IN PART REPLACES THE WHOLE ON THE SERVER.** Folder saves
  sent only the folders touched that session, a rule from when games shared
  one save folder; RomM overwrites by filename, so the server's copy lost the
  rest. Found by downloading RomM's zip and listing it, not by reading the
  upload log, which said "uploaded". **Check what the server holds.**

- **A GAME CAN BIND ITS SAVE TO THE PROFILE ID.** Forza Horizon 2 called its
  save "tampered" under any other ID. One ID on every console, for good, as
  the Xbox EEPROM is.

## Wii (Dolphin)

- **A FOLDER COPIED BY HAND ONTO THE REFERENCE CONSOLE HIDES ITS ABSENCE FROM
  THE IMAGE.** Dolphin's `Sys` (its 1,875 per-game settings files and the
  Wii's `shared2` files) sat in the A9's `bios/` from 2026-09-20 and was in no
  image. A game boots without it, so nothing failed; it only loses Dolphin's
  own fixes. Found by setting the folder aside and reading the core's log.
  **For any emulator, ask what the image carries, not what the A9 has.**

- **DOLPHIN CHOOSES DISC OR WAD BY THE FILE'S EXTENSION** (`Core/Boot/Boot.cpp`),
  against this project's rule. The console hands it a link named for what the
  bytes are when the name does not say.

- **GameTDB FILES FAN MODS UNDER THE REAL GAME'S CODE.** Dozens of Mario Kart
  Wii mods are RMCP02 to RMCPYP, some with no Classic Controller. A lookup by
  the first four letters must leave mods and homebrew out.

- **RomM'S `title_id` IS RELIABLE FOR WII AND NOT ALWAYS THERE.** The code is a
  fixed spot near the start of the file, no keys, and all 33 on the reference
  server matched the files. But RomM reads it only from 5.3, only in a scan
  that reads files, and not from WIA, CISO or GCZ. The console reads the file
  itself when RomM has none.

- **A HEADLESS LAUNCH WITH THE REAL ACCOUNT IS PLAY HISTORY ON RomM.** A test
  launch of Geometry Wars put it first on Home's Recent shelf. Say so, or use
  a game nobody minds seeing there.

- **DOLPHIN'S DEFAULTS WRITE WHERE NOTHING READS.** `dolphin_cheats_import`
  wrote RetroArch `.cht` files beside the system directory on every GameCube
  launch. Read every option's default from the source, not only the ones
  that sound like picture settings.

- **A COPY'S SPEAKER CAN WORK AND STILL NOT WORK IN GAMES.** The TechKen
  Remotes played test tones set up as WiiBrew documents, and froze within two
  seconds of the setup Wii games actually send (#276, "Wii: no sound from the
  Remote's speaker"). Test with the game's own
  bytes, read off `btmon`, not with the documented ones. And Dolphin's
  `WiimoteEnableSpeaker` is off by default, so no Remote speaker gets sound in
  RetroArch or Batocera either: before chasing a missing sound, read which
  setting turns it into something else (`WiimoteReal.cpp` makes it rumble).

- **A WAD BOOTED THROUGH THE CORE IS INSTALLED "TEMPORARY".** Dolphin puts
  it in the NAND before starting it and marks it in SYSCONF `IPL.TID`; the
  next WAD booted in the SAME NAND deletes it (`WiiUtils.cpp`). Harmless here,
  where every entry has its own NAND; fatal to any plan that boots several
  WADs into one NAND to install them (#275, "Wii: the Wii Menu, and Miis
  shared across games").

- **A `--core ... --rom ...` RUN SHARES ONE SCRATCH NAND** across
  every file run with that core (`users/0 - local/saves/local/0/<core>`). It
  is not empty just because the folder above it looks empty; list it before
  reading a test's result off it.

- **THE WII'S HOME MENU SAVES ITS SETTINGS WHEN IT CLOSES.** Rumble turned
  off there and the game exited with the menu still open: `BT.MOT` stayed 1
  on disk, and the console rightly kept rumble on. Closed with its Close
  button, the file had 0 within the second (#274, "Wii: keep the HOME Menu's
  rumble and speaker volume across games"). Before calling a guest
  setting unsaved, watch the file while the game runs (a half-second poll of
  its mtime and value); the shutdown path was not the cause.

- **A FRESH REMOTE'S MII AREA IS NOT EMPTY, IT IS UNFORMATTED.** The Mii
  Channel read 0xFF there and offered Format, as it would for a new Remote;
  format, then send. And Wii Sports refuses to save progress for a Mii taken
  from a Remote: a visiting Mii on a Wii too, not a fault (#275).

- **A LIST KEPT FOR REPLAY MUST HOLD ONLY WHAT NEEDS REPLAYING.** The bridge
  kept the speaker's per-sound commands as Remote setup, they filled its list,
  and the motion setup fell off the end. Nothing failed until a Remote dropped
  in a game.

## Wii U (Cemu)

- **CEMU PLAYS NO SOUND WHEN ITS OUTPUT DEVICE IS EMPTY, AND SAYS NOTHING.**
  `Audio/TVDevice` empty means no TV audio device is created at all
  (`IAudioAPI::CreateDeviceFromConfig`); the log still says "Cubeb:
  available". Found on the TV, confirmed by PipeWire showing no Cemu stream.
  `default` is Cemu's own "Default Device" and follows the system's output.
  **When an emulator's settings file is written from scratch, read what every
  missing key does, not only the ones being set.**

