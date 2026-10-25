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

**2026-10-23.** The PS Vita is in and, in his words, good: PR #540, "PS Vita
through Vita3K, played as a PlayStation TV", merged 2026-10-21 (PROJECT.md
question 49 still says its hand test waits for him; that line is behind).
Merged since the 3DS of 2026-10-20: #533, "CI: a change to one emulator
rebuilds only that emulator"; #549, "A Nintendo Wii Remote counts once, not
as a pad and a Remote", and #552, "Wii games that need MotionPlus wait for a
TechKen, the only supported Wii Remote"; #550, "Sound: a built-in speaker
never takes the sound from the TV" (it closed #547, "Sounds plays only from
onboard speakers (Beelink ser9)"); #551, "One table of what leans on other
projects, with checks for the gaps"; #553, "PROJECT.md: gamescope's crash
when a game's window closes waits for the base"; #556, "Wii U: player 1 is
the GamePad, so Shovel Knight's first screen answers"; #558, "An archived
game is unpacked once, not at every start (3DS: 37 s to under a second)"
(it closed #530, "An archived game unpacks again at every start (3DS: up to
37 s)"); and #560, "Changelog: no game-led Wii U entry, and no date lines".
The newest image in the changelog is 2026.10.25.4: #582, "Controls: move
an arcade game's buttons from its pause menu", merged and promoted on
2026-10-25, judged on the TV through the loop. Before it, 2026.10.25.2: #580,
"Hold Start in an arcade game to open its own settings (#577)", merged and
promoted on 2026-10-25, judged on the TV and checked on the testing image.
Before it,
2026.10.25: #576, "The 8BitDo Arcade Stick plays arcade games with a
fight-stick layout", merged and promoted. What the A9
runs now is not written here: read it from the console before a test. The
Azahar patches each have a tracking issue linked to a request upstream: #534,
"Azahar patch: each game sets its own folders (until Azahar fixes
SetUserPath)" (azahar-emu/azahar#2694), and #537, "Azahar patch: games get no
network (until Azahar has an option)" (azahar-emu/azahar#2695). The machines'
addresses are in his local notes. His standing rule: every check and every
comment is written out in full each time it is asked for, never referred to.

**His rules from 2026-10-19 and 2026-10-20:** anything mechanical (a
measurement, a build, research) runs in an Opus session, never in the Fable
parent; an unattended run never waits on a prompt; decisions one at a time,
mechanics first, in a player's words, never with options he did not ask
for; a feature with hidden state is shown on the TV; no issue is opened
unless he asks. Every loop launch past a minute records time played on his
RomM: keep test launches under a minute, or have his go.

## Next

1. **The pause menu's "Options" row, as a visual test for him** (his idea of
   2026-10-25, not a decision): the top level the six actions (Resume, Save
   state, Load latest state, Screenshot, Options, Exit to Home; Change disc
   stays at the top), and Options holding Look, Picture, Widescreen,
   Overlays, Screen, Colors and Controls. A level of the pause menu is drawn
   as the menu's own rows in its panel (`ChoiceScreen::setOverPauseMenu`,
   #582); the list panel holds one stepper row today, so it must learn
   several. He may not keep it.
2. **Stop at an out-of-range array write, at the line that does it.**
   **Not before his weekly limit resets (Saturday 2026-10-24, about 9 PM
   Eastern; MMagTech, 2026-10-21).** Estimated 300k to 500k Opus tokens
   (1.2 to 2 weekly points) if no check fires; more if one does.
   The crash of 2026-10-20 was a write past a two-entry array that landed
   silently on the grid screen next to it. Build the frontend with
   `-D_GLIBCXX_ASSERTIONS` and make the fixed arrays indexed by a value
   set at run time `std::array` (`pillFocus_` in `libraryscreen.h`,
   `slotFocus` and `slotPress` in `app.h`, and the others a grep finds),
   so a bad index stops the program and names the line instead of
   damaging what sits beside it. Measure the frame times on Home and a
   large grid on the A9 before and after; if the cost shows, stop and
   say so. One PR, CI green, his word to merge.
3. **The new checks turn strict on 2026-10-31**: Docs list and Decisions
   (`checks.yml`). Watch the first pull requests after. The 60-line cap on a
   decision file and the 5-line growth allowance for PROJECT.md are one
   number each in `ci/check-decisions.sh`. Decisions are one small file each
   in `docs/decisions/` since 2026-10-23 (CLAUDE.md, rule 7); PROJECT.md is
   frozen, and an old decision moves out when a session relies on it. Moving
   all of it at once was priced at about 4 million Sonnet tokens, about 12%
   of a week, and is left for a later call of his.
4. **36 faults the PROJECT.md scan found with no issue** (2026-10-22): which
   become issues is his call, one at a time; none is filed until he says.
   The list is in the notes of the session that made it ("Faults recorded in
   PROJECT.md with no GitHub issue"). Ten were checked in the code and still
   hold; five wait on an emulator or library.
5. **Kirby's first save, inside Mario Kart 7's.** Before the folder fix,
   Kirby: Planet Robobot saved into Mario Kart 7's folder, so Mario Kart
   7's save on his RomM still carries Kirby's first save (4 files). He
   wants it gone (2026-10-20). Each Mario Kart 7 start unpacks RomM's copy
   over the folder, so deleting it on the A9 alone does not last: Mario
   Kart 7's save on RomM has to be replaced. Restate exactly what goes and
   wait for his yes before anything is deleted.
6. **Words from him, one at a time, no work:** #181, "Updates and DLC as
   separate files in RomM (Switch, PS3, Xbox 360)", and #215, "Proposal, not
   decided: opt-in "Help improve CabinetOS" report sharing" (he is unsure of
   both).
7. **The Vectrex stall:** no fix; the next step is his evening journal of
   2026-10-19 read for what ran just before each stall (PROJECT.md, the
   Vectrex record under "The 4K misses measured again"). Only if it comes
   back.
8. **The automatic checks turn strict on 2026-10-26**: Doc paths, RomM paths,
   new warnings in touched files, the self-tests, and the Wii bridge pacing
   test. The last two run in the frontend job the image build waits on, so a
   failure there also stops an image. Nothing to do but watch the first pull
   request after.
9. **The monthly health check.** With the next real core change: #490's
   comments in the core and PCSX2 build files.
10. **Merged branches to delete, on his go naming them:** nine in the main
   checkout, `claude/3ds-azahar-core-9a119a`, `claude/3ds-hand-test-2414d9`,
   `claude/3ds-parity-research-f666db`, `claude/3ds-save-fix-57a0b1`,
   `claude/3ds-shader-cache-3c0de8`, `claude/3ds-site-count-5d7227`,
   `claude/3ds-switch-crash-d2c41b`, `claude/nintendo-remotes-5af3d9` and
   `claude/touchpad-survey-afccf9`; check each for a copy on GitHub first.

## Waiting, not for the next session unless MMagTech raises them

- **Needs the TV or a device:** #473 (a press while an emulator of its own
  has the screen), #411 (Moonlight), #290 (the
  C1, before a release), and #228 (the measurement in PROJECT.md section
  46 when the sound dies again).
- **Waiting on others:** #297 and #390 (Sunshine), #346, #353 and #362
  (RomM's save channels; revisit mid-January 2027), #545, the RomM
  token revoke (rommapp/romm#5274; the code is PR #528),
  eden-emulator/Issue-Reports#739 (a Switch controller connected mid-game),
  #203 closed until libretro/dolphin#508 moves.
- **Filed, not scheduled:** #557, "No emulator reaches the internet: one
  block for all of them (Wii's slow Mario Kart exit, 3DS)"; #559, "A
  power cut right after a download or an unpack can leave a damaged game
  that the console keeps (exFAT and NTFS drives)"; and #583, "During a PS2
  game the console's own loop runs at about 250 frames a second instead of
  60".
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
