# CabinetOS
#
# A console operating system for x86-64 PC hardware, built as a bootc image on
# top of Bazzite. The reference machine is a GEEKOM A9 Pro, but nothing here may
# assume it.
#
# Bazzite is used for its kernel, graphics stack, controller drivers and power
# handling. Its desktop and its Steam integration are removed. See
# docs/PROJECT.md for the full rationale.
#
# This file is never built on a Mac. Builds run in GitHub Actions. See README.md.

# Build scripts are mounted, not copied, so they never end up in the final image.
FROM scratch AS ctx
COPY build_files /
COPY system_files /system_files
# The terms this image ships under, carried in so build.sh can put them INSIDE
# it. Referenced from their real homes rather than duplicated into
# system_files/, so the copy in the image cannot drift from the copy in the
# repository.
COPY LICENSE /licences/LICENSE
COPY docs/LICENCES.md /licences/LICENCES.md
# And the full licence text of everything the image carries (#120), fetched at
# each pinned revision by tools/fetch-licences.py. Outside docs/ on purpose:
# docs/ does not start a build.
COPY licences /licences/texts
# The controller list (SDL_GameControllerDB). Installed with the frontend, in
# the small frontend layer, NOT from system_files/: there it rode in the OS layer,
# and its first update shipped 60 MB to the console instead of one file.
COPY frontend/data /frontend-data
# Everything the image carries that is built elsewhere: the frontend, the
# cores, PCSX2, Cemu, Sunshine and their files (ci/stage-image-payload.sh
# lists it).
#
# None of them is in this repository and none of them is built here — they are
# far too slow, and .github/workflows/build-frontend.yml and build-core.yml
# already build them properly, each core pinned to an exact commit and each
# asserting that commit back out of the finished .so.
#
# ci/stage-image-payload.sh collects them into image_payload/ and refuses if
# anything is missing, so this COPY is the last step of a chain rather than the
# place the checking happens. If the build fails here saying image_payload does
# not exist, that script has not been run — see README.md.
COPY image_payload /payload

# ---------------------------------------------------------------------------
# Base image
# ---------------------------------------------------------------------------
#
# Pinned to a specific Bazzite stable tag AND its digest. The tag is for humans;
# the digest is the actual pin. Moving this is a deliberate act: bump it in its
# own commit and re-run the VM boot test.
#
# Resolved 2026-10-18:
#   ghcr.io/ublue-os/bazzite:stable == 44.20261012.1
#
# Variant choice: `bazzite-deck`.
FROM ghcr.io/ublue-os/bazzite-deck:stable
# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
#
# The OS layer's customisation is build_files/build.sh, which calls the strip
# scripts in order; each later layer runs one script (below). Keeping it in
# scripts rather than in RUN lines means the reasoning for each removal lives
# next to the removal.
#
# LAYERS ORDERED BY HOW OFTEN EACH CHANGES, 2026-09-23. This was one RUN, so
# a 1.4 MB frontend change rebuilt and re-shipped everything CabinetOS adds:
# 546 MB to the console. See docs/PROJECT.md open question 27. Sizes as
# compressed in 2026.10.18.7:
#
#    1. the OS            70 MB  build.sh: Bazzite stripped and configured.
#                                Moves on a base bump or a change to
#                                build_files/ or system_files/.
#    2. the cores         95 MB  pinned, and PCSX2. Moves when one moves.
#    3. RPCS3            131 MB  pinned. Moves when RPCS3 is bumped.
#    4. Xenia Edge        21 MB  pinned. Moves when Edge is bumped.
#    5. Cemu              35 MB  built at a pin by build-cemu.yml.
#    6. xemu's drive       1 MB  pinned.
#    7. the core files    16 MB  PPSSPP's, Dolphin's and PCSX2's resources.
#    8. the frontend       4 MB  the frontend, the Wii bridge, the
#                                controller lists and the shaders. Moves
#                                with nearly every change.
#    9. the boot splash  254 MB  the initramfs. Moves with the OS layer.
#   10. the version       1 kB  every image.
#   11. the lint               (nothing in it)
#
# A console downloads only the layers whose bytes changed, so a frontend change
# ships layer 8, PROVIDED the other layers rebuild to identical bytes. CI
# builds from nothing every time, so that rests on `podman build --timestamp`
# in the Justfile, on build.sh stamping the packages it installs with a fixed
# time (#511), and on there being NO RECHUNK, which throws this order away
# and regroups the image by RPM package.
#
# One image, one digest, one update. Splitting layers changes how the image is
# stored and fetched, never what a console updates as a unit.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=cache,dst=/var/cache \
    --mount=type=cache,dst=/var/log \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/build.sh

RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-frontend.sh cores

# RPCS3, the PS3 emulator: the RPCS3 team's own build, pinned by checksum,
# 338 MB. Beside the cores because it moves as rarely as they do. Not from
# Flathub; build_files/install-rpcs3.sh says why.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-rpcs3.sh

# Xenia Edge, the Xbox 360 emulator: its developer's own Linux build, pinned by
# checksum, 53 MB. A layer of its own after RPCS3's, because Edge is released
# many times a day and its pin will move more often than RPCS3's.
# build_files/install-xenia.sh says why.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-xenia.sh

# Vita3K, the PlayStation Vita emulator: its developers' own numbered build,
# pinned by checksum, 258 MB. A layer of its own after Xenia's, since its pin
# may move more often than the cores'. build_files/install-vita3k.sh says why.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-vita3k.sh

# Wii U: Cemu, built from source at a pin by build-cemu.yml, from the payload.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-cemu.sh

# xemu's blank Xbox hard drive, pinned by checksum, 1.6 MB, beside the EEPROM
# that system_files puts in the same folder. xemu itself is a Flatpak
# (flatpaks.list); build_files/install-xemu-drive.sh says why this is not.
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-xemu-drive.sh

RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-frontend.sh system

RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/install-frontend.sh frontend

# THE BOOT SPLASH (#108): the initramfs rebuilt with the CabinetOS watermark
# build.sh put in place, 250 MB. os-release is branded first, WITHOUT the
# version, so the initramfs's copy names CabinetOS and is the same in every
# image (#511: with the version in it, every update downloaded this layer).
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    --mount=type=tmpfs,dst=/tmp \
    /ctx/brand-os-release.sh && \
    /ctx/boot-splash.sh

# THE VERSION, IN A LAYER OF ITS OWN, LAST but for the lint. It changes with
# every image, so anywhere earlier it would drag a layer that did not change
# into every update. Here it is two small files. The console reads it after
# booting to tell whether an update applied; docs/SETTINGS.md, System. The
# number comes from ci/next-version.sh through the Justfile. os-release takes
# the version here for the same reason (#137): its PRETTY_NAME carries it.
ARG CABINETOS_VERSION=dev
RUN --mount=type=bind,from=ctx,source=/,target=/ctx \
    printf '%s\n' "${CABINETOS_VERSION}" > /usr/share/cabinetos/version && \
    /ctx/brand-os-release.sh "${CABINETOS_VERSION}"

# ---------------------------------------------------------------------------
# Lint
# ---------------------------------------------------------------------------
#
# Verifies the image is a structurally valid bootc image. This catches a class
# of mistakes (broken /usr layout, missing kernel, bad symlinks) that would
# otherwise only show up when the disk image fails to boot.
RUN bootc container lint
