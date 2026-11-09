# Either console's pause stops a LinkUp game for both, and a drop pauses it too

- **Decided:** Either console pausing stops the game for both. The friend's pause button opens "Paused" with Resume and Leave, and the host sees "Alex paused"; A on either console resumes both. A dropped connection under about 2 seconds shows nothing, the friend's last input is held and lost pieces are resent. After that the game pauses on both with "Reconnecting to Alex", and after a minute it reads "Alex left". Three messages mean one thing each, the same on both consoles: "Alex paused", "Reconnecting to Alex" and "Waiting for Alex".
- **Date:** 2026-11-05
- **Why:** A co-op partner is never killed while the other has stepped away, and Remote Play already pauses at each handover. A person needs to know a pause from a drop, since neither can tell whose a drop is. A blip never strands anyone, and about a minute follows Parsec and GGPO.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; PR #666, "LinkUp: the console's side, step 3 of #654".
- **Replaces:** the earlier text in which the friend's pause button only asked "Leave Alex's game?" and the host's game kept running.
