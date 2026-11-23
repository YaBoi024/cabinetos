# An arcade game with a disc is one folder, its CHD at the top

- **Decided:** A game whose set has a disc is one RomM folder holding the zip and its CHD side by side, as `cps3/sfiii3/sfiii3.zip` and `cps3/sfiii3/cap-33s-2.chd`. The console fetches the folder's top-level files, starts the zip, never the largest file, and links each CHD into `<game>/<set>/`, where MAME and Flycast look. A CHD in a subfolder, anything deeper, and MAME's own layout of a zip beside a folder of the same name are not supported; a game whose CHD is not at the top of its folder says its disc image is missing.
- **Date:** 2026-10-28
- **Why:** MMagTech: RomM's documentation is the source of truth, and what it does not document is not supported. RomM's docs describe a game as a file, or a folder with its files at the top and subfolders only for extras such as `dlc` and `manual`. RomM 5.4.0 happens to list a CHD in a subfolder named after the set, but its docs do not say so, and its own rule makes MAME's side-by-side layout two games, which the console never joins.
- **Numbers:** none.
- **Replaces:** none.
