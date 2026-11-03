# Solved cases

Past investigations, told in full. Reference only: the rule each one taught is in the topic files. Some say "see item 3b" and mean the old handover, `git show 69f581d:docs/NEXT-SESSION.md`.


**Finished work, with its investigations intact. Nothing here is queued and
nothing here is owed.** It sits at the back so the queue above holds only live
work, and it is kept rather than deleted because several of these records are
worth more than the fault they closed — what ruling something out actually
looked like, and three separate cases of measuring in a configuration nobody
plays in.

**Cross-references elsewhere in this file still point here by number** — "see
item 3b", "item 9c", "item 1b". No item was renumbered when it moved.

### PLAYSTATION 2 AND GAMECUBE PLAY — 2026-09-20

**1232 playable games, up from 1147.** Both systems draw a picture on the
television off the A9's Radeon. The work is in the `vulkan-host` branch.

**It was never about the emulators.** Both libretro cores existed, both already
had Vulkan compiled in, and `catalog.cpp` has routed `ps2 -> pcsx2` and
`ngc -> dolphin` since the table was written — so two `.so` files in a core
directory turned "not built on this console yet" into 85 playable games with
no code change at all. What was missing was **this frontend's half of a
contract libretro already specifies**: the instance, the device, the queue and
somewhere to put the picture. RetroArch implements that end; this console owns
its frontend and had only ever done the OpenGL ES half.

See open question 20 for the whole thing. The three one-line faults:

1. `GET_PREFERRED_HW_RENDER` was hard-wired to GLES, so **Dolphin never asked
   for the Vulkan it has compiled in**.
2. `SET_HW_RENDER` refused Vulkan by name.
3. **Dolphin checks for a `VkSurfaceKHR` to decide whether it has a display.**
   With none it renders and never presents — fifty seconds of emulated Mario
   Kart, correct audio, a black screen. It gets a `VK_EXT_headless_surface`.

**MEMORY CARDS: PS2 TRAVELS, GAMECUBE DOES NOT YET.** A card written on the Mac
lands on the console and the game reads it — proved with Burnout 3, which is
the only real save that exists for either system. GameCube saves and syncs but
uses this console's own row naming; see open question 12b for why that is
deliberate.

**SIX OF THE SEVEN CARDS ON THE SERVER WERE EMPTY** and were deleted at
MMagTech's request on 2026-09-20. Cabinet for Mac uploaded a card on first play
regardless of content and had no freshness rule for either platform — the same
fault `catalog.h` [now `savetable.h`] records for Dreamcast, worse ratio. **CabinetOS now refuses**;
both rules are measured and in `filesave.cpp`.

**WHAT IS NOT DONE, and none of it is hidden:**

- **Nobody has saved inside a game and watched it go up.** The download half is
  proved; the upload half is the same code Crazy Taxi 2 proved for Dreamcast.
- **THE PS2 *LIBRETRO CORE* STILL CANNOT SHIP AND NEVER WILL** — its source
  repository does not exist. **But PlayStation 2 is no longer blocked**, because
  the embed route was measured on 2026-09-21 and is open: see item 1a. The
  GameCube core can be pinned and that part is unchanged.
- **TWO OPEN BUGS FROM ONE HOUR OF PLAY, both reported by MMagTech and neither
  reproducible from here.** See item 1b.

### 1b. BOTH BUGS FOUND BY PLAYING ARE CLOSED — one fixed, one not reproduced

#### THE TUNNEL IS FIXED — MMagTech, 2026-09-21: *"tunnel was gone on new core"*

It went away with the move to upstream PCSX2, so it belonged to the libretro
core that has since been deleted from the machine — which is also why it never
reproduced from here: **every measurement was taken against the emulator that
did not have the fault.** No change was made to chase it and none is needed.

The investigation is kept below because its conclusion was right for the wrong
reason, and the warning at the end of it is still good advice.

**"It looks like I was looking through a tunnel when racing"**, Burnout 3 on
the television. **Every capture taken here measures a correct 4:3 picture
exactly filling its quad** — at 1920x1080 and 3840x2160, in a menu and in
gameplay, on both bridge routes, with the widescreen hint on and off. The new
`[picture]` line prints the four numbers that have to agree and they agree:

```
[picture] core 640x448 aspect 1.3333 -> quad 1440x1080 (1.3333) at 240,0
          uv 0.0000,0.0000..0.8333,0.8750
```

**So it is not the layout and not the texture coordinates.** "Tunnel" describes
a FIELD OF VIEW rather than an aspect, which points at the emulator's own
settings rather than the frontend — start with the 78 core options, and ask
which game and whether it happens from a cold boot or only after loading the
Mac's memory card. **Do not trust a bounding-box measurement here**: the first
three this session were confounded by the game's own black borders and by the
pause menu's dimming, and one of them sent an hour the wrong way.

#### NOT REPRODUCED, WHICH IS NOT THE SAME AS FIXED: THE PAUSE MENU'S EXIT

**This heading said STILL OPEN and the entry above it said closed.** Both were
written the same day and the second is right: MMagTech, *"you can consider the
hang done as we havent hit it again."* **No change was made that targeted it**,
so the suspect below was never eliminated — it outlived a change rather than
being killed by one. If it comes back, this is still where to look.

**THE PAUSE MENU'S EXIT LEFT THE CONSOLE STUCK.** The menu was on screen with
"Exit to Home" focused and the process was **asleep at 0% CPU** — and the UI
loop is unpaced, so 0% means the frame loop had STOPPED, not that a button was
ignored. That is a hang in teardown. **It does not reproduce**: exiting at 1500
frames in works headlessly and under gamescope when driven from code. The
suspect is `Core::unloadGame` on a threaded core —
`vk::destroyContext` calls `deviceWaitIdle` on a device the CORE created and
may already have torn down in its own `context_destroy`. **Get a backtrace next
time rather than theorising**: the process is still there, so
`gdb -p <pid> -batch -ex "thread apply all bt 12"`.

### 2. Vertical arcade games play the right way up — DONE 2026-09-19

**`RETRO_ENVIRONMENT_SET_ROTATION` was in `libretro.h` and handled nowhere.**
DoDonPachi and every other TATE board rendered sideways; 223 arcade games were
affected. It is fixed, and PROJECT.md's *Vertical arcade boards, and the turn
they ask for* has the whole thing. The three parts worth carrying:

- **The turn goes on the quad's CORNER in the vertex shader**, before the
  corner looks up a texture coordinate. It cannot go in `u0,v0,u1,v1` — those
  flip, they never transpose.
- **A turned board's declared aspect is ALREADY turned.** FBNeo says 0.75 for
  DoDonPachi while handing back 448x224. Inverting it turns the picture twice;
  Cabinet's own comment says that "stretched every vertical game". So a turned
  picture takes its shape from raw pixels. Safe because **only arcade cores
  ever rotate** — a console was built to output to a television — which is
  MMagTech's point and what bounds the whole rule.
- **A turned picture fills the height at its true shape**, MMagTech's call:
  1080x2160 on the A9, 28% of the width. Upright games keep integer scaling.

**What has NOT been done is look at it on the television.** Every check was a
capture, including one at 3840x2160 off the A9's own Radeon. A person with a
pad is still owed.

### 3. One real in-game save, on Dreamcast — **DONE 2026-09-19**

**A person played Crazy Taxi 2, saved inside it, quit through the overlay, and
the VMU reached RomM.** No save in this class had ever been written by actually
playing a game here; every round trip before this restored a real card, watched
the core read it, and sent back byte-identical bytes, which is the correct
answer for a session that saved nothing and is why forcing an upload needed
`--sync-test`.

```
22:45:24  [save] 131072 bytes from the server into /var/lib/cabinetos/bios/dc/vmu_save_A1.bin
22:48:19  [save] 131072 bytes from dc/vmu_save_A1.bin
22:48:19  [overlay] exited to Home
22:48:20  [save] uploaded flycast-native
```

The middle line is the whole result: it prints only when the card differs from
the baseline taken at launch, so it is the console saying *this game wrote
something*. On the server, `Crazy Taxi 2 (USA) (Cabinet).srm` now reads
`updated 2026-09-20T02:48:19Z` against a `created 2026-08-16` — **the same row
overwritten rather than a second one**, 131072 bytes, tagged `flycast-native`,
which is the row Cabinet on the Apple TV read.

**It could not have happened a day earlier**, and that is worth keeping: the
Dreamcast's accelerator and brake are its analogue triggers, and this console
answered "how far is the trigger pressed" with the right stick's Y axis until
the same evening. Crazy Taxi literally could not be driven. See *Things that
will bite you*.

**What is still owed in this area:** every other file-writing platform is
proven by round trip rather than by play — 3DO, Sega CD, Neo Geo Pocket, DS and
both arcade emulators. The mechanism is now known to work end to end, so those
are a matter of playing them.

### 3b. A game that draws nothing — **SOLVED 2026-09-21. It was the SECOND game.**

**THE RULE IS: PLAY A GAME, LEAVE IT, PLAY ANOTHER OF THE SAME PIXEL SIZE.**
That is the whole of it, and it is why two days of deliberate attempts failed —
everybody was reproducing "launch this game" and the trigger was the game
BEFORE it.

`Core::load()` calls `unload()`, which deletes the frame texture and sets
`texture_ = 0` while leaving `frameWidth_`/`frameHeight_` at the previous game's
values. The next game generates a fresh texture with NO STORAGE, `sizeChanged`
comes out false because the dimensions match, and the upload calls
`glTexSubImage2D` on it: `GL_INVALID_OPERATION`, nothing uploaded, a texture
that samples black — while the core runs perfectly and plays perfect audio.

Six FBNeo boards in one session all share a resolution. Air Zonk then Devil's
Crush are both 256x240 and the second was black; Air Zonk then a SNES game is
256x240 then 256x224, so `sizeChanged` was true and it worked. The first game
after a restart always worked, because those fields start at zero.

**Fixed** by zeroing them where the texture is generated — the only place that
knows the texture has no storage, and correct however the texture came to be
missing. Confirmed by MMagTech on TurboGrafx and 3DO.

**Everything the 2026-09-19 investigation established was true and none of it
was wrong** — the core was innocent, the upload "succeeded", the texture was on
the right unit. It was looking at the second game and measuring the first.

**Original report, kept because the shape of it is the lesson.** Found by
MMagTech on the television, 2026-09-19: six FBNeo launches in one session —
DoDonPachi, Deathsmiles, Pink Sweets, ESP Ra.De., Mushihime-sama Futari — drew a
black picture while the core ran normally and played sound. Every launch in
every fresh process since was fine, including deliberate attempts to reproduce
it.

**What was established, and it is a lot:**

- **The core is innocent.** The probe read the buffer it hands over:
  `rgbaMax=248`, a real picture, every frame.
- **The upload is innocent.** `upload ok`, correct texture on the correct unit,
  no GL error.
- **The layout is innocent.** The letterbox glow was drawn from the picture
  rect and its profile put the window at x 1120..2790 on a 3840x2160 panel,
  which is exactly where a 240x320 board belongs.
- **The loop is innocent.** 44% of a core, sleeping in poll, presenting.

So **the frame reaches the texture intact and is lost at sampling**, and the
console keeps drawing everything else perfectly — which is why it reads as
"this game does not work" rather than as a fault.

**Two theories, both killed by measurement**, recorded so nobody spends the day
again: *Flycast poisons the cores after it* (the launch order fitted six for
six, then Crazy Taxi 2 followed by Pink Sweets rendered fine), and *Flycast
leaves a GLES sampler object bound* (it leaves none — probed).

**One loose end**: the first upload after a Flycast session reports
`errBefore=0x502`, a `GL_INVALID_OPERATION` left pending by the teardown.
Unexplained, and not shown to be related.

**THE INSTRUMENT THAT MAKES THIS TRACTABLE, AND IT IS NEW.** The television can
be photographed directly, with the session running and undisturbed:

```
export XDG_RUNTIME_DIR=/run/user/$(id -u) GAMESCOPE_WAYLAND_DISPLAY=gamescope-1
gamescopectl screenshot /var/home/cabinet/now.png
```

**`gamescope-1`, not `gamescope-0`** — the first socket refuses the connection.
That turns "it looks black" into a number: a black game screen measures
**max pixel 8**, which is not black at all, it is the bias glow at 0.025, and
reading that is what proved the geometry was right.

### 4b. First run — BUILT, start to finish

**A person can now set this console up with a keyboard and a phone, and never
touch SSH.** Five screens, the whole chain, on the reference machine.

| | |
|---|---|
| `firstrun.{h,cpp}` | the chain, and every rule about what may be skipped |
| `setup.{h,cpp}` | the five screens, and the workers that keep them drawing |
| `qr.{h,cpp}` | byte mode, versions 1–10, error correction M |
| `net.{h,cpp}` | status, scan, join, forget, and the polkit verdict |
| `bluetooth.{h,cpp}` | the adapter, the scan, and pair/trust/connect |
| `proc.{h,cpp}` | the one place that starts a process, argv only, never a shell |
| `60-cabinetos-network.rules` | the grant that stops Phase 6 breaking Wi-Fi |

**SEE IT WITHOUT DISTURBING THE TELEVISION:**

```
SDL_VIDEODRIVER=offscreen ./cabinetos-frontend --setup-step pair \
  --screenshot /tmp/x.bmp --render-size 3840x2160 --frames 400
```

`--setup` forces the flow on a machine that is already configured and **never
writes anything**, which is the only way anybody here can look at it — both
machines are set up and taking that away to see a screen is a silly way to lose
an afternoon.

**THE CHAIN IS ENFORCED, NOT DESCRIBED.** `Machine` is handed a `Facts` and
judges it; it never calls the network, the disk or a server. `observe()` is the
one place that goes and looks. That is the same split `screens::` makes and it
buys the same thing — the whole flow can be walked at any point in it, on a
machine with nothing attached.

**Which made the rules testable, and the test found a real deadlock on its first
run.** `--first-run-rules` walks all 96 [576 today] reachable combinations of facts and
asserts the REFUSALS rather than the happy path. What it caught: the Wi-Fi
step's skip was keyed on **Ethernet** being up rather than on being **online**,
and those coincide only while this console knows about exactly two kinds of
link. One `net.cpp` change later, a machine online over a third kind would pass
the network gate and then sit at a Wi-Fi step it could neither satisfy nor skip.
Reading the code again would not have found it.

**THE A9 SAYS IT NEEDS NO SETUP, AND THAT IS THE RULE THAT MATTERS MOST.** A
machine with a server address, a token and a user behind that token is ADOPTED
rather than walked through a wizard, and the marker is back-filled saying so.
Without that rule, the reference console — set up by hand over SSH, working for
a day — would have presented a welcome screen the next time it booted. The
question is *"is this machine configured"*, not *"has this flow been run"*.

**THE QR IS PROVED ALL THE WAY TO THE GLASS.** A capture of the finished
3840x2160 frame off the A9's own Radeon was handed to a decoder with no cropping
and no help — exactly as a phone pointed at the television sees it — and read
back the live pairing URL the server had issued seconds earlier.

### THE WRITES ARE TESTED NOW — `--first-run-writes`

**Completing setup had never written anything, ever.** Every walkthrough used
`--setup`, which forces the flow on a configured machine and deliberately writes
nothing — so `setServerAddress` and `markCompleted` had never once been executed
by the product. **That is the worst failure this feature can have**: a marker
that does not persist means a console completes setup and boots straight back
into setup, for ever, on a machine somebody has just installed. It would look
exactly like a console that cannot be set up at all.

Given that three of 2026-09-20's faults were in code that looked correct and had
simply never run, that was not a risk worth carrying. `--first-run-writes` runs
against a scratch root, touches nothing real, passes nine checks and belongs in
CI. **The token save is the one write not covered, and it is the one that is
already proven** — both machines here were paired with `--romm-pair`, which
calls the same `saveToken` [gone; first run and `--romm-pair` now both write
the token through `accounts::recordPairing`, accounts.cpp].

**MMagTech will not reinstall until the UI is finished and every core is built
and tested** (2026-09-20), which is the right call — a fresh install is
expensive and should be spent once on something complete. So the fresh install
is the FINAL ACCEPTANCE TEST rather than a prerequisite, and most of what it
would prove can be had sooner:

| | |
|---|---|
| The writes | **done** — `--first-run-writes` |
| An empty server field | **the VM**, with its config moved aside; nothing needs reinstalling |
| An empty Bluetooth list | **the A9, reversibly** — `bluetoothctl remove` the Pro Controller and it has to be DISCOVERED, which is the real first-run case. Re-pairing it through the product is the test. |
| The whole thing on a virgin machine | only a fresh install |

### 6. The UI freeze IS LIFTED

**Decided 2026-09-17: no more UI is designed or tuned until CabinetOS is
installed on the reference machine.** The user's call, and **the condition is
met**: the console runs the frontend on a television, on its own GPU, at the
panel's native 3840x2160. Everything under *Available now that the console runs
on a television* is available, in the order it is written.

**The line is the acceptance test, not the subsystem.** If the test is "does
this look right", it waits. If the test is a measurement or a behaviour, it
goes ahead — and a screen that already exists is not frozen, because fixing
something *wrong* is not the same as tuning something.

### 9c. A BLACK SCREEN ON LAUNCH, WITH AUDIO — SOLVED, see item 3b

**This was the second-game fault and it is fixed.** Everything below was measured
before the cause was known and is left as a record of what ruling things out
looked like — every measurement in it is correct and none of it found the bug,
because every one of them launched a game into a FRESH PROCESS, which is the one
case that always worked.

**The lesson worth keeping: a headless test cannot reproduce a fault whose
trigger is the previous game.** `--launch` on a cold process was the wrong
instrument, used four times.

#### What was measured before the cause was found

MMagTech, during the UI session: two platforms *"launching with a black screen"*
and, a moment later, *"i hear audio"* — so the core is running and the picture is
not arriving. This is almost certainly the same fault as item 3b, which is
already recorded as real and not reproducible.

**WHAT WAS MEASURED, so nobody repeats it:**

- **3DO renders correctly HEADLESS.** `SDL_VIDEODRIVER=offscreen --launch 3068
  --frames 900 --render-size 1280x720 --screenshot` gives a frame whose extrema
  are (0,247) per channel. Not black, and the geometry is right: 320x240 at
  59.94 fps integer-scaled to a 1280x960 quad at 320,60 of the 1920x1080 canvas.
- **3DO renders correctly IN THE LIVE SESSION TOO**, on the same build, launched
  through `tools/ui-loop.sh --game 3068`. Ballz reaches its character-select
  screen and photographs cleanly.

So it is intermittent rather than per-platform, which is exactly what item 3b
says. **The two reported platforms are not written down here** because the
message said "msn snd 3d0" and only the 3DO half is certain — ask before
assuming which the other one was.

**Do not start from the frontend's draw path.** It was measured above and it is
correct. The next useful thing is a capture taken AT THE MOMENT it is black,
which the UI loop can now take without restarting the session: signal the
running frontend with `kill -USR1` and fetch `/tmp/cabinetos-frame.bmp`. A frame
that is black in that capture and a frame that is black only on the television
are two different faults — the second one is gamescope's.
