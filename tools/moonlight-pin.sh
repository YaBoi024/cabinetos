#!/usr/bin/env bash
# FORK (handheld): prints the flatpaks.list lines that pin Moonlight, read off
# Flathub right now. It changes nothing; paste what it prints into
# system_files/usr/share/cabinetos/flatpaks.list in its own commit.
#
# Run it on the Deck (or any machine with flatpak and Flathub reachable):
#
#   tools/moonlight-pin.sh
set -euo pipefail

APP=com.moonlight_stream.Moonlight
BRANCH=stable

command -v flatpak >/dev/null || { echo "flatpak is not installed here" >&2; exit 1; }

commit_of() {   # ref branch
    flatpak remote-info --system flathub "$1//$2" 2>/dev/null |
        awk -F': *' '$1 ~ /^ *Commit$/ { print $2; exit }'
}

app_commit=$(commit_of "$APP" "$BRANCH")
[ -n "$app_commit" ] || { echo "could not read $APP from Flathub" >&2; exit 1; }

# The runtime the app sits on: "runtime=org.kde.Platform/x86_64/6.11" in its metadata.
runtime=$(flatpak remote-info --system --show-metadata flathub "$APP//$BRANCH" 2>/dev/null |
    awk -F= '$1 == "runtime" { print $2; exit }')
[ -n "$runtime" ] || { echo "could not read the runtime of $APP" >&2; exit 1; }
rt_ref=${runtime%%/*}
rt_branch=${runtime##*/}
rt_commit=$(commit_of "$rt_ref" "$rt_branch")
[ -n "$rt_commit" ] || { echo "could not read $rt_ref from Flathub" >&2; exit 1; }

echo "# Add under '--- runtimes' (skip it if that runtime is already listed):"
printf 'runtime  %s  %s  %s\n' "$rt_ref" "$rt_branch" "$rt_commit"
echo
echo "# Add under '--- emulators':"
printf 'app  %s  %s  %s\n' "$APP" "$BRANCH" "$app_commit"
