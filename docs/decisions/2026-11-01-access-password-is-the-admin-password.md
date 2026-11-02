# The File access password stays the console's admin password

- **Decided:** File access and Developer access keep one password, which is also the console user's (`cabinet`) login and sudo password. It is stored where the console's own programs can read it (0640, root:cabinet), so Settings can show it on the TV. A program running as the console's user can therefore read it and become root once either feature has been turned on. This is accepted for now; a separate admin account for Developer access is the fix if it ever has to close.
- **Date:** 2026-11-01
- **Why:** Only a program already running on the console could use it, such as a Steam game or an emulator the player chose, and such a program can already read the player's saves and sign-ins as the console's user. Root adds little to that on a home console. The fix changes how Developer access and sudo work for anyone using them, which costs more than it saves today. Before either feature is first turned on, the account has no password at all.
- **Numbers:** #626 ("The keyboard never logs what was typed; the report leaves it out"), from the password review of 2026-10-30; #629 ("Wi-Fi: a typed password sits on the join command's line for a few seconds"), that review's other open finding.
- **Replaces:** none.
