# A LinkUp friend's console draws the glow around the host's game

- **Decided:** The host tells the friend's console where the game sits on its screen, when a game first shows and whenever it moves, the pixel shift included. The friend's console draws black bars over the stream's own and the glow over them, on its own TV. With no word from the host (an emulator in a window of its own, or an older console) it draws nothing, as before. The host's TV is unchanged.
- **Date:** 2026-11-10
- **Why:** The stream carried the host's glow, and the encoder keeps nothing that faint (`min-qp=28` for H.265), so the fade reached the friend in flat steps, and more bitrate did not change it. Leaving the glow off the host for a session was the smaller change, but the host's TV would have lost its glow for the length of every session, which MMagTech judged would look wrong.
- **Numbers:** #692, "LinkUp: the friend's console draws its own letterbox glow"; PR #695, "LinkUp: the friend's console draws its own letterbox glow (#692)".
- **Replaces:** none.
