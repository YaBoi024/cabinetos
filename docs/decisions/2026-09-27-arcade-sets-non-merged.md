# Arcade sets are non-merged

- **Decided:** Every arcade emulator plays non-merged sets only: each game's zip carries everything it needs, its board's BIOS included. Split and merged sets, and shared parent or BIOS zips, are not supported.
- **Date:** 2026-09-27 (restated by MMagTech on 2026-10-27: "For all emulators, I only support non-merged sets.")
- **Why:** A failure is then about one zip, never a missing parent or BIOS zip, which keeps troubleshooting simple, lets each game sit alone in the cache, and lets a zip be matched against one emulator's list.
- **Numbers:** docs/PROJECT.md, "Arcade sets must be non-merged".
- **Replaces:** that paragraph of docs/PROJECT.md, which becomes a one-line pointer to this file.
