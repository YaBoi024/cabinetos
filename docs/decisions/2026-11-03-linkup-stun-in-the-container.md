# LinkUp's STUN list lives in the server, never in the OS

- **Decided:** The list of STUN addresses is held by the server's container, Cloudflare then Google by default. The owner can add their own, set the order and remove any, and Reset restores the defaults. The console uses the first that answers.
- **Date:** 2026-11-03
- **Why:** A change to the list never needs an OS update. `webrtcbin`'s `stun-server` takes one server, so the list has an order rather than equal entries. Cloudflare answered in 8 ms against Google's 21 ms from one home machine, and Cloudflare documents its STUN as free and unlimited; no such page was found for Google. The container cannot be the only STUN, because a console on the same network as its server cannot learn its outside address from it.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet".
- **Replaces:** none.
