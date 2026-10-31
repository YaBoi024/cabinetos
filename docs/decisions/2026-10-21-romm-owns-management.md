# The console uses RomM, never manages it

- **Decided:** The console uses what RomM offers: it plays games from RomM and sends saves, states, screenshots and achievements to it. Looking at, sorting, sharing and deleting those things is RomM's job, so the console has no gallery, file list or manager of its own. A feature that makes something a person will want to look at or organise later waits until RomM can store it; the console never builds a stand-in.
- **Date:** 2026-10-21
- **Why:** The console is a way to play and to use what RomM offers, not a second place to manage a library. Keeping management in RomM means one place does it for every client: a RomM feature serves CabinetOS, Argosy and every other app, and none of them builds its own. It is why there is no screenshot gallery on the console, why gameplay recording waits for RomM to store clips (rommapp/romm#5135), and why the Arcade tile has no board filter: a RomM collection holds one board on its own.
- **Numbers:** docs/PROJECT.md question 53, "Gameplay recording, and clips in RomM"; PR #542, "PROJECT.md question 53: gameplay recording, paused until RomM stores clips"; #611, "Proposal, not decided: one Arcade tile for every arcade folder".
- **Replaces:** none.
