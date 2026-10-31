# Every emulator update is taken to break saves and states

- **Decided:** A game uses the same emulator for everyone, decided from its files and a fixed order, never from who plays it or what they have saved; it changes only when an emulator's pin moves. Every emulator update is assumed to break the saves and states made with the version before. So an update goes ahead only for a reason in [when-an-emulator-pin-moves](2026-10-23-when-an-emulator-pin-moves.md), is run on the reference console before it ships, and its changelog entry says saves and states from the old version may not load. For arcade, CI lists every game whose emulator would change.
- **Date:** 2026-10-28
- **Why:** MMagTech, 2026-10-28: from this date on, an update may break a player's saves or states only for a valid reason, tested before it ships and explained in the changelog; saves made before it do not count. A test on one library cannot vouch for anyone else's, so the gain is judged and the player is told. MAME seldom changes in ways a player gains from, so it should seldom move.
- **Numbers:** none; settled in conversation.
- **Replaces:** none.
