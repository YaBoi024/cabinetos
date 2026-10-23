#!/usr/bin/env bash
#
# The code nothing calls, as the linker sees it: a report in the run's
# summary, never a failure (#398, step 4).
#
# The frontend is compiled again into a scratch copy with every function and
# every variable in a section of its own (-ffunction-sections
# -fdata-sections) and linked with --gc-sections --print-gc-sections, which
# names each section nothing reachable from main refers to. AT -O0, because
# at -O2 a function used only where it was inlined has an unused copy of
# its own, and would be listed though it is used. The flags are the
# Makefile's own with -O2 made -O0, so they cannot drift from the real build.
# The binary that ships is not this one and is not touched: this builds under
# $RUNNER_TEMP, after the upload.
#
# A SECTION DROPPED IN ONE FILE IS NOT YET UNUSED. A header's inline
# function, or its constant, is compiled into every file that includes it,
# and the copies a file never calls are dropped one by one. So a name is
# listed only when no copy of it is left in the linked program (its symbol
# table, read with nm). One name dropped from several files is a header's,
# and is listed once with the count.
#
# What is listed: functions, and variables that can change (.data, .bss).
# Constants are not: a constexpr is folded where it is used and never
# referenced, so it would be listed though it is used. Nor the C++
# library's templates, or vendored rcheevos (counted only). Never seen at
# all: a static function nobody calls (-Wunused-function names those in the
# Warnings step) and an inline one nobody calls (a header's, or one written
# in its class), which the compiler never emits. A function used only by
# another listed one is listed too.
#
# ON A PULL REQUEST it also names what is unused in the files the pull request
# touches, as warnings on it and a list at the end of the report
# (ci/warn-unused-touched.sh, which has the names held on purpose). CLAUDE.md,
# rule 6 asks each of those to be removed, or kept with the reason in the pull
# request. Still a warning, never a failure.
#
# Usage: ci/report-unused-code.sh
#
# Run by build-frontend.yml at the end of the Compile job, on pull requests.
# It needs the cabinetos-builder container the job already has.

set -uo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 0

SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
WORK="${RUNNER_TEMP:-/tmp}/unused-code"
rm -rf "$WORK"
mkdir -p "$WORK"
REPORT="$WORK/report.md"
printf '## Unused code (a report, never a failure)\n\n' > "$REPORT"

# Every way out says why in the summary and passes.
done_with() {
    [[ -n "${1:-}" ]] && printf '%s\n' "$1" >> "$REPORT"
    cat "$REPORT"
    cat "$REPORT" >> "$SUMMARY"
    exit 0
}

cxxflags=$(sed -n 's/^CXXFLAGS[[:space:]]*?=[[:space:]]*//p' frontend/Makefile)
cflags=$(sed -n 's/^CFLAGS[[:space:]]*?=[[:space:]]*//p' frontend/Makefile)
if [[ "$cxxflags" != *-O2* || "$cflags" != *-O2* ]]; then
    done_with "No report: frontend/Makefile's CXXFLAGS or CFLAGS default is not where this reads it, or has no -O2."
fi
cxxflags="${cxxflags/-O2/-O0} -ffunction-sections -fdata-sections"
cflags="${cflags/-O2/-O0} -ffunction-sections -fdata-sections"

git archive HEAD frontend/Makefile frontend/src frontend/third_party | tar -x -C "$WORK"
SECONDS=0
if ! podman run --rm -v "$WORK/frontend":/src:Z -w /src \
        -e CXXFLAGS="$cxxflags" -e CFLAGS="$cflags" cabinetos-builder \
        sh -c "make -j$(nproc) LDFLAGS='-Wl,--gc-sections -Wl,--print-gc-sections' > build.log 2>&1 &&
               nm --defined-only build/cabinetos-frontend > kept.nm"; then
    tail -n 40 "$WORK/frontend/build.log"
    done_with "No report: the scratch build did not link (its log is in this step's output)."
fi
seconds=$SECONDS

# "ld: removing unused section '.text._ZN4cache4findEi' in file 'src/cache.o'"
# becomes "<function or variable> TAB <object> TAB <symbol>". A section in a
# group carries the group's name in brackets after its own.
awk -v q="'" '
    /removing unused section/ {
        split($0, part, q)
        sec = part[2]; sub(/\[.*$/, "", sec); kind = ""
        if (sub(/^[.]text[.]/, "", sec)) kind = "function"
        else if (sub(/^[.](data|bss|tdata|tbss)[.]/, "", sec)) kind = "variable"
        if (kind == "" || sec ~ /^(startup|unlikely|hot|exit)$/ || sec ~ /^rel[.]ro[.]/) next
        sub(/^rel([.]local)?[.]/, "", sec)
        print kind "\t" part[4] "\t" sec
    }' "$WORK/frontend/build.log" > "$WORK/dropped"
awk '{ print $NF }' "$WORK/frontend/kept.nm" | sort -u > "$WORK/kept"
awk -F'\t' 'NR == FNR { kept[$0] = 1; next } !($3 in kept)' "$WORK/kept" "$WORK/dropped" > "$WORK/gone"

# Names, by the builder's c++filt: Ubuntu's is too old for C++20's.
cut -f3 "$WORK/gone" | podman run --rm -i cabinetos-builder c++filt > "$WORK/names"
cut -f1,2 "$WORK/gone" | paste - "$WORK/names" > "$WORK/all"

vendored=$(awk -F'\t' '$2 ~ /^third_party\// { n[$3] = 1 } END { c = 0; for (x in n) c++; print c }' "$WORK/all")
# Ours, one line per name: "<file> TAB <kind> TAB <name>", where a name
# dropped from several files goes under "~" (sorted last) with its count.
awk -F'\t' '
    $2 ~ /^third_party\// { next }
    $3 ~ /^((vtable|VTT|construction vtable) for )?(std|__gnu_cxx)::/ { next }
    $3 ~ /^(typeinfo |operator (new|delete)|_ZN?K?St|_ZN9__gnu_cxx)/ { next }
    {
        k = $1 FS $3; n[k]++
        if (!(k in f)) { f[k] = $2; sub(/[.]o$/, ".cpp", f[k]); f[k] = "frontend/" f[k] }
    }
    END {
        for (k in n) {
            split(k, p, FS)
            if (n[k] == 1) print f[k] "\t" p[1] "\t" p[2]
            else print "~\t" p[1] "\t" p[2] " (" n[k] " files)"
        }
    }' "$WORK/all" | sort > "$WORK/ours"
functions=$(awk -F'\t' '$2 == "function"' "$WORK/ours" | wc -l)
variables=$(awk -F'\t' '$2 == "variable"' "$WORK/ours" | wc -l)

{
    echo "The linker found $functions function(s) and $variables variable(s) that" \
        "nothing reachable from \`main\` uses (and $vendored in vendored rcheevos," \
        "not listed). A scratch build at -O0, ${seconds}s; the binary that ships" \
        "is not this one."
    echo
    if [[ -s "$WORK/ours" ]]; then
        echo "<details><summary>The list, by file</summary>"
        echo
        echo '```'
        awk -F'\t' '
            $1 != last {
                print ($1 == "~" ? "In a header, or the same name in several files" : $1)
                last = $1
            }
            { print "    " $3 ($2 == "variable" ? "  (variable)" : "") }' "$WORK/ours"
        echo '```'
        echo "</details>"
    fi
} >> "$REPORT"

# On a pull request, HEAD is GitHub's merge of it into its base (its first
# parent), as in check-new-warnings.sh: the names in the files the pull
# request touches also go on the pull request as warnings.
if git rev-parse -q --verify 'HEAD^2' > /dev/null; then
    GITHUB_STEP_SUMMARY="$REPORT" ci/warn-unused-touched.sh "$WORK/ours" "$(git rev-parse 'HEAD^1')" HEAD || true
fi
done_with
