# An arcade game takes no other app's save

- **Decided:** An arcade game never starts from a save another app made, whichever emulator that save names: the console keeps its own and says "Couldn't use the newest save". Saves between CabinetOS consoles travel as before.
- **Date:** 2026-11-20
- **Why:** A board's memory file carries nothing that says which emulator, or which version of it, wrote it; RomM converts no arcade save; and an app's label for its emulator can be wrong. CabinetOS consoles on one update run the same emulator build, so their saves always fit.
- **Numbers:** #346 "Move saves to RomM's save channels (Sigil)"; #722 "Save channels: the console's saves move to RomM's channels (#346, steps 2 to 11)".
- **Replaces:** none.
