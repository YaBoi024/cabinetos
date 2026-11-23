# Cabinet

CabinetOS began on 2026-09-14 as a port of Cabinet, MMagTech's RomM client
for Apple TV, iPhone and Mac. Much of its design, its save row names and
many of its comments came from there.

**Cabinet today is Cabinet Go**, the pocket version: it plays the handheld
systems in your own RomM library on an iPhone
([MMagTech/cabinet-go](https://github.com/MMagTech/cabinet-go)). It is
unfinished and has no date. The Apple TV, iPhone and Mac apps are retired;
their repository, [MMagTech/cabinet](https://github.com/MMagTech/cabinet), is
archived.

**Since 2026-10-11 Cabinet is a reference, not a constraint**
(docs/PROJECT.md, open question 12b; CONTRIBUTING.md, "The rules"). Nothing
CabinetOS does has to match an Apple app.

**What CabinetOS shares with Cabinet Go is the server's side:** the save and
state rows on RomM, their names and their `emulator` tags, many in Cabinet's
spelling (`frontend/src/savetable.cpp`). Existing saves are filed under
them and Cabinet Go will read them, so changing one hides those saves.

**The history:** [docs/history/CABINET.md](history/CABINET.md) is the map of
the Apple apps' source, written 2026-09-14, and still worth reading for how a
problem was solved before. Where a comment or docs/PROJECT.md says "as
Cabinet does" or names tvOS, that is the precedent it means.
