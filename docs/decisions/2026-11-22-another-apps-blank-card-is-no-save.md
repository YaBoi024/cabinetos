# Another app's blank memory card is no save

- **Decided:** When RomM says the newest save, another app's, holds none of the game's saves (Sigil's NOT_FOUND, as for a blank memory card or backup volume), the console starts as if it were not there: from its own save, saying nothing, and what the game saves then goes on top of it by the usual rule.
- **Date:** 2026-11-22
- **Why:** An app leaves such a card on RomM when a game was started there and never saved: RomM's web player sends the core's fresh card as the game boots and again as the player leaves. Taken as a save the console could not use, it made the TV say "Couldn't use the newest save" at every start and kept the TV's saves beside it, so the other apps went on starting from the blank card. The console already never sends or restores a card a core wrote just by being switched on; this is the same rule for another app's card, which only RomM can read.
- **Numbers:** #346 "Move saves to RomM's save channels (Sigil)"; #722 "Save channels: the console's saves move to RomM's channels (#346, steps 2 to 11)".
- **Replaces:** none.
