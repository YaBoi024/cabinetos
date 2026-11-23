# LinkUp and Remote Play run one at a time

- **Decided:** While a device is streaming through Remote Play, every friend is grey with their real status, the top line reads "Remote Play in use", and you show Offline to your friends. While a LinkUp session is open, on either console, Sunshine is stopped so no device can connect; it comes back by itself when the session ends, with the switch unchanged. Settings cannot be reached during a session.
- **Date:** 2026-11-04
- **Why:** Both take the screen and the video engine, and a Remote Play stream sets the TV's controllers aside where LinkUp needs them, so nobody there can answer an invite. "In use" on a friend's tile read as the friend being in use. Stopping Sunshine also means nobody else's device can get into the game.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** none.
