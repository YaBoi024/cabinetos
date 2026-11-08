# LinkUp's program maps its own port, with libnice's UPnP off

- **Decided:** The program asks the router for its port itself, with a lease it renews, and libnice's own UPnP is off. The ICE agent is held to LinkUp's fixed ports (`min-rtp-port`, `max-rtp-port`), with ICE over TCP off.
- **Date:** 2026-11-04
- **Why:** On the test VM, libnice asked a router for a permanent lease, made one mapping for every local address including IPv6, left the mappings behind when the program died, and opened UDP 1900 and random ports outside the fixed range. With its UPnP off and the agent held to the fixed ports, only those ports are open. The firewall lock names the fixed ports for incoming traffic and drops them for every program, not only LinkUp's, so they are ports nothing else on the console uses.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** none.
