# LinkUp's protocol only adds, a console names who is behind, and one server is pinned

- **Decided:** Changes to what consoles and servers say to each other only add, never rename or remove. Two numbers are stated on connect, one for console to server and one for console to console, so a change to the stream never makes an owner update a server. Different versions play on what the older one knows; a real break names who is behind ("Console needs an update", "LinkUp server needs an update", or "Alex's console needs an update"), and an update is never forced, on Manual or otherwise. CabinetOS pins one exact server image, by digest, and its CI runs two stand-in consoles against it. The order is server first, then console, and moving the pin is a pull request.
- **Date:** 2026-11-04
- **Why:** Consoles update from the image, but a container updates only when its owner pulls a new one, so old containers are the real risk. Consoles update when their owners install, so the two will often differ, and since changes only add, a break is rare. The cost: a second repository's checks to look after, and a pin to move for each server release.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet".
- **Replaces:** none.
