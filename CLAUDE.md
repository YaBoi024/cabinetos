# CabinetOS, for coding sessions

CabinetOS is a console operating system for RomM libraries: a bootc image on
Bazzite that boots into one C++ program, the frontend in `frontend/`, which
draws every screen and hosts the emulators. Everything comes from the
player's RomM server.

## Read first

For a session working with MMagTech; an outside contributor needs only the
first three.

1. [CONTRIBUTING.md](CONTRIBUTING.md): building, testing, what CI checks,
   how a change reaches a console, and the project's rules.
2. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the map of the code, what
   each part owns and must never do, the main flows, where to look.
3. [docs/lessons/README.md](docs/lessons/README.md): the ten rules, then the
   lessons file for the area in hand.
4. [docs/NEXT-SESSION.md](docs/NEXT-SESSION.md): what is being worked on now.
5. [docs/WORKING.md](docs/WORKING.md): how MMagTech works: the loop, the
   testing branch, merges, judging on the television.

## Build, run, test

- Nothing builds on a Mac and the console's image has no compiler. The
  frontend builds in the `cabinetos-builder` container on Linux
  (CONTRIBUTING.md, "Building"): in CI, or in that container on the
  reference console in the maintainer's loop (`tools/ui-loop.sh`).
- Headless runs and the self-tests: CONTRIBUTING.md, "Running and testing".
- Never write to a RomM server you do not own. Test through
  `tools/romm-readonly-relay.py` (its server comes from
  `CABINETOS_RELAY_UP`). A test that has to write goes to the project's
  write-test server in `CABINETOS_TEST_ROMM`, and only there. The tools
  take every machine from the environment (CONTRIBUTING.md, "Running and
  testing").

## Rules for every change

MMagTech's seven, for this code:

1. **Search before you write.** Before a new function or file, grep for one
   that already does the job (`frontend/src`, and `build_files/`, `ci/` and
   `system_files/` for scripts): whole files (`fileio.h`), running a program
   (`proc.h`), where things live (`storage.h`), RomM (`romm.h`), the design's
   numbers (`design.h`), a worker polled once a frame (`job.h`). Use or
   extend it. A new file is named in the pull request, with why.
2. **New code goes where docs/ARCHITECTURE.md puts its job.** A new
   self-contained job gets a file of its own, named for the job, never a few
   hundred lines added to a file that does something else. A new kind of
   place (a folder, a program, a part ARCHITECTURE.md does not name) is
   asked about before it is made: ask MMagTech, or on an issue. A new file
   under docs/ is asked about first, and its one job goes in the first line
   under its title; a decision file in docs/decisions/ (rule 7) is the one
   exception.
3. **Remove what you replace**, in the same change: the old function, its
   declaration, its callers, its flag, its comment. If it must stay, say why
   in the pull request.
4. **No commented-out code**, and no `#if 0`. Git keeps old code.
5. **Docs change with the code.** A change that adds, removes, renames or
   moves a file, or changes who calls whom, updates docs/ARCHITECTURE.md in
   the same pull request. A comment that names a file, a line or a function
   is updated when that thing moves: a stale comment reads exactly like a
   current one (lessons rule 8). A change that makes a line in any doc untrue
   rewrites or deletes that line in the same pull request. It does not add a
   note beside it.
6. **Before saying it is done:** the frontend builds in the builder with no
   new warnings in the files you touched (CI lists every warning in the
   Compile job's summary; there is no count to compare yet);
   shellcheck passes on every script you touched;
   `cabinetos-frontend --check-option-tables cores/options` passes if you
   touched a core or an option; the self-tests your change touches pass;
   each name the unused-code report (the Compile job's summary) lists in the
   files you touched is removed, or kept with the reason in the pull request.
   If you touched docs/PROJECT.md, the pull request says what you added,
   what you removed, and that none of it is a fault.
   List every file created, deleted or moved, and check the docs still
   match. If a check fails, say so; never work around it.
7. **Decisions live in docs/decisions/, one small file each.** Write one
   when you or MMagTech chose between real options and a later reader would
   ask why it is this way: what was decided, the date, why, the issue or
   pull request numbers, and what it replaces. docs/decisions/README.md has
   the template and the index. A decision file holds no fault, no research,
   no build log and no how-it-works. docs/PROJECT.md is the old record and
   is frozen: nothing new goes in it. When your work relies on a decision in
   it, move that decision into a file of its own and replace its text there
   with a one-line pointer. A fault you find and do not fix goes in your
   closing message in issue form (what a player notices, where, how found);
   MMagTech decides whether it becomes an issue, and no session files it
   itself.

The project's own rules (the console connects only to fixed places, no em
dashes on screen, never rely on Steam, Vulkan first, one rule for every
game, and the rest) are in
CONTRIBUTING.md, "The rules". MMagTech's working rules for sessions are in
docs/WORKING.md and docs/NEXT-SESSION.md.

## Machine details

This repository is public, so no new address, host name, password or home
path of anybody's machines goes in a tracked file. The ones still there are
being moved out (docs/ARCHITECTURE.md, "Known gaps"). The maintainer's are in
a file in his home folder, loaded here:

@~/.claude/cabinetos.local.md

[CLAUDE.local.example.md](CLAUDE.local.example.md) says what goes in it. A
session without that file loses nothing but those addresses.
