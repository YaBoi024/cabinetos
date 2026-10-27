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

**2026-10-26.** The newest release is 2026.10.10. The newest image promoted
to consoles is 2026.10.27: #595, "No emulator reaches the internet: one
block for all of them (#557)", merged on 2026-10-26 after his TV checks
(Mario Kart Wii's download refused and its exit at once, Shovel Knight's
pause and exit). Before it, 2026.10.26.3: #591, "Sound comes back after the
TV sleeps: a backup output while the TV's is gone". Nothing else is in
flight. #590, "A Wii Remote turned
on after a Wii game starts is ignored by the game", waits in Later; fix it
only in the console's own code, never by patching Dolphin (MMagTech,
2026-10-25). What the A9 runs now is not written here: read it from
the console before a test.
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

1. **Mario Kart Wii on the TV, with no stream (his check, asked for
   2026-10-26).** Over a Moonlight stream on 2026.10.27 its races skipped
   104 game frames per 10 s on average (worst 293) and he heard the sound
   crackle; on the TV alone before, 1 to 2 (worst 13). Mario Kart DS and
   Mario Kart 7 were fine over the same stream. His steps: play a race of
   Mario Kart Wii on the TV, with Moonlight not connected, for a few
   minutes. Success: smooth sound. Then read the `[pace]` lines of that run
   (game frames skipped near the old 1 to 2). Smooth means the stream is
   the cause, and nothing is opened unless he asks; crackling means it is
   Wii at 5x on the A9, also his call.
2. **Stop at an out-of-range array write, at the line that does it.**
   Estimated 300k to 500k Opus tokens (1.2 to 2 weekly points) if no check
   fires; more if one does.
   The crash of 2026-10-20 was a write past a two-entry array that landed
   silently on the grid screen next to it. Build the frontend with
   `-D_GLIBCXX_ASSERTIONS` and make the fixed arrays indexed by a value
   set at run time `std::array` (`pillFocus_` in `libraryscreen.h`,
   `slotFocus` and `slotPress` in `app.h`, and the others a grep finds),
   so a bad index stops the program and names the line instead of
   damaging what sits beside it. Measure the frame times on Home and a
   large grid on the A9 before and after; if the cost shows, stop and
   say so. One PR, CI green, his word to merge.
3. **The report-only checks turn strict.** From 2026-10-25, 8 PM Eastern
   (2026-10-26 UTC): Doc paths, RomM paths and the other checks in
   `checks.yml`, and in the Compile job new warnings in touched files, the
   self-tests and the Wii bridge pacing test. The last two run in the
   frontend job the image build waits on, so a failure there also stops an
   image. From 2026-10-31, 8 PM Eastern (2026-11-01 UTC): Docs list and
   Decisions, which have their own date in `checks.yml`; the 60-line cap on
   a decision file and the 5-line growth allowance for PROJECT.md are one
   number each in `ci/check-decisions.sh`. Nothing to do but watch the first
   pull requests after each.
4. **Kirby's first save, inside Mario Kart 7's.** Before the folder fix,
   Kirby: Planet Robobot saved into Mario Kart 7's folder. Read on RomM on
   2026-10-25: Mario Kart 7's save (uploaded 2026-10-22) still carries
   Kirby's save data and its extra data, four files. He wants them gone
   (2026-10-20). Each Mario Kart 7 start unpacks RomM's copy over the
   folder, so deleting them on the A9 alone does not last: Mario Kart 7's
   save on RomM has to be replaced. Restate exactly what goes and wait for
   his yes before anything is deleted.
5. **Merged branches to delete, on his go naming them.** Eleven in the main
   checkout, all merged: `claude/3ds-azahar-core-9a119a`,
   `claude/3ds-hand-test-2414d9`, `claude/3ds-parity-research-f666db`,
   `claude/3ds-save-fix-57a0b1`, `claude/3ds-shader-cache-3c0de8`,
   `claude/3ds-site-count-5d7227`, `claude/3ds-switch-crash-d2c41b`,
   `claude/nintendo-remotes-5af3d9`, `claude/touchpad-survey-afccf9`,
   `claude/arcade-stick-layout-9148b6` and `claude/next-session-2026-10-23`.
   On GitHub, one: `claude/next-session-2026-10-23`. Checked 2026-10-25;
   check again first.
6. **Words from him, one at a time, no work:** #181, "Updates and DLC as
   separate files in RomM (Switch, PS3, Xbox 360)", and #215, "Proposal, not
   decided: opt-in "Help improve CabinetOS" report sharing" (he is unsure of
   both); and which of the 36 faults the PROJECT.md scan found with no issue
   (2026-10-22) become issues, none filed until he says. That list is in the
   notes of the session that made it ("Faults recorded in PROJECT.md with no
   GitHub issue"). Ten were checked in the code and still hold; five wait on
   an emulator or library.

## Waiting, not for the next session unless MMagTech raises them

- **Needs the TV or a device:** #473 (a press while an emulator of its own
  has the screen), #411 (Moonlight), #290 (the
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
  drives)".
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
