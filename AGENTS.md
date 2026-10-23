# For AI coding tools

CabinetOS is a console operating system for RomM libraries. A human
maintainer reviews every pull request. Read these before changing anything:

1. `CONTRIBUTING.md`: building, testing, what CI checks, the project's rules.
2. `docs/ARCHITECTURE.md`: where each job lives and what each part must
   never do.
3. `docs/lessons/README.md`: the ten rules, then the lessons for your area.

Then follow "Rules for every change" in `CLAUDE.md`. They apply to every
tool, not only Claude.

## Before you write code

- **A small fix needs no issue:** a few lines that put back what was already
  meant to work, such as a typo on screen, a label cut off, or a crash from
  a missing check.
- **Anything that decides something new needs an issue the maintainer has
  agreed to first:** a feature, a setting, a changed default, a new
  emulator or system, a new file or folder, a new download or checksum, a
  change to the build or CI, a refactor across files. Ask your person to
  open the issue; you may draft it. Link it from the pull request.
- **A pin never moves without a named reason and its evidence.**
  `docs/decisions/2026-10-23-when-an-emulator-pin-moves.md` lists the five
  reasons. "It is old" or "there is a newer version" is not one.
- **No fix for one game.** One rule for every game.
- If unsure which of these applies, ask on an issue.

## Also

- Procedures for specific jobs are in `.claude/skills/`. They are plain
  Markdown: read the one that matches your task and follow it.
- This repository is public: no address, host name, password or home path of
  any machine goes in a file.
- Never write to a RomM server you do not own. `CONTRIBUTING.md`, "Running
  and testing", says how to test.
- Before saying a change is done, run the checks in `CLAUDE.md`, rule 6, and
  list every file you created, deleted or moved. If a check fails, say so.
  Never work around it.
- The person who sends a pull request answers for it.
