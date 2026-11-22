# No core file for emulators run as programs

- **Decided:** every emulator run as a program (Eden, xemu, RPCS3, Xenia Edge, Cemu, Vita3K, sm2-emu, Supermodel) starts with its core-file limit at 0 (`proc::spawn`), so a crash leaves no copy of its memory and ends it at once. A crash still shows in the journal: the kernel's line names the thread and the spot. Whoever investigates a crash runs the emulator by hand with core files on.
- **Date:** 2026-11-22
- **Why:** Eden 0.2.1 crashes now and then as it closes a busy game (its fix is in Eden's development code, in no release yet). With core files on, systemd-coredump kept the dying Eden for its 5 to 6 GB copy: the console waited its 10 s grace, then every write as the game was left waited behind the dump, so Home came 40 s to 2 min 14 s after Exit on a tester's slower drive. With the limit at 0 the crash ended Eden 0.33 s after the close (A9, measured). The other way, watching for the crash and forcing Eden at once, cuts the dump short anyway (the tester's three were unreadable) and is more code. Turning off Eden's asynchronous GPU would likely avoid the crash but slow every Switch game, and patching Eden is ruled out. A player's dump could never leave their console anyway: it holds the keys Eden loaded.
- **Numbers:** none; a tester's report on Discord, and the findings of 2026-11-22.
- **Replaces:** none.
