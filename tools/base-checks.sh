#!/usr/bin/env bash
#
# The Deck half of a base update's checks. Run from the Mac once the Deck runs
# the testing image built from the base-update branch.
#
# THE BASE-UPDATE PULL REQUEST LISTS ITS OWN CHECKS (MMagTech, 2026-10-09):
# only for the areas of ci/base-watch.txt the update changed, split into what
# the Deck's log answers and what needs a person by hand. This does the first
# half: it works out the changed areas the same way the pull request did
# (ci/base-report.py, main's manifest against the branch's), confirms the Deck
# really runs the new base, runs each area's log: check there as `cabinet`,
# and prints the by-hand checks. It changes nothing on the console and
# presses nothing.
#
# It connects the way tools/deck-loop.sh does: CABINETOS_DECK=cabinet@<address>
# (CABINETOS_A9 still works), Developer access on in the Deck's Settings, About,
# and the Deck's password asked once, because the connection is shared.
#
#   tools/base-checks.sh base-update/44.20261006.1
#
# Exit 0 when every log check passed.
set -uo pipefail
export LC_ALL=C

BRANCH="${1:?usage: tools/base-checks.sh <base-update branch>}"
DECK="${CABINETOS_DECK:-${CABINETOS_A9:-}}"
[[ -n "$DECK" ]] || { echo "set CABINETOS_DECK to cabinet@<the Deck's address> (see tools/deck-loop.sh)" >&2; exit 2; }
PORT="${CABINETOS_DECK_PORT:-2222}"
SOCK="/tmp/base-checks-%C"
SSHOPT=(-p "$PORT" -o ConnectTimeout=20 -o StrictHostKeyChecking=accept-new
        -o ControlMaster=auto -o ControlPath="$SOCK" -o ControlPersist=15m)
[[ -n "${CABINETOS_KEY:-}" ]] && SSHOPT+=(-i "$CABINETOS_KEY")
SSH=(ssh "${SSHOPT[@]}" "$DECK")
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT" || exit 2
git fetch -q origin main "$BRANCH" || { echo "could not fetch main and $BRANCH"; exit 2; }
git show origin/main:base-manifest.txt > "$TMP/old.txt" &&
    git show "origin/$BRANCH:base-manifest.txt" > "$TMP/new.txt" || exit 2
python3 ci/base-report.py checks "$TMP/old.txt" "$TMP/new.txt" > "$TMP/checks.tsv" || exit 2

fail=0
ok() { printf 'ok    %s\n' "$1"; }
# shellcheck disable=SC2001 # sed indents every line, which ${//} cannot
bad() { printf 'FAIL  %s\n' "$1"; [[ -n "${2:-}" ]] && sed 's/^/      /' <<<"$2"; fail=1; }

echo "== areas this update changed"
awk -F'\t' '$1 == "area" { print "      " $2 }' "$TMP/checks.tsv"

echo "== from the Deck's log ($DECK)"
# Proof first: the Deck runs THIS base, not the one before. Every package the
# update brings to a watched area must be installed at its new version (ones
# CabinetOS removes are left out).
want=$(python3 ci/base-report.py expect "$TMP/old.txt" "$TMP/new.txt" | sort)
if [[ -n "$want" ]]; then
    have=$("${SSH[@]}" "rpm -q --qf '%{NAME} %{VERSION}-%{RELEASE}\n' $(awk '{print $1}' <<<"$want" | tr '\n' ' ') 2>/dev/null")
    missing=$(comm -23 <(echo "$want") <(grep -v 'not installed' <<<"$have" | sort -u))
    if [[ -z "$missing" ]]; then
        ok "The Deck runs this base: the changed packages have their new versions"
    else
        bad "The Deck runs this base: $(wc -l <<<"$missing" | tr -d ' ') packages are not at the branch's versions" \
            "$(head -8 <<<"$missing")"
    fi
fi

while IFS=$'\t' read -r kind check command; do
    [[ "$kind" == log ]] || continue
    if out=$("${SSH[@]}" bash -s <<<"$command" 2>&1); then ok "$check"; else bad "$check" "$out"; fi
done < "$TMP/checks.tsv"

echo "== by hand (on the Deck, and on a TV when docked)"
n=0
while IFS=$'\t' read -r kind check _; do
    [[ "$kind" == tv ]] || continue
    n=$((n + 1)); printf '%3d.  %s\n' "$n" "$check"
done < "$TMP/checks.tsv"
(( n )) || echo "      nothing: the log answers all of it"

exit $fail
