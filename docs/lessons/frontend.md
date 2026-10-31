# Lessons: screens, network, pairing and first run

Read before working on a screen, the network, Bluetooth, pairing or first run.

## Words on screen

- **A truncated explanation is worse than none.** A tile's second line holds
  about sixteen characters beside a cover. Measure the column before writing the
  string.

- **Two tiles that read the same are one tile.**

- **On screen it is "the server", never "your server".** The person at the TV
  may be on someone else's RomM, a parent's or a friend's. Say what is missing
  and where; whoever owns the server knows it is theirs. (MMagTech, 2026-10-29)

- **THE COPY ASSUMES A COMPETENT ADULT.** MMagTech, 2026-09-20: *"if you have a
  RomM server and can install an OS I shouldn't need to tell you in depth how to
  pair a controller."* Every line says the CONSTRAINT — required or optional,
  and why only when the why is not obvious — and stops. Titles say what the step
  does, not hello.

- **NEVER LET FOCUS LAND ON A ROW THAT DOES NOTHING.** Every placeholder in the
  setup flow is disabled, so this is the common case. A focus rim on a row that
  ignores the button cannot be told apart from a crash.

- **A GUARANTEE STATED UNCONDITIONALLY BY A FLOW THAT CAN BE SKIPPED IS A BUG.**
  First run's last screen said "you can unplug the keyboard" — the promise the
  whole design exists to make — while the controller step it follows is
  deliberately skippable. Somebody who skips it has exactly one input and was
  being told to unplug it. **Anything that can be skipped must have its
  consequence said on the step that offers the skip, and every later promise has
  to be conditional on what actually happened.**

## Anything from outside the process

- **ANYTHING FROM OUTSIDE THIS PROCESS IS A SNAPSHOT WITH A COST, AND EVERY
  SCREEN SHOWING ONE OWES TWO ANSWERS: WHO REFRESHES IT, AND ON WHICH THREAD.**
  That one sentence covers seven bugs found in an hour of walking first run on
  the television. Neither the Wi-Fi list nor the Bluetooth list had an answer to
  the first — both went on reporting what was true a minute ago, and a deleted
  Wi-Fi profile left a row claiming to be connected AND saved, so pressing it
  tried to join with no password. Four calls had the wrong answer to the second,
  the worst being `bt::adapter()` inside `rebuild()`: **two subprocesses every
  two seconds, for ever, to choose the wording of one row.**

- **A BLOCKING CALL ON THE FRAME THREAD LOOKS EXACTLY LIKE A WORKING FRAME IN A
  SCREENSHOT.** `bt::known()` ran there after a successful pairing — one process
  to list devices and another PER DEVICE, twenty-odd on a real scan — so the
  console froze for seconds at the moment it had just said "Controller ready."

- **THE CONSOLE SHOWED A BLANK SCREEN ON EVERY BOOT AND NOBODY HAD NOTICED.**
  Reaching the server, adopting the user and pulling sixteen hundred games all
  happen before the frame loop exists — seconds normally, up to NINETY when the
  server is not up yet. It took somebody pressing "Start playing" and expecting
  something to happen. `setup::showWaiting` now draws a still frame naming the
  stage. Nobody watches a console boot with a stopwatch.

- **A SCREEN THAT WAITS FOR SOMETHING MUST KEEP LOOKING, AND MUST FETCH WHAT IT
  NEEDS RATHER THAN WHAT ITS ENTRY POINT NEEDED.** First run's network step read
  the facts once on arrival and started its Wi-Fi scan the same way. Enter it on
  a cable, then unplug: the panel correctly switched to a Wi-Fi list and showed
  the empty one nobody had ever filled — "Nothing on the air", in a house with
  four networks — and plugging the cable back in changed nothing on screen. Both
  halves are now driven by what the screen NEEDS, on a two-second worker.

- **NetworkManager REFUSES `--rescan yes` while its own scan is running**, and
  that reads as an empty sky. Fall back to the cached list. And tell "we looked
  and there is nothing" apart from "we could not look" — only one means retry.

- **DO NOT SAY A SERVER DID NOT ANSWER BEFORE ASKING IT.** Arriving at the
  server step with an address already in `session.env` is the common case, and
  the screen reported it unreachable before sending a packet. It needed a fact
  at the rules level, not a fix in the screen.

## Network and polkit

- **A COMMAND THE FRONTEND *RUNS* IS INVISIBLE TO EVERY CHECK THIS REPO HAS.**
  `ci/base-watch.txt` watches shared LIBRARIES and `require-frontend-libs.sh`
  reads `ldd`, so a strip pass that removed NetworkManager or polkit would leave
  a green build, a binary that links perfectly, and a console that cannot see a
  Wi-Fi network or say why. `net.cpp` needs `/usr/bin/nmcli` and
  `/usr/bin/pkcheck`; `build.sh` asserts both. **Anything else that shells
  out needs the same treatment.** **And the check goes after the strip:** until
  2026-10-17 it ran before it, green on every build and blind to the very
  removal it was written for (#398, SCRIPTS-12).

- **`pkcheck` PRINTS SEVERAL `key=value` LINES, NOT ONE.** Taking the last `=`
  in its output reports `1` — the value of
  `polkit\56retains_authorization_after_challenge`, which is not even one of the
  values the action can have. Match the line whose key ends in `result`.

- **THE POLKIT ANSWER DEPENDS ON WHO IS ASKING, AND BOTH ANSWERS ARE RIGHT.**
  Over SSH the verdict for saving a network is `auth_admin_keep`; from the
  console's own session it is `yes`. The rule requires `subject.local`, on
  purpose: Developer access hands out SSH deliberately, and a shell over the
  network should not inherit the console's privileges. **`--network` says which
  question it put**, because a probe that printed one number without saying
  would be the third lying instrument this project has fixed.

- **A POLKIT RULE THAT GRANTS SOMETHING ALREADY GRANTED PROVES NOTHING.** The
  machine already answers `yes` via `wheel`, so installing a rule that also says
  yes changes nothing observable. **The decisive test is to install it returning
  `NO` and watch the verdict flip** — that proves yours is consulted first.
  `60-` sorts before `org.freedesktop.NetworkManager.rules`, and polkit takes
  the first rule that returns a result. Put the machine back afterwards.

- **`nmcli --terse` ESCAPES COLONS, AND AN SSID MAY CONTAIN ONE.** Anybody
  within radio range picks their own SSID, so a naive `split(':')` is a stranger
  deciding how many fields this console thinks it received. And **nothing may go
  through a shell**: every nmcli call is `fork`/`execvp` with an argv array,
  because a scan puts unvetted bytes from strangers into this process every time
  it runs.

- **ONE ROW PER NETWORK, NOT ONE PER ACCESS POINT.** A house with three mesh
  nodes broadcasts the same SSID three times and nmcli lists all three.

- **`/etc/cabinetos/session.env` IS NOT IN YOUR ENVIRONMENT OVER SSH.** The
  session script sources it; an SSH shell does not. Every check of first run is
  made over SSH, so `--first-run` reported "NEEDED" on a console that had been
  working for a day. The frontend now reads the file directly as well.

## Bluetooth

- **BLUEZ USES THE ADDRESS AS THE NAME when a device has not given one**, with
  dashes where the address has colons. An unnamed device does not have an empty
  name, it has a name that looks like one — and the controller list filled with
  SIXTEEN of the neighbours' beacons before anybody noticed.

- **`bluetoothctl pair` WITHOUT `trust` LOOKS EXACTLY LIKE A BROKEN PAD.** bluez
  refuses the incoming connection every time the controller wakes, so the pad
  pairs perfectly once and then never reconnects. It reads as "it keeps
  disconnecting" and has nothing to do with pairing.

- **A BLUETOOTH LE PAD CANNOT PAIR WHILE NO AGENT EXISTS, EVEN FOR "JUST
  WORKS".** `bluetoothctl pair` as a command registers none. Classic pads
  never ask, so every pad paired until a Series X|S pad (2026-10-10): "No
  agent available for request type 2", AuthenticationFailed, "Couldn't pair"
  four times. bluetoothd accepts a just-works pairing it was asked for BY
  ITSELF, but only after `new_auth` finds an agent (src/device.c,
  `device_confirm_passkey`), so a default agent that is merely there fixed
  it and was never called. Do not look for a "yes" line in the log as proof;
  look for the "No agent" line being gone. And an LE link that drops says
  "Reason.Timeout" and AuthenticationCanceled, not ConnectionAttemptFailed:
  the same flake in other words, which the retry must know.

- **A PAD TO TEST PAIRING WITH MUST BE ONE THE CONSOLE NEVER SAW.** A pad
  already bonded skips every step that fails.

## Pairing and QR codes

- **A QR CODE CANNOT BE CHECKED BY LOOKING AT IT** — a wrong one looks exactly
  like a right one. Check it three ways: module-for-module against a reference
  **with the mask forced**, a second reference to break ties, and a round trip
  through a real decoder. Each of the three found something the others did not.

- **THE MASK IS A LEGITIMATE DIFFERENCE BETWEEN ENCODERS.** Three
  implementations picked three different masks for the same string and all three
  are valid. So "matches a reference exactly" is not achievable across
  implementations — force the mask to compare the rest, and decode to settle it.

- **IN A BCH REMAINDER, TEST THE GENERATOR'S DEGREE, NOT THE FIELD'S WIDTH.**
  Testing bit 14 instead of bit 10 reduces nothing and puts **no error
  correction at all** in the format field. The data region was perfect and no
  scanner would read it.

- **THE QUIET ZONE IS NOT OPTIONAL AND IT IS THE RENDERER'S.** Measured: the
  same code drawn flush to the edge does not decode at all; with four modules of
  margin it decodes every time. It is the commonest reason a correct code will
  not scan.

- **`(6,8)` AND `(8,6)` ARE TIMING, NOT FORMAT.** They sit inside the format
  area's L shape and belong to the timing pattern. Reserving them blanks two
  modules of the timing line — a two-module difference in a 33x33 symbol that no
  scanner will accept.

- **`--romm-pair` ALWAYS PAIRS, AND WHOEVER APPROVES IS ADDED TO THE
  CONSOLE.** It records the person who approves the code as an account (or
  refreshes their token if they are already one): the token goes in
  `$HOME/.config/cabinetos/accounts/<id>.json` and the account list in the
  storage root's `config/accounts.json` (`accounts::recordPairing`,
  `probes.cpp`). To get a real pairing code without adding anyone to a
  console, point HOME somewhere empty and use a scratch root:
  ```
  HOME=/tmp/pairhome ./build/cabinetos-frontend --storage-root /tmp/pairroot \
    --romm <addr> --romm-pair
  ```
  Both land in the throwaway folders and nothing else on the console changes.

- **THE PAIRING URL IS THE SERVER'S, NOT A SHAPE YOU CAN GUESS.** It is
  `base + verification_path_complete` from `/api/auth/device/init`, and on RomM
  5.1.0 that is `/pair/device?user_code=…`. A fabricated one produces a QR that
  scans perfectly and lands on a page saying the code does not exist.

## Code

- **GLASS FADES ONLY WITH THE RENDERER'S CONTENT ALPHA, NOT A COLOUR'S.**
  `drawGlass` reads `contentAlpha_` for its blur and the tint's alpha only
  for the tint. Fade a glass element by multiplying its colours and the tint
  goes smoothly while the frosted blur stays at full strength to the last
  frame, then vanishes: "not fading out as a smooth element ... like it's in
  two phases" (the grid's letter index, 2026-10-10). Fade anything with glass
  in it through `setContentAlpha`, and put the old value back after.

- **A NEW PIECE OF UI IS BUILT FROM THE PARTS ALREADY ON SCREEN.** The letter
  index went through four looks in one evening ("hideous", "it just doesn't
  even look like it fits in it") because each was tuned as a new object. The
  one he accepted took every part from what exists: the browsing screens'
  regular glass, the top bar's focus capsule and dimming, Title 3, the focus
  duration. Read design.h's surfaces and the bar before drawing anything new.

- **A `void` FUNCTION THAT ENDS A SESSION TELLS NOBODY IT DID.**
  `Keyboard::pressKey` handled its own "done" and "cancel" keys internally, so
  driving the on-screen keyboard with a CONTROLLER and pressing A on "done"
  closed the panel and threw away what had been typed. The physical keyboard's
  Return worked, which is exactly why it survived. Three call sites doing the
  same job is what let one of them go unwired.

- **WALK EVERY COMBINATION RATHER THAN RE-READING THE RULES.** The state
  machine's exhaustive check (`--first-run-rules`) is 144 cases, needs
  nothing, and found a deadlock the code read as correct. Assert the
  REFUSALS: the happy path is the part that already works.

- **Every scripted edit must assert its anchor.** A patch that matches nothing
  leaves a green build with the fix absent.

- **A RomM ID SAYS NOTHING WITHOUT ITS KIND.** Platforms, collections and
  smart collections are counted separately, all from 1, and the Library
  matched tiles by id alone: a collection's "4 of 12" could land on platform
  2, and a grid's later pages fill the wrong tile (found 2026-10-12, #352,
  "RomM Smart Collections don't show up (only regular collections do)",
  when smart collection 2 joined collection 2). A tile is its query now
  (`Tile::query`).

- **A field added to the middle of a positional struct re-assigns the rest of
  the row.** `catalog.cpp`'s table is positional.

- **A picture drawn nearest-pixel must sit on whole screen pixels.** The
  game's rectangle was a fraction of a pixel wider than the screen effect
  drawn into it, so one column was drawn twice (x=1924 on a 4K TV) and a CRT
  mask shifted a pixel from there on. Invisible on the TV; Remote Play's
  shrink turned it into a green half and a purple half (#286, "Remote Play:
  stream the console to Moonlight (Sunshine), off by default"). Found by
  reading the mask's phase column by column in a 4K capture, after two wrong
  explanations; when a fault splits a picture down a line, look for the
  column where the pattern breaks.
