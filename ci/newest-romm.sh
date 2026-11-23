#!/usr/bin/env bash
#
# Print the newest release of the RomM version the console needs, as its tag
# on GitHub (5.5 -> 5.5.0-beta.1, 5.5.0, 5.5.2); print nothing when RomM has
# none.
#
#   ci/newest-romm.sh
#
# The version is ROMM_NEEDED in cabinetos.env. A pre-release counts: testers
# run a version before its stable release, as 5.5's alphas were run. Needs gh,
# signed in (GH_TOKEN on GitHub). Used by check-sigil-follows-romm.sh and
# romm-api.yml.

set -euo pipefail

cd "$(dirname "$0")/.."

needed=$(sed -n 's/^ROMM_NEEDED="\{0,1\}\([0-9.]*\)"\{0,1\}$/\1/p' cabinetos.env)
[[ -n "${needed}" ]] || { echo "cabinetos.env has no ROMM_NEEDED line." >&2; exit 1; }

gh api "repos/rommapp/romm/releases?per_page=100" \
    --jq '[.[] | select(.draft | not)] | sort_by(.published_at) | reverse | .[].tag_name' |
    grep -E "^v?${needed//./\\.}([.-]|$)" | head -1 || true
