# Dirt Devils plays by a patch to Supermodel

- **Decided:** Supermodel is built with one patch of ours: a machine whose net board is linked to itself takes the master's path whatever its game is set to, so a game set as a slave cabinet, Dirt Devils, plays alone as a master does.
- **Date:** 2026-11-01
- **Why:** Linked to itself (model-3-net-board-linked-to-itself), a game set as a slave waits for itself forever, and Dirt Devils stopped at its network check. Supermodel declined the change: on the hardware the operator sets such a game to no link in its own menu. The console sets no game up from its test menu, and writing one game's setting in would be a fix for one game; the patch is one rule for every game linked to itself, and a game set as master runs as before. No Supermodel release will carry it, so it stays; the build stops when it no longer applies.
- **Numbers:** trzy/Supermodel#360, "A net game set as slave hangs when the net board is linked to itself (Dirt Devils)", declined 2026-10-30; #634, "Supermodel patch: a machine linked to itself runs as the only cabinet (Dirt Devils)", the patch's tracking issue; #635, "Supermodel: a machine linked to itself runs as the only cabinet, so Dirt Devils plays".
- **Replaces:** the call of 2026-10-29 to carry no patch and wait for Supermodel (model-3-net-board-linked-to-itself's Numbers).
