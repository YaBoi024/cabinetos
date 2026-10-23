# An emulator's pin moves only for one of five reasons

- **Decided:** An emulator, core or pinned Flatpak moves only for one of
  these: (A) a major feature that changes what a player can do or how a game
  plays, not new options; (B) a fix for a bug or a measured slowness we have
  seen; (C) a meaningful set of games that did not play now do, judged on the
  emulator's own public compatibility data and never on one person's
  library; (D) a patch we carry is fixed upstream, so we can drop it; (E) a
  move we do not choose: a runtime reaching end of life, a security fix in
  something facing the network, or a fix an emulator needs for hardware we
  support. Newness alone is not a reason. The reason is named with its
  evidence, such as an issue, an upstream report or a list of games. Every
  proposed move starts with that question asked out loud, the answer is
  recorded on its issue or pull request, and without a reason the answer is no.
- **Date:** 2026-10-23 (the rule of 2026-10-14, widened)
- **Why:** The console is a family's, and stability matters more than the
  newest version. A move costs a build, a round of checks on the television
  and the chance of a new fault, so it has to be worth that.
- **Numbers:** none; docs/PROJECT.md, "Constraints and principles", 8.
- **Replaces:** the wording of principle 8, which becomes a one-line pointer
  to this file in PROJECT.md.
