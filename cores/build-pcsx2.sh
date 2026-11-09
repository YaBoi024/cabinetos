#!/usr/bin/env bash
#
# Builds upstream PCSX2 as a LIBRARY for Linux x86-64, at an exact revision,
# and measures what a CabinetOS host layer would still owe it.
#
# WHY THIS IS NOT cores/build-core.sh. That script builds libretro cores: one
# upstream, one `make`, one .so that already implements a frontend contract.
# PCSX2 is not a libretro core and has no retro_run. It is a whole emulator
# that expects a frontend to exist around it, and upstream ships exactly two —
# the Qt desktop app and the headless GS runner. CabinetOS has to be the third.
# Until that third one is written there is no .so to ship, so this script's
# product is a library plus an honest measurement, not a core.
#
# THE QUESTION IT WAS WRITTEN TO ANSWER, AND THE ANSWER:
#
#   Does upstream PCSX2's CMake produce a linkable library on Linux, or must
#   the frontend be carved out the way Cabinet had to on the Mac?
#
#   IT PRODUCES ONE, AND NOTHING HAS TO BE CARVED OUT. `add_library(PCSX2)` is
#   upstream's own target in pcsx2/CMakeLists.txt; the application is a
#   SEPARATE target, pcsx2-qt, behind `if(ENABLE_QT_UI)` in the top-level
#   CMakeLists. Turning that option off is supported upstream and leaves the
#   emulator as a static library. Building that takes no patch. Compare
#   tools/patch-pcsx2-mac.py in Cabinet, which is 546 lines of them. The one
#   patch here is for the sound, not the build (below).
#
# Usage:
#   cores/build-pcsx2.sh              build the library and run the probe
#   cores/build-pcsx2.sh --probe-only re-run the probe against an existing build
#
# Run it on a machine with the container image and the disk: the A9, the test
# VM, or a CI runner. It needs about 1.5 GB for the checkout and the build.

set -euo pipefail

# UPSTREAM PCSX2/pcsx2, AND NOT THE isztldav FORK CABINET PINS.
#
# That fork exists to add an ARM64 recompiler for Apple Silicon, where upstream
# stubs one out. This is x86-64, where upstream's recompiler is the original
# rather than a translation of it, and the fork is 291 commits behind master.
# Taking it here would be shipping a staler, Apple-specific PCSX2 on the one
# platform PCSX2 supports natively. docs/PROJECT.md, open question 12b.
#
# AND NOT libretro/pcsx2 EITHER, WHICH DOES NOT EXIST. `git ls-remote` on it
# says "Repository not found". That is what sent PlayStation 2 down this route
# rather than the libretro one; see open question 12b again.
REPO=https://github.com/PCSX2/pcsx2.git

# v2.8.2, the current stable release, 2026-09-04. The version to pin against is
# a decision rather than a default: upstream's HEAD is a v2.9.x development
# series, and a console should follow releases.
COMMIT=fd9d310ccbb6b8b62c976da8886a3c8fd3a10ff3
TAG=v2.8.2

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/pcsx2-upstream"
BUILD=build-cabinetos
BUILDER="${CABINETOS_PCSX2_BUILDER:-cabinetos-pcsx2-builder}"
OUT="${CABINETOS_PCSX2_OUT:-$ROOT/cores/build/pcsx2}"

PROBE_ONLY=0
[ "${1:-}" = "--probe-only" ] && PROBE_ONLY=1

# --- the container ---------------------------------------------------------
if ! podman image exists "$BUILDER"; then
    echo "building $BUILDER"
    podman build -t "$BUILDER" -f "$ROOT/cores/pcsx2-builder/Containerfile" \
        "$ROOT/cores/pcsx2-builder"
fi

# --- the source ------------------------------------------------------------
if [ "$PROBE_ONLY" -eq 0 ]; then
    mkdir -p "$SRC_ROOT"
    if [ ! -d "$SRC/.git" ]; then
        # --filter=blob:none for the same reason build-core.sh gives, and there
        # are NO submodules to worry about here: PCSX2's .gitmodules is an empty
        # file and all of 3rdparty/ is vendored in the tree. 151 MB checked out,
        # 44 MB of which is 3rdparty.
        git clone --filter=blob:none "$REPO" "$SRC"
    fi
    git -C "$SRC" fetch --quiet origin "$COMMIT" 2>/dev/null || git -C "$SRC" fetch --quiet --all --tags
    git -C "$SRC" checkout --quiet --force "$COMMIT"

    # Asserted, never trusted. Same rule as every core in build-core.sh.
    HEAD=$(git -C "$SRC" rev-parse HEAD)
    if [ "$HEAD" != "$COMMIT" ]; then
        echo "pcsx2 is at $HEAD, expected the pinned $COMMIT" >&2
        exit 1
    fi
    echo "pcsx2 @ $COMMIT ($TAG)"

    # ONE PATCH. It is not needed to build PCSX2. It is needed to stop PCSX2
    # making its own sound.
    #
    # THE CONSOLE OWNS AUDIO. Every libretro core hands its samples
    # to the frontend, which owns one device, one volume and one latency, and
    # lets the in-game overlay duck it. PCSX2 would otherwise open a second
    # device of its own through cubeb or SDL, which is none of those things and
    # is the same shape of fault as a second input path onto the same pad.
    #
    # Three lines: SPU2's SDL backend is pointed at CabinetCreateAudioStream,
    # which frontend/ps2/CabinetPS2Audio.cpp defines and which resolves at link
    # time. Cabinet makes the same edit on the Mac, for the same reason.
    #
    # It asserts its own anchor, because a scripted edit that silently matches
    # nothing leaves a green build with the change absent — which has happened
    # twice on this project and is why every patch in cores/build-core.sh does
    # the same.
    AUDIO_CPP="$SRC/pcsx2/Host/AudioStream.cpp"
    AUDIO_H="$SRC/pcsx2/Host/AudioStream.h"

    if ! grep -q CABINET_AUDIO "$AUDIO_CPP"; then
        python3 - "$AUDIO_CPP" <<'PATCH'
import sys, pathlib
p = pathlib.Path(sys.argv[1])
t = p.read_text()
old = ("\t\tcase AudioBackend::SDL:\n"
       "\t\t\treturn CreateSDLAudioStream(sample_rate, parameters, stretch_enabled, error);")
new = ("\t\tcase AudioBackend::SDL:\n"
       "\t\t\t// CABINET_AUDIO: the console owns audio, so SPU2's samples go to\n"
       "\t\t\t// the frontend rather than to a device of PCSX2's own. See\n"
       "\t\t\t// frontend/ps2/CabinetPS2Audio.cpp and cores/build-pcsx2.sh.\n"
       "\t\t\treturn CabinetCreateAudioStream(sample_rate, parameters, stretch_enabled, error);")
if old not in t:
    raise SystemExit("CABINET_AUDIO: the SDL backend case has moved; patch not applied")
p.write_text(t.replace(old, new, 1))
PATCH
        grep -q CABINET_AUDIO "$AUDIO_CPP" || { echo "audio patch did not apply" >&2; exit 1; }
        echo "patched: SPU2's SDL backend hands its samples to the frontend"
    fi

    if ! grep -q CabinetCreateAudioStream "$AUDIO_H"; then
        cat >> "$AUDIO_H" <<'DECL'

// CABINET_AUDIO: defined in frontend/ps2/CabinetPS2Audio.cpp and resolved at
// link time. Appended rather than placed beside the other factories, which sit
// above the class this returns and cannot name it.
std::unique_ptr<AudioStream> CabinetCreateAudioStream(
	u32 sample_rate, const AudioStreamParameters& parameters, bool stretch_enabled, Error* error);
DECL
        echo "patched: declared CabinetCreateAudioStream"
    fi
fi

# --- configure and build ---------------------------------------------------
# Every flag here is a decision, so each one says why.
#
#   ENABLE_QT_UI=OFF    the frontend is CabinetOS's. This is the flag that makes
#                       the whole route work: it drops add_subdirectory(pcsx2-qt)
#                       and leaves the PCSX2 library standing on its own.
#   ENABLE_TESTS=OFF    googletest builds a host binary, pointless here.
#   ENABLE_GSRUNNER=OFF not built by default — but it is still CONFIGURED, so
#                       the probe below can build it on demand as a link test.
#   USE_VULKAN=ON       the point. PCSX2 supports Vulkan natively and the A9 has
#                       it, so the Metal wall Cabinet had to climb is not here.
#                       docs/PROJECT.md, open question 20.
#   USE_OPENGL=ON       kept as the fallback for a machine with no Vulkan, which
#                       is the test VM, and which is exactly why the frontend's
#                       own host discovers rather than assumes.
#   X11_API=ON          the console runs on X11 under gamescope. There is no EGL
#                       display in that process at all; see docs/lessons/emulators.md.
#   WAYLAND_API=OFF     would pull in extra-cmake-modules and Wayland-Egl for a
#                       windowing path this frontend will never use.
#   USE_BACKTRACE=OFF   libbacktrace-devel is the one dependency Fedora 44 has
#                       no package for. It is a crash-reporter nicety.
#   POSITION_INDEPENDENT_CODE=ON
#                       upstream's default, and load-bearing here rather than
#                       incidental: open question 12's correction says each
#                       emulator becomes its own .so, and a non-PIC static
#                       library cannot be linked into one at all.
#   DISABLE_ADVANCE_SIMD=ON
#                       PCSX2's "multi-ISA" build: SSE4.1 as the floor, with
#                       AVX and AVX2 versions of the hot code chosen at run
#                       time. WITHOUT IT PCSX2 BUILDS WITH -march=native, for
#                       whatever processor the CI runner has. Every image
#                       until 2026-10-04 did, and played only because the
#                       runners happened to suit the A9; the PCSX2 rebuilt for
#                       #226 got one that did not, and PS2 died with SIGILL in
#                       ReverbDownsample_avx the moment a game made sound.
#                       PCSX2's own Linux, Flatpak, Windows and Mac release
#                       builds all set this. The check after the build below
#                       refuses -march=native if it ever comes back.
CMAKE_ARGS=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    -DENABLE_QT_UI=OFF
    -DENABLE_TESTS=OFF
    -DENABLE_GSRUNNER=OFF
    -DUSE_VULKAN=ON
    -DUSE_OPENGL=ON
    -DX11_API=ON
    -DWAYLAND_API=OFF
    -DUSE_BACKTRACE=OFF
    -DPOSITION_INDEPENDENT_CODE=ON
    -DDISABLE_ADVANCE_SIMD=ON
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

run_in_builder() {
    # safe.directory through the environment rather than a gitconfig written
    # into the mounted tree — build-core.sh's reasoning, and it matters here
    # too: PCSX2's get_git_version_info() shells out to git and compiles the
    # answer in. A refused checkout gives a library that does not know which
    # revision it is.
    podman run --rm -v "$SRC":/src:Z -w /src \
        -e GIT_CONFIG_COUNT=1 \
        -e GIT_CONFIG_KEY_0=safe.directory \
        -e GIT_CONFIG_VALUE_0=/src \
        "$BUILDER" bash -c "$1"
}

if [ "$PROBE_ONLY" -eq 0 ]; then
    run_in_builder "cmake -B $BUILD $(printf '%q ' "${CMAKE_ARGS[@]}") ."
    run_in_builder "cmake --build $BUILD --target PCSX2 GS-sse4 GS-avx GS-avx2 --parallel \$(nproc)"

    # MULTI-ISA MAKES PCSX2 AN OBJECT LIBRARY, NOT libpcsx2.a (pcsx2/
    # CMakeLists.txt: `add_library(PCSX2 OBJECT)` under DISABLE_ADVANCE_SIMD),
    # with the GS, IPU and SPU2 code built three more times, once per
    # instruction set, as libGS-sse4.a, libGS-avx.a and libGS-avx2.a. The
    # objects are archived here as libpcsx2.a, so everything after this reads
    # one library as before; compile.sh links the three beside it.
    run_in_builder "rm -f $BUILD/pcsx2/libpcsx2.a && find $BUILD/pcsx2/CMakeFiles/PCSX2.dir -name '*.o' | sort | xargs ar qcs $BUILD/pcsx2/libpcsx2.a"
    for a in libpcsx2.a libGS-sse4.a libGS-avx.a libGS-avx2.a; do
        test -s "$SRC/$BUILD/pcsx2/$a" || { echo "PCSX2's build did not produce $a" >&2; exit 1; }
    done

    # A BUILD FOR ONE PROCESSOR IS NOT A BUILD FOR THE CONSOLE. See
    # DISABLE_ADVANCE_SIMD above: the runner's CPU is not the player's.
    # Asserted to exist first: a grep of a missing file finds nothing and
    # would pass.
    test -s "$SRC/$BUILD/compile_commands.json" || {
        echo "no compile_commands.json to check the processor target in" >&2
        exit 1
    }
    if grep -q -- '-march=native' "$SRC/$BUILD/compile_commands.json"; then
        echo "PCSX2 was built with -march=native, for the build machine's processor only" >&2
        exit 1
    fi

    mkdir -p "$OUT"
    cp "$SRC/$BUILD/pcsx2/"{libpcsx2.a,libGS-sse4.a,libGS-avx.a,libGS-avx2.a} "$OUT/"
    cp "$SRC/$BUILD/common/libcommon.a" "$OUT/"
    find "$SRC/$BUILD/3rdparty" -name '*.a' -exec cp {} "$OUT/" \;
    echo "wrote $OUT ($(du -sh "$OUT" | cut -f1), $(find "$OUT" -name '*.a' | wc -l) archives)"
fi

# --- CabinetOS's host layer ------------------------------------------------
# Compiled OUTSIDE PCSX2's own CMake, against the flags its build used. That is
# the whole reason this script still says "zero patches": adding our sources to
# PCSX2's CMakeLists — which is what Cabinet had to do on the Mac — would mean
# editing upstream's tree, and there is no need to on Linux. The include paths
# and defines come out of compile_commands.json for a real PCSX2 translation
# unit, so they cannot drift from what the library was built with.
if [ -d "$ROOT/frontend/ps2" ]; then
    # rm FIRST. `cp -r src dst` copies INTO dst when dst already exists, so a
    # second run produced cabinet-ps2/ps2/ and went on compiling the previous
    # run's files — a stale build that looks exactly like a change that did not
    # take. It cost one confusing link error before anybody looked.
    rm -rf "$SRC/cabinet-ps2"
    cp -r "$ROOT/frontend/ps2" "$SRC/cabinet-ps2"
    run_in_builder "bash /src/cabinet-ps2/compile.sh $BUILD $TAG" || {
        echo "the host layer did not build" >&2
        exit 1
    }
    cp "$SRC/$BUILD/cabinet-ps2-probe" "$OUT/" 2>/dev/null || true
    # The file the console loads. Named the way catalog.cpp will look for it.
    cp "$SRC/$BUILD/cabinetos-ps2.so" "$OUT/" 2>/dev/null || true

    # --- EVERYTHING ELSE THE CONSOLE NEEDS TO RUN IT ------------------------
    #
    # This script used to stop at the .so, and for as long as PlayStation 2 was
    # only ever run by hand on the reference console that was enough: the two
    # bundled libraries and PCSX2's resources had been put beside it by hand,
    # once, and nothing wrote down that they had been. An image built from this
    # script's output would have carried an emulator that cannot dlopen and,
    # if it had, one that refuses to start because its resources are absent.
    #
    # So the OUTPUT OF THIS SCRIPT IS NOW THE WHOLE PAYLOAD, which is what CI
    # uploads and what ci/stage-image-payload.sh installs.

    # The libraries the console's image does not have. compile.sh works out
    # which those are rather than being told — see the long comment there — and
    # writes the list it acted on, so this copies what it actually bundled and
    # cannot drift from it.
    if [ -f "$SRC/$BUILD/cabinetos-ps2.bundled" ]; then
        cp "$SRC/$BUILD/cabinetos-ps2.bundled" "$OUT/"
        while read -r soname; do
            [ -n "$soname" ] || continue
            cp -L "$SRC/$BUILD/$soname" "$OUT/" || {
                echo "compile.sh said it bundled $soname and it is not there" >&2
                exit 1
            }
        done < "$SRC/$BUILD/cabinetos-ps2.bundled"
        echo "bundled libraries: $(tr '\n' ' ' < "$OUT/cabinetos-ps2.bundled")"
    fi

    # PCSX2 REFUSES TO START WITHOUT ITS RESOURCES — its game database, its
    # fonts and its GS shaders. Not a warning and not a degraded picture: it
    # does not boot. They are upstream's own bin/resources and they are 9.4 MB.
    if [ -d "$SRC/bin/resources" ]; then
        rm -rf "$OUT/resources"
        cp -R "$SRC/bin/resources" "$OUT/resources"
        echo "resources: $(du -sh "$OUT/resources" | cut -f1)"
    else
        echo "upstream has no bin/resources — PCSX2 would not start" >&2
        exit 1
    fi

    # PCSX2'S OWN GAME PATCHES, patches.zip (#217). Every official PCSX2
    # download carries it; it is not in the source, because upstream's CI
    # downloads the newest one at build time ("releases/latest", rewritten on
    # every change). That is not a pin, so the zip is packed here the way
    # PCSX2/pcsx2_patches packs it (pack_patches.yml: the files of patches/,
    # flat), from a fixed commit of that repository. Moving PATCHES_COMMIT is a
    # version bump like any other.
    PATCHES_REPO=https://github.com/PCSX2/pcsx2_patches.git
    PATCHES_COMMIT=9f82a4d2b8a2aaf83807421f18d5d30263ea826c   # 2026-09-22
    PATCHES_SRC="$SRC_ROOT/pcsx2_patches"
    if [ ! -d "$PATCHES_SRC/.git" ]; then
        git clone --quiet --filter=blob:none "$PATCHES_REPO" "$PATCHES_SRC"
    fi
    git -C "$PATCHES_SRC" fetch --quiet origin "$PATCHES_COMMIT" 2>/dev/null || true
    git -C "$PATCHES_SRC" checkout --quiet --force "$PATCHES_COMMIT"
    if [ "$(git -C "$PATCHES_SRC" rev-parse HEAD)" != "$PATCHES_COMMIT" ]; then
        echo "pcsx2_patches is not at the pinned $PATCHES_COMMIT" >&2
        exit 1
    fi
    # Sorted names and a fixed time, so the same commit makes the same bytes.
    python3 - "$PATCHES_SRC/patches" "$OUT/resources/patches.zip" <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
names = sorted(n for n in os.listdir(src) if n.lower().endswith(".pnach"))
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n in names:
        info = zipfile.ZipInfo(n, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        with open(os.path.join(src, n), "rb") as f:
            z.writestr(info, f.read())
print(f"patches.zip: {len(names)} games, {os.path.getsize(out)} bytes")
PY
    if [ ! -s "$OUT/resources/patches.zip" ]; then
        echo "patches.zip was not made — PS2 would have no widescreen patches" >&2
        exit 1
    fi
fi

# --- the probe -------------------------------------------------------------
# THREE THINGS ARE ASSERTED, AND EACH ONE FAILED A DIFFERENT WAY WHEN IT WAS
# FIRST RUN BY HAND, WHICH IS WHY ALL THREE ARE HERE RATHER THAN JUST THE FIRST.
#
#   1. The library exists and carries the renderers and recompilers this
#      console needs. A library that built but contained a software renderer
#      and no JIT would be a green build and a useless one.
#   2. It LINKS. A static library resolves nothing — Cabinet's own comment on
#      CabinetPS2Smoke.cpp says so — and the cheapest honest link test is
#      upstream's own non-Qt frontend, pcsx2-gsrunner, which is 1332 lines in
#      one file and already implements the whole Host contract.
#   3. What a host layer still owes it, counted rather than estimated. A shared
#      object will link happily with undefined symbols and then fail at dlopen
#      with the FIRST one only, which tells you nothing about the size of the
#      job. `-z defs` makes the linker refuse instead and name them all.
cat > /tmp/cabinetos-pcsx2-probe.sh <<'PROBE'
set -u
cd "/src/$1"
L=pcsx2/libpcsx2.a
fail() { echo "PROBE FAILED: $*" >&2; exit 1; }

echo "--- 1. what is in the library ---"
[ -f "$L" ] || fail "no libpcsx2.a"
vk=$(nm -C "$L" 2>/dev/null | grep -c "GSDeviceVK::")
gl=$(nm -C "$L" 2>/dev/null | grep -c "GSDeviceOGL")
mtl=$(nm -C "$L" 2>/dev/null | grep -c "GSDeviceMTL")
rec=$(nm -C "$L" 2>/dev/null | grep -c "microVU")
printf "  %-26s %s\n" "Vulkan renderer" "$vk symbols"
printf "  %-26s %s\n" "OpenGL renderer" "$gl symbols"
printf "  %-26s %s\n" "Metal renderer" "$mtl symbols (must be 0 on Linux)"
printf "  %-26s %s\n" "microVU recompiler" "$rec symbols"
[ "$vk"  -gt 0 ] || fail "no Vulkan renderer — USE_VULKAN did not take"
[ "$rec" -gt 0 ] || fail "no recompiler — the whole point of x86-64"
[ "$mtl" -eq 0 ] || fail "Metal renderer on Linux, which cannot be right"

echo "--- 2. does CabinetOS's host layer link? ---"
# **THIS USED TO BUILD UPSTREAM'S OWN pcsx2-gsrunner AND NO LONGER CAN.** That
# was the right test while there was no host layer here: a static library
# resolves nothing, so linking SOMETHING was the only way to find a Host
# function we had forgotten, and gsrunner already implements all 55.
#
# The audio patch ends that. libpcsx2.a now references CabinetCreateAudioStream,
# which is ours, so upstream's frontend cannot link it — correctly. Our own
# probe is the link test now, and it is a better one, because it is the layer
# that actually ships rather than a stand-in for it.
bash /src/cabinet-ps2/compile.sh "$1" probe >/tmp/link.txt 2>&1 \
    || fail "the host layer did not link; see /tmp/link.txt$(printf '\n'; tail -5 /tmp/link.txt)"
[ -x cabinet-ps2-probe ] || fail "no cabinet-ps2-probe binary"
echo "  linked: cabinet-ps2-probe ($(du -h cabinet-ps2-probe | cut -f1))"
# And it must RUN, not merely link. PCSX2's resources folder is not optional:
# it reads its game database, fonts and shaders from it and refuses to start.
./cabinet-ps2-probe --help >/tmp/run.txt 2>&1 || true
grep -q "usage:" /tmp/run.txt || fail "the binary linked but would not start"
echo "  ran: the binary starts"

echo "--- 3. what a CabinetOS host layer still owes it ---"
SYS="-lpng -ljpeg -lz -lzstd -llz4 -lwebp -lsharpyuv -lfreetype -lharfbuzz"
SYS="$SYS -lplutovg -lplutosvg -lryml -lcurl -lpcap -lfontconfig -ludev"
SYS="$SYS -lX11 -lXrandr -lXi -lXext -ldbus-1 -lSDL3 -lshaderc_shared -ldl -lpthread"
clang++ -shared -Wl,-z,defs -o /tmp/strict.so \
    -Wl,--whole-archive pcsx2/libpcsx2.a pcsx2/libGS-sse4.a pcsx2/libGS-avx.a pcsx2/libGS-avx2.a \
    common/libcommon.a -Wl,--no-whole-archive \
    $(find 3rdparty -name '*.a') $SYS >/tmp/link.txt 2>&1 || true
grep -oE "undefined reference to .[^']+" /tmp/link.txt \
    | sed "s/undefined reference to .//" | c++filt \
    | sed 's/\[abi:cxx11\]//' | sed 's/(.*//' | sort -u > /tmp/owed.txt
n=$(wc -l < /tmp/owed.txt)
h=$(grep -c '^Host::' /tmp/owed.txt || true)
echo "  $n symbols, $h of them in the Host:: namespace"
[ "$n" -gt 0 ] || fail "zero owed symbols, which means the probe did not run"
cat /tmp/owed.txt
PROBE
podman run --rm -v "$SRC":/src:Z -v /tmp/cabinetos-pcsx2-probe.sh:/probe.sh:Z \
    -w /src "$BUILDER" bash /probe.sh "$BUILD"
