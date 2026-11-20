# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `CONTRIBUTING.md`, then `docs/ARCHITECTURE.md`.
`docs/decisions/` is what is decided and why, one small file each, with an
index; `docs/PROJECT.md` is the older record, frozen on 2026-10-23: look up
the question in hand (its index, at the top, gives every section and
numbered question with where it ended), never read it through. Name every
issue and pull request by
its title as well as its number, every time. Replies to reporters are in
MMagTech's voice ("I"), ask them to test, and never claim a fix nobody
reproduced. RomM's documentation together with RomM's behaviour is the
source of truth for anything on RomM's data.

## Where things stand

**2026-11-18.** The newest release is 2026.11.13.2. The newest image
promoted to consoles is 2026.11.18: #721, "System update holds back an
update the server's RomM is too old for", merged on 2026-11-18.
docs/wiki/changelog.md lists
every image before it. Open: #528 waits on RomM. #590, "A Wii Remote turned
on after a Wii game starts is ignored by the game", waits in Later; fix it
only in the console's own code, never by patching Dolphin (MMagTech,
2026-10-25). What the A9 runs now is not written here: read it from the
console before a test. The machines' addresses are in his local notes. His
standing rule: every check and every comment is written out in full each
time it is asked for, never referred to.

**His rules from 2026-10-19 and 2026-10-20:** anything mechanical (a
measurement, a build, research) runs in an Opus session, never in the Fable
parent; an unattended run never waits on a prompt; decisions one at a time,
mechanics first, in a player's words, never with options he did not ask
for; a feature with hidden state is shown on the TV; no issue is opened
unless he asks. Every loop launch past a minute records time played on his
RomM: keep test launches under a minute, or have his go.

## Next

1. **Save channels, #346, "Move saves to RomM's save channels (Sigil)".**
   Built in the eleven steps of the plan posted on the issue on 2026-11-17,
   every step approved; nothing in it is decided again. Step 1, System
   update holding back an update that needs a newer RomM than the server's,
   went out alone in 2026.11.18 (PR #721), since only a version already on
   a console can hold back the next one. Steps 2 to 11 go on one draft pull
   request, #722, "Save channels: the console's saves move to RomM's
   channels (#346, steps 2 to 11)", cut from main. Steps 2 to 6 are built
   there: the RomM client learning channels and snapshots; what the
   console remembers about each game's save (the channels as last seen,
   the snapshot it holds, one owed marker per game); starting a game,
   which continues from the newest save RomM holds that the console can
   use, in a save channel or from before them, says "Continuing from
   another device" when that save was made elsewhere, and keeps a save of
   its own aside before replacing it; leaving a game, whose save goes up
   as one snapshot once the game has closed, into the channel it started
   from, put on top of another device's only when it was made later, and
   never into a hardcore channel; and save states, which go up inside that
   snapshot in slots 1 to 3: the TV shows and a push names the three newest
   of its own states wherever they are on RomM, as RomM's web player
   chooses, and states made alone go into the snapshot on top, beside
   another device's newer save (step 6 and its second pass). Before step 7,
   a step of its own, MMagTech's of 2026-11-19, is built on #722 too: the
   save's own picture, taken about a second after the game's save stops
   changing and sent with that save, as RomM's web player and Argosy do;
   Sega CD, Neo Geo Pocket Color, 3DO, Xbox and the arcade boards get none,
   since their emulators write the save only as the game closes. Step 7 is
   built: a game's save from before the update moves into a channel the
   first time the game is started, RomM copying the old row in itself and
   leaving it as it was, with the console's own copies of the game's states
   from before the update; a save still owed from before the update goes
   up as the game's snapshot. Step 8 is built: a save another app made
   (RomM's web player, Argosy, Ludo), in a channel or in none, is asked of
   RomM in the console's own form when it is the newest, checked (size,
   the format's marks, a save in it), and used with "Continuing from
   another device", or refused with "Couldn't use the newest save" and the
   newest the console can use; DS, Game Boy Advance and the Mega Drive
   family only from the same emulator; every push names the core in
   Sigil's spelling; Dreamcast keeps one card per game in the game's
   folder; Wii and 3DS saves go up in Sigil's shape. Step 8's two
   questions are settled (2026-11-20): another app's save in no channel is
   not copied into a channel at the start, since an app that saves outside
   channels is that app's to move onto them (MMagTech); and the pill shows
   for such a save even when it names no device, the same message as any
   save made elsewhere (left to the session, judged on the TV). Next is
   step 9, offline and coming back.
   Each step is a fresh
   session that keeps to CLAUDE.md's seven rules and its piece's files, reuses, removals, docs and
   tests in the plan: Fable for steps 4 and 5 (starting and leaving a
   game, the two that can lose a save; his choice, 2026-11-18), Opus for
   the rest. Step 11 is #353's cartridge clocks and Saturn
   cartridge; its arcade high scores wait.
2. **LinkUp: two server tidy-ups, no issue.** The internet test, step 6 of
   #654, passed twice: his console and his friend's, two servers linking up
   and playing a game (his word, 2026-11-10). Left: `.dockerignore` lets
   `docs/` into the server's build stage, and the relay's block list lacks
   3fff::/20.
3. **Kirby's first save, inside Mario Kart 7's.** Before the folder fix,
   Kirby: Planet Robobot saved into Mario Kart 7's folder. Read on RomM on
   2026-10-25: Mario Kart 7's save (uploaded 2026-10-22) still carries
   Kirby's save data and its extra data, four files. He wants them gone
   (2026-10-20). Each Mario Kart 7 start unpacks RomM's copy over the
   folder, so deleting them on the A9 alone does not last: Mario Kart 7's
   save on RomM has to be replaced. Restate exactly what goes and wait for
   his yes before anything is deleted.
4. **Words from him, one at a time, no work:** which of the 36 faults the
   PROJECT.md scan found with no issue (2026-10-22) become issues, none
   filed until he says. That list is in the
   notes of the session that made it ("Faults recorded in PROJECT.md with no
   GitHub issue"). Ten were checked in the code and still hold; five wait on
   an emulator or library.

## Waiting, not for the next session unless MMagTech raises them

- **Needs the TV or a device:** #575, "Sinden light guns: plug in and
  play gun games (waiting on the guns)".
- **Waiting on others:** #297 (Sunshine), the arcade high scores of #353,
  "Three kinds of save never reach RomM: Saturn cartridge, arcade high
  scores, cartridge clocks", and #362, "Discuss later: sharing a save with
  another person on the server" (revisit mid-January 2027), #181, "Updates and DLC
  as separate files in RomM (Switch, PS3, Xbox 360)" (kept open on
  2026-11-11 to wait for RomM 5.5), #545 and PR #528 (the
  RomM token revoke, rommapp/romm#5274), #534 (the Azahar patch,
  azahar-emu/azahar#2694),
  eden-emulator/Issue-Reports#739 (a Switch controller connected mid-game),
  #203 closed until libretro/dolphin#508 moves.
- **Filed, not scheduled:** #675, "Arcade: a Naomi or Atomiswave game with
  a plain joystick moves only on the d-pad", the fix agreed in principle on
  2026-10-28 and written on the issue.
- **With the next real core change:** the comments #490, "Code comments
  that say what the code no longer does, and machine details left in
  comments", left in the core and PCSX2 build files and `core*.cpp`, kept
  so players do not download those layers again for comments alone. Still
  stale on 2026-10-25; the list is the issue's checklist.
- **The Vectrex stall:** no fix. Only if it comes back: his evening journal
  of 2026-10-19 read for what ran just before each stall (PROJECT.md, the
  Vectrex record under "The 4K misses measured again").
- **PROJECT.md moved out all at once:** priced at about 4 million Sonnet
  tokens, about 12% of a week, and left for a later call of his. Until
  then an old decision moves out when a session relies on it.
- **An idea of his, 2026-10-19, not scheduled:** a channel-change animation
  when a game starts and when it exits, on the console's own layers only,
  silent, no setting; pause stays instant.
- **The game page's launch messages, later** (MMagTech, 2026-10-19, at the
  TV): "Disc can't be read" sits as a loose grey line under Download, as do
  all ten or so reasons Play or Download fails. He wants their placement
  (the Play row's right end, in amber, was offered; or the bottom pill) and
  their wording reviewed together, with a way to cycle through each on the
  TV first, like `--notice-gallery`. Not an issue.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
