# Model 3's net board is on, linked to itself

- **Decided:** Supermodel runs every Model 3 game with its net board on, linked to itself: it listens on port 1970 and connects to 127.0.0.1:1970, inside the empty network every emulator is started in. This in place of turning the board off (`-no-net`), which the arcade build plan of 2026-10-28 named.
- **Date:** 2026-10-29
- **Why:** With no net board, Scud Race and Daytona USA 2, set up as linked twin cabinets, loop at "Network board not present" and never reach a race. Linked to itself, the board is Supermodel's own way to run a linked cabinet alone, and both play. The sandbox, not the flag, keeps it off the network: nothing outside it sees the port. On the reference console on 2026-10-29, 15 of the 17 Model 3 games in the reference library reached gameplay this way, against 14 with the board off: Dirt Devils, whose own setting makes it a slave cabinet, waits for itself forever this way, where it plays with the board off.
- **Numbers:** #616, "Arcade: Sega Model 3 games play in Supermodel"; trzy/Supermodel#360, "A net game set as slave hangs when the net board is linked to itself (Dirt Devils)", the change asked of Supermodel instead of a patch of ours.
- **Replaces:** none; the plan's `-no-net` never shipped.
