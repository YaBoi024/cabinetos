#!/usr/bin/env bash
#
# The decision files, and the frozen record they replace. (CLAUDE.md, rule 7)
#
#   ci/check-decisions.sh [<base> <head>]
#
# Three things, all in docs/decisions/ and docs/PROJECT.md:
#
#  1. EACH DECISION FILE IS SMALL AND LISTED. A file in docs/decisions/ other
#     than README.md is named `YYYY-MM-DD-short-name.md`, is at most LIMIT
#     lines, and has exactly one line in the README's index
#     (`- [name](file.md): ...`); an index line points at a file that is
#     there. The whole tree is read, so this runs the same by hand.
#  2. PROJECT.md DOES NOT GROW. Given a pull request's base and head, the
#     lines it adds to docs/PROJECT.md, less the lines it removes, are at
#     most GROWTH. The file was frozen on 2026-10-23: a decision moved out of
#     it leaves a one-line pointer, which is a net loss, and a line made
#     untrue is rewritten, which is about nothing.
#  3. FAULT WORDING. A line a pull request adds to docs/decisions/ or
#     docs/PROJECT.md that says "known gap", "not fixed", "still open" or
#     "left for later" is a warning on the pull request: a decision file
#     holds no fault (rule 7). A heuristic, so it never fails.
#
# .github/workflows/checks.yml runs it on every pull request; on GitHub each
# finding is also marked on the pull request.
# REPORT ONLY UNTIL A DATE for 1 and 2: while today (UTC) is not past
# REPORT_ONLY_UNTIL (YYYY-MM-DD, set by checks.yml for this job), they are
# warnings and it exits 0. Prints each finding; exits 1 if there is one that
# fails.
#
# WHY. docs/PROJECT.md grew from 4,076 lines to 21,438 in five weeks, and the
# 20,018-line main.cpp before it, for the same reason: nothing said where a
# new thing did not go. A decision gets a file of its own, small enough to
# read; the old file stops taking anything new.

set -euo pipefail

cd "$(dirname "$0")/.."

LIMIT=60    # lines in one decision file
GROWTH=5    # net lines one pull request may add to docs/PROJECT.md
DIR=docs/decisions
FROZEN=docs/PROJECT.md
FAULTS='known gap|not fixed|still open|left for later'

base=""
if [[ $# -eq 2 ]]; then
    base=$(git merge-base "$1" "$2")
    head="$2"
elif [[ $# -ne 0 ]]; then
    echo "usage: check-decisions.sh [<base> <head>]" >&2
    exit 2
fi

level=error
if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
    level=warning
fi

found=0
note() {  # <file> <message>: a finding that fails after the first week
    found=$((found + 1))
    echo "$1: $2"
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::${level} file=$1::$2"
    fi
}

# 1. Each decision file, and the README's index.
index=""
if [[ -f "${DIR}/README.md" ]]; then
    index=$(awk '/^## Index/ { on = 1; next } on' "${DIR}/README.md" |
            sed -n 's/^- \[[^]]*\](\([^)]*\.md\)).*/\1/p')
fi
files=()
while IFS= read -r f; do
    files+=("${f}")
done < <(git -c core.quotePath=false ls-files "${DIR}" | grep -v "^${DIR}/README\.md$" || true)

for f in ${files[@]+"${files[@]}"}; do
    name="${f#"${DIR}/"}"
    if [[ "${name}" == */* || ! "${name}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}-[a-z0-9-]+\.md$ ]]; then
        note "${f}" "A decision file is named YYYY-MM-DD-short-name.md and sits directly in ${DIR}/ (${DIR}/README.md)."
        continue
    fi
    lines=$(wc -l < "${f}" | tr -d ' ')
    if [[ "${lines}" -gt "${LIMIT}" ]]; then
        note "${f}" "${lines} lines, over ${LIMIT}. A decision file holds the decision, its date, why, the numbers and what it replaces; research and detail go elsewhere (CLAUDE.md, rule 7)."
    fi
    count=$(grep -cxF -- "${name}" <<< "${index}" || true)
    if [[ "${count}" -ne 1 ]]; then
        note "${f}" "Has ${count} line(s) in the index of ${DIR}/README.md; it needs exactly one."
    fi
done
while IFS= read -r target; do
    [[ -n "${target}" ]] || continue
    [[ -f "${DIR}/${target}" ]] || note "${DIR}/README.md" "The index names ${target}, and there is no such file in ${DIR}/."
done <<< "${index}"

# 2 and 3 need a pull request's base and head.
if [[ -n "${base}" ]]; then
    read -r added removed _ < <(git diff --numstat "${base}" "${head}" -- "${FROZEN}") || true
    added=${added:-0}; removed=${removed:-0}
    # A binary file shows "-"; the frozen file is text.
    if [[ "${added}" =~ ^[0-9]+$ && "${removed}" =~ ^[0-9]+$ ]]; then
        grew=$((added - removed))
        if [[ "${grew}" -gt "${GROWTH}" ]]; then
            note "${FROZEN}" "Grew by ${grew} lines (+${added}, -${removed}); it is frozen and may grow by at most ${GROWTH} (CLAUDE.md, rule 7). A new decision is a file in ${DIR}/."
        fi
    fi

    while IFS=$'\t' read -r file line text; do
        echo "${file}:${line}: ${text#"${text%%[![:space:]]*}"}"
        if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
            echo "::warning file=${file},line=${line}::Fault wording. A decision file holds no fault: a fault you find and do not fix goes in your closing message for MMagTech to decide (CLAUDE.md, rule 7)."
        fi
    done < <(git -c core.quotePath=false diff --unified=0 --no-color -M "${base}" "${head}" \
                 -- "${DIR}" "${FROZEN}" |
             awk -v words="${FAULTS}" '
                 /^\+\+\+ / { file = ($0 == "+++ /dev/null") ? "" : substr($0, 7); next }
                 /^@@ /     { split($3, c, ","); line = substr(c[1], 2); next }
                 /^\+/      { if (file != "" && tolower($0) ~ words) print file "\t" line "\t" substr($0, 2); line++ }
             ')
fi

if [[ ${found} -eq 0 ]]; then
    echo "The decision files are in order."
    exit 0
fi
echo "${found} finding(s)."
if [[ "${level}" == warning ]]; then
    echo "Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
    exit 0
fi
exit 1
