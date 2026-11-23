#!/usr/bin/env bash
#
# Does a pull request spell a RomM path outside the RomM client? (#398, step 4)
#
#   ci/check-romm-paths.sh <base> <head>   the lines added from base to head
#   ci/check-romm-paths.sh                 every line of the tree, by hand
#
# Prints each line that spells one, as file:line, and exits 1 if there is any;
# 0 when there is none. .github/workflows/checks.yml runs the first form on
# every pull request; on GitHub each line is also marked on the pull request.
# REPORT ONLY UNTIL A DATE: while today (UTC) is not past REPORT_ONLY_UNTIL
# (YYYY-MM-DD, set by checks.yml), the lines are warnings and it exits 0.
#
# WHY. "The RomM client is the only code that talks to RomM"
# (docs/ARCHITECTURE.md). A path spelled anywhere else is a second copy of
# RomM's API that the client cannot see: when RomM changes it, or the client
# learns to ask differently, that copy goes on asking the old way. The ones
# already there are a known gap (docs/ARCHITECTURE.md, "RomM paths spelled
# outside the client"); this stops the list growing.
#
# A RomM PATH is `/api/`, `/assets/`, `/resources/roms/` or `/cover/` in code.
# The last two are how image.cpp and covercache.cpp take a cover's address
# apart, which `/api/` and `/assets/romm` alone would miss (the audit's
# ROMM-18). Comments are not code: a line that starts with `//`, `/*`, `*` or
# `#`, and whatever follows ` //` or ` #`, is left out, so a comment may name
# the call it explains.
#
# NOT CHECKED: `.md` files and docs/ (prose), tools/ (run by hand, never on a
# console), and EXEMPT below. KNOWN is every path spelled outside the client
# today, by file and by the text that spells it, so a pull request that edits
# or moves one of those lines is not failed for it, and the run over the whole
# tree is quiet. Moving one into the client takes its entry out of KNOWN.
# A known text is taken out of the line before the line is checked, so the
# same line spelling a second, new path still fails.

set -euo pipefail

PATHS='/api/|/assets/|/resources/roms/|/cover/'

# Files that may spell any of them: the RomM client itself, Sunshine's client
# (its `/api/` is Sunshine's own API, on this console), and this file.
EXEMPT=(
    'frontend/src/romm.h'
    'frontend/src/romm_*'
    'frontend/src/remoteplay.cpp'
    'ci/check-romm-paths.sh'
    # RomM's API as it is written out on purpose: the client's uses list, the
    # copy of RomM's description, and the check that reads both.
    'ci/check-romm-api.py'
    'ci/romm-api-uses.txt'
    'ci/romm-api.json'
)

# file <tab> the text that spells it <tab> what it is
read -r -d '' KNOWN <<'EOF' || true
frontend/src/launch_routes.cpp	"/api/firmware/"	a firmware file's download
frontend/src/launch_routes.cpp	"/api/roms/"	a one-file game's download
frontend/src/probes.cpp	"/api/roms/"	--rom-probe's download
frontend/src/manual.cpp	"/assets/	the manual's address
frontend/src/startup.cpp	"/api/users/"	an avatar's address
frontend/src/image.cpp	"/assets/"	a cover's address, in a log line
frontend/src/image.cpp	"/cover/	the large cover when the small one will not do
frontend/src/covercache.cpp	"/resources/roms/"	the cover cache's key
system_files/usr/libexec/cabinetos-report	"/api/heartbeat"	the report's server check, a program of its own
EOF

if [[ $# -eq 2 ]]; then
    range=("$1...$2")
elif [[ $# -eq 0 ]]; then
    range=("$(git hash-object -t tree /dev/null)" HEAD)
else
    echo "usage: check-romm-paths.sh [<base> <head>]" >&2
    exit 2
fi

exempt() {
    local file="$1" glob
    for glob in "${EXEMPT[@]}"; do
        # shellcheck disable=SC2053  # the right side is a glob on purpose
        [[ "${file}" == ${glob} ]] && return 0
    done
    return 1
}

# The code on a line: nothing for a comment line, the line up to ` //` or ` #`
# otherwise. C and C++ comment with `//`, everything else here with `#`.
code_of() {
    local file="$1" text="$2" trimmed
    trimmed="${text#"${text%%[![:space:]]*}"}"
    case "${file}" in
        *.c|*.cc|*.cpp|*.h|*.hpp)
            case "${trimmed}" in '//'*|'/*'*|'*'*) return 0 ;; esac
            text="${text%%[[:space:]]//*}"
            text="${text%%[[:space:]]/\**}"
            ;;
        *)
            case "${trimmed}" in '#'*) return 0 ;; esac
            text="${text%%[[:space:]]#*}"
            ;;
    esac
    printf '%s' "${text}"
}

level=error
if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
    level=warning
fi

found=0
while IFS=$'\t' read -r file line text; do
    exempt "${file}" && continue
    code=$(code_of "${file}" "${text}")
    [[ -n "${code}" ]] || continue
    while IFS=$'\t' read -r kfile ktext _; do
        [[ "${kfile}" == "${file}" ]] && code="${code//"${ktext}"/}"
    done <<< "${KNOWN}"
    if grep -qE -- "${PATHS}" <<< "${code}"; then
        found=$((found + 1))
        echo "${file}:${line}: ${text#"${text%%[![:space:]]*}"}"
        if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
            echo "::${level} file=${file},line=${line}::A RomM path outside the RomM client. Ask the client for it (romm.h), or see ci/check-romm-paths.sh."
        fi
    fi
done < <(git -c core.quotePath=false diff --unified=0 --no-color -M "${range[@]}" \
             -- . ':!*.md' ':!docs/' ':!tools/' |
         awk -v paths="${PATHS}" '
             /^\+\+\+ / { file = ($0 == "+++ /dev/null") ? "" : substr($0, 7); next }
             /^@@ /     { split($3, c, ","); line = substr(c[1], 2); next }
             /^\+/      { if (file != "" && $0 ~ paths) print file "\t" line "\t" substr($0, 2); line++ }
         ')

if [[ ${found} -eq 0 ]]; then
    echo "No RomM path outside the RomM client."
    exit 0
fi
echo "${found} line(s) spell a RomM path outside the RomM client."
if [[ "${level}" == warning ]]; then
    echo "Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
    exit 0
fi
exit 1
