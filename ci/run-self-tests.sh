#!/usr/bin/env bash
#
# The frontend's self-tests that need no screen, no server, no controller
# and no console, run against the binary the Compile step just built (#398,
# step 4). They are the table in CONTRIBUTING.md, "Running and testing",
# except --check-option-tables, which has a step of its own. Each exits 0
# when it passes.
#
# EVERY TEST GETS A SCRATCH WORLD. A storage root, a HOME (the tokens and
# the PIN live under it, not under the root) and CABINETOS_DRIVES pointed at
# an empty folder, so the drive search finds no games drive. Without the
# last, --first-run-writes's sign-out empties every games drive it finds
# (FIRSTRUN-14); a runner has none, but the scratch is what keeps that true.
# They run in the builder container, whose libraries the binary was built
# against, from frontend/ as CONTRIBUTING.md says.
#
# NOT RUN HERE, and why:
#   --romm-race-test  needs its stand-in RomM server running beside it
#                     (tools/racemock.py), and must never be pointed at a
#                     real one.
#   --evict-test, --state-test, --speed-test, --sync-test, --overlay-test
#                     need a screen, a core and a game, or a server.
#
# REPORT ONLY UNTIL A DATE: while today (UTC) is not past REPORT_ONLY_UNTIL
# (YYYY-MM-DD, set by build-frontend.yml), a failing test is the same report
# and a warning annotation, and it exits 0; from the next day it fails, the
# rule checks.yml's checks follow (CONTRIBUTING.md, "What CI checks").
#
# Usage: ci/run-self-tests.sh
#
# Run by build-frontend.yml after the Compile step; it needs
# frontend/build/cabinetos-frontend and the cabinetos-builder container.

set -euo pipefail

TESTS=(
    --first-run-rules
    --first-run-writes
    --accounts-test
    --players-test
    --fightstick-test
    --arcade-settings-test
    --battery-test
    --volume-test
    --playtime-test
    --disc-test
    --same-names-test
    --files-test
    --vectrex-sheets-test
)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
[[ -x frontend/build/cabinetos-frontend ]] || {
    echo "::error::frontend/build/cabinetos-frontend is not built"
    exit 1
}

SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
WORK="${RUNNER_TEMP:-/tmp}/self-tests"
rm -rf "$WORK"
mkdir -p "$WORK"
REPORT="$WORK/report.md"

{
    echo "## Self-tests"
    echo
    echo "| Test | Result | Seconds |"
    echo "|---|---|---|"
} > "$REPORT"

failed=()
for t in "${TESTS[@]}"; do
    name="${t#--}"
    scratch="$WORK/$name"
    mkdir -p "$scratch/root" "$scratch/home" "$scratch/drives"
    SECONDS=0
    rc=0
    timeout 300 podman run --rm \
        -v "$ROOT/frontend":/src:Z -v "$scratch":/scratch:Z -w /src \
        -e HOME=/scratch/home -e CABINETOS_DRIVES=/scratch/drives \
        cabinetos-builder \
        build/cabinetos-frontend --storage-root /scratch/root "$t" \
        > "$WORK/$name.log" 2>&1 || rc=$?
    seconds=$SECONDS
    echo "::group::$t (exit $rc, ${seconds}s)"
    cat "$WORK/$name.log"
    echo "::endgroup::"
    if [[ $rc -eq 0 ]]; then
        echo "| \`$t\` | passed | $seconds |" >> "$REPORT"
    else
        [[ $rc -eq 124 ]] && what="did not finish in 300s" || what="failed, exit $rc"
        echo "| \`$t\` | **$what** | $seconds |" >> "$REPORT"
        failed+=("$t")
    fi
done

for t in "${failed[@]}"; do
    {
        echo
        echo "<details><summary>$t</summary>"
        echo
        echo '```'
        tail -n 60 "$WORK/${t#--}.log"
        echo '```'
        echo "</details>"
    } >> "$REPORT"
done

if [[ ${#failed[@]} -eq 0 ]]; then
    why="All ${#TESTS[@]} self-tests passed."
else
    why="${#failed[@]} of ${#TESTS[@]} self-tests failed: ${failed[*]}."
fi
printf '\n%s\n' "$why" >> "$REPORT"
cat "$REPORT" >> "$SUMMARY"
echo "$why"

if [[ ${#failed[@]} -gt 0 ]]; then
    if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
        echo "::warning::$why Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
        exit 0
    fi
    echo "::error::$why"
    exit 1
fi
