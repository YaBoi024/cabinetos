# RomM converts saves; the console's Sigil only reads Switch IDs

- **Decided:** A save made in another app is converted by RomM, through the Sigil RomM builds in: the console asks RomM for it in its own form and converts nothing itself. The console carries Sigil only to read a Switch game's title ID with the player's keys and give it to RomM, as Argosy does when a game is downloaded. That Switch step comes after step 11 of #346.
- **Date:** 2026-11-20
- **Why:** RomM is the source of truth and of management, and RomM's intent is to do as much as it can; Sigil's maintainer confirmed that a client relying on RomM's conversion is fine, and that Sigil is the same on either side. RomM keeps players' Switch keys off the server on purpose (rommapp/romm 1055fcff8), so an encrypted Switch dump gets its ID only from an app that holds the keys, and RomM's identity route (`PUT /api/roms/{id}/identity`, `roms.write`) takes it from such an app. Converting on the console as Argosy does would redo step 8 and prove every system's saves again for nothing RomM does not already give.
- **Numbers:** #346 "Move saves to RomM's save channels (Sigil)"; #722 "Save channels: the console's saves move to RomM's channels (#346, steps 2 to 11)"; rommapp/romm#3925 "feat: add binary title ID and save ID extraction during scan".
- **Replaces:** none.
