# The image has no help pages

- **Decided:** The image carries no manual pages: `man-db`, `man-pages` and every file under `/usr/share/man` are removed at build, and the base's job that rebuilds their index at every start is masked (`build_files/strip-desktop.sh`). The same image serves every install, so this holds on a stick and on an internal drive alike.
- **Date:** 2026-11-04 (MMagTech)
- **Why:** Nothing on a console reads them: no package requires them, and nothing in this repository uses them. They are for someone typing commands on a desktop, and the same manuals are online. The job is not free: it took 1 to 3.5 minutes at every start from a USB stick and about a second on an NVMe drive. Taking the pages out is a couple of lines in the build, so putting them back is as easy if something ever needs them.
- **Numbers:** #656, "Sound: one rule on every PC, read from what each output reports"; #653, "Portable install, step 3: the disk image and its boot check".
- **Replaces:** none.
