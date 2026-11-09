# LinkUp needs a RomM server on both consoles, and works only while it is reachable

- **Decided:** Both players need a normal CabinetOS console on a RomM server, their own or one someone shares with them, and LinkUp works between servers. Nobody has to join the host's RomM server, and setup does not change. LinkUp works only while the console can reach its RomM server: when the top bar says Offline, LinkUp is greyed in the same place and focus skips it, on the host's console and the friend's, and a game already running carries on.
- **Date:** 2026-11-03
- **Why:** The OS relies on a RomM server throughout, and a console with no server would add a second path through setup, start-up and Settings that every later feature has to allow for. Offline already says the server can't be reached, so LinkUp follows that one rule the player can see, and keeping LinkUp in place stops the top bar's items moving when the server drops or comes back.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** the first plan's friend who never touches a RomM server, which first run does not allow.
