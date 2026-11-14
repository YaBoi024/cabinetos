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

**2026-11-11.** The newest release is 2026.10.10. The newest image promoted
to consoles is 2026.11.11.3: #697, "LinkUp: the friend's controller tilts
Switch and PS3 games, and the idle pause works again (#669)", merged on
2026-11-11 after his checks between the A9 and the mini PC (Mario Kart 8
Deluxe with both players tilting, LittleBigPlanet, a LinkUp game paused after
10 minutes with nothing pressed). It closed #669. Before it, 2026.11.11.2:
#696, "Gyro for Switch, Wii U and Vita: a stand-in DualSense carries the
player's tilt (#669)"; 2026.11.11: #695, "LinkUp: the friend's console
draws its own letterbox glow (#692)"; on 2026-11-10, 2026.11.10.3 (#689,
PS3 motion, #683), 2026.11.10.2 (#691, the Flathub pins) and 2026.11.10
(#685, Game Boy Advance and 3DS tilt, #670). Open:
#528 waits on RomM and #644 is the controller list. #590, "A
Wii Remote turned on after a Wii game starts is ignored by the game",
waits in Later; fix it only in the console's own code, never by patching
Dolphin (MMagTech, 2026-10-25). What the A9 runs now is not written here:
read it from the console before a test.
The machines' addresses are in his local notes. His standing rule: every
check and every comment is written out in full each time it is asked for,
never referred to.

**His rules from 2026-10-19 and 2026-10-20:** anything mechanical (a
measurement, a build, research) runs in an Opus session, never in the Fable
parent; an unattended run never waits on a prompt; decisions one at a time,
mechanics first, in a player's words, never with options he did not ask
for; a feature with hidden state is shown on the TV; no issue is opened
unless he asks. Every loop launch past a minute records time played on his
RomM: keep test launches under a minute, or have his go.

## Next

1. **LinkUp: two server tidy-ups, no issue.** The internet test, step 6 of
   #654, passed twice: his console and his friend's, two servers linking up
   and playing a game (his word, 2026-11-10). Left: `.dockerignore` lets
   `docs/` into the server's build stage, and the relay's block list lacks
   3fff::/20.
2. **Kirby's first save, inside Mario Kart 7's.** Before the folder fix,
   Kirby: Planet Robobot saved into Mario Kart 7's folder. Read on RomM on
   2026-10-25: Mario Kart 7's save (uploaded 2026-10-22) still carries
   Kirby's save data and its extra data, four files. He wants them gone
   (2026-10-20). Each Mario Kart 7 start unpacks RomM's copy over the
   folder, so deleting them on the A9 alone does not last: Mario Kart 7's
   save on RomM has to be replaced. Restate exactly what goes and wait for
   his yes before anything is deleted.
3. **Words from him, one at a time, no work:** which of the 36 faults the
   PROJECT.md scan found with no issue (2026-10-22) become issues, none
   filed until he says. That list is in the
   notes of the session that made it ("Faults recorded in PROJECT.md with no
   GitHub issue"). Ten were checked in the code and still hold; five wait on
   an emulator or library.

## Waiting, not for the next session unless MMagTech raises them

- **Needs the TV or a device:** #575, "Sinden light guns: plug in and
  play gun games (waiting on the guns)".
- **Waiting on others:** #297 (Sunshine), #346, #353 and #362
  (RomM's save channels; revisit mid-January 2027), #181, "Updates and DLC
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
