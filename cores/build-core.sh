#!/usr/bin/env bash
#
# Builds one libretro core for Linux x86-64, at an exact revision.
#
# This is the Linux column of Cabinet's core manifest. The discipline it
# enforces is the whole point, and it is what Cabinet's own build scripts did
# not do (see docs/PROJECT.md, open question 13):
#
#   * the commit is checked out and then ASSERTED, never cloned as HEAD;
#   * the make arguments are recorded here, not left to the platform default,
#     because a plain Linux build silently turns ON recompilers the Apple build
#     has OFF, in five cores;
#   * the output is a plain .so. No symbol prefixing, no relocatable merge, no
#     exported-symbol lists. RTLD_LOCAL gives namespace isolation for free, so
#     the entire apparatus Cabinet needs for Apple simply does not exist here.
#
# Split by necessity: git lives on the host, the toolchain lives in the
# frontend's builder container, and neither has the other. So this does the
# version control itself and hands the compile to podman. Run it on the machine
# with the container image, which today is the test VM and in CI is the runner.
#
# Usage: cores/build-core.sh <core>

set -euo pipefail

CORE="${1:-}"

# Whether the finished .so can be asked which revision it is. Almost every core
# compiles `git rev-parse --short HEAD` into the string it reports, and where it
# does, that is asserted. A few cannot, through upstream bugs rather than
# anything we do, and those are marked in their case arm with the reason.
#
# NOT patched into working. Adding the missing flag ourselves would change the
# binary against Cabinet's, which builds the same upstream and has the same
# blind spot, and diverging from Cabinet to satisfy our own test is exactly
# backwards. The checkout is still asserted at the pinned commit either way;
# what is lost is only the ability to read it back out.
VERIFY_REVISION=1

# make is how most cores build. A core with no Makefile.libretro (mGBA's
# upstream dropped it; others never had one) sets BUILDSYS=cmake with
# CMAKEARGS and CMAKE_TARGET in its case arm instead.
BUILDSYS="make"

# Files from the core's own source tree that have to be installed under the
# frontend's system directory, in ASSET_DIR — the folder name the core itself
# looks for, which is not necessarily the manifest's name for it.
#
# Empty for most cores: their firmware is a console's, and it comes from
# RomM with the game. A core that sets it says why in its case arm.
ASSETS=()
ASSET_DIR=""
ASSET_SRC=""

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
OUT="${CABINETOS_CORE_OUT:-$ROOT/cores/build}"
# Where a core's own system files go, for the cores that have any. This is
# not the frontend's system directory — that is a working directory holding
# BIOS files RomM served and files the cores themselves write. This is the
# staging area the deploy copies FROM, and in the image it becomes a path in
# /usr. See the ppsspp case arm.
SYSTEM_OUT="${CABINETOS_SYSTEM_OUT:-$ROOT/cores/system}"

# Per core: upstream, pinned commit, make directory, makefile, extra arguments.
#
# COMMIT values come from Cabinet's core-manifest.json `pinned_commit`, which is
# the revision every platform must build from going forward. They are not to be
# "updated" here; they move in Cabinet, and this follows.
#
# EVERYTHING ABOUT ONE CORE GOES IN ITS ARM, or in a patch block of its own
# below. CI caches each core under a hash of its own arm and blocks and of
# every line outside all of them (ci/core-cache-key.sh), so a change inside
# one arm rebuilds that core alone and a change to any other line, comments
# included, rebuilds every core. That is why the lines outside the arms
# carry no count or list of cores: adding a core must touch only its own.
case "$CORE" in
gambatte)
    REPO=https://github.com/libretro/gambatte-libretro.git
    COMMIT=d9d6cd06382d1ced30de34d56d3609452323dab1
    MAKEDIR=.
    MAKEFILE=Makefile.libretro
    # No recompiler exists in this core at all, on any platform, so there is
    # nothing here to keep in step with Cabinet. That is exactly why it is the
    # first core: it proves the pipeline without also testing the parity
    # question.
    MAKEARGS=()
    ;;
genesis_plus_gx)
    REPO=https://github.com/libretro/Genesis-Plus-GX.git
    COMMIT=a7985a9c4278ac352f8ca7bb4d3cc6b36e9e3e7d
    MAKEDIR=.
    MAKEFILE=Makefile.libretro
    # HAVE_CDROM is the lever here, and it is not a recompiler — see
    # docs/PROJECT.md, open question 13. Makefile.libretro line 7 defaults it to
    # 0; the unix branch then turns it on from a `uname -s` test, and no Apple
    # branch does anything equivalent. The manifest agrees: build_args is null
    # for ios, tvos and mac, so nothing overrides the default there.
    #
    # It is the libretro PHYSICAL CD-ROM DRIVE interface, so it lands on Sega
    # CD, the one system of this core's four that saves by a different
    # mechanism. Whether it perturbs the state format is not established, and
    # the standing rule is to match Cabinet until it is. Free to obey: the
    # console has no optical drive and never will, so this turns off a feature
    # the hardware cannot use.
    MAKEARGS=(HAVE_CDROM=0)
    ;;
fceumm)
    REPO=https://github.com/libretro/libretro-fceumm.git
    COMMIT=236ccdfc911e84c60fea6b9d0699c2d440a8de14
    MAKEDIR=.
    MAKEFILE=Makefile
    # NES.
    MAKEARGS=()
    ;;
snes9x)
    REPO=https://github.com/libretro/snes9x.git
    COMMIT=890b5d445538fe790aa3add3d5702c80f551e0ae
    MAKEDIR=libretro
    MAKEFILE=Makefile
    # SNES. Its makefile lives in libretro/, not at the root.
    MAKEARGS=()
    ;;
beetle_pce_fast)
    REPO=https://github.com/libretro/beetle-pce-fast-libretro.git
    COMMIT=2f623abd033257b969370b73d9da982dcb0c3fdd
    # Cannot report its revision, and it is upstream's bug rather than ours:
    # libretro.c is a C file that uses GIT_VERSION, while the Makefile adds
    # -DGIT_VERSION to CXXFLAGS only, so the define never reaches it and the
    # `#ifndef GIT_VERSION / #define GIT_VERSION ""` fallback wins. Cabinet
    # builds the same upstream and has the same blind spot.
    VERIFY_REVISION=0
    MAKEDIR=.
    MAKEFILE=Makefile
    # TurboGrafx-16 and TurboGrafx-CD, both.
    MAKEARGS=()
    ;;
beetle_ngp)
    REPO=https://github.com/libretro/beetle-ngp-libretro.git
    COMMIT=a50d5ac288a81f2104ddf43195a4efdd15c72227
    MAKEDIR=.
    MAKEFILE=Makefile
    # Neo Geo Pocket Color.
    MAKEARGS=()
    ;;
beetle_vb)
    REPO=https://github.com/libretro/beetle-vb-libretro.git
    COMMIT=83ed42608601fb7b01d41e4f8fb2007a37b8c84e
    MAKEDIR=.
    MAKEFILE=Makefile
    # Virtual Boy.
    MAKEARGS=()
    ;;
beetle_saturn)
    REPO=https://github.com/libretro/beetle-saturn-libretro.git
    COMMIT=ed549bdac0e1a830bb794fa720e45c225a45355c
    # Same upstream bug as beetle_pce_fast, and the same family: libretro.c is
    # a C file using GIT_VERSION while the Makefile puts -DGIT_VERSION in
    # CXXFLAGS. Verified, not assumed from the symptom.
    VERIFY_REVISION=0
    MAKEDIR=.
    MAKEFILE=Makefile
    # Saturn. Needs a region BIOS, which the launcher fetches.
    MAKEARGS=()
    ;;
stella2014)
    REPO=https://github.com/libretro/stella2014-libretro.git
    COMMIT=4a7da82595d27b8df7af1ecb467a64b642a41bc9
    MAKEDIR=.
    MAKEFILE=Makefile
    # Atari 2600.
    MAKEARGS=()
    ;;
prosystem)
    REPO=https://github.com/libretro/prosystem-libretro.git
    COMMIT=8a88014287c7a01cd568067e5a557d0a2b2a051f
    MAKEDIR=.
    MAKEFILE=Makefile
    # Atari 7800.
    MAKEARGS=()
    ;;
opera)
    REPO=https://github.com/libretro/opera-libretro.git
    COMMIT=a501a278d057b952d1ad6165549c59ab178ca497
    MAKEDIR=.
    MAKEFILE=Makefile
    # 3DO. Needs a BIOS.
    MAKEARGS=()
    ;;
vecx)
    REPO=https://github.com/libretro/libretro-vecx.git
    COMMIT=8f671cc9d737f2890c3ce19e177e2984dcae121f
    MAKEDIR=.
    MAKEFILE=Makefile
    # Vectrex. HAS_GPU=0 is the lever, and it is NOT a recompiler: the
    # Makefile defaults HAS_GPU=1 off macOS, which builds a GLES2 path this
    # frontend cannot drive. Cabinet passes it on both Apple platforms, so this
    # matches rather than diverges.
    MAKEARGS=("HAS_GPU=0")
    ;;
mame2003_plus)
    REPO=https://github.com/libretro/mame2003-plus-libretro.git
    COMMIT=21256d24120b04916c5197d95b757635ca880fd9
    MAKEDIR=.
    MAKEFILE=Makefile
    # Arcade, the MAME 2003-Plus half of it.
    MAKEARGS=()
    ;;
fbneo_libretro)
    REPO=https://github.com/libretro/FBNeo.git
    COMMIT=2444fbe3ddab193b6c0e6f2d39b6dde041fbee4c
    MAKEDIR=src/burner/libretro
    MAKEFILE=Makefile
    # Arcade, the FinalBurn Neo half. Its makefile is four directories down.
    MAKEARGS=()
    ;;
pcsx_rearmed)
    REPO=https://github.com/libretro/pcsx_rearmed.git
    COMMIT=ba61a4fdee1f789e8012f205f1b63826667644fa
    MAKEDIR=.
    MAKEFILE=Makefile.libretro
    # PlayStation. THE CPU BACKEND IS THE LEVER and it is the open question:
    # Cabinet runs DYNAREC=0 on iOS and tvOS and DYNAREC=ari64 on the Mac, and
    # tags the states from BOTH as pcsx-rearmed-native — so Cabinet already
    # depends on the state format not caring which backend produced it.
    # CABINETOS_DYNAREC exists so both can be built and compared, and that
    # comparison has now been made. The default is DYNAREC=lightrec —
    # lightrec, the real recompiler, and it is safe to differ from Apple here:
    # the state format tolerates either backend. ndrc_freeze writes nothing
    # without blocks, skips an absent section on load, and consumes a present
    # one it cannot use — and the section is block ADDRESSES, a cache hint, not
    # machine state. A LIGHTREC build takes the same stubs the interpreter does
    # and writes no section at all. See docs/PROJECT.md, open question 13.
    #
    # Override with CABINETOS_DYNAREC=0 to build the interpreter for comparison.
    MAKEARGS=("DYNAREC=${CABINETOS_DYNAREC:-lightrec}")
    ;;
melonds)
    REPO=https://github.com/libretro/melonDS.git
    COMMIT=66b5d2634cd0a79030562811e6e05f5532f800ba
    MAKEDIR=.
    MAKEFILE=Makefile
    # Nintendo DS. TWO levers here, and only one of them is a recompiler —
    # which is the reason this core is not simply "melonDS, like the others".
    #
    # JIT_ARCH is the recompiler. The unix branch sets x64 on x86-64; Cabinet's
    # iOS and tvOS builds set nothing at all and run the interpreter, while its
    # Mac sets aarch64. So Cabinet ALREADY ships this core with two different
    # CPU backends under one emulator tag, exactly as it does for pcsx_rearmed.
    # CABINETOS_MELONDS_JIT builds either side for comparison.
    #
    # x64 IS SAFE, AND THIS ONE WAS MEASURED RATHER THAN READ. Both builds were
    # run against Tetris DS through tools/state-probe.c: identical video and
    # audio digests over 600 frames, identical state size, and every load
    # combination — each build's own state and the other's — ending on the same
    # digest, with the two own-state runs as the control. Upstream intends this:
    # the only two JIT-conditional lines in any DoSavestate are guarded
    # `if (!file->Saving)`, so nothing about the recompiler is ever WRITTEN into
    # a state, and on load a JIT build refills the pipeline and resets its block
    # cache. melonDS's own comment says why — "we still want JIT save states to
    # be loaded while running the interpreter". See docs/PROJECT.md, question 13.
    #
    # HAVE_OPENGL is not a recompiler: it compiles in melonDS's OpenGL
    # renderer, the only one that draws the DS's 3D above its own 256x192, and
    # its four options (melonds_opengl_*). The unix branch turns it on for x86
    # and x86-64, as RetroArch's and Batocera's x86-64 builds have it (#219).
    # It is desktop OpenGL 3.1 and nothing else, no GLES and no Vulkan, so the
    # core host gives it a desktop GL context of its own beside the UI's
    # (core_hwrender.cpp). The renderer is still off unless the console turns
    # it on: picture quality does, at Balanced and Quality, where that context
    # can be made (quality.cpp). Cabinet's Apple builds leave it off.
    MAKEARGS=("JIT_ARCH=${CABINETOS_MELONDS_JIT-x64}" "HAVE_OPENGL=1")
    ;;
picodrive)
    REPO=https://github.com/libretro/picodrive.git
    COMMIT=733c711a477a642fd2006d5a7a581b2790ec36b4
    MAKEDIR=.
    MAKEFILE=Makefile.libretro
    # Sega 32X, and only 32X — Genesis, Master System and Game Gear are all
    # Genesis Plus GX. Reading the core list as a platform list gets that
    # backwards.
    #
    # use_sh2drc is the lever: the SH2 recompiler, which the 32X needs two of.
    # It defaults to 1 on x86-64, and Cabinet gets 0 from the Makefile's own
    # Apple block — turned off there for code-signing reasons ("It needs
    # signing and notarizing on the later versions"), which is a constraint
    # this console does not have. CABINETOS_PICODRIVE_SH2DRC builds either side.
    #
    # THE STATE IS SAFE EITHER WAY, AND WE TAKE 0 ANYWAY. Both builds were run
    # against Space Harrier through tools/state-probe.c, and at frame 600 their
    # save states are BYTE-IDENTICAL — not merely compatible. The source says
    # why: sh2_pack copies SH2_REG_SIZE bytes, which stops at `macl`, and every
    # drc field in the struct sits after it. So the recompiler is never in a
    # state, and SH2_STATE_SIZE is a compile-time constant that does not move.
    #
    # But the same test showed the two backends do NOT produce the same picture:
    # from an identical boot they diverge in video and audio digest within 60
    # frames, while converging on that identical machine state. Something
    # timing-visible lands differently — a raster effect a line out, most
    # likely — and each build is deterministic on its own, so it is the backend
    # and not noise.
    #
    # Nothing here needs the recompiler. The 32X is two 23 MHz SH2s and this is
    # an x86-64 console, so the interpreter is not the bottleneck it was on a
    # phone. Taking it would buy performance nobody is short of and pay for it
    # with a picture that differs from the Apple TV's. Matching Cabinet exactly
    # costs nothing and makes the shared tag unarguable.
    #
    # Revisit only with a measurement from real hardware showing the interpreter
    # is short on 32X, and know from the above that the states will survive it.
    MAKEARGS=("use_sh2drc=${CABINETOS_PICODRIVE_SH2DRC:-0}")
    ;;
mgba)
    REPO=https://github.com/libretro/mgba.git
    COMMIT=e31759b24e7a4e3899285ff720d7b573ac328ae7
    # Game Boy Advance, and the only core in the set with no backend question:
    # mGBA has no recompiler on any platform. What it has instead is a
    # different BUILD, because upstream dropped Makefile.libretro and left a
    # CMake target in its place.
    #
    # The first group of flags is Cabinet's, from tools/build-core.sh's cmake
    # branch, minus the four that only name an Apple SDK. They are all "do not
    # build the parts of mGBA that are not the core": no Qt or SDL front end, no
    # ffmpeg, sqlite, Discord or editline, and none of the three GL renderers,
    # which this frontend could not drive anyway.
    #
    # THE SECOND GROUP IS WHERE THIS CORE HIDES ITS unix BRANCH. mGBA has no
    # platform cases; it probes for libraries and switches features on wherever
    # it finds them. Cabinet builds against an SDK that has libz and none of the
    # rest, so it gets those features off by accident of the platform. This
    # container has freetype, json-c, libpng and libzip in it FOR THE FRONTEND,
    # and mGBA would have silently taken all four.
    #
    # One of them is not cosmetic: mGBA's own configure summary calls USE_PNG
    # "Screenshot/advanced savestate support". A lever that changes what a save
    # state can contain is exactly the thing this whole question is about.
    #
    # Not reasoned about — READ OFF THE SHIPPING ARCHIVES. `nm -u` on
    # libmgba_tvos.a and libmgba_mac.a lists zlib's inflate/deflate/crc32 and
    # nothing else: no png_*, no zip, no sqlite, no FT_*, no json_*. So zlib on
    # and the rest off is what Cabinet actually ships, rather than what it looks
    # like it should ship.
    #
    # CMAKE_BUILD_TYPE is deliberately NOT set, because Cabinet does not set it
    # either. mGBA's libretro target appends its own -O3, so the build is
    # optimised regardless; adding Release here would put a second, different
    # optimisation flag into our binary and not into Cabinet's.
    BUILDSYS=cmake
    CMAKE_TARGET=mgba_libretro
    CMAKEARGS=(
        -DBUILD_LIBRETRO=ON -DBUILD_QT=OFF -DBUILD_SDL=OFF
        -DUSE_FFMPEG=OFF -DUSE_SQLITE3=OFF -DUSE_DISCORD_RPC=OFF
        -DUSE_EDITLINE=OFF -DUSE_ELF=OFF
        -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DBUILD_GL=OFF

        -DUSE_ZLIB=ON
        -DUSE_PNG=OFF -DUSE_MINIZIP=OFF -DUSE_LIBZIP=OFF
        -DUSE_FREETYPE=OFF -DUSE_JSON_C=OFF -DUSE_LUA=OFF
        -DUSE_EPOXY=OFF -DUSE_CMOCKA=OFF -DENABLE_PYTHON=OFF
    )
    ;;
mupen64plus)
    REPO=https://github.com/libretro/mupen64plus-libretro-nx
    COMMIT=f275caf4b2bfa1e6d1c51636746ea793f3d80320
    MAKEDIR=.
    MAKEFILE=Makefile
    # Nintendo 64, and one of the three cores that renders through a GL context
    # the frontend has to own. It builds here; it cannot run here yet, and
    # catalog.cpp says so rather than offering an N64 game that would fail.
    #
    # FOUR levers, which is the most of any core in the set, and the reason
    # this one is not a one-line diff against Cabinet:
    #
    #   WITH_DYNAREC   the unix branch defaults it to the ARCH, so x86_64 — a
    #                  real recompiler, assembled with nasm. Cabinet's
    #                  ios-arm64 case forces it EMPTY, because iOS forbids a
    #                  JIT, and that is the lever in question here.
    #   DYNAFLAGS      carries -DNO_ASM, and WITHOUT IT THE LINK FAILS. Turning
    #                  the recompiler off is not enough on its own: cp0.c,
    #                  interrupt.c and r4300_core.c still call dyna_jump,
    #                  dyna_stop and dynarec_jump_to, guarded by `#ifndef
    #                  NO_ASM` rather than by WITH_DYNAREC, so the .so has five
    #                  undefined references and does not link. Cabinet gets the
    #                  define from its ios-arm64 case; the unix case has no
    #                  equivalent. DYNAFLAGS is the one variable the Makefile
    #                  sets with := and never appends to when WITH_DYNAREC is
    #                  empty, so it is the only clean way in from the command
    #                  line — every other flags variable would be REPLACED
    #                  rather than extended and take the rest of the build with
    #                  it.
    #   FORCE_GLES3    Cabinet builds GLES3; a plain unix build links desktop
    #                  -lGL. This frontend's context is EGL/GLES3, so GLES3 is
    #                  both what Cabinet has and what we could actually drive.
    #   LLE, HAVE_PARALLEL_RSP, HAVE_PARALLEL_RDP, HAVE_THR_AL
    #                  low-level RSP and RDP emulation. Cabinet turns all four
    #                  on; the unix branch leaves them at 0. Matched here, so
    #                  that the video path is the same machine as Cabinet's
    #                  rather than a different plugin wearing the same name.
    # ONE DIFFERENCE IS RECORDED RATHER THAN MATCHED. Cabinet's ios-arm64 case
    # also adds -Ofast -funsafe-math-optimizations to three separate flags
    # variables, and there is no command-line way to extend those without
    # replacing them. This build gets the unix branch's -O3 -ffast-math
    # instead. It is a floating-point difference in an emulator whose output is
    # floating point, so it is not obviously harmless — and it cannot be judged
    # until an N64 game can actually be run here.
    #
    # WHICH IS WHY THE TAG IS NOT SHARED YET. catalog.cpp returns no emulator
    # tag for this core, so nothing uploads a state that Cabinet might offer
    # back. Settle it when the frontend can host a hardware-rendered core and
    # there is something to compare.
    #
    # THE RECOMPILER IS ON, 2026-09-27 (#147). It was left out on 2026-09-13
    # to match Cabinet's Apple TV build, where Apple forbids a JIT; that is a
    # platform rule, not a save-format one, and RetroArch and Batocera run
    # this core with it on PC. Measured on the A9 before and after: see
    # docs/PROJECT.md, *The N64 and PSP recompilers*. The states it writes
    # load in the interpreter build and the other way round, because the
    # state is the emulated machine, not the engine running it.
    # CABINETOS_N64_DYNAREC= (empty) still builds the interpreter, for
    # comparing the two; NO_ASM is needed then and only then.
    N64_DYNAREC="${CABINETOS_N64_DYNAREC-x86_64}"
    MAKEARGS=(
        "WITH_DYNAREC=$N64_DYNAREC"
        FORCE_GLES3=1 GLES3=1
        LLE=1 HAVE_PARALLEL_RSP=1 HAVE_PARALLEL_RDP=1 HAVE_THR_AL=1
    )
    if [ -z "$N64_DYNAREC" ]; then MAKEARGS+=(DYNAFLAGS=-DNO_ASM); fi
    ;;
flycast)
    REPO=https://github.com/flyinghead/flycast.git
    COMMIT=a172e0001351dfbc49b86860a13d5390b1c493fe
    # Dreamcast AND Naomi, the second hardware-rendered core, and the one with a
    # Cabinet-side problem this repository cannot fix.
    #
    # CMake, like mGBA, but for the opposite reason: Flycast never had a
    # Makefile.libretro.
    #
    # THE TAG IS NOT SHARED AND CANNOT BE, TODAY. The manifest's own patch entry
    # says Flycast is built from "tools/build-flycast.sh, and UNSCRIPTED edits in
    # the working tree", so commit a172e000 plus that script does not reproduce
    # what Cabinet ships. There is no revision for our build to match, whatever
    # flags we pass. Until that diff is captured in Cabinet, catalog.cpp returns
    # no emulator tag for this core. See docs/PROJECT.md, open question 13.
    #
    # The recompiler lever is real and is NOT the blocker: Cabinet builds
    # -DTARGET_NO_REC on iOS and tvOS and turns the recompilers on for the Mac,
    # so it already ships both. CABINETOS_FLYCAST_REC exists to compare them
    # when there is a reproducible Cabinet build to compare against.
    #
    # CPU_RATIO is a patch Cabinet applies and this build does not, deliberately.
    # Upstream charges every INTERPRETED SH4 instruction 8 cycles, an effective
    # 25 MHz, which is what made heavy scenes slow down inside the emulated
    # machine; Cabinet changes it to 2. With the dynarec on, that constant is
    # not used at all, so patching it here would change nothing and only look
    # like parity.
    BUILDSYS=cmake
    CMAKE_TARGET=flycast_libretro
    CMAKEARGS=(
        -DCMAKE_BUILD_TYPE=Release
        -DLIBRETRO=ON
        -DUSE_OPENGL=ON
        -DUSE_VULKAN=ON
    )
    if [ -n "${CABINETOS_FLYCAST_REC:-}" ] && [ "$CABINETOS_FLYCAST_REC" = 0 ]; then
        CMAKEARGS+=(-DCMAKE_C_FLAGS=-DTARGET_NO_REC -DCMAKE_CXX_FLAGS=-DTARGET_NO_REC)
    fi
    ;;
ppsspp)
    REPO=https://github.com/hrydgard/ppsspp.git
    COMMIT=c989c2553e1099730736d965c221823fe974fa55
    # PlayStation Portable, the twenty-first and last core, and the third and
    # last that renders through a GL context this frontend owns.
    #
    # CMake, like mGBA and Flycast. Upstream's own libretro target, driven the
    # way Cabinet drives it — tools/build-ppsspp.sh's own header says it "just
    # drives upstream's own supported configuration", and so does this.
    #
    # TWO LEVERS, and neither of them is a recompiler, which is the surprise in
    # this core. PPSSPP picks its CPU engine AT RUNTIME from the
    # ppsspp_cpu_core option, not at compile time, so the thing that is a build
    # flag in five other cores is a core option here — see
    # catalog::optionOverrides, which has its first entry because of it.
    #
    #   USING_GLES2    THE ONE THAT DECIDES WHETHER THIS CORE RUNS HERE AT ALL.
    #                  LibretroGLContext asks for RETRO_HW_CONTEXT_OPENGLES2
    #                  when it is defined and RETRO_HW_CONTEXT_OPENGL when it
    #                  is not, and this frontend's context is EGL/GLES: it
    #                  refuses desktop GL by name rather than accepting it and
    #                  failing inside the core. Cabinet gets this from
    #                  upstream's ios.cmake toolchain; the unix build has no
    #                  equivalent and would quietly ask for desktop GL. So this
    #                  is both what Cabinet builds and the only thing that
    #                  works here, which is a pleasant coincidence rather than
    #                  a compromise.
    #   MOBILE_DEVICE  Cabinet gets this from ios.cmake too, and LIBRETRO does
    #                  not imply it — only ANDROID does. READ RATHER THAN
    #                  ASSUMED: every use of it in the tree is AVI/WAV dumping,
    #                  window geometry, the keymap and the desktop UI. Nothing
    #                  under it touches the emulated machine, and the three
    #                  sites in Core/SaveState.cpp are all dump-restart
    #                  bookkeeping around a save, not state content. It is not
    #                  free, though: the block it disables in Core/Config.cpp
    #                  also carries AnisotropyLevel's default of 4, so leaving
    #                  it off would change texture filtering against the Apple
    #                  TV's picture for no reason.
    #
    # The rest are Cabinet's own, minus the ones that only name an Apple SDK.
    BUILDSYS=cmake
    CMAKE_TARGET=ppsspp_libretro
    CMAKEARGS=(
        -DCMAKE_BUILD_TYPE=Release
        -DLIBRETRO=ON
        -DUSING_GLES2=ON
        -DMOBILE_DEVICE=ON
        -DUSE_SYSTEM_FFMPEG=OFF
        -DUSE_DISCORD=OFF
    )
    # PPSSPP's firmware is the special case this project has been warning
    # itself about since Phase 0: its system files do not come from RomM the
    # way every other platform's BIOS does, because they are not a console's
    # firmware — they are fonts, VFPU lookup tables and a per-game
    # compatibility list that ship with the emulator. On Apple they are in the
    # app bundle. Here they are files this script installs beside the core,
    # and the frontend's system directory is where they have to land:
    # retro_init appends "PPSSPP" to it and warns "Core system files missing,
    # expect bugs" when compat.ini is not there.
    #
    # The list is Cabinet's, not upstream's whole assets/ directory. 43 files
    # against 22 MB of everything, and the difference is the desktop UI's —
    # the web debugger, themes, UI images, sound effects, the SDL controller
    # database — none of which a libretro core presents. This subset is the
    # one that has actually run PSP games on a television.
    #
    # PPSSPP, capitalised, because that is the literal the core appends to the
    # system directory — not the manifest's lowercase name for it.
    ASSET_DIR=PPSSPP
    ASSET_SRC=assets
    ASSETS=(asciifont_atlas.meta asciifont_atlas.zim compat.ini compatvr.ini
            flash0 font_atlas.meta font_atlas.zim knownfuncs.ini langregion.ini
            ppge_atlas.meta ppge_atlas.zim vfpu)
    ;;
dolphin)
    REPO=https://github.com/libretro/dolphin.git
    COMMIT=1a0f97270b703c71625c87dee178d778348e540b
    # GameCube — and Wii later, for free, because Dolphin is both. The
    # twenty-second core, and the first added since the image started carrying
    # them.
    #
    # THE PLATFORM AUDIT SAID THIS WAS THE CHEAP HALF OF THE 85 MISSING GAMES
    # and it was right: `catalog.cpp` has routed `ngc -> dolphin` since the
    # table was written, so this needs no frontend change at all. What it
    # needed was a pinned revision and a place to build it, which is this.
    #
    # It was running on the reference console for a day before this arm
    # existed, from a .so built by hand and copied into a home directory — the
    # same state PlayStation 2 was in, and the same reason it had to stop:
    # nobody could say what revision it was without reading strings out of the
    # binary. That is how this commit was chosen, in fact. It is what the
    # hand-built copy turned out to be.
    #
    # CMake, and the argument list is short because upstream's own `LIBRETRO`
    # option does the work: it forces ALSA, Qt, SDL, evdev, PulseAudio,
    # analytics, the updater, the CLI tool, Discord, UPnP and the test suite
    # off by itself. Adding those here would be restating upstream's own
    # if(LIBRETRO) block and would rot the moment it changed.
    #
    # VULKAN IS WHY THIS CORE WORKS HERE AT ALL, and it is not a build flag.
    # Dolphin renders from a thread of its own, which a GLES context cannot
    # serve because a GL context belongs to one thread at a time — measured on
    # the A9, where on GLES it drew nothing and sometimes crashed. The frontend
    # hands it Vulkan instead; see docs/PROJECT.md open question 20 and
    # `vkhost.cpp`. Nothing here selects that. The core asks through
    # GET_PREFERRED_HW_RENDER and the frontend answers.
    #
    # ENABLE_X11=OFF, AND IT IS CORRECT RATHER THAN A WORKAROUND. Upstream
    # defaults it ON and then `pkg_check_modules(X11_INPUT REQUIRED xi>=1.5.0)`
    # fails the configure outright in a container that has no X11 headers —
    # which is what this core did on its first CI run.
    #
    # The tempting fix is to put libXrandr-devel and libXi-devel in the builder
    # container, and it would be the wrong one: it grows the container every
    # other core also builds in, to satisfy a dependency this core does not
    # actually have. CHECKED RATHER THAN ASSUMED — every "X11" in
    # Source/Core/DolphinLibretro is DX11, Direct3D 11, and not X11 at all. The
    # X11 support upstream means here is Dolphin's own desktop windowing and
    # input, which a libretro core never reaches: the frontend owns the window
    # and hands the core a graphics context.
    BUILDSYS=cmake
    CMAKE_TARGET=dolphin_libretro
    CMAKEARGS=(
        -DCMAKE_BUILD_TYPE=Release
        -DLIBRETRO=ON
        -DENABLE_X11=OFF
    )
    # DOLPHIN'S OWN `Sys` FOLDER, and it was on the reference console only as
    # a hand copy from 2026-09-20 until the Wii work found it (docs/PROJECT.md
    # open question 35). The core looks for `<system>/dolphin-emu/Sys`
    # (DolphinLibretro/Boot.cpp). Without it a game still boots, which is why
    # nobody noticed, and quietly loses Dolphin's 1,875 per-game settings
    # files (Donkey Kong Country Returns' texture cache, Mario Kart Wii's EFB
    # format changes, an enabled fix for A Boy and His Blob), the Wii's
    # `shared2/wc24` files it copies into the NAND at every Wii boot, the
    # cheat code handler and the post-processing shaders. Upstream's data, not
    # ours, from the same commit as the core; RetroArch's core info names it
    # as required.
    ASSET_DIR=dolphin-emu
    ASSET_SRC=Data
    ASSETS=(Sys)
    ;;
azahar)
    REPO=https://github.com/azahar-emu/azahar.git
    COMMIT=219d743a1e698101fe1ab8bef43e6563126c3c27
    # Nintendo 3DS, the twenty-third core: Azahar's own libretro core, built
    # from Azahar's repository with the emulator (src/citra_libretro). The
    # old libretro/citra fork is a different core. The research and the
    # conditions of the go are docs/PROJECT.md, open question 50.
    #
    # The commit is the 2126.2 release tag (2026-10-08, branch azahar-2126),
    # which carries the backported fix for a crash on unload (#2572). The
    # core reports the short commit as its version (GenerateBuildInfo.cmake,
    # BUILD_FULLNAME) whenever the build cannot see a tag in the GitHub
    # environment, which inside the container it never can.
    #
    # CMake, shaped like Dolphin's arm. ENABLE_LIBRETRO forces the Qt window,
    # SDL, the web services, the scripting server, the GDB stub, OpenAL,
    # cubeb, libusb and the multiplayer rooms off by itself (CMakeLists.txt,
    # _LIBRETRO_INCOMPATIBLE_OPTIONS); restating them here would rot when that
    # list changes. The target is citra_libretro; its file comes out as
    # azahar_libretro.so, which is this arm's name, so nothing is renamed.
    #
    #   ENABLE_TESTS=OFF     the test executable, which nothing here runs.
    #   ENABLE_BUILTIN_KEYBLOB=OFF
    #                        Azahar compiles a blob of the 3DS's own keys into
    #                        the binary unless told not to, and the project
    #                        ships no keys (CONTRIBUTING.md, "The rules").
    #                        Games do not need them: Azahar loads only
    #                        decrypted games. What goes is multiplayer, amiibo
    #                        and the eShop, none of which the console offers.
    #   ENABLE_LTO=OFF       as Azahar's own Linux libretro release builds it
    #                        (.github/workflows/libretro.yml), which is the
    #                        build its players run.
    BUILDSYS=cmake
    CMAKE_TARGET=citra_libretro
    CMAKEARGS=(
        -DCMAKE_BUILD_TYPE=Release
        -DENABLE_LIBRETRO=ON
        -DENABLE_TESTS=OFF
        -DENABLE_BUILTIN_KEYBLOB=OFF
        -DENABLE_LTO=OFF
    )
    ;;
*)
    echo "unknown core: $CORE" >&2
    exit 1
    ;;
esac

SRC="$SRC_ROOT/$CORE"

# The name the artifact is filed under. See the long comment at the copy below:
# it is the MANIFEST name, not upstream's output name, and the same three lines
# live in catalog::coreFileName and ci/stage-image-payload.sh.
SO="${CORE%_libretro}_libretro.so"

# The container the compile and the verification both run in.
BUILDER="${CABINETOS_BUILDER:-cabinetos-builder}"

# Read the revision back out of the FINISHED artifact and check it against the
# commit that was supposed to be built. Asserting the checkout proves what went
# in; this proves what came out, which is the assertion open question 13
# actually asks for. It runs inside the builder because the binary is linked
# against Fedora 44's glibc and the host running this script need not have it.
verify_core() {
    local expect=""
    echo "verifying $SO"
    if [ "$VERIFY_REVISION" -eq 1 ]; then
        expect="$COMMIT"
    else
        echo "note: this core cannot report its revision — see its case arm"
    fi
    podman run --rm -v "$ROOT":/repo:Z -v "$OUT":/out:Z -w /repo "$BUILDER" \
        sh -c 'gcc -O2 -Wall -Wextra -o /tmp/core-info tools/core-info.c -ldl \
               && if [ -n "$2" ]; then exec /tmp/core-info "/out/$1" "$2"; \
                  else exec /tmp/core-info "/out/$1"; fi' _ "$SO" "$expect"
    echo "sha256      $(sha256sum "$OUT/$SO" | cut -d' ' -f1)"
    # Its options against cores/options/<core>.txt. A separate script so that
    # editing the check does not change this function, which is in every
    # core's cache key in CI; read its header.
    CABINETOS_CORE_OUT="$OUT" CABINETOS_BUILDER="$BUILDER" \
        "$ROOT/cores/check-options.sh" "$CORE"
}

# CHECK AN ARTIFACT THAT ALREADY EXISTS AND BUILD NOTHING.
#
# This is for the CI cache and nothing else. A core is determined entirely by
# this script — the pinned commit, the patches and the build arguments are all
# here — and by the toolchain in frontend/Containerfile, so CI keys each core's
# cache on the parts of this script that run for that core and on the
# Containerfile (ci/core-cache-key.sh), and restores the .so instead of
# spending eight minutes rebuilding something that cannot have changed.
#
# THE ASSERTION STILL RUNS ON A CACHE HIT, and that is the point of having this
# mode rather than just skipping the job. What makes the whole workflow worth
# its runtime is that every build proves the finished binary reports its pinned
# revision; a cache that skipped the proof would be trading away the only thing
# being bought. Ten seconds of dlopen is not worth saving.
if [ -n "${CABINETOS_VERIFY_ONLY:-}" ]; then
    if [ ! -f "$OUT/$SO" ]; then
        echo "$CORE: --verify-only, but $OUT/$SO is not there" >&2
        exit 1
    fi
    echo "$CORE @ $COMMIT (from cache, not rebuilt)"
    verify_core
    exit 0
fi

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$SRC_ROOT"
    # Not --depth 1: a shallow clone of a branch cannot check out an arbitrary
    # commit, and an arbitrary commit is the entire requirement.
    #
    # --filter=blob:none instead. A partial clone takes the whole commit graph
    # and none of the file contents, then fetches the blobs the checkout below
    # actually needs — so an arbitrary commit still works, which --depth 1 is
    # the one thing that would break.
    #
    # It was PPSSPP that forced this and the numbers are not marginal. A full
    # clone of that repository is 324,844 objects and GitHub served them to the
    # test VM at 55 KB/s — three hours — on a machine that pulls a release
    # tarball at 9.8 MB/s, so it is the repository being throttled rather than
    # the network. The partial clone finished in under 45 seconds and the
    # checkout took four, for 183 MB on disk instead of gigabytes.
    #
    # Existing checkouts are untouched: this runs only when there is no .git.
    git clone --filter=blob:none "$REPO" "$SRC"
fi

git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all
git -C "$SRC" checkout --quiet --force "$COMMIT"
# Only when there is something to fetch. mupen64plus at its pinned commit has NO
# .gitmodules and one stray gitlink left in the tree —
# mupen64plus-rsp-paraLLEl/lightning/gnulib — which git cannot resolve to a URL
# and refuses outright, failing the build over a directory the build never
# reads. That is upstream debris rather than a missing dependency. Guarding on
# the file means a core WITH real submodules still gets them, and still fails
# loudly if one of those cannot be fetched.
# Deliberately NOT --filter=blob:none, although the superproject's clone above
# is. Measured on PPSSPP, whose submodules include a repository of prebuilt
# ffmpeg binaries: a partial submodule clone then has to fetch those blobs
# lazily, and GitHub served that at 14 KB/s — thirty times slower than cloning
# the same submodules whole, which comes off a cached pack. The filter helps
# where history is large and hurts where the checkout is.
if [ -f "$SRC/.gitmodules" ]; then
    git -C "$SRC" submodule update --init --recursive --quiet
fi

HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "$CORE is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
echo "$CORE @ $COMMIT"

# Source patches that TRAVEL. Most of Cabinet's in-flight patches are Apple
# walls that simply do not exist on Linux and disappear here; a few change
# BEHAVIOUR, and those have to be applied identically or the two builds are not
# the same emulator. docs/PROJECT.md, open question 13.
#
# Every one of them asserts its own anchor. A patch that silently matches
# nothing leaves a green build with the fix absent, which has happened twice on
# this project and is the reason the assertion is not optional.
#
# Each core's patches are one block of this exact shape, so that
# ci/core-cache-key.sh counts the block as that core's alone:
# `if [ "$CORE" = <core> ]; then` and `fi`, both in the first column, with
# everything between them indented, its comments too.
if [ "$CORE" = melonds ]; then
    # melonDS's libretro build has no background flush thread — __LIBRETRO__
    # compiles it out — and instead debounce-flushes the .sav two seconds after
    # the game's last SRAM write, at the end of a retro_run. Its
    # retro_unload_game is NDS::DeInit() alone, so a save made less than two
    # seconds before quitting is dropped, and save-then-quit is exactly how
    # people leave a game.
    #
    # FlushSecondaryBuffer() writes only when there is unflushed data, so this
    # is a no-op otherwise. Cabinet applies it on all three of its platforms.
    LIBRETRO_CPP="$SRC/src/libretro/libretro.cpp"
    perl -0pi -e 's/void retro_unload_game\(void\)\n\{\n   NDS::DeInit\(\);/void retro_unload_game(void)\n{\n   NDSCart_SRAMManager::FlushSecondaryBuffer();\n   NDS::DeInit();/' \
        "$LIBRETRO_CPP"
    grep -q 'NDSCart_SRAMManager::FlushSecondaryBuffer();' "$LIBRETRO_CPP" || {
        echo "melonds unload-flush patch did not apply; upstream shape changed" >&2
        exit 1
    }
    echo "patched: final SRAM flush in retro_unload_game"
fi

if [ "$CORE" = ppsspp ]; then
    # Save the GL shader cache when the CONTEXT is lost, not only when the GPU
    # object is destroyed.
    #
    # This is Cabinet's patch and it travels because the thing that makes it
    # necessary is true here too. Both frontends drive context_destroy BEFORE
    # retro_unload_game — RetroArch's order, and the one Core::unloadGame
    # already uses for every hardware-rendered core — and PPSSPP's
    # context-destroy path drops its linked-shader list on the spot. The save
    # in ~GPU_GLES then finds an empty list and writes nothing, so no
    # .glshadercache is ever produced. The periodic save is every 32,767
    # frames, about nine minutes, so a short session never reaches it either.
    #
    # The cost of not having it is a console that recompiles every shader on
    # every launch, which is the stutter a person notices and cannot explain.
    # Cabinet applies it on all three of its platforms.
    GPU_GLES="$SRC/GPU/GLES/GPU_GLES.cpp"
    perl -0pi -e '
        s/(void GPU_GLES::DeviceLost\(\) \{\n\tINFO_LOG\(Log::G3D, "GPU_GLES: DeviceLost"\);\n)/$1\t\/\/ cabinet: save the shader cache before the list below is cleared.\n\tif (shaderCachePath_.Valid() \&\& draw_ \&\& g_Config.bShaderCache) {\n\t\tshaderManagerGL_->SaveCache(shaderCachePath_, \&drawEngine_);\n\t}\n/
        unless /cabinet: save the shader cache/;
    ' "$GPU_GLES"
    grep -q 'cabinet: save the shader cache' "$GPU_GLES" || {
        echo "ppsspp shader-cache patch did not apply; upstream shape changed" >&2
        exit 1
    }
    echo "patched: shader cache saved on context loss"

    # Make the core state which CPU engine it is running, once per boot.
    #
    # THE ONLY PATCH IN THIS SCRIPT THAT CABINET DOES NOT APPLY ON EVERY
    # PLATFORM — it is in Cabinet's Mac build alone — and it is here on purpose.
    # PPSSPP picks its engine silently: MIPSState::Init turns cpuCore into one
    # of three very different objects and says nothing, and the libretro layer
    # will rewrite a request for the recompiler into the IR interpreter without
    # telling anyone. "PPSSPP runs and renders" is therefore not evidence of
    # which engine ran, and in Cabinet that question sat unresolved for days
    # because there was nothing to read.
    #
    # It is a log line and nothing else: it cannot change the machine, it
    # cannot reach a save state, and it turns the one fact this core hides into
    # one line of the frontend's own log. Given that ppsspp_cpu_core is an
    # option rather than a build flag, being able to confirm what it did is the
    # difference between a measurement and an assumption.
    #
    # WARN rather than Cabinet's INFO, because this host's log floor is WARN and
    # raising it is not free: a flag that let INFO through made PPSSPP abort at
    # exit, twice, where the identical run without it exited cleanly. A probe
    # that cannot be read is not a probe.
    MIPS_CPP="$SRC/Core/MIPS/MIPS.cpp"
    perl -0pi -e '
        s/(\tif \(PSP_CoreParameter\(\)\.cpuCore == CPUCore::JIT \|\| PSP_CoreParameter\(\)\.cpuCore == CPUCore::JIT_IR\) \{\n)/\tWARN_LOG(Log::CPU, "cabinet: CPU engine = %d (0 interpreter, 1 native JIT, 2 IR interpreter, 3 JIT+IR)", (int)PSP_CoreParameter().cpuCore);\n$1/
        unless /cabinet: CPU engine/;
    ' "$MIPS_CPP"
    grep -q 'cabinet: CPU engine' "$MIPS_CPP" || {
        echo "ppsspp cpu-engine probe patch did not apply; upstream shape changed" >&2
        exit 1
    }
    echo "patched: the core names its own CPU engine at boot"
fi

if [ "$CORE" = azahar ]; then
    # Each replacement in this patch is literal and must match exactly
    # once, or the build stops: a patch that matches nothing is a green build
    # without it, with every game in the first game's folders.
    patch_once() {
        FROM="$2" TO="$3" perl -0pi -e '
            my $n = () = /\Q$ENV{FROM}\E/g;
            die "matched $n times\n" unless $n == 1;
            s/\Q$ENV{FROM}\E/$ENV{TO}/;
        ' "$SRC/$1" || {
            echo "azahar patch did not apply to $1; upstream shape changed" >&2
            exit 1
        }
    }

    # EACH GAME SETS ITS OWN FOLDERS. Azahar's bug, not ours, tracked on
    # #534 (reported as azahar-emu/azahar#2694); drop this patch when
    # an Azahar release fixes SetUserPath. Every start hands the core that
    # game's and that person's save folder, and SetUserPath
    # (src/common/file_util.cpp) fills its table of folders with emplace,
    # which never replaces an entry already there. The core is never really
    # unloaded (glibc keeps a library with GNU unique symbols loaded, and
    # Azahar has thousands), so the table outlives the game: every 3DS game
    # after the first since the frontend started, and every person after the
    # first, read and wrote the first game's sdmc, nand, sysdata and shader
    # folders, and their saves went up to RomM under the first game. The
    # table is emptied only when the folder changes: the core calls
    # SetUserPath again, with the same folder, whenever an option changes
    # while a game runs (the pause menu's rows), and emptying it then would
    # free paths the running game may be reading. Nothing fills the table
    # before it in this build (UpdateUserPath is the Qt window's alone), so
    # nothing is lost.
    patch_once src/common/file_util.cpp 'void SetUserPath(const std::string& path) {
    std::string& user_path = g_paths[UserPath::UserDir];' \
        'void SetUserPath(const std::string& path) {
    if (path != g_paths[UserPath::UserDir])
        g_paths.clear(); // cabinetos: each game sets its own folders
    std::string& user_path = g_paths[UserPath::UserDir];'
    echo "patched: each game sets its own folders (SetUserPath drops the last game's)"
fi

if [ "$CORE" = mame2003_plus ]; then
    # NOT A PATCH, A CHECK (#576). A fight stick's layout in a MAME 2003-Plus
    # game (frontend/src/fightstick.cpp, kClassic) puts the game's buttons in
    # order by the core's Classic table, copied from this file at the pin:
    # which joypad id is which of the game's buttons, for the plain joypad the
    # console plugs into every port. A pin where either reads otherwise stops
    # here, so the copy is put right in the same change instead of a stick's
    # buttons landing in the wrong places with no error.
    MAME_C="$SRC/src/mame2003/mame2003.c"
    grep -Eq '^#define PAD_CLASSIC +RETRO_DEVICE_JOYPAD$' "$MAME_C" || {
        echo "mame2003_plus: PAD_CLASSIC is no longer the plain joypad; check fightstick.cpp's kClassic" >&2
        exit 1
    }
    CLASSIC=$(sed -n '/^#define EMIT_RETROPAD_CLASSIC(/,/EMIT_COMMON_CODES/ s/.*OSD_JOYPAD_\([A-Z0-9]*\), *JOYCODE_##DISPLAY_IDX##_BUTTON\([0-9]*\)}.*/\1:\2/p' "$MAME_C" |
        grep -Ev '^(L3|R3):' | tr '\n' ' ')
    if [ "$CLASSIC" != "B:1 A:2 Y:3 X:4 L:5 R:6 L2:7 R2:8 " ]; then
        echo "mame2003_plus: its Classic table reads '$CLASSIC', not B 1, A 2, Y 3, X 4, L 5, R 6, L2 7, R2 8; put fightstick.cpp's kClassic right" >&2
        exit 1
    fi
    echo "checked: the Classic button table matches fightstick.cpp"

    # AND A SECOND CHECK (#577). Holding Start opens MAME 2003-Plus's own
    # menu: the console presses Tab for it, and keeps player 1's left stick
    # click from it, since that click is the menu's other key
    # (frontend/src/arcadesettings.h). That leans on three things here: the
    # menu's keys, Tab or player 1's ninth button; the left stick click being
    # the ninth button in all four layouts; and the keyboard being read beside
    # the pad by default. A pin where any reads otherwise stops here, so the
    # hold is put right in the same change instead of opening nothing.
    INPTPORT_C="$SRC/src/inptport.c"
    grep -Eq 'IPT_UI_CONFIGURE, +"Config Menu", +SEQ_DEF_3\(KEYCODE_TAB, +CODE_OR, JOYCODE_1_BUTTON9\)' "$INPTPORT_C" || {
        echo "mame2003_plus: its menu no longer opens on Tab or player 1's ninth button; check arcadesettings.h" >&2
        exit 1
    }
    L3_NINTH=$(grep -c 'OSD_JOYPAD_L3, *JOYCODE_##DISPLAY_IDX##_BUTTON9}' "$MAME_C")
    if [ "$L3_NINTH" != 4 ]; then
        echo "mame2003_plus: the left stick click is the ninth button in $L3_NINTH of its 4 layouts, not all; check arcadesettings.h" >&2
        exit 1
    fi
    sed -n '/option_def_input_interface = {/,/^};/p' "$SRC/src/mame2003/core_options.c" |
        grep -q '^ *"simultaneous"$' || {
        echo "mame2003_plus: it no longer reads the keyboard beside the pad by default; check arcadesettings.h" >&2
        exit 1
    }
    echo "checked: MAME's menu opens on Tab, and the left stick click is its other key"
fi

if [ "$CORE" = fbneo_libretro ]; then
    # NOT A PATCH, A CHECK (#576). A fight stick leaves FBNeo's own
    # combination buttons off (frontend/src/fightstick.cpp,
    # fbneoCombination), and knows them only by their names: FBNeo makes
    # each "P1 Buttons AB" and the like, takes the "P1 " off, and the
    # console reads "Buttons ...". A pin where a combination button is named
    # otherwise, the "P1 " stays on, or a game's own button is named
    # "Buttons ..." stops here, so the stick's rule is put right in the same
    # change instead of a combination button taking one of a game's places.
    RETRO_INPUT="$SRC/src/burner/libretro/retro_input.cpp"
    ALL=$(grep -c 'sprintf(pgi->Macro.szName,' "$RETRO_INPUT")
    NAMED=$(grep -c 'sprintf(pgi->Macro.szName, "P%i Buttons ' "$RETRO_INPUT")
    if [ "$ALL" -eq 0 ] || [ "$ALL" != "$NAMED" ]; then
        echo "fbneo_libretro: $NAMED of its $ALL combination buttons are named \"P%i Buttons ...\"; check fightstick.cpp's fbneoCombination" >&2
        exit 1
    fi
    grep -qF "if (strlen(szn) > 3 && szn[0] == 'P' && szn[2] == ' ')" "$RETRO_INPUT" || {
        echo "fbneo_libretro: it no longer takes \"P1 \" off a button's name the way it did; check fightstick.cpp's fbneoCombination" >&2
        exit 1
    }
    if grep -rqE '\{ *"(P[0-9] )?Buttons ' "$SRC/src/burn/drv"; then
        echo "fbneo_libretro: a game's own button is named \"Buttons ...\"; check fightstick.cpp's fbneoCombination" >&2
        exit 1
    fi
    echo "checked: FBNeo's combination buttons are the only \"Buttons ...\""
fi

if [ "$CORE" = flycast ]; then
    # NOT A PATCH, A CHECK. Naomi and Atomiswave games lean on three things
    # here: the board memory Flycast writes, `reicast/<the zip's own
    # name>.nvmem` in the save folder (frontend/src/savetable.cpp, the one
    # arcade file of Flycast's that reaches RomM); its RetroPad order for a
    # board's buttons, B A Y X R L R2 L2 on Naomi and B A Y X R on Atomiswave,
    # which it declares the game's buttons in (frontend/src/fightstick.cpp);
    # and the board's Test and Service on L3 and R3, which the console keeps
    # from the game and presses Test with when Start is held
    # (frontend/src/arcadesettings.h). A pin where any reads otherwise stops
    # here, so the console is put right in the same change.
    FLY_LR="$SRC/shell/libretro/libretro.cpp"
    if ! { grep -q 'snprintf(save_dir, sizeof(save_dir), "%s%creicast%c", dir, slash, slash);' "$FLY_LR" &&
        grep -q 'arcadeFlashPath = std::string(save_dir) + g_base_name;' "$FLY_LR" &&
        grep -q 'sys_nvmem->Save(getArcadeFlashPath());' "$SRC/core/hw/flashrom/nvmem.cpp" &&
        grep -q 'suffix = ".nvmem";' "$SRC/core/hw/flashrom/nvmem.cpp"; }; then
        echo "flycast: its arcade board memory is no longer reicast/<file>.nvmem in the save folder; check savetable.cpp" >&2
        exit 1
    fi
    for MAP in nao_joymap aw_joymap; do
        ORDER=$(sed -n "/static const uint32_t ${MAP}\[\] =/,/};/ s/.*JOYPAD_\([A-Z0-9]*\) *\*\/ *[A-Z]*_\([A-Z0-9]*\)_KEY.*/\1:\2/p" "$FLY_LR" | tr '\n' ' ')
        case "$MAP:$ORDER" in
        "nao_joymap:B:BTN0 Y:BTN2 SELECT:COIN START:START UP:UP DOWN:DOWN LEFT:LEFT RIGHT:RIGHT A:BTN1 X:BTN3 L:BTN5 R:BTN4 L2:BTN7 R2:BTN6 L3:TEST R3:SERVICE ") ;;
        "aw_joymap:B:BTN0 Y:BTN2 SELECT:COIN START:START UP:UP DOWN:DOWN LEFT:LEFT RIGHT:RIGHT A:BTN1 X:BTN3 R:BTN4 L3:TEST R3:SERVICE ") ;;
        *)
            echo "flycast: its $MAP reads '$ORDER'; check fightstick.cpp and arcadesettings.h" >&2
            exit 1
            ;;
        esac
    done
    echo "checked: Flycast's board memory, button order, Test and Service"
fi

# platform=unix is the core's own Linux case, and on every Makefile-based core
# in the set it is also the default when uname says Linux. It is the
# best-tested path these cores have; the ios-arm64 case Cabinet uses is the
# unusual one. BUILDER is set near the top, because verify_core needs it too.

# safe.directory is not paranoia about this checkout, it is about the core's
# own Makefile. Every Makefile-based core in the set does
#   GIT_VERSION := $(shell git rev-parse --short HEAD || echo unknown)
# and compiles the answer into the version string it reports. If git refuses
# the bind-mounted tree as dubiously owned it fails QUIETLY into "unknown", the
# `||` swallows it, and the core ships not knowing what revision it is. Passing
# it through the environment avoids writing a gitconfig into the mounted tree.
if [ "$BUILDSYS" = cmake ]; then
    # Out of tree, into the checkout, so that the .so discovery below and the
    # `make clean` a rebuild wants both find it in the one place a core's
    # output ever lives. .cabinetos-build is not a name upstream uses.
    podman run --rm -v "$SRC":/src:Z -w /src \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0=/src \
        "$BUILDER" \
        sh -c 'target=$1; shift
               cmake -S /src -B /src/.cabinetos-build "$@" \
               && cmake --build /src/.cabinetos-build --target "$target" -j'"$(nproc)" \
        _ "$CMAKE_TARGET" "${CMAKEARGS[@]}"
else
    podman run --rm -v "$SRC":/src:Z -w /src \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0=/src \
        "$BUILDER" \
        make -C "$MAKEDIR" -f "$MAKEFILE" platform=unix "${MAKEARGS[@]}" -j"$(nproc)"
fi

mkdir -p "$OUT"

# DISCOVER the .so rather than being told its name, and file it under the
# MANIFEST core name.
#
# Upstream output names do not match manifest names and there is no rule to it:
# beetle_ngp builds mednafen_ngp_libretro.so, beetle_pce_fast builds
# mednafen_pce_fast_libretro.so. Hand-maintaining that list for every core
# is a table that goes stale, and the frontend would need a second copy of it to
# find anything.
#
# So the artifact is named after the core as the manifest knows it — the same
# identity the pins, the emulator tags and catalog.cpp already use — and this
# script finds whatever was actually produced. One rule, no mapping.
mapfile -t BUILT < <(find "$SRC" -name '*_libretro.so' -newer "$SRC/.git" 2>/dev/null)
if [ "${#BUILT[@]}" -eq 0 ]; then
    mapfile -t BUILT < <(find "$SRC" -name '*_libretro.so')
fi
[ "${#BUILT[@]}" -ne 0 ] || { echo "no *_libretro.so was produced" >&2; exit 1; }
if [ "${#BUILT[@]}" -gt 1 ]; then
    echo "ambiguous: the build produced ${#BUILT[@]} cores" >&2
    printf '  %s\n' "${BUILT[@]}" >&2
    exit 1
fi
# A manifest name that already ends in _libretro does not get a second one:
# fbneo_libretro would otherwise be filed as fbneo_libretro_libretro.so.
# catalog.cpp applies the same rule when it looks for the file — the two must
# agree, and this comment is on both. SO is set near the top so that
# --verify-only knows the name without building anything.
UPSTREAM=$(basename "${BUILT[0]}")
[ "$UPSTREAM" = "$SO" ] || echo "built $UPSTREAM, filing it as $SO"
cp "${BUILT[0]}" "$OUT/$SO"
echo "wrote $OUT/$SO ($(du -h "$OUT/$SO" | cut -f1))"

# The core's own system files, for the cores that have any.
#
# Every entry is asserted to exist before anything is copied. A missing one
# means upstream moved it, and the failure that would otherwise follow is the
# worst kind this project has: the core loads, the game starts, and it is
# quietly wrong — PPSSPP without its font files renders no text and says so
# only in a log line nobody reads.
if [ "${#ASSETS[@]}" -ne 0 ]; then
    DEST="$SYSTEM_OUT/$ASSET_DIR"
    for a in "${ASSETS[@]}"; do
        [ -e "$SRC/$ASSET_SRC/$a" ] || {
            echo "$CORE: $ASSET_SRC/$a is not in the source tree at $COMMIT" >&2
            exit 1
        }
    done
    rm -rf "$DEST"
    mkdir -p "$DEST"
    for a in "${ASSETS[@]}"; do
        cp -R "$SRC/$ASSET_SRC/$a" "$DEST/"
    done
    echo "system      $DEST ($(du -sh "$DEST" | cut -f1), ${#ASSETS[@]} entries)"
    echo "            copy its parent's contents into the frontend's system directory"
fi

# Asserting the CHECKOUT is at the pinned commit proves what went in;
# verify_core proves what came out. See its definition near the top.
verify_core
