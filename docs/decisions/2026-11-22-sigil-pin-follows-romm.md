# Sigil's pin follows RomM's

- **Decided:** Sigil's pin follows RomM's: the console builds in the Sigil commit that the RomM release it needs builds in (`SIGIL_VERSION` in RomM's Dockerfile). It moves with that RomM release, in the pull request that checks the console against it, and ahead of RomM only for one of the five reasons of [the emulator pin rule](2026-10-23-when-an-emulator-pin-moves.md).
- **Date:** 2026-11-22
- **Why:** RomM is where every app's saves meet, and the console needs one named RomM release ([romm-version-needed](2026-11-17-romm-version-needed.md)), so one RomM goes with one Sigil and that pair is what is tested. A Sigil pinned on its own would drift from RomM's unseen: the self-test holds the console's Switch saves to the console's own copy of Sigil, not to RomM's, so a change in how RomM's Sigil packs a Switch save would reach players first. Other apps each build in a Sigil of their own, so matching every app is not possible; RomM is the one place they all pass through. Sigil reads game files and other apps' saves, data from outside, so a security fix in it is one of the five reasons to move ahead.
- **Numbers:** #346, "Move saves to RomM's save channels (Sigil)"; PR #722, "Save channels: the console's saves move to RomM's channels (#346, steps 2 to 11)".
- **Replaces:** none.
