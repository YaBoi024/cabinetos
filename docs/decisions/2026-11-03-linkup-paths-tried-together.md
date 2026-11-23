# LinkUp tries every path together, and a player never picks one

- **Decided:** IPv6, a reachable IPv4 port (through UPnP or a port forwarded by hand), hole punching with STUN, and the relay last are tried together by WebRTC's connection setup (ICE). Direct or relay, and the delay, go to the log only, for reports: there is no pill for what the screen already shows. One reachable console should be enough for a pair; two behind carrier-grade NAT need IPv6 or a relay.
- **Date:** 2026-11-03
- **Why:** This is the standard order of ICE (RFC 8445), which Valve's peer-to-peer library also uses. Direct has the least delay and is the usual outcome: Tailscale reports more than nine in ten of its connections end up direct. The game itself does not go through the server, so a running game carries on if the server goes down, and only setting up a connection needs it.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** none.
