#!/usr/bin/env bash
#
# Was RPCS3's settings snapshot taken against the RPCS3 the image pins?
#
#   ci/check-rpcs3-snapshot.sh
#
# RPCS3's per-game settings ride in the image as a snapshot,
# build_files/rpcs3-config-database.json, refreshed by
# tools/fetch-rpcs3-config-database.py only when RPCS3's pin moves, in that
# pull request (docs/decisions/2026-11-06-rpcs3-settings-database-snapshot.md;
# CONTRIBUTING.md, "Duties that come with some changes"). The tool records
# the pin it ran against in build_files/rpcs3-config-database.pin (its first
# word); this compares that word with RPCS3_VERSION in
# build_files/install-rpcs3.sh, and prints the finding and exits 1 when they
# differ, 0 when they match. The whole tree is read, not a diff, so it runs
# the same by hand, and a pin move that left the snapshot as it was is caught
# whether or not the server's file had changed. build_files/install-rpcs3.sh
# makes the same comparison at the image build, so the testing push cannot
# ship a mismatch either; this one says so on the pull request first.
# .github/workflows/checks.yml runs it on every pull request; on GitHub the
# finding is also marked on the pull request. REPORT ONLY UNTIL A DATE: while
# today (UTC) is not past REPORT_ONLY_UNTIL (YYYY-MM-DD, set by checks.yml),
# it is a warning and exits 0.
#
# WHY. The refresh would otherwise rest on whoever moves the pin remembering
# the duty, and a snapshot taken against an older build drifts from what the
# new one needs (the decision file says how).

set -euo pipefail

cd "$(dirname "$0")/.."

SCRIPT=build_files/install-rpcs3.sh
PIN_FILE=build_files/rpcs3-config-database.pin

level=error
if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
    level=warning
fi

finding() {  # <message>
    echo "${PIN_FILE}: $1"
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::${level} file=${PIN_FILE}::$1"
    fi
    [[ "${level}" == warning ]] && exit 0
    exit 1
}

pinned=$(sed -n 's/^readonly RPCS3_VERSION="\([^"]*\)".*/\1/p' "${SCRIPT}" | head -1)
[[ -n "${pinned}" ]] || finding "no readonly RPCS3_VERSION line in ${SCRIPT}"
[[ -f "${PIN_FILE}" ]] || finding "missing: run tools/fetch-rpcs3-config-database.py and commit it"
taken=$(head -1 "${PIN_FILE}" | cut -d' ' -f1)
if [[ "${taken}" != "${pinned}" ]]; then
    finding "the settings snapshot was taken against RPCS3 ${taken}, the image pins ${pinned}: run tools/fetch-rpcs3-config-database.py and commit it (CONTRIBUTING.md, Duties)"
fi
echo "RPCS3's settings snapshot was taken against the pinned build, ${pinned}."
