# Change server with another server's address asks for that server's code

- **Decided:** When the address typed in Change server is another server's, the console asks for that server's code on the same Code screen as Add a server, with the address under it, then pairs with it and forgets the old server. If the code is cancelled or wrong, the old server stays as it was. The same server at a new address still needs no code, and an address where nothing answers yet is kept, as before.
- **Date:** 2026-11-08
- **Why:** Change server took any address and only swapped it, keeping the old server's pairing, so another server's address was taken silently and LinkUp stopped working with nothing on the screen to say why. The console can tell the two apart without a code: only the server it paired with can open a session under the key it remembers. Asking for the code on the screen Add a server already has keeps it to words a player has seen.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** none.
