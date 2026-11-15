# An emulator's pin moves only for one of five reasons

- **Decided:** An emulator, core or pinned Flatpak moves only for one of
  these: (A) a major feature that changes what a player can do or how a game
  plays, not new options; (B) a fix for a bug or a measured slowness we have
  seen; (C) a meaningful set of games that did not play now do, judged on the
  emulator's own public compatibility data and never on one person's
  library; (D) a patch we carry is fixed upstream, so we can drop it; (E) a
  move we do not choose: a runtime reaching end of life, a security fix for a
  hole that outside data can reach (a request over the network, or a game
  file, save or state), or a fix an emulator needs for hardware we support.
  Newness alone is not a reason. The reason is named with its evidence, such
  as an issue, an upstream report or a list of games. Every proposed move
  starts with that question asked out loud, the answer is recorded on its
  issue or pull request, and without a reason the answer is no. A security
  fix moves the pin to the smallest step that carries it, not the newest
  build; where the fix exists only in a large jump, that move goes through
  the full check like any other, with saves and states tried first. Security
  is a reason to move, never a reason to rush.
- **Date:** 2026-11-14 (the rule of 2026-10-14, widened on 2026-10-23; (E)
  widened from things facing the network to every hole outside data can
  reach on 2026-11-14)
- **Why:** The console is a family's, and stability matters more than the
  newest version. A move costs a build, a round of checks on the television
  and the chance of a new fault, so it has to be worth that. Security holes in
  emulators are rare: the first check of every pin, on 2026-11-14, found
  published ones in two cores, so counting them does not mean moving every
  month. But the libretro cores run inside the
  console's own program, which holds the RomM sign-in, and saves and states
  come down from RomM, so a crafted file reaches more than one emulator. A
  known hole left open with no date to close it is a choice, and this rule
  makes it one we do not make.
- **Numbers:** docs/PROJECT.md, "Constraints and principles", 8;
  #713 (A skill that checks our own pins for known security holes), whose
  first run found published holes in two cores that (E) did not count.
- **Replaces:** the wording of principle 8, which becomes a one-line pointer
  to this file in PROJECT.md; and this file's own (E) of 2026-10-23, which
  counted only a security fix in something facing the network.
