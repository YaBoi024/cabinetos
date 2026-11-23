#!/usr/bin/env bash
#
# No new compiler warnings in the files a pull request touches.
#
# CLAUDE.md's rule 6 asks every change for "no new warnings in the files you
# touched"; this is that rule, held by CI (#398, step 4). The tree already
# carries warnings, so -Werror would fail every change for old ones. Instead
# the warnings in the touched files are compared with the same files built
# at the pull request's base, and one that is not at the base is new.
#
# THE BASE IS COMPILED ONLY WHERE IT CAN MATTER. A warning in a .cpp comes
# from compiling that .cpp; one in a header from compiling any .cpp that
# includes it. So at the base this compiles the touched .cpp files, and
# every file that includes a touched header, read from the .d files the
# head's build just wrote (-MMD). Those are the base's own includes too,
# unless an include line changed, and a file whose include line changed is
# itself touched, so the .cpp at the top of that chain is compiled anyway.
# A pull request that touches only a .cpp compiles that one file again; one
# that touches app.h compiles the 53 files that include it.
# The Wii bridge is three files and is built whole, at the base and again
# here at the head, when any of it is touched.
#
# A WARNING IS COMPARED BY ITS TEXT, across all the touched files, not by
# its line or its file: code moved within a file, or from one touched file
# to another (a split, a rename), brings its warning along and nothing new.
# Two of the same warning where the base had one is one new. What that lets
# through is the same warning fixed in one touched file and written in
# another, which leaves the count where it was.
#
# VENDORED FILES ARE NOT CHECKED: rcheevos and Sigil (frontend/third_party/) and the
# libretro API (frontend/src/libretro.h), check-long-files.sh's VENDORED.
# They are kept as they come, so a warning a new version brings is not one
# a pull request can fix; the Warnings step still lists it.
#
# REPORT ONLY UNTIL A DATE: while today (UTC) is not past REPORT_ONLY_UNTIL
# (YYYY-MM-DD, set by build-frontend.yml), a new warning is the same report
# and a warning annotation, and it exits 0; from the next day it fails, the
# rule checks.yml's checks follow (CONTRIBUTING.md, "What CI checks").
#
# Usage: ci/check-new-warnings.sh <the head's build log>
#
# Run by build-frontend.yml after the Compile step, on pull requests only:
# HEAD is then GitHub's merge of the pull request into its base, so its first
# parent is the base and the checkout needs fetch-depth 2. It needs the
# cabinetos-builder container the job already has.

set -euo pipefail
export LC_ALL=C

HEAD_LOG="${1:?usage: ci/check-new-warnings.sh <the head build log>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
WORK="${RUNNER_TEMP:-/tmp}/new-warnings"
rm -rf "$WORK"
mkdir -p "$WORK/base" "$WORK/head"
REPORT="$WORK/report.md"

# The end of every run: the report to the log and the run summary, then the
# exit the date allows.
finish() {
    local failed="$1" why="$2"
    printf '\n%s\n' "$why" >> "$REPORT"
    cat "$REPORT"
    cat "$REPORT" >> "$SUMMARY"
    if [[ "$failed" == yes ]]; then
        if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
            echo "::warning::$why Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
            exit 0
        fi
        echo "::error::$why"
        exit 1
    fi
    exit 0
}

printf '## New warnings in the files this pull request touches\n\n' > "$REPORT"

if ! git rev-parse -q --verify 'HEAD^2' > /dev/null; then
    finish yes "HEAD is not a merge commit, so there is no base to compare with (is the checkout's fetch-depth 2?)."
fi
BASE="$(git rev-parse 'HEAD^1')"

# The files the frontend's Makefile and the Wii bridge's compile, less the
# vendored ones, as the pull request changed them (a rename is the old file
# gone and a new one).
git diff --name-only --no-renames "$BASE" HEAD -- \
    frontend/src ':!frontend/src/libretro.h' wiibridge > "$WORK/changed"
grep -E '\.(c|cc|cpp|h|hpp|inc)$' "$WORK/changed" > "$WORK/touched" || true

if [[ ! -s "$WORK/touched" ]]; then
    finish no "No file the frontend or the Wii bridge compiles changed, vendored ones aside."
fi

at_base() { git cat-file -e "$BASE:$1" 2> /dev/null; }

# The base's frontend objects to compile, as Makefile targets (relative to
# frontend/): each touched source still at the base, and each object whose
# .d names a touched header.
: > "$WORK/objects"
: > "$WORK/headers"
while read -r f; do
    case "$f" in
        frontend/*.cpp)
            if at_base "$f"; then
                rel="${f#frontend/}"
                echo "${rel%.*}.o" >> "$WORK/objects"
            fi
            ;;
        frontend/*) echo "${f#frontend/}" >> "$WORK/headers" ;;
    esac
done < "$WORK/touched"
if [[ -s "$WORK/headers" ]]; then
    find frontend/src frontend/third_party -name '*.d' | while read -r d; do
        if tr ' ' '\n' < "$d" | grep -xF -f "$WORK/headers" > /dev/null; then
            obj="${d#frontend/}"
            obj="${obj%.d}.o"
            src="${obj%.o}.cpp"
            if [[ "$obj" == third_party/* ]]; then src="${obj%.o}.c"; fi
            if at_base "frontend/$src"; then echo "$obj"; fi
        fi
    done >> "$WORK/objects"
fi
sort -u -o "$WORK/objects" "$WORK/objects"

git archive "$BASE" frontend/Makefile frontend/src frontend/third_party wiibridge \
    | tar -x -C "$WORK/base"
: > "$WORK/frontend-base.log"
: > "$WORK/bridge-base.log"
: > "$WORK/bridge-head.log"
SECONDS=0
if [[ -s "$WORK/objects" ]]; then
    mapfile -t objects < "$WORK/objects"
    # -O keeps each compile's lines together, so two compiles at once cannot
    # cut a warning in half.
    if ! podman run --rm -v "$WORK/base/frontend":/src:Z -w /src cabinetos-builder \
            make -j"$(nproc)" -O "${objects[@]}" > "$WORK/frontend-base.log" 2>&1; then
        cat "$WORK/frontend-base.log"
        finish yes "The base did not compile, so it cannot be compared."
    fi
fi
if grep -q '^wiibridge/' "$WORK/touched"; then
    git archive HEAD wiibridge | tar -x -C "$WORK/head"
    for side in base head; do
        if ! podman run --rm -v "$WORK/$side/wiibridge":/src:Z -w /src cabinetos-builder \
                make > "$WORK/bridge-$side.log" 2>&1; then
            cat "$WORK/bridge-$side.log"
            finish yes "The Wii bridge did not compile at the $side, so it cannot be compared."
        fi
    done
fi
seconds=$SECONDS

# One line per warning in a touched file, "<file> TAB <text> TAB <line>",
# the file from the repository's root. A header's warning repeats once per
# .cpp that includes it, so exact repeats go first.
warnings() {
    local log="$1" prefix="$2"
    { grep -E '^[^[:space:]:]+:[0-9]+:([0-9]+:)? warning: ' "$log" || true; } | sort -u \
        | awk -v prefix="$prefix" -v touched="$WORK/touched" '
            BEGIN { while ((getline t < touched) > 0) want[t] = 1 }
            {
                i = index($0, ": warning: ")
                split(substr($0, 1, i - 1), at, ":")
                file = prefix at[1]
                if (file in want) print file "\t" substr($0, i + 11) "\t" at[2]
            }'
}
# The head's frontend lines are in the job's log. The Wii bridge's lines
# there have no folder in their names, so they come from the build here.
{
    warnings "$WORK/frontend-base.log" frontend/
    warnings "$WORK/bridge-base.log" wiibridge/
} > "$WORK/base.w"
{
    warnings "$HEAD_LOG" frontend/
    warnings "$WORK/bridge-head.log" wiibridge/
} > "$WORK/head.w"

# By file for the table; by text alone for what is new.
cut -f1,2 "$WORK/base.w" | sort > "$WORK/base.k"
cut -f1,2 "$WORK/head.w" | sort > "$WORK/head.k"
cut -f2 "$WORK/base.w" | sort > "$WORK/base.t"
cut -f2 "$WORK/head.w" | sort > "$WORK/head.t"
comm -23 "$WORK/head.t" "$WORK/base.t" > "$WORK/new.t"

{
    echo "Base \`$(git rev-parse --short "$BASE")\`: $(wc -l < "$WORK/touched") source" \
        "file(s) touched; $(wc -l < "$WORK/objects") frontend object(s) compiled again" \
        "at the base, in ${seconds}s."
    echo
    if [[ ! -s "$WORK/head.k" && ! -s "$WORK/base.k" ]]; then
        echo "No warnings in the touched files, at the base or now."
    else
        echo "| File | At the base | Now |"
        echo "|---|---|---|"
        awk -F'\t' 'FILENAME ~ /base\.k$/ { b[$1]++ } FILENAME ~ /head\.k$/ { h[$1]++ }
                    { f[$1] = 1 }
                    END { for (x in f) printf "| `%s` | %d | %d |\n", x, b[x], h[x] }' \
            "$WORK/base.k" "$WORK/head.k" | sort
    fi
} >> "$REPORT"

if [[ ! -s "$WORK/new.t" ]]; then
    finish no "No new warnings."
fi

# Each new warning with every place it is now: one of them is the new one.
{
    echo
    echo '```'
    awk -F'\t' 'NR == FNR { n[$1]++; next }
                $2 in n { at[$2] = at[$2] sep[$2] $1 ":" $3; sep[$2] = ", " }
                END { for (t in n)
                      printf "warning: %s (%d more than at the base; now at %s)\n", t, n[t], at[t] }' \
        "$WORK/new.t" "$WORK/head.w" | sort
    echo '```'
} >> "$REPORT"
finish yes "$(wc -l < "$WORK/new.t" | tr -d ' ') new warning(s) in the files this pull request touches."
