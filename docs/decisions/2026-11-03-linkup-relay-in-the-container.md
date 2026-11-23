# LinkUp's relay is a program in the server, off until its owner turns it on

- **Decided:** The relay is a program inside the server's container, off by default. The owner's admin page turns it on or off and sets a monthly GB limit with its reset day. Passwords are per game and short-lived, given only for a game involving that server's people, and a member cannot pass relay access on. The host's server's relay is used first, the friend's if the host's is off. It caps each game's bitrate and refuses to relay to private addresses. For carrier-grade NAT the answer is the relay: an owner behind it can pay for a cheap VPS to run the server with the relay on, and a custom STUN address does not solve it.
- **Date:** 2026-11-03
- **Why:** Some connections have no other way through, and the owner's own machine carries the relay with no account anywhere. Refusing private addresses means it cannot be used to reach into an owner's home network. STUN only tells a console its outside address and carries nothing, so it cannot help when no path exists between the two. The cost: every relayed game uses the owner's upload.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet".
- **Replaces:** the first plan's relay on Cloudflare, paid for by sponsors.
