#!/usr/bin/env bash
#
# Does this build lever reach the save state?
#
# THE QUESTION. CabinetOS shares Cabinet's `emulator` tag on RomM, which is what
# stops a launch screen offering someone a state that cannot load. Sharing it is
# only safe where the two builds are provably the same emulator — same pinned
# commit AND the same build arguments. Several cores turn a recompiler ON for
# Linux x86-64 that Cabinet had OFF, so on those the arguments are not the same,
# and the tag has to be either justified or given up.
#
# THE METHOD. Build the core twice, changing exactly one variable, and compare
# the object files. Objects that differ tell you which source files the lever
# actually reaches, and the answer is usually a handful out of hundreds — at
# which point you read those files rather than the whole core.
#
# This is stronger than the cross-load test it replaces, and far cheaper. Loading
# one game's state written by the other build proves that one game's state
# survives. Finding that nothing under the emulation core differs proves the two
# builds are the same machine code for every game in the library. It was how
# genesis_plus_gx's HAVE_CDROM question was settled (three objects differed, all
# of them CD-ROM plumbing, nothing under core/) and how pcsx_rearmed's DYNAREC
# question was opened (one object differed, and it was the one holding SaveState).
#
# IT IS NOT THE WHOLE ANSWER. A differing object is a place to look, not a
# verdict: pcsx_rearmed's differing object was misc.o, where SaveState lives, and
# reading it showed the state format tolerates either backend deliberately. So
# this points at the files worth reading. It does not read them for you.
#
# Usage:
#   cores/backend-diff.sh <core> <VAR=value-A> <VAR=value-B>
#
# Example:
#   cores/backend-diff.sh picodrive CABINETOS_PICODRIVE_SH2DRC=0 \
#                                   CABINETOS_PICODRIVE_SH2DRC=1

set -euo pipefail

CORE="${1:-}"
A="${2:-}"
B="${3:-}"
if [ -z "$CORE" ] || [ -z "$A" ] || [ -z "$B" ]; then
    echo "usage: cores/backend-diff.sh <core> <VAR=a> <VAR=b>" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/$CORE"
WORK="${TMPDIR:-/tmp}/cabinetos-backend-diff-$CORE"
mkdir -p "$WORK"

# The build leaves its .so in cores/build, and running this twice would leave
# whichever variant happened to be second. Neither is necessarily the one that
# should ship, so each goes to its own directory and cores/build is left alone.
# They are kept rather than deleted: two cores that differ in exactly one lever
# are what tools/state-probe.c needs to answer this empirically, and the object
# diff is the thing that tells you whether that is even worth running.

snapshot() {
    local setting="$1" into="$2"
    echo "=== building with $setting ==="
    export CABINETOS_CORE_OUT="$WORK/out-$into"
    # Objects from the previous variant would otherwise be linked into this
    # one, and the comparison would be against a mixture.
    git -C "$SRC" clean -xfdq 2>/dev/null || true
    env "$setting" "$ROOT/cores/build-core.sh" "$CORE" >"$WORK/$into.log" 2>&1 || {
        echo "build failed with $setting; see $WORK/$into.log" >&2
        tail -20 "$WORK/$into.log" >&2
        exit 1
    }
    # Sorted by path so the two lists line up, and hashed rather than kept: two
    # full sets of objects for a large core is gigabytes, and the hash answers
    # the only question being asked.
    #
    # Hashed through the LTO normaliser, because a plain sha256 answers the
    # wrong question on any core built with -flto. See hash-objects.py.
    ( cd "$SRC" && find . -name '*.o' -type f | sort ) \
        | python3 "$(dirname "$0")/hash-objects.py" "$SRC" > "$WORK/$into.txt"
    echo "$(wc -l < "$WORK/$into.txt") objects"
}

snapshot "$A" a
snapshot "$B" b

echo
echo "=== $CORE: $A against $B ==="
echo
echo "the artifact each build produced:"
sha256sum "$WORK"/out-a/*.so "$WORK"/out-b/*.so | sed 's/^/  /'
echo

# RUN THE CONTROL FIRST, by passing the same setting on both sides. It costs one
# extra pair of builds and it is not optional on a core you have not compared
# before: picodrive reported 102 of 103 objects differing, including zlib's,
# which no CPU backend can reach. The cause was LTO's random per-build id and not
# the recompiler at all — see cores/hash-objects.py. Believed without the
# control, that reading would have cost this core its shared emulator tag.
if [ "$A" = "$B" ]; then
    echo "(control run: the same setting on both sides)"
    if cmp -s "$WORK/a.txt" "$WORK/b.txt"; then
        echo "PASS — the build is reproducible, so a real difference will mean"
        echo "something. Now run it again with the two settings you care about."
        exit 0
    fi
    if cmp -s <(sha256sum < "$WORK"/out-a/*.so) <(sha256sum < "$WORK"/out-b/*.so)
    then
        echo "FAIL on objects, PASS on the artifact — and that combination has a"
        echo "known cause. With -flto the objects hold compiler bytecode rather"
        echo "than code; the machine is generated at LINK time, so only the .so"
        echo "is meaningfully reproducible. picodrive is exactly this: four"
        echo "builds gave four identical .so files and a different handful of"
        echo "objects each time."
        echo
        echo "So on THIS core the object diff is the wrong instrument. Use the"
        echo "two .so files above with tools/state-probe.c instead: write a state"
        echo "with each and load it into the other, with each build loading its"
        echo "own state as the control."
        exit 0
    fi
    echo "FAIL — two builds of IDENTICAL source disagree, so this core's objects"
    echo "cannot be compared until that is explained. Anything a real comparison"
    echo "reported would be noise wearing the shape of a result."
    # Fall through and print what differs, which is the start of the diagnosis.
fi

# A lever that changes NOTHING is the answer nobody should accept quietly: it
# usually means the variable was misspelled, or the Makefile ignores it, and the
# test then proves nothing while looking like a pass. The standing rule on this
# project is that a test must first show the thing it measures actually varies.
if cmp -s "$WORK/a.txt" "$WORK/b.txt"; then
    echo "NOTHING DIFFERS — every object is byte-identical."
    echo "Treat this as a broken test, not as a result: the lever probably did"
    echo "not reach the build. Check the variable name and the Makefile."
    exit 1
fi

join -j 1 <(awk '{print $2" "$1}' "$WORK/a.txt" | sort) \
          <(awk '{print $2" "$1}' "$WORK/b.txt" | sort) \
    | awk '$2 != $3 {print "  " $1}' > "$WORK/differ.txt"

ONLY_A=$(comm -23 <(awk '{print $2}' "$WORK/a.txt" | sort) \
                  <(awk '{print $2}' "$WORK/b.txt" | sort))
ONLY_B=$(comm -13 <(awk '{print $2}' "$WORK/a.txt" | sort) \
                  <(awk '{print $2}' "$WORK/b.txt" | sort))

echo "objects that differ: $(wc -l < "$WORK/differ.txt")"
cat "$WORK/differ.txt"
if [ -n "$ONLY_A" ]; then
    echo "built only with $A:"
    awk '{print "  " $0}' <<<"$ONLY_A"
fi
if [ -n "$ONLY_B" ]; then
    echo "built only with $B:"
    awk '{print "  " $0}' <<<"$ONLY_B"
fi
echo
echo "Now READ those files. A differing object is where to look, not a verdict."
