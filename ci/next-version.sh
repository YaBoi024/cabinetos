#!/usr/bin/env bash
#
# The version of the image about to be built: the UTC date, 2026.09.28, and
# 2026.09.28.2, .3 for further images the same day. MMagTech, 2026-09-25;
# docs/SETTINGS.md, System.
#
# NUMBERED BY THE REGISTRY, not by a counter kept anywhere: every published
# image is also tagged with its version, so the tags already there say which
# numbers today has used. A docs-only commit builds nothing and so uses no
# number up. `latest` and `testing` share the one sequence, so a version names
# exactly one image whichever channel it went out on.
#
# Usage: ci/next-version.sh [--strict] <registry/image>
#   --strict  fail if the registry cannot be read, rather than guess. Used for
#             every build that publishes; without it (a run by hand) a
#             registry it cannot read makes it guess.

set -euo pipefail

strict=0
if [[ "${1:-}" == --strict ]]; then strict=1; shift; fi
IMAGE="${1:?usage: ci/next-version.sh --strict <registry/image>}"

today=$(date -u +%Y.%m.%d)

if ! tags=$(skopeo list-tags "docker://${IMAGE}" | jq -r '.Tags[]'); then
    if (( strict )); then
        echo "cannot read the tags of ${IMAGE}, so cannot number this image" >&2
        exit 1
    fi
    tags=""
fi

if ! grep -qxF "${today}" <<<"${tags}"; then
    echo "${today}"
    exit 0
fi
n=2
while grep -qxF "${today}.${n}" <<<"${tags}"; do n=$(( n + 1 )); done
echo "${today}.${n}"
