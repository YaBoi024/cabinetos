# A game starts at once, and a power cut is caught at its next start

- **Decided:** after a download, an unpacking or an install, the console starts the game at once and lists, on its own drive, the files the game's drive may not have yet; a thread drops the list once that drive has them. A list left from an earlier boot deletes those files at that game's next start, so it is downloaded, unpacked or installed again. The console waits for the drive only where the list cannot be written, and for a firmware's note, which is on its own drive.
- **Date:** 2026-11-14
- **Why:** waiting for the drive before a game counts as complete also stops a damaged game being kept after a power cut, but on a slow USB drive it cost minutes at each game's first start: Mario Kart 7 from a card writing 9 MB/s went from 19 s to 3 min 27 s. The list gives the same protection with no wait.
- **Numbers:** #559, "A power cut right after a download or an unpack can leave a damaged game that the console keeps (exFAT and NTFS drives)"; PR #709, "A power cut right after a download no longer leaves a damaged game (#559)".
- **Replaces:** none.
