#!/usr/bin/env bash
#
# Which source files are over about 1,500 lines? (#398, step 4)
#
#   ci/check-long-files.sh [<base> <head>]
#
# REPORT ONLY, NEVER A FAILURE: prints every source file over LIMIT lines,
# longest first, and always exits 0. Given a pull request's base and head
# (.github/workflows/checks.yml runs it on every pull request), it also says
# how much each grew in the pull request, and on GitHub a file it grew is a
# warning on the pull request and the list is the run's summary.
#
# WHY. main.cpp reached 20,018 lines one small addition at a time before #398
# split it into files named for their jobs. A long file is not wrong by
# itself, but new code that is a job of its own belongs in a file of its own
# (CLAUDE.md, rule 2), and a file growing past this size is the sign to look.
#
# A SOURCE FILE is a tracked file under FOLDERS whose name ends .c, .cc, .cpp,
# .h, .hpp, .sh or .py, or that starts with `#!` (the scripts in
# system_files/ have no ending). Data is not source: the shaders and
# controller lists in frontend/data/, and cores/options/, end otherwise.
# VENDORED files are another project's code, kept as it is; they are left out.

set -euo pipefail

LIMIT=1500
FOLDERS=(frontend wiibridge build_files ci cores system_files tools)
VENDORED=(
    'frontend/third_party/'   # rcheevos, RetroAchievements' own library; Sigil, RomM's save library
    'frontend/src/libretro.h' # the libretro API, from libretro-common
)

base=""
if [[ $# -eq 2 ]]; then
    base=$(git merge-base "$1" "$2")
    head="$2"
elif [[ $# -eq 0 ]]; then
    head=HEAD
else
    echo "usage: check-long-files.sh [<base> <head>]" >&2
    exit 2
fi

vendored() {
    local file="$1" v
    for v in "${VENDORED[@]}"; do
        [[ "${file}" == "${v}"* ]] && return 0
    done
    return 1
}

lines_in() {  # <commit> <file>: its lines there, 0 when it is not there
    git cat-file -e "$1:$2" 2>/dev/null || { echo 0; return; }
    git cat-file -p "$1:$2" | wc -l | tr -d ' '
}

# Every text file's lines in one pass (`<head>:<file>:<lines>`), then a
# closer look at the few over the limit.
long=()
while IFS= read -r counted; do
    counted="${counted#"${head}:"}"
    file="${counted%:*}"
    n="${counted##*:}"
    [[ "${n}" -gt "${LIMIT}" ]] || continue
    vendored "${file}" && continue
    case "${file}" in
        *.c|*.cc|*.cpp|*.h|*.hpp|*.sh|*.py) ;;
        *) [[ "$(git cat-file -p "${head}:${file}" | head -c 2)" == '#!' ]] || continue ;;
    esac
    long+=("$(lines_in "${head}" "${file}")"$'\t'"${file}")
done < <(git -c core.quotePath=false grep -I -c '' "${head}" -- "${FOLDERS[@]}")

if [[ ${#long[@]} -eq 0 ]]; then
    echo "No source file is over ${LIMIT} lines."
    exit 0
fi

summary="${GITHUB_STEP_SUMMARY:-/dev/null}"
{
    echo "### Source files over ${LIMIT} lines (report only)"
    echo
    echo "| File | Lines | In this pull request |"
    echo "|---|---|---|"
} >> "${summary}"
echo "Source files over ${LIMIT} lines (report only, never a failure):"
while IFS=$'\t' read -r n file; do
    grew=""
    if [[ -n "${base}" ]]; then
        was=$(lines_in "${base}" "${file}")
        if [[ "${n}" -gt "${was}" ]]; then
            grew="+$((n - was))"
        elif [[ "${n}" -lt "${was}" ]]; then
            grew="-$((was - n))"
        fi
    fi
    echo "  ${n}  ${file}${grew:+  (${grew} in this pull request)}"
    echo "| \`${file}\` | ${n} | ${grew} |" >> "${summary}"
    if [[ "${grew}" == +* && -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::warning file=${file}::${file} is ${n} lines (${grew} here), over ${LIMIT}. New code that is a job of its own goes in a file of its own (CLAUDE.md, rule 2)."
    fi
done < <(printf '%s\n' "${long[@]}" | sort -rn)
exit 0
