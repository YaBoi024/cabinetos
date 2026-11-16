#!/usr/bin/env bash
#
# Every Flathub pin can still be downloaded, with room to spare.
#
#   ci/check-flatpak-pins.sh [<manifest>]
#
# system_files/usr/share/cabinetos/flatpaks.list pins Eden, xemu, their
# runtimes and their graphics driver to exact Flathub builds, and every
# console installs those builds (cabinetos-flatpak-setup). Flathub keeps only
# its last few builds of each (4 to 7 on 2026-11-10) and drops the oldest as
# new ones come. A console installed after a pin has gone quietly gets the
# newest build instead, one nobody tried on the A9.
#
# So for each line this reads the history Flathub still serves and fails when
# the pinned build is gone, or when fewer than MARGIN older builds are left
# behind it, since the next releases would take it. The fix is to try a newer
# build on the A9 and move the pin (flatpaks.list says how).
#
# .github/workflows/flatpak-pins.yml runs it on the 1st of each month and on
# a pull request that changes the manifest, so a mistyped commit fails before
# it ships. Needs flatpak; adds Flathub as a --user remote when it is not
# there. Prints a line per pin; exits 1 if one fails.
set -euo pipefail
cd "$(dirname "$0")/.."

MANIFEST=${1:-system_files/usr/share/cabinetos/flatpaks.list}
REMOTE=flathub
MARGIN=2

flatpak remote-add --user --if-not-exists "${REMOTE}" \
    https://dl.flathub.org/repo/flathub.flatpakrepo

summary=""
note() {
    echo "$1"
    summary+="$1"$'\n'
}

failed=0
while read -r kind ref branch commit; do
    [[ -z "${kind:-}" || "${kind}" == \#* ]] && continue
    name="${ref}//${branch}"
    if ! history=$(flatpak remote-info --user --log "${REMOTE}" \
            "${kind}/${ref}/x86_64/${branch}" 2>&1); then
        note "- **${name}**: Flathub did not answer: ${history//$'\n'/ }"
        failed=1
        continue
    fi
    # Newest first: the current build, then each parent Flathub still has.
    mapfile -t builds < <(sed -n 's/^ *Commit: *\([0-9a-f]\{64\}\)$/\1/p' <<<"${history}")
    at=-1
    for i in "${!builds[@]}"; do
        [[ "${builds[$i]}" == "${commit}" ]] && at=$i && break
    done
    if (( at < 0 )); then
        note "- **${name}**: ${commit:0:12} is no longer on Flathub (it keeps ${#builds[@]}); try a newer build on the A9 and move the pin"
        failed=1
        continue
    fi
    older=$(( ${#builds[@]} - at - 1 ))
    newer=${at}
    if (( older < MARGIN )); then
        note "- **${name}**: ${commit:0:12} has only ${older} older build(s) left behind it on Flathub (${newer} newer); try a newer build on the A9 and move the pin"
        failed=1
    else
        note "- ${name}: ${commit:0:12} is there, ${newer} newer and ${older} older"
    fi
done < "${MANIFEST}"

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    {
        echo "### Flathub pins"
        echo
        echo -n "${summary}"
    } >> "${GITHUB_STEP_SUMMARY}"
fi
exit "${failed}"
