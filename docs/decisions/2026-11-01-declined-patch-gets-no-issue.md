# A patch upstream declined has no open issue

- **Decided:** A patch to an emulator's source keeps its CabinetOS issue open only while upstream might take the change. When upstream declines it, or it was never an upstream fault, the issue is closed, and the patch's row in docs/ARCHITECTURE.md section 8, its decision file and its comment in the recipe are its record. The patch itself stays, asserted in its build.
- **Date:** 2026-11-01
- **Why:** An open issue says there is still work to do, and the open list is meant to be that list. A declined patch never closes: nothing upstream will change, and its build stops on its own if the patch no longer applies, so the open issue added nothing the section 8 row, the decision file and the recipe do not already say. The PS2 sound patch, which is no upstream fault, already had no issue.
- **Numbers:** #634, "Supermodel patch: a machine linked to itself runs as the only cabinet (Dirt Devils)", closed by this; trzy/Supermodel#360, "A net game set as slave hangs when the net board is linked to itself (Dirt Devils)", declined 2026-10-30.
- **Replaces:** the 2026-10-20 rule that every approved patch is tracked on an issue (docs/WORKING.md, "Patches to an emulator's code"), for declined patches and those for no upstream fault.
