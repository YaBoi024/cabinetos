# A LinkUp friend plays by streaming, not by netplay or Remote Play

- **Decided:** The host's console runs the game and streams its picture and sound to the friend, whose controller comes back as player two. The stream is GStreamer's `webrtcbin`, already in the image, fed by LinkUp's own capture of gamescope's picture. It is neither each emulator's own netplay nor Remote Play.
- **Date:** 2026-11-02
- **Why:** One piece of work covers every system, and only the host needs the game. Netplay needs the same game, version and settings on both sides and loses players to mismatches. The cost is that the friend plays a little behind, because of the stream's delay: fine for co-op, accepted for versus games. Steam Remote Play Together and Nintendo's Switch 2 GameShare work the way LinkUp does. Remote Play is not used because Moonlight connects only directly, through port forwarding or a VPN, and warns that a relayed Tailscale path will not stream, and because a Remote Play stream sets the TV's own controllers aside where LinkUp needs both players at once. `webrtcbin` is LGPL and BSD licensed, and handles WebRTC's connection setup, encryption and relays by itself.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** none.
