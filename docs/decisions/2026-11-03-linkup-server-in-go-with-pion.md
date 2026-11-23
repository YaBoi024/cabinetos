# LinkUp's server is one small Go program with Pion's relay inside

- **Decided:** The server is one small Go program with Pion's relay (`pion/turn`) inside it, in a nearly empty container. The consoles keep GStreamer's `webrtcbin`, and Go is not added to the console's image.
- **Date:** 2026-11-03
- **Why:** One file with almost no operating system around it leaves little to patch, and Go rules out most memory bugs. Pion started in 2018, is MIT licensed, and LiveKit uses it. coturn was not chosen: it published at least 10 security advisories in the summer of 2026, including a memory disclosure before login, and it is written in C. The costs: the project has no Go yet, the monthly GB limit is ours to write, one person has made most of the relay's commits, and Pion lists no advisories, which may partly mean fewer people are looking.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet".
- **Replaces:** none.
