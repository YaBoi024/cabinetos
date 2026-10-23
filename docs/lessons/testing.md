# Lessons: testing on the A9 and the TV

Read before judging anything, testing on the A9, or driving the TV.

## THE SILENT DAY, 2026-10-12: check what a person notices, not what you changed

The volume keys (#351, "Steam Deck: the volume rocker does nothing, and
there's no way to change the volume") were tested in every kind of game by pressing a key
and reading the volume back, and every reading was right. The console made
**no sound at all** from the first test build onward, menus and games, and it
was released that way. MMagTech found it at the TV: "I don't hear anything,
period."

- **The fault:** the volume work started just before the app opened its
  sound. The app's stream stayed connected at full volume and was never fed
  (pw-top: about 47 missed deliveries a second; the app's sound thread had
  no CPU time). Starting the volume work after the sound is open fixed it
  (#367, "Sound works again: the volume keys start after the sound is
  open"). The exact mechanism was not pinned down.
- **The console said so, and nobody read it.** The `[pace]` line in the very
  first test game read `sound +0.00%, sound per game frame +0.00%` (silent),
  in the same log the volume lines were being read from. Until #398's fix
  for the counters (2026-10-17), every game's first `[pace]` line read +0.00%
  per game frame whatever it played, so on a first line that figure proved
  nothing; since then a core that makes no sound reads -100.00% there.
- **The tests measured the change, not the console.** Anything that runs at
  every start (a thread, a reset, a service) touches everything: check sound,
  picture and a pad, not only the feature.
- **"Judged in the loop, no checks needed" was wrong here,** because nothing
  in the loop had listened. A change to start-up gets a smoke check on the
  testing image: MMagTech hears a menu click and a game.
- **Telling him it would not be a problem, from reasoning, is the other half
  of the mistake.** Say what was measured.

**Now built in:** `tools/ui-loop.sh` checks after every deploy that the app's
sound stream is fed (pw-top's ERR column; more than 20 missed in 4 s prints
SOUND IS NOT BEING FED). On the broken build it would have read about 190.
To check by hand: `pw-top -b -n 2 | grep cabinetos-frontend` twice, a few
seconds apart; ERR must not climb. A beep straight to the TV
(`pw-play` of a short WAV) tells the TV's half from the app's.

## A GAME OLDER THAN THE LOG WINDOW, 2026-10-12: ask "is a game running", not "was one started"

Before a stretch of A9 builds, headless runs and a loop deploy (#364, "Game
manuals: pull them from RomM and read them from the Home menu in a game"), the
journal's last 30 minutes were searched for `[launch]` lines, none were
found, and the console was called idle. MMagTech had started Burnout 3 41
minutes earlier and was playing it. The builds made his sound stutter, and
the loop deploy restarted the session and ended the game.

- **A deploy over a running game loses its quit.** The card Burnout saved
  in-game at 11:14 was on the console but never uploaded, because the upload
  happens when a game is left. At the next start RomM's older card (two days
  old) would have been put over it: the launch takes RomM's newest save
  unless the console's is marked as still owed (`cache::isPending`). It was
  saved by uploading the console's card to RomM on his go.
- **Check for a game that is running now:** the newest `[pace]` or
  `[ps2] .. fps` lines within the last minute, or the cores mapped into the
  frontend (`grep _libretro.so\|cabinetos-ps2.so /proc/<pid>/maps`; a core
  stays mapped after a game is left, so read the log as well). `pgrep -f`
  with the name in the ssh command matches the command itself.
- **Heavy work on the A9 is felt in a game:** a `-j24` build or a 4K headless
  run stutters the sound of whatever is playing. Ask in chat first if there
  is any doubt.

## The thirty-second loop on the TV


**`tools/ui-loop.sh` is the fast loop and I did not find it for most of a
session.** MMagTech, 2026-09-22: *"i need a way to visualize ui additions and
changes or test features or bugs on the a9 without waiting 50 minutes."* That
tool already did it, and an evening of llvmpipe screenshots was spent because
nobody had written it down anywhere a fresh assistant would look.

```
tools/ui-loop.sh                              Home, on the television, picture back
tools/ui-loop.sh --args "--screen accounts"   any screen the frontend can open
tools/ui-loop.sh --game 305 --menu            a game, with the pause menu over it
tools/ui-loop.sh --args "--glow strong" --no-build    tune a number, no rebuild
tools/ui-loop.sh --restore                    put the console back on the image
```

**About 28 seconds**, on the A9's own Radeon, under real gamescope, at
3840x2160. The capture is the frontend's own `SIGUSR1` rather than
`gamescopectl`, which cannot see overlay planes.

**IT BUILDS ON THE CONSOLE NOW, 2026-09-22.** The A9 has 24 cores and 26 GB
against the VM's 5 and 3, and a FULL clean frontend build there takes **6.5
seconds** — less than an incremental one on the VM. The binary never crosses a
machine. `--via-vm` keeps the old route for when somebody is watching
television.

**THE DROP-IN IS TRANSIENT.** It lives in `/run/systemd/system/`, so a
forgotten `--restore` is undone by the next reboot rather than leaving the
reference console on a hand-built binary for ever — which is the trap that was
cleaned up on 2026-09-21.

### WHAT THE LOOP IS FOR, AND WHAT IT IS NOT

**Is it the frontend? Thirty seconds. Is it the operating system? Build an
image.** Cores in `/usr/lib/cabinetos/cores`, systemd units, `tmpfiles.d`, the
session script, base-image changes, first run on a virgin machine and the
installer only exist in an image. Everything in `frontend/src` does not.

A new core is a file copy: the loop launches with `--core-dir` pointed at a
scratch folder of cores on the reference console (`tools/ui-loop.sh` names
it), so building a `.so` and dropping it there makes it playable on the
television without an image.

### AND THE RULE THAT MAKES THE FAST PATH SAFE

**Do not conclude from something CI could not reproduce.** That is narrower
than "be careful with hand-built binaries", and the difference matters —
MMagTech made the point and the record backs it.

**Seven of the lessons below are about judging in an environment that was not
the real one** — llvmpipe, offscreen, no session, no gamescope — including one
marked *"AND THIS IS THE SECOND TIME"*. **Not one is about a hand-built
binary.** The loop removes that whole class, so using it more is the
correction, not a new risk.

The one case that did burn this project — an unshippable PlayStation 2 core
sitting in a home directory, which a session concluded meant PS2 was solved —
happened on the A9, with a GPU, and ran perfectly. What was wrong was that its
source repository does not exist, so **CI could never build it.** A frontend
compiled from `frontend/src` is the same source CI compiles; that is the line.

**And nothing in this loop can reach an end user.** Checked 2026-09-22: no
workflow, no script in `ci/` and nothing in `build_files/` references either
machine, `scp`, `rsync` or `ssh`. The image is built by GitHub's runners from
the repository alone.

## Judging what you see

- **Judge nothing visual on the VM.** Software rendering on llvmpipe. And it is
  not only motion: a television's overscan eats more vertical room than a
  framebuffer capture shows. **Vertical fit cannot be judged here either.**

- **Read the pixels before believing the picture.** PPSSPP's first capture was
  not blank — it was a plausible, nearly-black rendering with faintly legible
  text. The maximum pixel in the whole 1920x1080 frame was RGB **(4,4,4)**.

- **Every screen photographs itself, headless.** `SDL_VIDEODRIVER=offscreen`
  needs no compositor, no session and no controller:
  ```
  SDL_VIDEODRIVER=offscreen ./build/cabinetos-frontend --romm <server address> \
    --screen library --screenshot /tmp/x.bmp --render-size 1920x1080 --frames 60
  ```
  `--screen` opens by walking the route a person walks, so a capture cannot show
  a state the product cannot reach. `--storage`, `--download` and `--unkeep` do
  the same for the things with no picture.

- **`--frames N` only ends the run when there is a `--screenshot` to take.**
  Without one the loop never exits and the command sits there at full CPU.

- **`--launch-after` IS WALL-CLOCK SECONDS AND THE UI LOOP IS NOT PACED**, so
  on the A9's Radeon a run of 1600 frames goes by in under a second and the
  launch never fires at all — no error, no `[launch]` line, just a screenshot
  of Home. It worked on the VM the whole time because llvmpipe is slow enough
  to take longer than a second. **Use `--launch-after 0`.** Twenty minutes went
  on this, chasing a difference between two binaries that did not exist.

- **TWO TEST HOOKS STOP SHORT OF FIRST RUN, OR GO STRAIGHT TO THE SERVER.**
  Found by the faults step's overnight checks, 2026-10-18 (#488). `--nav`
  walks once, just before the frame loop; first run runs its own loops
  before that, so a walk never reaches its screens. And `--setup` writes nothing,
  but its pairing step asks the real RomM for a device code, so `--setup
  --setup-step pair` reaches the server at once: run it through the read-only
  relay, which refuses that request, unless the code is what is being checked.
  Until #488 a `--nav` walk without `--screenshot` also pressed nothing at
  all: the start-up screen was still down over Home and every press was
  dropped. A walk now skips that lift, as a capture always did.

- **A `--frames` CAPTURE OF A LAUNCH IS TAKEN BEFORE THE GAME HAS LOADED.**
  `--frames` counts the UI's frames, thousands a second headless, and the
  capture ends the run, so `--launch <id> --launch-after 0 --frames 600
  --screenshot` photographs the curtain and kills the game before `[core]
  loaded` (2026-10-15, move 2: a whole first run of launch tests proved
  nothing). Take the game's frame with the disc rig and a time limit:
  `--disc-script "wait 600; shot X; ui X"` under `timeout -s KILL 60`; `wait`
  counts the game's own frames. A PS1 game is still black at 600 game frames
  (the console's start); 3,000 shows its opening.

- **The same capture command is worth running ON THE A9**, with
  `SDL_VIDEODRIVER=offscreen` and `--render-size 3840x2160`. It uses the real
  Radeon, needs no compositor, and does not disturb the running session — so a
  4K frame off the reference GPU costs one command and no downtime.

- **`--storage-root <path>` puts a whole console somewhere else**, which is how
  the two-disk and the out-of-space tests were run without disturbing the real
  tree.

- **`$CABINETOS_ROMM` works everywhere `--romm` does**, which is often shorter.

- **To watch a real game, launch it**: `--launch <romId> --launch-after 1
  --frames N`. A PSP game needs about 2500 drawn frames to reach its attract
  demo on this VM; Dreamcast about 1400. Add `--overlay-exit` to make it quit
  back to Home by itself, which is the only way to exercise the unload path
  without a controller.

- **Stop the session before building on the VM.** The frontend runs at 300% CPU
  under llvmpipe and it is four cores. Or skip it and use the offscreen driver.

- **AN OFFSCREEN CAPTURE DOES NOT PROVE A WINDOW EVER GETS A FRAME.**
  `Renderer::beginFrame` binds an offscreen SCENE target so panels can blur what
  is behind them, and **`presentScene()` is what puts it on the real
  framebuffer**. Miss that call and the loop runs perfectly at sixty frames a
  second presenting nothing — while every `--render-size` capture comes out
  correct, because `saveFrame` reads the offscreen target directly. The
  television showed white, gamescope's own screenshot came back entirely black,
  the process sat at 5% of a core, and nothing logged an error. **Anything that
  draws a screen must call `presentScene()`, and anything drawn after it lands
  on top of the scene rather than inside it.**

- **AN OFFSCREEN CAPTURE DOES NOT PROVE THE SESSION WORKS, AND THIS IS THE
  SECOND TIME.** The existing note about `presentScene()` is the same lesson
  one layer down. **Anything touching the display path has to be run under
  gamescope before it is believed** — put `--launch <id> --launch-after 0` in
  the session drop-in, which is how both of these were finally caught.

- **A SETUP SCREEN'S LOOP MUST BE PACED, AND `--frames` DEPENDS ON IT.** A page
  of static text left unpaced runs at thousands of frames a second on the A9,
  and four hundred frames went by before the server had answered — so the
  capture of the pairing screen came out with no code on it. **The same trap
  `--launch-after` fell into, one screen along.**

## The A9 and the TV

- **DO NOT DIAGNOSE HARDWARE FROM A PHOTOGRAPH.** Two theories were built and
  discarded here — one from a filename nobody checked, one from a digit
  misread off a picture of a rotated monitor. Get a shell and read `dmesg`.

- **`journalctl -u cabinetos-session` shows almost nothing.** The script's own
  output carries the syslog identifier, so the unit filter returns only
  systemd's start/stop lines. Use **`journalctl -t cabinetos-session`**. And
  the clock jumps when NTP syncs after an install, so `--since` lies on the
  first boot; use `-n`.

- **gamescope does not pick the display's mode on its own.** With no
  `--output-width`/`--output-height` at all it still chose 1920x1080 on a
  3840x2160 panel. If you want native, read the connector and pass it.

- **A 4K panel is the default assumption now, not a possibility.** PROJECT.md
  always said most sets are 4K; the first one plugged in was.


**SSH TO THE A9 IS PORT 2222 (Developer access; port 22 is File access,
SFTP only), and sudo's password is in `/var/lib/cabinetos-files/password`**,
not `cabinet`: `ssh -p 2222 cabinet@<console address>`.

**The A9 can be pressed from the Mac, so MMagTech can watch while the
assistant drives.** `cabinet` can write `/dev/uinput` and the image ships
`ydotool`. Start the daemon once per boot:
`nohup ydotoold --socket-path=/tmp/ydotool.sock --socket-own=1000:1000 &`,
then `YDOTOOL_SOCKET=/tmp/ydotool.sock ydotool key 108:1 108:0` (Down; Up
103, Left 105, Right 106, Return 28, Escape 1, Backspace 14) or
`ydotool type '<server address>'`. The frontend takes a keyboard everywhere.

- **The frontend's SIGUSR1 capture only works in the main loop, over a
  picture it draws.** First run and the startup screen have loops of their
  own, and during a PS2 game or one in an emulator of its own the picture is
  another window's (the journal says `[capture] not taken`): use `gamescopectl
  screenshot` with `XDG_RUNTIME_DIR=/run/user/1000` and
  `GAMESCOPE_WAYLAND_DISPLAY` set to the NEWEST `gamescope-N` there; a
  session restart leaves the old socket behind and gamescopectl fails on it.

- **CONNECTING MOONLIGHT CAN PRESS PLAY (#411, "Connecting Moonlight can
  start the game Home has focused").** 2026-10-15, 5:38 PM: two
  seconds after the stream connected, as Sunshine set up its virtual pad
  again ("ControllerNumber already allocated"), two presses reached Home 0.2 s
  apart and started the focused game twice; the stream then dropped and the
  game ran with nobody there. Before testing over Moonlight, know what Home
  has focused, and read the journal for a `[launch]` nobody asked for.

- **NEVER SEND A "WAKE" PRESS WITHOUT A CAPTURE FIRST.** Right after a restart
  the screen is already awake, so a Return meant to wake it pressed Resume on
  Home and started Burnout 3 (2026-10-10). Capture, read where focus is, then
  press; and the keyboard cannot leave a PS2 game, so MMagTech had to exit it.

- **After five idle minutes the first press only wakes the screen,** and
  every later press lands one step off. That opened the Wi-Fi panel instead
  of Sign out once. Capture before any press that destroys something.

- **A restart in place keeps the loop's `--args`**, so a test deploy with
  `--screen settings` comes back on Settings, not Home.

- **`gamescopectl` IS NOT ALWAYS ON `gamescope-1`.** The socket number is
  whichever the current instance took, and it changes when the session
  restarts. List `$XDG_RUNTIME_DIR` and use the one whose mtime matches the
  running gamescope; stale sockets from earlier instances sit there looking
  identical.

- **`gamescopectl <convar>` WITH NO VALUE DOES NOT READ IT: IT SETS IT TO
  0.** gamescopectl sends the name and an empty value, and gamescope
  parses the empty value as false or 0 (gamescope 3.16.31,
  `gamescopectl.cpp` and `convar.h`). 2026-10-19 (#253, "4K: the console
  misses a screen refresh 1 to 5 times every 10 s (gamescope)"): a "read
  back" after `vblank_debug 1` switched it off again, and its log stayed
  empty. Check a switch by what it does (vblank_debug's lines in the
  journal), never by asking for it.

- **`Environment=` IN A SYSTEMD DROP-IN SPLITS ON WHITESPACE.** An unquoted
  `CABINETOS_APP=/path --core-dir x` sets the path and silently discards every
  argument, and the console comes up looking correct on the default core
  directory. Quote the whole value.

- **`sudo -S ... | tail -0` HIDES A FAILED SUDO.** Three restarts in a row did
  nothing and the journal kept showing the old process, because the output
  that would have said so was thrown away. Print the exit status.

- **A BINARY THAT IS RUNNING CANNOT BE OVERWRITTEN** — `cp` fails with "Text
  file busy" and the restart brings back the OLD build, which reads exactly
  like the fix not working. Stop the session, copy, start.

- **NEVER COPY A NEW `.so` OVER ONE A RUNNING PROCESS HAS LOADED.** The
  process has the file mapped, and writing into it kills the process with
  SIGBUS (2026-10-04: the console died in `UpdateTargetSpeed` inside
  `cabinetos-ps2.so`, the session restarted the game by itself, and the
  scripted presses that followed landed on the wrong screens). Put the
  console on Home first (`tools/ui-loop.sh --no-build`): PS2 and the cores
  are loaded only when a game starts. `coredumpctl list` shows it.

- **A SESSION SCRIPT UNDER TEST RUNS THROUGH BASH, NEVER AS `ExecStart` OF A
  HOME FILE.** SELinux refuses to exec `user_home_t` from init (`203/EXEC`),
  the service retries every 2 s, and with no session on the seat logind takes
  the sound devices away. WirePlumber logs `Permission denied` on `hw:0` at
  every retry, never takes the cards back, and the next frontend freezes
  opening sound: a black screen that looks like the change under test
  (#268, "Booting with no display answering leaves the console at a guessed
  1080p"; #331, "After a session start loop, the sound manager loses the
  cards and the app freezes opening sound"; 2026-10-10). Use a `/run` drop-in with
  `ExecStart=/usr/bin/bash /var/home/cabinet/<copy>`, and check `wpctl
  status` shows the HDMI sink before and after. If it is already broken,
  `systemctl --user restart wireplumber` brings the sink back.

- **gamescope crashes on its way out when the app is stopped**
  (`wl_display_destroy`, SIGSEGV or SIGABRT). It is every session stop, not
  the thing being tested; look at what stopped the app. A crash while the
  app was running is something else: 2026-10-22's, as a game's window
  closed (`wlserver_presentation_feedback_discard` in the core's journal
  entry), is gamescope's own bug, fixed upstream and waiting for the base
  (PROJECT.md, "Waiting for stable: gamescope crashes when a game's window
  closes").

- **A SESSION START REMAKES THE TV'S SOUND OUTPUT.** Measured 2026-10-11
  (#351): the HDMI sink was node 65, then 70, then 55 within a second of
  gamescope coming up, and the last came back at WirePlumber's saved level.
  Anything that sets the volume at start can be undone a second later; watch
  `wpctl get-volume` and `wpctl inspect @DEFAULT_AUDIO_SINK@` across a
  restart before trusting a log line that says it was set.

- **RESTARTING WIREPLUMBER UNDER A RUNNING APP LEAVES THE APP SILENT.**
  2026-10-14, trying the settings for #341 ("No audio [Expected on this
  machine]") as a user copy: `systemctl --user
  restart wireplumber` came back with the TV as the output and the app
  linked to it, and pw-top's ERR for `cabinetos-frontend` climbed about 47 a
  second (the silent day's sign) until the console was restarted. Try
  WirePlumber settings with a restart of the console (they load at start, as
  the image's will), and read ERR after any sound change.

- **A udev rule is tried in `/run/udev/rules.d`**, then `udevadm control
  --reload` and `udevadm trigger --subsystem-match=<s> --action=change`: gone
  at the next reboot, like the loop's drop-in. Keyboards on this image are
  the `input` group's alone; only pads get the seat user's grant.

- **ydotool's virtual keyboard has no key repeat**, so a held key sends one
  press. A held volume key cannot be tested with it. A held arrow can: the
  console runs its own hold from the press to the release, on every screen
  since #398 (PLAY-03). Headless, `--nav held:<key>` hands the event handler
  the system's repeat itself (#484): `--nav "key:],held:],held:]"` ends on
  the Library, where a repeat that acted would end on Search.

- **A SHORT YDOTOOL TAP CAN BE DROPPED.** 2026-10-19 (#253): with key-down
  and key-up 0.15 s apart, the Return after `ydotool type` into Search was
  dropped about half the time. Hold 0.25 s, and read the journal for the
  result (`[keyboard] committed: ...`) before the next press.

- **frames.py CANNOT SEE A REPEATED GAME FRAME.** (frames.py is a frame-time
  script kept on the reference console, not in this repository; it reads
  gamescope's frame times.) The console presents at
  every refresh whether the game made a new picture or not, so gamescope's
  frame times read a clean 16.67 ms over a game that repeats and skips
  frames every second. Read the console's own `[pace]` line (#221, "Built-in
  cores: no audio rate control, a frame repeats or drops every 10 to 17 s").

- **AND `[pace]` CANNOT SEE A PICTURE GAMESCOPE HOLDS.** `[pace]` counts
  only the console's own lateness. The message `frames.py` skips (app
  frame time all ones) carries the visible frame time: the time between
  the kernel's flips that put a new console frame on screen. 33.3 ms there
  is a picture shown for two refreshes. 2026-10-19 (#253): with
  `GAMESCOPE_DISABLE_TIMERFD=1`, Axelay showed 4 such pictures in 38 s
  while every `[pace]` window read 0 refreshes missed. Read both. The
  layout after mtype is version u32, pid u32, app frame time u64, two u8,
  visible frame time u64.

- **`--env` REACHES THE FRONTEND, NOT THE SESSION SCRIPT.** It goes on the
  frontend's command line, so `CABINETOS_OUTPUT` given that way changes
  nothing and the TV stays at 4K. `tools/ui-loop.sh --session-env` puts it
  on the session. The LG's 2560x1440 mode is 59.95 Hz, the one non-60.000
  rate this TV has for testing.

## Logs


`journalctl -u cabinetos-session` shows ONLY systemd's own start/stop lines and
none of the frontend's output, which reads exactly like a console that does not
log. It does. The frontend's lines are in the journal without that unit
attached, so ask for the journal itself and grep it:

```
sudo journalctl --since "-40 min" -o cat | grep -aE "\[launch\]|\[core\]|\[frontend\]"
```

That one command is the difference between diagnosing a launch failure in a
minute and guessing at it for twenty.

## Testing emulators headless

- **`--core-option k=v` wins over everything the console sets**, on every
  route: `--core <so> --rom <file>`, `--launch <id>` and Play on the
  television (`Core::setCommandLineOption`). Until 2026-10-20 it reached
  `--core` only, and a `--launch` run ignored it in silence. So a picture
  level's own key (`citra_resolution_factor`, `dolphin_efb_scale`) passed
  this way pins that value at every level; to try the levels, set the game's
  level instead (`config/picture-quality.json` under the storage root, or
  the pause menu's Picture row, from the next start). Each launch logs
  `[core] option from the command line: k = v`.

- **`--core-options-detail` sees only options declared at load.** FCEUmm
  and FBNeo declare theirs with a game loaded and show "0 options"; read a
  real launch's `[options] N declared, M asked` instead.

- **`--state-test` cannot see a Vulkan picture.** It hashes a GL frame, so
  on ParaLLEl-RDP it reports "picture STATIC" and then PASS; that PASS
  proves nothing about the picture.

- **Headless games sometimes open their own pause menu** and, once, chose
  Exit to Home after 6 s (Dreamcast). A headless run that goes silent or
  leaves is the rig until the TV says otherwise (Virtual Boy did this and
  was fine on the TV).

- **The sweep (`sweep.sh`, in a scratch folder on the reference console, not
  in this repository) launches the smallest game of every system** from its
  list, `sweep.txt` (`sweep-all.txt` is the full list), with dummy audio and
  writes `sweep/summary.txt` beside it. About 25 minutes for 30 systems. **It
  hammers the GPU: never run it while MMagTech is testing on the TV.** It
  made Mario Kart look "really bad" once.

- **A frame capture of a game has a pause menu over it** when the headless
  window loses focus. Read the picture around it.

- **A headless `--launch` on the A9 is a real play as far as the drive is
  concerned.** It touches the game's entry, so the game jumps to the front
  of the drive's last-used order: the offline Recent shelf (#88, "Offline:
  kept games play with no server") and the
  order the cache is cleared in. 2026-10-05: eight test launches reordered
  the console's offline Home and the dates had to be put back by hand with
  `touch -h -d`. Note an entry's time (`stat -c %Y`) before a launch test and
  restore it after, or test with the game that is already first. Kill with
  SIGKILL before two minutes and no play session is recorded.

- **A headless run on the A9 reads the real controllers.** Pads and Wii
  Remotes go to every frontend on the machine, not only the one on the TV.
  2026-10-05: while MMagTech played at the TV, a headless test copy took his
  presses, opened its power menu and started Wild West Guns offscreen (it
  restored a save folder; nothing was lost). **Never run a headless frontend
  while he is at the TV**; use `tools/ui-loop.sh` and let him watch instead.

- **Offline is tested with a wrong address, never by stopping RomM.**
  `CABINETOS_ROMM=<server host>:6099`, the server with a port nothing
  answers on (headless, or `tools/ui-loop.sh --env`), is offline from boot.
  For the server COMING BACK, point the console at a
  relay on the A9 itself, `CABINETOS_ROMM=127.0.0.1:16005` with
  `tools/offline-relay.py` copied to the A9 (it forwards to the RomM server
  in `CABINETOS_RELAY_UP`): relay off is no server, relay
  on is the server back. Covers are filed by address, so link
  `covers/<that address>` to the real server's folder
  (`covers/<server host>_<port>`) for the test and remove the link after.

- **A server layout nobody has is tested through `tools/romm-readonly-relay.py`,
  never by changing RomM.** It forwards reads only (any write gets 403 and is
  logged) and can show a platform under another folder name:
  `CABINETOS_RELAY_UP=http://<server>:<port> romm-readonly-relay.py 16006
  45=arcade:Arcade`, then
  `CABINETOS_ROMM=127.0.0.1:16006` headless with a scratch `--storage-root`
  (copy `config/*.json`, and set `CABINETOS_DRIVES` to an empty folder, or
  downloads go to the real games drive). #310 ("Arcade games in any RomM arcade folder
  show up, not only in folders named exactly FBNEO or MAME2003") was tested
  this way, 2026-10-10: a launch
  tried to upload Paperboy's NVRAM and the relay refused it.

- **The folder route is tested through the same relay's `folder:<id>`**,
  which shows a one-file game as a folder on the server, with a made-up
  `update/` file beside it. Almost nothing on the reference server takes that
  route with a core (only Star Ocean, 7.8 GB), so move 2 used
  `folder:34 folder:584` (Super Mario Land fetched file by file; Teleroboxer's
  one-file entry moved into a folder).

- **Reading a game with `GET /api/roms/<id>` makes RomM create that person's
  empty `rom_user` row**, which moves its `updated_at`. Harmless, but do not
  read it as something the console sent.

- **`--sync-test` WRITES TO RomM AS THE ACTIVE ACCOUNT, scratch root or
  not.** 2026-10-15, move 1 of #398 ("Audit the code and restructure it for
  contributors (main.cpp is 20,000 lines)"): a headless `--sync-test` of Cannon
  Spike (Dreamcast) uploaded its state to RomM as MMagTech under
  flycast-native. Flycast has a state tag (GameSession's comment saying it
  has none is stale), and the upload did two things nobody wanted: RomM set
  the game's last played, so Cannon Spike jumped to the front of Recent on
  every device, and the state counts toward the three kept per game, so his
  own older state goes one save sooner. A scratch `--storage-root` keeps the
  console's disk clean, not the server. **Run `--sync-test`, and any launch
  test that may save, through `tools/romm-readonly-relay.py`**: the relay's
  `REFUSED POST /api/states?rom_id=550&emulator=flycast-native` line is the
  proof the upload was tried, and nothing reaches RomM. A test killed with
  SIGKILL sends no file saves, and a play session under a minute is not
  recorded, so those were already safe.

- **THE DISC RIG (`tools/disc-rig/`, #162, "Multi-disc games: Change disc
  in the pause menu, one save per game (Saturn first)") DRIVES A GAME BY
  SCRIPT**:
  `--disc-script "wait 600; insert 2; wait 300; shot a; state; startover 1;
  load; resume; check; quit"`, through the same calls the pause menu makes,
  headless, through the read-only relay. It found two core bugs in a night
  that a single launch never shows. Its `shot` reads the game's own frame
  (works for Flycast's Vulkan picture); `ui` is the whole screen and is black
  where a Vulkan game is. After Load latest state the game waits on *Press (A)
  to continue*, so a script needs `resume`, or every `wait` after it hangs.
  `wait` counts the game's frames, which stop while a menu is open: use
  `sleep` there.

- **A SAVE PUT ON THE CONSOLE BY HAND IS IGNORED IF RomM HAS ONE.** At
  launch RomM's newest save wins over the console's file (#259, "Cartridge
  saves: the local save is never loaded, so an unsent or offline save can be
  lost"), and a first
  launch uploads even an empty, just-formatted PS1 card. 2026-10-11: a
  10-save Resident Evil 2 card copied into the save folder never reached the
  game ("no files" on both discs) because RomM held the empty card from the
  first launch. For a TV test, upload the save to RomM (on MMagTech's go),
  then launch.

- **A RIG STEP THAT SKIPS THE MENU CANNOT TEST THE MENU.** `insert N` goes
  straight to the disc, so the panel's row-to-disc mapping was never run; it
  picked disc 2 for the "Disc 3" row whenever a disc was missing (fixed
  2026-10-11). Drive anything about the panel through `menu`/`nav`/`activate`.

- **GameFAQs saves: the built-in browser gets them, a plain fetch gets 403.**
  Download inside the page with `fetch()` and return base64 from
  `javascript_tool`; `tools/disc-rig/savecards.py` puts them on the cards.

- **`pkill -f`/`grep` ON A WORD THE SSH COMMAND ITSELF CONTAINS KILLS THE SSH
  SESSION,** including a word only further along the same command line
  (`grep "[r]elay2.py 1600[78]"` matched `relay2.py 16008` later in the same
  `bash -c`). Start and stop helpers from a script file with a pid file.

- **A PAD LEFT ON CHANGES A SUITE'S OUTPUT.** 2026-10-15, move 3 of #398:
  an Xbox pad still awake from a TV test was seated by the base binary's
  first runs and switched itself off at 7:20 PM (battery 1 of 4), so a dozen
  outputs differed by pad lines and a Dreamcast state taken during the pause
  it caused. Read the console's journal for `[players]` lines across a suite
  run before comparing two binaries, and rerun the same binary on the tests
  that differ to show what timing alone changes.

- **A RESTART NEVER REACHES THE END OF main().** On Power > Restart, Power
  off or an update's restart, gamescope's reaper kills the frontend right
  after `[power] released` (measured 2026-10-15 on 2026.10.15.2 and
  2026.10.16), so nothing that runs when main() returns (the App's
  destructors, the abort in #406, "Four background threads are never
  stopped at exit (pairing, remove and sign out, reconnect, drive
  backfill)") runs then. The exits that do return are the
  Steam handover and a quit by SIGTERM: test an exit headless with `timeout
  --preserve-status -s TERM` (SDL makes it a quit, `[shutdown] asked to
  stop`), and on the TV with Steam and back. Since #305 they too are cut
  two seconds after the saves if what follows has not finished
  (`[shutdown] 2 s after the saves, still waiting on ...`).

- **A SLOW HANDOVER TO STEAM: SPLIT IT AT GAMESCOPE'S `launch: Primary
  child shut down!`** #305 ("Opening Steam sometimes takes 25 seconds
  instead of a fraction of one"), 2026-10-19: from `[steam] asked the
  session to hand over to Steam` to that line is the frontend's own exit
  (0.06 to 0.28 s in 20 of the 21 handovers since 2026-10-04); from it to
  the session's `handing the screen to Steam` is gamescope's. All four
  25 s cases were gamescope's: `[libseat] ... Could not close device:
  Connection timed out` exactly 25.2 s after its app was gone, the
  default D-Bus timeout on logind's ReleaseDevice. The same stall shows on
  gamescope exits that are not handovers. The brief's lead, the
  frontend's D-Bus calls on exit, was wrong: those run only inside a
  pairing. **The session now cuts it** (#305; cabinetos-session,
  `wait_for_compositor`): two seconds after the reaper has gone it logs
  `has not ended 2 s after its app; stopping it (TERM)`, then `(KILL)` a
  second later, and the handover takes about 3.2 s; one of four handovers
  on the A9 stalled by itself on 2026-10-19 and read exactly so. **To
  stall one on purpose**, as `cabinet`: `waitpid` on the
  `gamescopereaper`, then `kill -STOP` its parent gamescope (the STOP
  lands within 2 ms of `Primary child shut down!`), and `kill -CONT` it
  later if it is still there. **To open Steam and come back in a test:**
  on Home, Escape (the Power menu, Cancel focused), Down (Switch to
  Steam), Return, Return (Start); come back with `steam -shutdown` as
  `cabinet` with `XDG_RUNTIME_DIR=/run/user/1000`, which ends Steam's
  session the way its own menu does, so the session's whole return path
  runs. Escape on Settings goes Back to Home rather than opening the
  menu. Capture with
  `gamescopectl screenshot` into a file of your own while other sessions
  run headless frontends: they write and remove the same
  `/tmp/cabinetos-frame.bmp` the SIGUSR1 capture uses. Under
  `ui-loop.sh --session-script` the session's lines carry the identifier
  `bash`, not `cabinetos-session`, so `journalctl -t cabinetos-session`
  shows none of them; and pick gamescopectl's socket by the exact name
  `gamescope-<n>`, since `gamescope-<n>-ei` beside it has the same time
  and gamescopectl hangs on it.

- **A TEST GAME ON THE CONSOLE ENDS INSIDE A MINUTE, BY `--restore`, NEVER
  BY A KILL.** 2026-10-15, move 4 of #398: a loop launch (`tools/ui-loop.sh
  --game 305`) kept running because the next step's idle check read the test
  game's own `[pace]` lines and stopped the script. A `kill -9` of the
  frontend then left its play session open; the session started the loop's
  build again, which launches the game again, and that start recovered the
  open session ("a session left open, 60 s, is owed now") and sent it to
  RomM as MMagTech. Tetris & Dr. Mario went to the front of his Recent
  shelf. Under a minute a session is not recorded, but a kill does not stop
  the next start from recovering one. End a loop game with `--restore`
  straight after its capture, and let an idle check after it wait for the
  `[pace]` lines to age rather than stop. And a `grep` for the frontend's
  arguments also matches gamescope, whose command line carries them: that
  kill took gamescope with it.

- **A SCRATCH ROOT STILL SEES THE GAMES DRIVE.** `config/drives.json` is
  only the memory behind the missing-drive line, and writing `[]` to it
  kept nothing off the drive: the frontend found `/run/media/cabinet/Games`
  and its folder time moved. `CABINETOS_DRIVES` at an empty folder is what
  keeps a run off every drive (#488, FIRSTRUN-19).

### TWO GAMES AT DIFFERENT SOUND RATES, WITHOUT A RESTART — learned 2026-09-27

The audio stream was opened once, at the first game's sample rate, and kept
for every later game until the app restarted. It hid for eight days because
**every loop deploy, image update and reboot restarts the app**, so most
tests played one game in a fresh app, and a fresh app is always right. It
showed as DoDonPachi (47997 Hz) after Mortal Kombat II (32040 Hz): no sound
while playing, then the backlog playing on over Home after quitting, which
MMagTech read as "another version is running". **Any per-game setting that is
opened once and reused needs a test of two different games in one run.**

## Habits

- **Measure rather than reason, where you can.** Two minutes of measurement has
  beaten a plausible argument every time it has been tried here.

- **Run the control.** `--core-options-off` and `cores/backend-diff.sh` exist
  for it, and the control has now been more informative than the result three
  times — most recently the bare-`fedora:44` library sweep.

- **`/tmp` on the VM is a small tmpfs.** Copying 273 MB of cores into it fails
  with "Disk quota exceeded" halfway. Use `/var/mnt/games/` for anything large.

- **`pgrep -f "some string"` matches your own command line**, and so does
  `pkill -f`. **Match on something the checker cannot contain** — `pgrep -x`, a
  pid file, or the exit status of the thing you started. `pgrep -x` also
  refuses names over 15 characters, so `cabinetos-frontend` needs
  `ps -eo args | grep "[c]abinetos-frontend"`.

- **Two podman containers with `:Z` over overlapping paths will break each
  other.** `:Z` relabels the whole mounted tree for one container's SELinux
  category. Cost one PPSSPP build. **Do not start a second container over a
  parent of a running one.**

- **A COMMAND THAT FALLS THROUGH CAN HAND PODMAN THE HOME FOLDER.**
  2026-10-16, move 5 of #398: a check over SSH ran `bash idle.sh && cd
  <a scratch build folder> && ...; podman run -v "$PWD":/src:Z ...`. idle.sh said busy,
  the `&&` chain stopped before the `cd`, the `;` ran podman anyway, and
  `$PWD` was the home folder: `:Z` relabelled 30,276 files (`.config`,
  `.cache`, part of `.local/share`) to the container's SELinux label before
  it failed at Steam's folder. Nothing was refused only because the
  console's processes run unconfined. Put back as `cabinet` with `find ~
  -xdev -context '*c323,c693*' -print0 | xargs -0 restorecon -F` (the
  categories podman chose). **Give podman an absolute folder, never `$PWD`,
  and run a chain under `set -e` so nothing runs after a failed step.**

- **THE LOOP'S RSYNC KEPT FILES IT DID NOT KNOW.** `tools/ui-loop.sh`
  copied a tree's `frontend/src` onto the A9 without deleting, so after a
  tree with new files, an older tree's build failed on the files it does not
  have (move 4 and move 5 both), and a source removed or renamed in git was
  compiled again beside its replacement. Since 2026-10-17 (#398's faults
  step) the loop copies with `--delete`, on the A9 and the VM alike. Any
  other copy of `frontend/src` onto a build machine needs it too.

- **TWO SUITE RUNS SIDE BY SIDE NEED THEIR OWN PORTS, AND ONE PATH IS
  SHARED.** 2026-10-16, move 6 of #398: the binary under test and the base
  binary's second run went at the same time to save 25 minutes, each with
  its own relay ports (`suite6p.sh`, `P` the port base), roots, outputs and
  pid files. The frontend writes one fixed file, `/tmp/cabinetos-manual.pdf`
  (a game's manual), so the second run starts three minutes after the
  first. The relay's port is part of any address the console shows, so a
  QR of the pairing link differs by port alone: compare it on the same
  port, rerun alone. The two runs under the same load are the
  like-for-like pair; the first run, alone, shows what the load changes.

- **A CAPTURE TIMED BY `--frames` SHOWS A DIFFERENT FADE WHEN THE MACHINE IS
  BUSY.** The same move: the 4K Home capture (120 frames) took 6.6 s in one
  run against 0.8 s in every other, because the run beside it was busy, and
  caught Home fully faded in where the others caught it half dim: eight
  million pixels different, from timing alone. Alone, three runs on each
  binary gave the same picture. The fades follow the clock, not the frame
  count; rerun a large capture difference alone, and swap the roots, before
  reading anything into it.

- **A loop deploy restarts gamescope, and every `gamescopectl` setting goes
  with it.** 2026-10-07: `composite_force` was on for a Remote Play stream;
  `tools/ui-loop.sh` restarted the session, the setting was gone, and the
  phone's stream went black while the phone stayed "connected". Anything set
  by hand on the running gamescope is gone after a deploy; set it again, or
  make the thing that needs it set it.

- **A black stream with sound: capture gamescope before blaming the
  stream.** 2026-10-08, a game in Steam: a `gamescopectl screenshot` showed
  the game drawn normally while the phone was black, which put the fault in
  Sunshine's capture, not the game or the TV; Sunshine's own line said which
  ("Mapped 'HDMI-A-1' to kmsgrab monitor index 1": it numbers planes, and 1 is
  the overlay). A guess carried from the previous day (Steam's gamescope never
  composed) was half wrong: Steam's menus streamed without it. And ask what
  the TV shows too: one black TV that evening was a second, separate fault.

- **Sunshine picks its plane only at certain moments, so time those against
  gamescope, not just the end state.** #304 ("Stream into Steam: leaving
  Steam back to Home turns the phone's picture black"), 2026-10-09: back from Steam the
  stream went black although the console composed every frame. Sunshine had
  picked 0.35 s after the new gamescope came up, 0.7 s before it composed,
  while Home was still on two planes. The proof took a 5 ms plane sampler
  (libdrm through python ctypes, as `cabinet`, no root) run across a session
  restart: it shows which planes carry a picture from gamescope's first
  frame. Line Sunshine's "Reinitializing capture" and "Mapped ... index N"
  up with the session's "is up" and the moment composing starts.

- **Read a person's one-word answer against the question, not the hope.**
  "nope" to "is the seam still there?" was read as "gone", a fix was
  declared, and a comment saying so went on #286 ("Remote Play: stream the
  console to Moonlight (Sunshine), off by default") before "no the issue is
  still there" corrected it. When a yes/no question can be read both ways,
  ask it as "yes or no: is X still there?", and verify on the machine before
  writing it down.

- **"The colours look washed out" on a stream: read the stream's colour, not
  the program's version.** #304, 2026-10-09: a freshly built Sunshine was
  blamed. Sunshine logs the colour of every stream ("Color coding: SDR
  (Rec. 601)" or "HDR (Rec. 2020 + SMPTE 2084 PQ)"), and the TV's HDR state
  is the connector's HDR_OUTPUT_METADATA (eotf 2 is HDR10). Steam runs the TV
  in HDR; composed, the copied picture is HDR, so an SDR stream (Moonlight's
  HDR setting off) looks washed out, and with it on the stream matches the
  TV. The richer-looking picture was the wrong one. Ask which way the phone
  was set before changing anything, and when two pictures are compared,
  have the person compare each against the TV, not against each other.
