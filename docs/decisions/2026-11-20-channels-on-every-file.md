# A game's save channels are listed on every one of its files

- **Decided:** The console lists the person's channels of a game on every game file of it, in one `GET /api/channels` request naming each file, with the game's files kept in the game's record, rather than reading the game's own entry (`user_channels`) at each start as RomM's website does. A push into a channel names that channel's own file; RomM's `channel_file_id` is used only for a channel the console makes.
- **Date:** 2026-11-20
- **Why:** A file added to a game's folder on RomM (a `.cue` beside a `.chd`) moves `channel_file_id`, which RomM documents as the file a new channel goes on, while a channel stays on the file it was made on and RomM refuses a push that names another. Both ways find every channel of the game. Measured on the reference server, median of five over six games: the list on every file took 17 to 20 ms, about what the one file took (16 to 18 ms), and the game's own entry 39 to 59 ms, which would have made every start about 30 ms slower. A game of more than a hundred files takes a request a hundred; the folder games sampled there, one per system, have at most four.
- **Numbers:** #346 "Move saves to RomM's save channels (Sigil)"; #722 "Save channels: the console's saves move to RomM's channels (#346, steps 2 to 11)".
- **Replaces:** none.
