#!/usr/bin/env bash
#
# The unused code in the files a pull request touches, as warnings on it.
# (CLAUDE.md, rule 6)
#
#   ci/warn-unused-touched.sh <ours> <base> <head>
#
# <ours> is the list ci/report-unused-code.sh makes of what the linker found
# nothing reachable from `main` using, one per line, `<file> TAB <kind> TAB
# <name>`. This keeps the names in files the pull request changes (the
# diff from <base> to <head>), leaves out the ones on KEEP, and for each that
# is left says so in the run's summary and, on GitHub, as a warning on the
# file. WARNS ONLY, never a failure: the report is a signal, and the pull
# request says in its description what it removed and what it kept
# (CLAUDE.md, rule 6).
#
# A name the report cannot place in a file (a header's, or the same name
# dropped from several files) is not here; the full report lists those.
#
# KEEP is what is held on purpose: a name, or part of one, and why. A name
# goes here only with a reason, and comes off when the reason does. Empty
# today: the names once held (`NeedsHardwareRender` and the `hwRender` flag)
# were removed on 2026-10-19 (#492), and Dolphin's `Decrypt` and
# `KeyGen3rdParty` are in wiibridge/, which the report does not build.

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: warn-unused-touched.sh <ours> <base> <head>" >&2
    exit 2
fi
ours="$1"
base=$(git merge-base "$2" "$3")
head="$3"

# <part of the name> TAB <why it is kept>
read -r -d '' KEEP <<'EOF' || true
EOF

kept() {  # <name>: is it on KEEP?
    local name="$1" part
    while IFS=$'\t' read -r part _; do
        [[ -n "${part}" && "${name}" == *"${part}"* ]] && return 0
    done <<< "${KEEP}"
    return 1
}

touched=$(git -c core.quotePath=false diff --name-only --no-renames "${base}" "${head}")

summary="${GITHUB_STEP_SUMMARY:-/dev/null}"
n=0
listed=""
while IFS=$'\t' read -r file kind name; do
    [[ -n "${file}" && "${file}" != "~" ]] || continue
    grep -qxF -- "${file}" <<< "${touched}" || continue
    kept "${name}" && continue
    n=$((n + 1))
    listed+="- \`${file}\`: ${name} (${kind})"$'\n'
    echo "${file}: ${name} (${kind}) is used by nothing reachable from main."
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::warning file=${file}::${name} (${kind}) is used by nothing reachable from main. Remove it, or keep it and say why in the pull request (CLAUDE.md, rule 6)."
    fi
done < "${ours}"

if [[ ${n} -eq 0 ]]; then
    echo "Nothing unused in the files this pull request touches."
    exit 0
fi
{
    echo
    echo "### Unused, in the files this pull request touches (${n})"
    echo
    printf '%s' "${listed}"
} >> "${summary}"
exit 0
