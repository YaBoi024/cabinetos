# Model 2 on sm2-emu, Model 3 on Supermodel

- **Decided:** Sega Model 2 games play in sm2-emu and Model 3 games in Supermodel, each a program of its own, built into the image from a pinned version rather than taken from Flathub, and started with no network like every emulator. sm2-emu is built without libcurl, which leaves its artwork downloader out, and draws with Vulkan. Supermodel has no Vulkan renderer and draws with OpenGL. Supermodel is built only after a run of all the Model 3 games in the reference library on the reference console shows them playing.
- **Date:** 2026-10-28
- **Why:** MAME 0.289 runs 11 of the 36 Model 2 games it lists and none of its 23 Model 3 games. On the reference console on 2026-10-28, sm2-emu reached gameplay in 23 of the 27 Model 2 games in the reference library, 13 of them games MAME cannot run, with room to spare at 4K; Batocera uses it for Model 2. A pinned build keeps the same emulator for everyone; Flathub moves on its own. sm2-emu's licence lets an unchanged build keep its name and makes a patched copy take another, so it is not patched.
- **Numbers:** none.
- **Replaces:** none.
