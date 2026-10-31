# Rewind keeps nothing while in-game shortcuts are off

- **Decided:** A game keeps rewind's snapshots only while In-game shortcuts
  are on, since the shortcut button with L2 is the only way to rewind. With
  the switch off it keeps none; with it on, rewind works as before, on every
  system with save states but the 3DS. Rewind has no switch of its own: a
  player who turns shortcuts on for the state and fast forward shortcuts
  gets rewind with them.
- **Date:** 2026-10-29
- **Why:** In-game shortcuts are off by default, and every console still
  made a whole save state twice a second of play for a rewind nobody could
  start. On the A9 one state runs from under 1 KB (Atari 2600) to more than
  100 MB (a Dreamcast game, a CPS3 game in MAME 0.289). A Rewind switch of
  its own was weighed and not built: it asks a player to manage a cost they
  cannot see, and it is one more setting for a few. It is looked at again
  if players ask for the other shortcuts without rewind.
- **Numbers:** PR #609, "Rewind keeps nothing while in-game shortcuts are
  off".
- **Replaces:** none. docs/PROJECT.md, "Rewind is for getting back, not for
  scrubbing" (2026-09-27), still decides what rewind keeps and how it
  steps back.
