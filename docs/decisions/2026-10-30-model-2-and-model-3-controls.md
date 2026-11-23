# Model 2 and Model 3 games get Controls, from the next start

- **Decided:** A Model 2 game in sm2-emu or a Model 3 game in Supermodel has the Controls row of [arcade-controls](2026-10-25-arcade-controls.md) when its emulator's list gives it buttons: one row for each, "Button 1" on, as MAME 0.289's games have, and none for a driving game, whose list gives none. Everything else about Controls is the same, but a change plays from the game's next start, as a Picture change does: the pause menu says *Exit and relaunch to apply*.
- **Date:** 2026-10-30
- **Why:** Both are programs of their own. They hear where each button is only from the settings the console writes before they start, never from the console while they run, so a change can reach them only at a start. Neither names a game's buttons; their lists only count them, so the rows are numbered as MAME 0.289's are.
- **Numbers:** #617, "Arcade: a Controls row for Model 2 and Model 3 games".
- **Replaces:** none.
