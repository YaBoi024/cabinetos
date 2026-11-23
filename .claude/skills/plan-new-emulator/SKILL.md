---
name: plan-new-emulator
description: Before a new emulator, core or system is built, settle every choice with the maintainer and list the work it brings, so the build starts with nothing left to decide. Use when adding an emulator, core or system is proposed.
argument-hint: [system or emulator]
model: opus
---

# Plan a new emulator

Adding an emulator, core or system is the most expensive work on this
project. The 3DS (#524) cost far more than any core before it because
choices were made during the build (docs/WORKING.md, "Keeping the cost
down"). This skill settles everything first and builds nothing. A new
emulator or system needs an issue MMagTech has agreed to (`AGENTS.md`); the
plan is recorded on that issue.

## First

- **The rules:** check it against CONTRIBUTING.md, "The rules". A rule it
  breaks ends the plan.
- **Whether it runs:** never say it will or will not run from memory.
  Launch it on the reference console, or read the pinned build's own
  history, and say which.

## Find out only what decides something

1. **Which emulator:** a libretro core or a program of its own, the
   candidates, and each one's own compatibility data.
2. **How RetroArch and Batocera build and set it up:** their build flags and
   the options they set are the starting point. Name every difference.
3. **Its Vulkan renderer**, and what it does without one.
4. **What the player must supply:** firmware, keys, and the formats RomM
   serves. A game fact such as a disc count comes from libretro-database's
   Redump files, never from memory.
5. **What it leans on that it does not promise:** settings keys, log lines,
   window titles. Each becomes a row in docs/ARCHITECTURE.md, "What leans on
   other projects", with a check (CONTRIBUTING.md, "Duties that come with
   some changes").
6. **Any change to its source** is named to MMagTech before it is built
   (docs/WORKING.md, "Patches to an emulator's code").

Research that would not change what gets built is left out, and offered for
later.

## Everything it ships with

All of it in one pull request: save states; RetroAchievements wherever
RetroAchievements supports the system; picture levels that aim at 4K
(Performance at the system's own size, Balanced about 1080p, Quality about
4K for the layout drawn), then measured on the reference console; and every
answer to the questions in docs/lessons/emulators.md, "The questions for
every core". An older rule that would leave a feature out is named to
MMagTech, never applied silently.

## The choices

List every choice the build needs, in a player's words: which emulator, the
controls, the screen layout, the picture levels, what plays and what does
not, what states promise across an emulator update, firmware, and anything
else the television shows. Put them to MMagTech one at a time, mechanics
first, each with your recommendation, and never with options he did not ask
for. A choice he leaves to you is decided and said. Record each answer on
the issue. A choice between real options also gets a decision file
(CLAUDE.md, rule 7).

## The plan

One list on the issue:
- the choices with his answers;
- everything it ships with;
- the duties in CONTRIBUTING.md, "Duties that come with some changes":
  licences, credits, the options list, the rows, and the controls pages
  when the controls change;
- the wiki page, and a changelog entry that says how to use it;
- the phases: research, build, television run, testing push. Each phase is
  a fresh session with a short brief that names the files and what done
  looks like;
- the checks for the television.

Nothing is built until every choice has an answer. A change after that is a
new piece of work.
