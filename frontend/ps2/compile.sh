#!/usr/bin/env bash
#
# Compiles CabinetOS's PCSX2 host layer and links the probe, using the exact
# flags PCSX2's own build used.
#
# THE FLAGS ARE READ, NOT WRITTEN DOWN. They come out of compile_commands.json
# for a real PCSX2 translation unit, so a define or an include path that moves
# upstream moves here with it. Writing them into this file by hand is how a
# host layer ends up compiled against a different ABI than the library it links
# into — which does not fail at compile time, and does not reliably fail at
# link time either.
set -euo pipefail

BUILD="${1:?build directory}"
# The PCSX2 revision this was built from, stamped into the .so so the console
# can print what it is ACTUALLY running. Every libretro core answers the same
# question through retro_get_system_info, for the same reason: a core that
# cannot say which revision it is cannot be audited, and this project has
# already spent days recovering revisions from shipped binaries with `strings`.
VERSION="${2:-unknown}"
cd /src

command -v python3 >/dev/null || { echo "python3 is needed to read compile_commands.json" >&2; exit 1; }

cmake -B "$BUILD" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON . >/dev/null

read -r -d '' EXTRACT <<'PY' || true
import json, shlex, sys
db = json.load(open(sys.argv[1]))
ent = [e for e in db if e["file"].endswith("pcsx2/VMManager.cpp")]
if not ent:
    sys.exit("no VMManager.cpp in compile_commands.json")
args = shlex.split(ent[0]["command"])
keep, i = [], 0
while i < len(args):
    a = args[i]
    if a in ("-isystem", "-I"):
        keep += [a, args[i + 1]]; i += 2; continue
    if a.startswith(("-I", "-D", "-isystem", "-std=", "-march=", "-mtune=", "-msse", "-mfxsr")) or a == "-fPIC":
        keep.append(a)
    i += 1
print(" ".join(shlex.quote(k) for k in keep))
PY
FLAGS=$(python3 -c "$EXTRACT" "$BUILD/compile_commands.json")

echo "host layer: compiling with $(echo "$FLAGS" | wc -w) flags from PCSX2's own build"

# -I/src/cabinet-ps2 so the two files find their own header; everything else is
# PCSX2's.
for f in CabinetPS2Host CabinetPS2Bios CabinetPS2HostStubs CabinetPS2Pad CabinetPS2Audio CabinetPS2Bridge CabinetPS2Probe; do
    # shellcheck disable=SC2086  # FLAGS is a deliberately word-split flag list
    clang++ $FLAGS -I/src/cabinet-ps2 -DCABINETOS_PS2_VERSION="\"$VERSION\"" \
        -c "/src/cabinet-ps2/$f.cpp" -o "$BUILD/$f.o"
done

# The link.
#
# THIS STATICALLY LINKS PCSX2 AND THE SHIPPING FRONTEND MUST NOT. PCSX2 is
# GPLv3 and this repository is MIT. docs/LICENCES.md rests its whole position on
# one sentence — "the cores are `dlopen`ed rather than statically linked" — and
# a static link puts the emulator inside the frontend's own binary, which makes
# that binary a combined work under GPLv3.
#
# It is fine HERE, because this probe is a development tool that is never
# distributed and never enters the image. The shipping path is the one open
# question 12's correction already chose for a different reason: each emulator
# becomes its own .so behind a struct of function pointers, exactly like
# every libretro core, which keeps the same arms-length arrangement
# LICENCES.md already relies on. Do not "simplify" this into the frontend link.
#
# Order matters for static archives, and --start-group is how you avoid having
# to work out what that order is for eighteen of them.
SYS="-lpng -ljpeg -lz -lzstd -llz4 -lwebp -lsharpyuv -lfreetype -lharfbuzz"
SYS="$SYS -lplutovg -lplutosvg -lryml -lcurl -lpcap -lfontconfig -ludev"
SYS="$SYS -lX11 -lXrandr -lXi -lXext -ldbus-1 -lSDL3 -lshaderc_shared -ldl -lpthread"

mapfile -t THIRDPARTY < <(find "$BUILD/3rdparty" -name '*.a')

# PCSX2 ITSELF, LINKED AS ITS OWN APP LINKS IT. A multi-ISA build (build-pcsx2.sh,
# DISABLE_ADVANCE_SIMD) has the GS, IPU and SPU2 code three more times, once per
# instruction set, and pcsx2/CMakeLists.txt links them WHOLE and IN THIS ORDER,
# oldest first: the copies share inline functions, the linker keeps the first
# one it sees, and an AVX2 copy kept first would run on a processor without
# AVX2, which is the SIGILL this build was fixing. All of PCSX2's own objects
# go in whole as well, as pcsx2-qt has them. A build without the three (an
# older build directory) links the one archive, as before.
GS_ISA=()
for isa in sse4 avx avx2; do
    [ -f "$BUILD/pcsx2/libGS-$isa.a" ] && GS_ISA+=("$BUILD/pcsx2/libGS-$isa.a")
done
if [ ${#GS_ISA[@]} -eq 3 ]; then
    # shellcheck disable=SC2054  # the commas are inside each linker flag
    PCSX2_LINK=(-Wl,--whole-archive "$BUILD/pcsx2/libpcsx2.a" "${GS_ISA[@]}" -Wl,--no-whole-archive)
elif [ ${#GS_ISA[@]} -eq 0 ]; then
    PCSX2_LINK=("$BUILD/pcsx2/libpcsx2.a")
else
    echo "only ${#GS_ISA[@]} of PCSX2's three per-ISA GS libraries are built" >&2
    exit 1
fi

# shellcheck disable=SC2086  # SYS is a deliberately word-split flag list
clang++ -o "$BUILD/cabinet-ps2-probe" \
    "$BUILD/CabinetPS2Host.o" "$BUILD/CabinetPS2Bios.o" "$BUILD/CabinetPS2HostStubs.o" "$BUILD/CabinetPS2Pad.o" \
    "$BUILD/CabinetPS2Audio.o" "$BUILD/CabinetPS2Probe.o" \
    -Wl,--start-group \
    "${PCSX2_LINK[@]}" "$BUILD/common/libcommon.a" \
    "${THIRDPARTY[@]}" \
    -Wl,--end-group \
    $SYS

echo "host layer: linked $BUILD/cabinet-ps2-probe"

# --- the shared object the console actually loads --------------------------
#
# THIS IS WHAT SHIPS. The probe above is a development tool; this is the file
# the frontend dlopens, and it exists rather than a static link for two
# reasons, either of which would decide it on its own.
#
# The licence is the hard one: PCSX2 is GPLv3 and this repository is MIT, and
# docs/LICENCES.md rests its position on the emulators being dlopen'ed rather
# than linked in. The practical one is that the frontend's builder image does
# not carry PCSX2's thirty-odd dependencies and should not grow them.
#
# -z defs so the linker REFUSES a .so with anything unresolved. Without it a
# missing symbol becomes a dlopen failure on the console at the moment somebody
# starts a game, naming one symbol and telling you nothing about the rest.
# -rpath $ORIGIN so the emulator can carry libraries the console does not have,
# and --disable-new-dtags because THE DIFFERENCE BETWEEN RPATH AND RUNPATH
# DECIDES WHETHER THIS WORKS AT ALL.
#
# The modern default is RUNPATH, and RUNPATH is NOT INHERITED: it is consulted
# for this object's own direct dependencies and ignored when one of THOSE goes
# looking for something. Measured here — libryml was found and then could not
# find libc4core, which sat in the same directory. RPATH, the older tag, does
# apply down the chain, which is exactly what a bundle of transitive libraries
# needs. The flag asks for the old tag.
#
# MEASURED RATHER THAN ASSUMED, on the reference console 2026-09-21: of
# everything this links, the image is missing exactly ONE — libryml, rapidyaml,
# which PCSX2 uses to read its game database. Everything else PCSX2 wants is
# already there for the frontend and the libretro cores: shaderc, plutovg,
# plutosvg, harfbuzz, pcap, SPIRV-Tools.
#
# So the emulator ships that one library beside itself rather than the image
# growing a package for it. That keeps a base bump from being able to take
# PlayStation 2 away quietly — which is a real risk this project has written up
# for the OTHER libraries in ci/base-watch.txt, where those do belong.
#
# shellcheck disable=SC2086,SC2016  # SYS is word-split on purpose, and $ORIGIN
# is a LINKER token that must reach ld unexpanded — the shell must not touch it.
# --wrap takes over PCSX2's one vibration call for rumble without a patch;
# CabinetPS2Bridge.cpp says why and what it does instead.
clang++ -shared -Wl,-z,defs -Wl,-rpath,'$ORIGIN' -Wl,--disable-new-dtags -o "$BUILD/cabinetos-ps2.so" \
    -Wl,--wrap=_ZN12InputManager24SetPadVibrationIntensityEjff \
    "$BUILD/CabinetPS2Host.o" "$BUILD/CabinetPS2Bios.o" "$BUILD/CabinetPS2HostStubs.o" "$BUILD/CabinetPS2Pad.o" \
    "$BUILD/CabinetPS2Audio.o" "$BUILD/CabinetPS2Bridge.o" \
    -Wl,--start-group \
    "${PCSX2_LINK[@]}" "$BUILD/common/libcommon.a" \
    "${THIRDPARTY[@]}" \
    -Wl,--end-group \
    $SYS

echo "host layer: linked $BUILD/cabinetos-ps2.so ($(du -h "$BUILD/cabinetos-ps2.so" | cut -f1))"

# It must LOAD, not merely link. A .so that satisfies the linker and fails at
# dlopen is the exact failure this project hit with g_host_hotkeys, and it
# reports one missing symbol at a time.
cat > /tmp/dlcheck.c <<'DL'
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char** argv) {
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); return 1; }
    const char* needed[] = {"cps2_start","cps2_stop","cps2_running","cps2_set_pad",
                            "cps2_take_frame","cps2_drain_audio","cps2_version", 0};
    for (int i = 0; needed[i]; i++)
        if (!dlsym(h, needed[i])) { printf("missing %s\n", needed[i]); return 1; }
    printf("dlopen ok, every entry point resolves\n");
    return 0;
}
DL
clang /tmp/dlcheck.c -o /tmp/dlcheck -ldl
# The libraries the CONSOLE does not have, copied beside the emulator so the
# rpath above finds them.
#
# THE LIST IS COMPUTED, NOT WRITTEN DOWN, and it has to be: measured on the
# reference console 2026-09-21, the image is missing exactly one of PCSX2's
# direct dependencies — libryml, rapidyaml, which reads its game database — and
# that one in turn needs libc4core, which nothing in the direct list names.
# A hand-maintained list would have shipped the first and missed the second,
# and the failure arrives as a dlopen error at the moment somebody starts a
# game.
#
# So this walks the whole tree from the console's point of view: anything the
# builder has that the console does not, plus anything THOSE need, until the
# set stops growing.
: > "$BUILD/cabinetos-ps2.bundled"
for _pass in 1 2 3 4 5; do
    added=0
    while read -r soname; do
        [ -n "$soname" ] || continue
        [ -f "$BUILD/$soname" ] && continue
        # Where the builder has it. It is on the console's missing list, so the
        # only copy that exists is in here.
        src=$(find /usr/lib64 /lib64 -maxdepth 1 -name "$soname" -print -quit 2>/dev/null)
        [ -n "$src" ] || continue
        cp -L "$src" "$BUILD/$soname"
        echo "$soname" >> "$BUILD/cabinetos-ps2.bundled"
        added=$((added + 1))
    done < <(
        # Everything the emulator and what it already carries depend on, that
        # is NOT in the console's own image: the two known names, so a build
        # outside CI still produces something that runs.
        {
            objdump -p "$BUILD/cabinetos-ps2.so" 2>/dev/null | awk '/NEEDED/ {print $2}'
            for b in "$BUILD"/*.so.*; do
                [ -f "$b" ] && objdump -p "$b" 2>/dev/null | awk '/NEEDED/ {print $2}'
            done
        } | sort -u | grep -E '^(libryml|libc4core)' || true
    )
    [ "$added" -eq 0 ] && break
done

# THE MANIFEST IS WRITTEN FROM WHAT IS ACTUALLY BESIDE THE EMULATOR, not from
# what this run happened to copy.
#
# The loop above skips a library that is already present — correctly, there is
# nothing to do — but it skipped RECORDING it too, so a second build in a tree
# that already had them copied nothing and wrote an EMPTY manifest while both
# libraries sat right there. That is invisible until something stages from the
# manifest, and then it ships an emulator that cannot dlopen, with the failure
# arriving when a person starts a game. Found 2026-09-21, before it shipped,
# only because the file was looked at rather than trusted.
find "$BUILD" -maxdepth 1 -name 'lib*.so.*' -printf '%f\n' | sort -u \
    > "$BUILD/cabinetos-ps2.bundled"
if [ -s "$BUILD/cabinetos-ps2.bundled" ]; then
    echo "host layer: carries $(tr '\n' ' ' < "$BUILD/cabinetos-ps2.bundled")"
fi

/tmp/dlcheck "$BUILD/cabinetos-ps2.so"
