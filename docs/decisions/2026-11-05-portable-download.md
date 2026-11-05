# The portable's download: the file that was tried, beside the ISO

- **Decided:** `build-disk.yml`'s `portable_run` input takes the portable file from a finished run and puts it on Cloudflare R2 as `cabinetos-portable.img.xz`, with its SHA-256, and builds nothing. It sits beside `cabinetos.iso` at `download.mmagtech.com`, one name replaced each time. `publish` stays for the ISO.
- **Date:** 2026-11-05
- **Why:** Building again with `publish` would put up a new file nobody had tried, and takes about 40 minutes. The finished run's file is, byte for byte, the one that passed the boot check and ran on a real PC, and sending it takes minutes. R2 because downloads there are free and the ISO is already there. The two files come to 12.5 GB, 2.5 GB over R2's free 10 GB, which at $0.015 per GB a month is about 4 cents a month. Staying inside the free storage would mean leaving out one of the two files, and each is the only way to its kind of install.
- **Numbers:** #308, "At the first release: the download goes on a GitHub Release, and the page and wiki point to it"; PR #653, "Portable install, step 3: the disk image and its boot check".
- **Replaces:** the bucket holding one file, inside R2's free storage (the comment on the workflow's publish step, 2026-10-10).
