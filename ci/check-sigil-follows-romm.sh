#!/usr/bin/env bash
#
# Does the console build in the Sigil commit that the RomM it needs builds in?
#
#   ci/check-sigil-follows-romm.sh
#
# Sigil's pin follows RomM's
# (docs/decisions/2026-11-22-sigil-pin-follows-romm.md): the console builds
# in the commit RomM builds in, SIGIL_VERSION in RomM's docker/Dockerfile.
# The RomM the console needs is ROMM_NEEDED in cabinetos.env ("5.5", the
# version docs/wiki/romm.md names). This reads RomM's releases on GitHub,
# takes the newest of that version (a pre-release counts: testers run a
# version before its stable release, as 5.5's alphas were run), reads its
# SIGIL_VERSION and compares it with the commit
# frontend/third_party/sigil/VERSION names. When they differ it says which
# way and how many commits apart, each commit's title, and which of Sigil's
# Switch files they touch (the skill check-emulator-update lists them). It
# fails when RomM's is newer, or the two have parted: the pin moves in the
# pull request that checks the console against that commit. The console's
# being newer passes with a note, since the pin rule lets it move ahead of
# RomM for one of its five reasons.
#
# .github/workflows/sigil-pin.yml runs it on the 1st of each month and on a
# pull request that changes the pin, the RomM the console needs, or this
# check. Needs gh, signed in (GH_TOKEN on GitHub). Prints what it found, and
# on GitHub writes it to the run's summary too. REPORT ONLY UNTIL A DATE, as
# the checks in checks.yml: while today (UTC) is not past REPORT_ONLY_UNTIL
# (YYYY-MM-DD, set by sigil-pin.yml), a failure is a warning and it exits 0.
#
# WHY. RomM moves Sigil in its own releases, with nothing to tell the
# console. Without this the two would drift apart until a player's Switch
# save failed between the console and another app.

set -euo pipefail

cd "$(dirname "$0")/.."

ROMM=rommapp/romm
SIGIL=rommapp/argosy-sigil
VERSION_FILE=frontend/third_party/sigil/VERSION
# Sigil's files the console's Switch saves go through, as check-emulator-update
# names them: the switch_ files, and the profile and layout files, which hold
# every system's rows.
SWITCH_FILES='^src/(switch_[^/]*|sync_profiles\.c|save_profiles\.c|save_layout\.c)$'

level=error
if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
    level=warning
fi

report=""
say() {
    echo "$1"
    report+="$1"$'\n'
}

finish() {  # <0 for a pass, 1 for a finding> <one-line finding>
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        printf '%s' "${report}" >>"${GITHUB_STEP_SUMMARY}"
    fi
    (( $1 == 0 )) && exit 0
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::${level} file=${VERSION_FILE}::$2"
    fi
    if [[ "${level}" == warning ]]; then
        echo "Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
        exit 0
    fi
    exit 1
}

needed=$(sed -n 's/^ROMM_NEEDED="\{0,1\}\([0-9.]*\)"\{0,1\}$/\1/p' cabinetos.env)
ours=$(sed -n '1s/^sigil [^,]*, commit \([0-9a-f]\{40\}\)$/\1/p' "${VERSION_FILE}")
[[ -n "${needed}" ]] || { say "cabinetos.env has no ROMM_NEEDED line."; finish 1 "no ROMM_NEEDED in cabinetos.env"; }
[[ -n "${ours}" ]] || { say "${VERSION_FILE} names no commit on its first line."; finish 1 "no commit in ${VERSION_FILE}"; }

# The newest release of the version the console needs.
tag=$(ci/newest-romm.sh)
if [[ -z "${tag}" ]]; then
    say "RomM has no release of ${needed}, the version cabinetos.env says the console needs."
    finish 1 "no RomM ${needed} release"
fi

theirs=$(gh api "repos/${ROMM}/contents/docker/Dockerfile?ref=${tag}" --jq .content | base64 -d |
    sed -n 's/^ARG SIGIL_VERSION=\([0-9a-f]\{40\}\)[[:space:]]*$/\1/p' | head -1)
if [[ -z "${theirs}" ]]; then
    say "RomM ${tag}'s docker/Dockerfile has no ARG SIGIL_VERSION line: read it, and say in ${VERSION_FILE} which Sigil it builds in."
    finish 1 "no SIGIL_VERSION in RomM ${tag}"
fi

if [[ "${theirs}" == "${ours}" ]]; then
    say "RomM ${tag}, the newest release of RomM ${needed}, builds in Sigil ${ours:0:9}, the commit the console builds in."
    finish 0 ""
fi

# Which way and how far. The commits listed are those of the newer side
# (RomM's, or ours when ours is ahead).
status=$(gh api "repos/${SIGIL}/compare/${ours}...${theirs}" --jq .status)
if [[ "${status}" == behind ]]; then
    from=${theirs} to=${ours}
else
    from=${ours} to=${theirs}
fi
compare=$(gh api "repos/${SIGIL}/compare/${from}...${to}")
say "RomM ${tag}, the newest release of RomM ${needed}, builds in Sigil ${theirs:0:9}; the console builds in ${ours:0:9}."
count=$(jq -r .ahead_by <<<"${compare}")
case "${status}" in
    ahead) say "RomM's is ${count} commit(s) newer:" ;;
    behind) say "The console's is ${count} commit(s) newer:" ;;
    *) say "They have parted ($(jq -r .behind_by <<<"${compare}") commit(s) only in the console's). RomM's own:" ;;
esac
while IFS= read -r line; do
    say "- ${line}"
done < <(jq -r '.commits[] | .sha[0:9] + " " + (.commit.message | split("\n")[0])' <<<"${compare}")
switch=$(jq -r '.files[].filename' <<<"${compare}" | grep -E "${SWITCH_FILES}" | paste -sd ' ' - || true)
if [[ -n "${switch}" ]]; then
    say "Files the console's Switch saves go through that changed (the profile and layout files hold other systems too, so read what changed): ${switch}."
else
    say "None of the files the console's Switch saves go through changed."
fi

# Ahead of RomM is allowed for one of the five reasons of the pin rule,
# named in the pull request that moved it, so it is said and not failed.
if [[ "${status}" == behind ]]; then
    say "The console is ahead of RomM, which the pin rule allows only for one of its five reasons; the pull request that moved it names the reason. The pin comes back to RomM's when RomM builds in this commit or a newer one."
    finish 0 ""
fi
say "Move the pin in the pull request that checks the console against RomM ${tag} (${VERSION_FILE} says how; the skill check-emulator-update)."
finish 1 "RomM ${tag} builds in Sigil ${theirs:0:9}, the console ${ours:0:9}"
