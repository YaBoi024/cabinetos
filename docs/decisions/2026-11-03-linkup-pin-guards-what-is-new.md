# With a PIN set, LinkUp asks for it only for what is new

- **Decided:** With a PIN set, on any account but the owner's, the PIN is asked for turning LinkUp on, adding or changing a server, and Leave, through the existing `askPin`. Playing with friends and Status ask nothing. With no PIN set, nothing asks.
- **Date:** 2026-11-03
- **Why:** One PIN set by the owner is the console's only protection, and every protected action already goes through it. LinkUp has no chat, so a friend gets no further than a username and a game.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** the earlier rule that sending or accepting a request, and hosting or joining by code, asked for the PIN every time.
