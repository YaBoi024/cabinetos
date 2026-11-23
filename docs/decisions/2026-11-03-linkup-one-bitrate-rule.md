# One bitrate rule for every LinkUp game

- **Decided:** The encoder spends what the picture needs, up to 18 Mbit/s, and stops once the picture is clean. There are no tiers by system. It runs at a steady bitrate with a quality floor, and the padding the encoder adds is dropped before sending. When the connection cannot carry that, see [linkup-picture-floor](2026-11-07-linkup-picture-floor.md).
- **Date:** 2026-11-03
- **Why:** One rule for every game, with the encoder deciding. With the console's CRT looks, on by default, a 2D arcade game costs as much as fast 3D (DoDonPachi DaiOuJou needs the full 18), so tiers would blur the games that look simplest. The encoder's own quality mode (QVBR) kept 18 only as a long-run average and sent one-second bursts of up to 50 Mbit/s, more than a friend's connection or the relay may carry. A steady bitrate with a floor keeps to 18 and spends less on easy pictures.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** none.
