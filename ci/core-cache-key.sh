#!/usr/bin/env bash
#
# THE KEY CI CACHES ONE CORE'S BUILD UNDER (build-core.yml): a hash of
# everything that can change that core, and nothing that cannot.
#
# A core is made by cores/build-core.sh in the builder container, which is
# frontend/Containerfile. Three parts of the script reach a given core: its
# own case arm (the pinned commit and the build arguments), its own patch
# blocks (`if [ "$CORE" = <core> ]; then` to `fi`), and every line that
# belongs to no core, which runs for all of them. Another core's arm or
# patch block never runs for it. So the key hashes the script with every
# other core's arms and blocks taken out, and the Containerfile: a change
# inside one core's arm or patches rebuilds that core alone, and a change
# anywhere else in the script rebuilds every core.
#
# The text counts, not what it means: a comment in an arm rebuilds that
# core, and one outside every arm rebuilds them all. That is why
# build-core.sh keeps counts and lists of cores out of its shared lines.
#
# STRICT ABOUT SHAPE, because a part read as the wrong core's is a stale
# core. An arm runs from its `name)` line to the next line that starts in
# the first column, and every line in between is indented or blank; a patch
# block likewise, up to its `fi` in the first column and the blank lines
# after it. Anything else in the first column inside either stops this with
# the line number, rather than guessing whose it is.
#
# Usage: ci/core-cache-key.sh <core>      prints the key, 64 hex characters

set -euo pipefail

CORE="${1:?usage: ci/core-cache-key.sh <core>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# The script as <core> sees it. `part` is whose line this is: empty for
# every core's, or one core's name.
recipe="$(awk -v core="$CORE" '
    function fail(msg) {
        printf "cores/build-core.sh:%d: %s\n", NR, msg > "/dev/stderr"
        bad = 1
        exit 1
    }
    {
        # The blank lines after a patch block belong to the block too, so
        # adding or removing a block takes its separating line with it.
        # They follow a fi in the first column, at the top of the script,
        # where a blank line does nothing.
        if (after) {
            if ($0 ~ /^[ \t]*$/) {
                if (part == core) print
                next
            }
            after = 0
            part = ""
        }
        if (incase) {
            if ($0 ~ /^esac$/) { incase = 0; part = "" }
            else if ($0 ~ /^\*\)/) part = ""
            else if (match($0, /^[a-z0-9_]+\)/)) {
                part = substr($0, 1, RLENGTH - 1)
                arms[part] = 1
            }
            else if ($0 ~ /^[^ \t]/) fail("in the case but in no arm; indent it into one")
        } else if (inblock) {
            if ($0 ~ /^fi$/) {
                if (part == core) print
                inblock = 0
                after = 1
                next
            }
            if ($0 ~ /^[^ \t]/) fail("in the first column inside the patch block for " part "; indent it, or end the block with fi")
        } else if ($0 ~ /^case "\$CORE" in$/) {
            incase = 1
        } else if ($0 ~ /^if \[ "\$CORE" = [a-z0-9_]+ \]; then$/) {
            split($0, w, " ")
            part = w[5]
            inblock = 1
        }
        if (part == "" || part == core) print
    }
    END {
        if (bad) exit 1
        if (incase) fail("the case has no esac in the first column")
        if (inblock) fail("the patch block for " part " has no fi in the first column")
        if (!(core in arms)) {
            printf "cores/build-core.sh has no case arm for %s\n", core > "/dev/stderr"
            exit 1
        }
    }
' "$ROOT/cores/build-core.sh")"

{
    printf '%s\n' "$recipe" | sha256sum
    sha256sum < "$ROOT/frontend/Containerfile"
} | sha256sum | cut -d' ' -f1
