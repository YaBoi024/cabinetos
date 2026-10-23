#!/usr/bin/env bash
#
# Is every doc named in the list of the docs and their jobs? (CLAUDE.md, rule 2)
#
#   ci/check-docs-list.sh
#
# Every `.md` file directly under docs/ (`docs/WORKING.md`) and every folder
# under docs/ that holds `.md` files (`docs/lessons/`) must be named in
# CONTRIBUTING.md, in the section "The docs, each with its one job", which
# says what each one is for. A doc nobody listed is a doc with no job, and a
# file with no job is how docs/PROJECT.md reached 21,000 lines. Prints each
# one that is missing and exits 1 if there is any; 0 when there is none.
# .github/workflows/checks.yml runs it on every pull request, and on GitHub
# each one is also marked on the pull request.
# REPORT ONLY UNTIL A DATE: while today (UTC) is not past REPORT_ONLY_UNTIL
# (YYYY-MM-DD, set by checks.yml for this job), the missing ones are warnings
# and it exits 0.
#
# NOT CHECKED: what is inside a folder. docs/decisions/ has its own index and
# its own check (ci/check-decisions.sh); docs/lessons/, docs/wiki/ and
# docs/history/ each have a README or a list of their own. The site's files
# (docs/_config.yml, docs/assets/, docs/media/) hold no `.md` and are not docs.

set -euo pipefail

cd "$(dirname "$0")/.."

LIST=CONTRIBUTING.md
HEADING='## The docs, each with its one job'

level=error
if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
    level=warning
fi

section=$(awk -v h="${HEADING}" '
    $0 == h { on = 1; next }
    on && /^## / { exit }
    on { print }' "${LIST}")
if [[ -z "${section}" ]]; then
    echo "${LIST} has no section \"${HEADING#\#\# }\", so no doc is listed."
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::${level} file=${LIST}::No section \"${HEADING#\#\# }\"; ci/check-docs-list.sh reads it."
    fi
    [[ "${level}" == warning ]] && exit 0
    exit 1
fi

# The docs: `docs/NAME.md` for a file directly in docs/, `docs/NAME/` for a
# folder that holds any `.md`.
names=()
while IFS= read -r name; do
    names+=("${name}")
done < <(git -c core.quotePath=false ls-files docs |
         awk -F/ 'NF == 2 && $2 ~ /[.]md$/ { print "docs/" $2 }
                  NF > 2 && $NF ~ /[.]md$/ { print "docs/" $2 "/" }' | sort -u)

missing=0
for name in ${names[@]+"${names[@]}"}; do
    grep -qF -- "${name}" <<< "${section}" && continue
    missing=$((missing + 1))
    echo "${name} is not in the list of the docs."
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::${level} file=${LIST}::${name} is not in the list of the docs and their jobs. Add a line saying its one job (CLAUDE.md, rule 2)."
    fi
done

if [[ ${missing} -eq 0 ]]; then
    echo "Every doc is in the list (${#names[@]} read)."
    exit 0
fi
echo "${missing} doc(s) not in the list in ${LIST}."
if [[ "${level}" == warning ]]; then
    echo "Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
    exit 0
fi
exit 1
