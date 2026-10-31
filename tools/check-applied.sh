#!/usr/bin/env bash
# shellcheck disable=SC2015 # `test && ok || bad` is if-then-else: the helpers only print
#
# Did the emulators take what the console told them? Run from the Mac, against
# the A9, after a version bump of anything CI cannot load.
#
# THE A9 HALF OF THE VERSION-BUMP CHECK (#63, MMagTech 2026-10-02). CI lists
# every built-in core's options and fails on a difference (check-options.sh,
# tools/core-options.c), and checks every value the console sets against
# those lists (frontend --check-option-tables). Two kinds of emulator are out
# of its reach:
#
#   1. Dolphin and FBNeo declare their options only with a real game loaded.
#      This lists them with games from the console's own cache and compares
#      with cores/options/.
#   2. PS2 and the separate emulators (Eden, RPCS3, xemu, Xenia, Cemu) take
#      settings files or flags, not libretro options. A renamed key is ignored
#      silently by most of them, and Xenia prints its help and exits on an
#      unknown flag. This reads the LAST launch of each from the console's
#      journal and the emulator's own log or rewritten settings file, and says
#      whether what was written was taken.
#
# So the procedure for a bump is: put the image on the A9, start one game on
# each emulator whose pin moved (from Home, or tools/ui-loop.sh --game N),
# then run this. It changes nothing on the console.
#
#   tools/check-applied.sh            both halves
#   tools/check-applied.sh --lists    only the Dolphin and FBNeo lists
#
set -uo pipefail

A9="${CABINETOS_A9:?set it to cabinet@<your console> (CLAUDE.local.example.md)}"
KEY="${CABINETOS_KEY:-$HOME/.ssh/cabinetos}"
SSH=(ssh -p 2222 -i "$KEY" -o ConnectTimeout=8 "$A9")
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ONLY_LISTS=0
[[ "${1:-}" == "--lists" ]] && ONLY_LISTS=1

fail=0
say() { printf '%s\n' "$*"; }
bad() { printf 'FAIL  %s\n' "$*"; fail=1; }
ok() { printf 'ok    %s\n' "$*"; }
note() { printf 'note  %s\n' "$*"; }

# --- 1. Dolphin and FBNeo, listed with real games ----------------------------
say "== option lists that need a real game"
rsync -az -e "ssh -p 2222 -i $KEY" "$ROOT/tools/core-options.c" "$ROOT/frontend/src/libretro.h" \
    "$A9:/tmp/check-applied/" >/dev/null 2>&1 || {
    "${SSH[@]}" 'mkdir -p /tmp/check-applied' &&
        rsync -az -e "ssh -p 2222 -i $KEY" "$ROOT/tools/core-options.c" \
            "$ROOT/frontend/src/libretro.h" "$A9:/tmp/check-applied/"
}
"${SSH[@]}" 'cd /tmp/check-applied && sed -i "s#../frontend/src/libretro.h#libretro.h#" core-options.c &&
    podman run --rm -v /tmp/check-applied:/w:Z -w /w localhost/cabinetos-builder \
        gcc -O2 -o core-options core-options.c -ldl' || bad "could not build core-options on the A9"

list_with_game() {   # <core file stem> <list name> <glob of a game in the cache> [<skip>]
    # <skip>: a pattern for the options that belong to one game rather than
    # the core (FBNeo's DIP switches, and its high scores, offered only for a
    # game that keeps them), left out on both sides.
    local so="$1" list="$2" glob="$3" skip="${4:-^$}" game now
    game=$("${SSH[@]}" "ls -d $glob 2>/dev/null | head -1")
    if [[ -z "$game" ]]; then
        note "$list: no game in the cache to list it with ($glob)"
        return
    fi
    # Only name<TAB>default<TAB>values lines, on both sides, as
    # cores/check-options.sh compares them: a core printing to stdout while it
    # loads is not an option (PCSX ReARMed's "Using 32-bit LUT").
    now=$("${SSH[@]}" "cd /tmp/check-applied && timeout 120 ./core-options \
        /usr/lib/cabinetos/cores/${so}_libretro.so --game \"$game\" 2>/dev/null | grep -P '\t'" |
        grep -Ev "$skip")
    if diff -u <(grep $'\t' "$ROOT/cores/options/$list.txt" | grep -Ev "$skip") <(printf '%s\n' "$now") \
        >/tmp/check-applied.diff; then
        ok "$list: $(printf '%s\n' "$now" | grep -c .) options, as listed"
    else
        bad "$list: its options differ from cores/options/$list.txt"
        cat /tmp/check-applied.diff
    fi
}
list_with_game dolphin dolphin '/var/lib/cabinetos/cache/"Nintendo Gamecube"/*/*.rvz'
# A Neo Geo game, since FBNeo declares its Neo Geo options only for one, and
# the console sets one of them (fbneo-neogeo-mode, coreoptions.cpp).
list_with_game fbneo fbneo_libretro '/var/lib/cabinetos/cache/NeoGeoMVS/*/*.zip' \
    $'^fbneo-(dipswitch-|debug-dip-|hiscores\t)'
# The listing runs with /tmp as the system folder; Dolphin and FBNeo leave
# folders there.
"${SSH[@]}" 'rm -rf /tmp/check-applied /tmp/User /tmp/fbneo /tmp/Mupen64plus' >/dev/null 2>&1

[[ $ONLY_LISTS -eq 1 ]] && exit $fail

# --- 2. What the last launch of each emulator took ---------------------------
say "== the last launch of each emulator"
# As `cabinet`, which reads the journal (it is in wheel on an installed
# console), the way tools/base-checks.sh reads it: no sudo, so a console that
# never had File access's password still gets its PS2 checks. -a: the
# journal holds a few binary lines, and grep would stop at "binary file".
J() { "${SSH[@]}" "journalctl -o cat --since '-14 days' 2>/dev/null | grep -aF -- \"$1\" | tail -1"; }
E=/var/lib/cabinetos/emulators

# PS2: our own line says what PCSX2 applied, read back from its config.
line=$(J "[ps2] renderer")
if [[ -z "$line" ]]; then note "PS2: no launch in the last two weeks"
elif [[ "$line" == *"renderer Vulkan"* && "$line" == *"anisotropy 16"* ]]; then ok "PS2: $line"
else bad "PS2: $line (expected Vulkan with anisotropy 16)"; fi
patches=$(J "game patches are active")
[[ -n "$patches" ]] && ok "PS2: $patches" || note "PS2: no game with a patch launched lately"
J "Failed to open patches.zip" | grep -q . && bad "PS2: patches.zip missing on a recent start"

# Eden prints every setting it loaded and marks the changed ones "M-". A key
# it no longer knows is never printed, so each one we write must appear.
log=$("${SSH[@]}" "cat $E/eden/user/log/eden_log.txt 2>/dev/null")
if [[ -z "$log" ]]; then note "Eden: no log"
else
    for k in resolution_setup max_anisotropy use_asynchronous_shaders use_vsync; do
        if grep -q "Renderer\.$k:" <<<"$log"; then ok "Eden: $(grep -m1 "Renderer\.$k:" <<<"$log")"
        else bad "Eden: Renderer.$k not in its log: renamed?"; fi
    done
fi

# RPCS3 dumps the configuration it used. Our Video lines must be in it with
# the values we wrote into config.yml.
cfg=$("${SSH[@]}" "sed -n '/^Video:/,/^[A-Z]/p' $E/rpcs3/rpcs3/config.yml 2>/dev/null")
logf=$("${SSH[@]}" "ls -t /var/lib/cabinetos/cache/'Sony Playstation 3'/*/rpcs3/RPCS3.log 2>/dev/null | head -1")
if [[ -z "$cfg" || -z "$logf" ]]; then note "RPCS3: no settings or log"
else
    used=$("${SSH[@]}" "cat \"$logf\"")
    while IFS= read -r l; do
        [[ "$l" =~ ^\ \ ([A-Za-z].*):\ (.*)$ ]] || continue
        if grep -qF "$l" <<<"$used"; then ok "RPCS3:$l"
        else bad "RPCS3: wrote '$l', its log does not show it: renamed?"; fi
    done <<<"$cfg"
fi

# xemu warns on a key it does not know and rewrites the file with only what
# differs from its defaults: the renderer must survive as VULKAN.
toml=$("${SSH[@]}" "cat $E/xemu/xemu.toml 2>/dev/null")
if [[ -z "$toml" ]]; then note "xemu: no settings file"
else
    grep -q "renderer = 'VULKAN'\|renderer = \"VULKAN\"" <<<"$toml" && ok "xemu: Vulkan" ||
        bad "xemu: renderer not VULKAN after its rewrite: it fell back to OpenGL"
    grep -q "surface_scale" <<<"$toml" && ok "xemu: $(grep surface_scale <<<"$toml")" ||
        note "xemu: no surface_scale after its rewrite (normal at Performance, where it is the default 1)"
fi

# Xenia prints its help and exits 0 on an unknown flag, before its log starts:
# an empty log after a launch is the sign.
xl=$("${SSH[@]}" "wc -c < $E/xenia/xenia.log 2>/dev/null")
if [[ -z "$xl" ]]; then note "Xenia: no log"
elif (( xl < 1000 )); then bad "Xenia: its log is ${xl} bytes: a flag it does not know?"
else ok "Xenia: log ${xl} bytes, started"; fi

# Cemu drops unknown keys when it rewrites settings.xml on exit; ours must
# still be there, with the values standalone_cemu.cpp writes.
cx=$("${SSH[@]}" "cat $E/cemu/Cemu/settings.xml 2>/dev/null")
if [[ -z "$cx" ]]; then note "Cemu: no settings"
else
    for k in '<api>1</api>' '<VSync>1</VSync>' '<AsyncCompile>true</AsyncCompile>'; do
        grep -qF "$k" <<<"$cx" && ok "Cemu: $k" || bad "Cemu: $k gone after its rewrite: renamed?"
    done
fi

say "== $([[ $fail -eq 0 ]] && echo 'all taken' || echo 'something was not taken: see FAIL lines')"
exit $fail
