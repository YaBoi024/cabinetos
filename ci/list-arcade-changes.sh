#!/usr/bin/env bash
#
# Every arcade game whose emulator would change, when a pull request changes
# what the arcade emulators' catalogues are made from. A game that moves to
# another emulator loses its saves and states, so the move is seen before it
# ships (docs/decisions/2026-10-28-emulator-updates-and-saves.md).
#
# WHAT MAKES THE CATALOGUES: cores/arcade-sets.sh itself, the arcade cores'
# arms in cores/build-core.sh (their pins), cores/fetch-mame.sh (MAME 0.289's
# -listxml), cores/build-sm2emu.sh (sm2-emu's pin) and cores/build-supermodel.sh
# (Supermodel's). When none of it
# differs from the base, it says so and stops. Otherwise it makes every
# catalogue at the base and here, and asks
# the frontend this run compiled, with this pull request's rule, which
# emulator each set gets from each (cabinetos-frontend --arcade-changes,
# frontend/src/arcadematch.h): by name, and whether its files changed in the
# emulator that keeps it. Every set any list names, not one library's.
#
# A REPORT, never a failure: what to do about a game that moves is the pull
# request's question. A catalogue that cannot be made does fail, as it would
# fail the image.
#
# Usage: ci/list-arcade-changes.sh <base commit>   from the repository root,
#   after the frontend is compiled (build-frontend.yml), with the builder
#   container at cabinetos-builder

set -euo pipefail

BASE="${1:?usage: ci/list-arcade-changes.sh <base commit>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORES=(flycast fbneo_libretro mame2003_plus sm2emu supermodel mame_libretro)
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"

git -C "${ROOT}" cat-file -e "${BASE}^{commit}" 2>/dev/null ||
    git -C "${ROOT}" fetch --quiet --no-tags --depth=1 origin "${BASE}"

# What a commit makes the catalogues from, as text.
inputs() {
    local rev="$1" core
    git -C "${ROOT}" show "${rev}:cores/arcade-sets.sh" 2>/dev/null || true
    for core in "${CORES[@]}"; do
        git -C "${ROOT}" show "${rev}:cores/build-core.sh" 2>/dev/null |
            awk -v c="${core})" '$1 == c {on=1} on {print} on && /;;/ {exit}'
    done
    git -C "${ROOT}" show "${rev}:cores/fetch-mame.sh" 2>/dev/null | grep '^LISTXML_' || true
    git -C "${ROOT}" show "${rev}:cores/build-sm2emu.sh" 2>/dev/null | grep -E '^(REPO|COMMIT)=' || true
    git -C "${ROOT}" show "${rev}:cores/build-supermodel.sh" 2>/dev/null | grep -E '^(REPO|COMMIT)=' || true
}

if [[ "$(inputs "${BASE}" | sha256sum)" == "$(inputs HEAD | sha256sum)" ]]; then
    echo "No arcade catalogue's sources changed: no game changes emulator."
    { echo "## Arcade games whose emulator would change"; echo; echo "None: no catalogue's sources changed."; } >> "${SUMMARY}"
    exit 0
fi

tmp=$(mktemp -d)
trap 'git -C "${ROOT}" worktree remove --force "${tmp}/base" >/dev/null 2>&1 || true; rm -rf "${tmp}"' EXIT
git -C "${ROOT}" worktree add --quiet --detach "${tmp}/base" "${BASE}"
mkdir -p "${tmp}/old" "${tmp}/new"
for core in "${CORES[@]}"; do
    # A core the base does not know writes nothing, and reads as empty.
    bash "${tmp}/base/cores/arcade-sets.sh" "${core}" "${tmp}/old"
    bash "${ROOT}/cores/arcade-sets.sh" "${core}" "${tmp}/new"
done

podman run --rm -v "${ROOT}":/repo:Z -v "${tmp}":/cat:Z -w /repo cabinetos-builder \
    frontend/build/cabinetos-frontend --arcade-changes /cat/old /cat/new > "${tmp}/changes.txt"
cat "${tmp}/changes.txt"

total=$(grep -c . "${tmp}/changes.txt" || true)
{
    echo "## Arcade games whose emulator would change"
    echo
    echo "Old catalogues at ${BASE:0:12}, new ones here; every set any list names. A game that"
    echo "changes emulator loses its saves and states. The whole list is in this step's log."
    echo
    echo '```'
    tail -1 "${tmp}/changes.txt"
    if (( total > 1 )); then
        sed -n "1,$(( total - 1 < 300 ? total - 1 : 300 ))p" "${tmp}/changes.txt"
        if (( total - 1 > 300 )); then
            echo "... and $(( total - 301 )) more in the log"
        fi
    fi
    echo '```'
} >> "${SUMMARY}"
