# MAME 0.289 is libretro's own build

- **Decided:** MAME 0.289 is the `mame_libretro.so` that libretro's build server made from libretro/mame commit `5f5a07748ce5` (tag `lrmame0289` plus 12 libretro commits), reporting "0.289 (5f5a0774)". It is kept as an asset of a release of MMagTech/cabinetos and checked by its SHA256 when the image is built. It is the one core we do not build: a prebuilt core is allowed only when our own build would take hours and we change nothing in it. It is never patched; a change we needed would mean building it ourselves.
- **Date:** 2026-10-28 (asked for by MMagTech on 2026-10-27)
- **Why:** libretro's own Linux build of MAME took 206 minutes, and we carry no patch and set only options, so our build would make the same file. Batocera ships the same version. It needs only glibc's libraries.
- **Numbers:** none.
- **Replaces:** none. Every other core is still built from source.
