#!/usr/bin/env bash
#
# Is every licence text the one fetched at the revision the image ships?
#
#   ci/check-licences.sh
#
# Runs `tools/fetch-licences.py --check`: every text licences/SOURCES lists is
# in licences/, and its first line names the repository, path and revision
# SOURCES pins, as the tool writes it. A text added by hand, or a pin moved in
# SOURCES without running the tool, fails, so the text in the image is the one
# that applies to what ships (docs/LICENCES.md). Nothing is downloaded.
# .github/workflows/checks.yml runs it on every pull request.
# REPORT ONLY UNTIL A DATE: while today (UTC) is not past REPORT_ONLY_UNTIL
# (YYYY-MM-DD, set by checks.yml for this job), a failure is a warning and it
# exits 0.

set -euo pipefail

cd "$(dirname "$0")/.."

if python3 -I tools/fetch-licences.py --check; then
    exit 0
fi

level=error
if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
    level=warning
fi
if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    echo "::${level} file=licences/SOURCES::A licence text is missing or was not fetched at the revision licences/SOURCES names. Run tools/fetch-licences.py and commit what it writes."
fi
if [[ "${level}" == warning ]]; then
    echo "Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
    exit 0
fi
exit 1
