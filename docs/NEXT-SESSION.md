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

**2026-11-09.** The newest release is 2026.10.10. The newest image promoted
to consoles is 2026.11.09.5: #678, "Wi-Fi: Forget removes a network saved
twice (#642)", carrying #677, "System update row: "under 1 MB" instead of
"0.0 MB", and no size when it is not known" (#672), merged on 2026-11-09
after his TV checks. Before it the same day: #671, "On-screen keyboard: a
held direction repeats, Shift locks, and the field has a cursor"
(2026.11.09.4), and #666, "LinkUp: the console's side, step 3 of #654"
(2026.11.09.3); LinkUp is on the wiki, in the README and in the changelog
(#673, #674), and every wiki page and the README were read against the
code on 2026-11-09, the lines found wrong fixed in #679, "Wiki: fix the
lines the docs review found wrong, stale or missing", and #680, "Controls
guide: LinkUp in the top bar, the keyboard's Shift lock, the date". Open:
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

1. **LinkUp, step 6 of #654, "LinkUp: play a game with a friend on another
   CabinetOS console over the internet": across the internet.** Steps 1 to 5
   are done and recorded on the issue; the server is cabinet-linkup v0.1.1,
   pinned in `ci/linkup-server.txt`. First his friend, on latest 2026.11.09.5,
   against his own server, whose Relay tab test passed on 2026-11-09; then the
   two testers in other homes. The result goes on #654 (how each game
   connected, direct or relay, from the server's admin page); nothing is filed
   from it unless he asks. Beside it: cabinet-linkup#7, the server's README
   head, merges on his word; two server tidy-ups with no issue,
   `.dockerignore` lets `docs/` into the build stage and the relay's block
   list lacks 3fff::/20.
2. **Gyro, beside item 1, which waits on his friend and holds up nothing
   here but the last session; in this order, each a fresh session that
   starts from its issue and builds nothing the issue does not say:** #670, "Tilt
   for the Game Boy Advance and 3DS cores: answer libretro's sensor
   interface" (Opus, one session; TV: WarioWare: Twisted!, Ocarina of Time
   3D, Star Fox 64 3D); then #683, "Motion for PS3: a sensor device beside
   each player's virtual pad" (Opus, one session; TV: LittleBigPlanet); then
   #669, "Gyro for Switch, Wii U and Vita: a virtual pad that carries the
   player's tilt", in two sessions: the fake pad proven with one Switch game
   in Eden (Fable, decided 2026-11-09, the one session where a wrong turn
   costs most; no TV sitting, the proof is the log), then the three emulators
   (Opus; TV: Mario Kart 8 Deluxe, Captain Toad, Breath of the Wild's shrines,
   Gravity Rush); last, the friend's tilt over LinkUp for Switch and PS3
   (Opus, one session, two consoles; the friend needs a gyro pad of their
   own). If it lands before item 1's sitting, that sitting tests both, as
   two steps, the connection first; if not, the tilt gets its own short
   test later. Settled 2026-11-09: Game Boy and Game
   Boy Color stay on Gambatte and the two MBC7 cartridges stay out; no
   greying for a few games; each issue carries its files and docs under
   #398's rules. His DualSense is the gyro pad for every sitting.
3. **Kirby's first save, inside Mario Kart 7's.** Before the folder fix,
   Kirby: Planet Robobot saved into Mario Kart 7's folder. Read on RomM on
   2026-10-25: Mario Kart 7's save (uploaded 2026-10-22) still carries
   Kirby's save data and its extra data, four files. He wants them gone
   (2026-10-20). Each Mario Kart 7 start unpacks RomM's copy over the
   folder, so deleting them on the A9 alone does not last: Mario Kart 7's
   save on RomM has to be replaced. Restate exactly what goes and wait for
   his yes before anything is deleted.
4. **Words from him, one at a time, no work:** #181, "Updates and DLC as
   separate files in RomM (Switch, PS3, Xbox 360)", and #215, "Proposal, not
   decided: opt-in "Help improve CabinetOS" report sharing" (he is unsure of
   both); and which of the 36 faults the PROJECT.md scan found with no issue
   (2026-10-22) become issues, none filed until he says. That list is in the
   notes of the session that made it ("Faults recorded in PROJECT.md with no
   GitHub issue"). Ten were checked in the code and still hold; five wait on
   an emulator or library.

## Waiting, not for the next session unless MMagTech raises them

- **Needs the TV or a device:** #473 (a press while an emulator of its own
  has the screen), #290 (the
  C1, before a release), and #575, "Sinden light guns: plug in and
  play gun games (waiting on the guns)".
- **Waiting on others:** #297 and #390 (Sunshine), #346, #353 and #362
  (RomM's save channels; revisit mid-January 2027), #545 and PR #528 (the
  RomM token revoke, rommapp/romm#5274), #534 (the Azahar patch,
  azahar-emu/azahar#2694),
  eden-emulator/Issue-Reports#739 (a Switch controller connected mid-game),
  #203 closed until libretro/dolphin#508 moves.
- **Filed, not scheduled:** #559, "A power cut right after a download or an
  unpack can leave a damaged game that the console keeps (exFAT and NTFS
  drives)", and #675, "Arcade: a Naomi or Atomiswave game with a plain
  joystick moves only on the d-pad", the fix agreed in principle on
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
