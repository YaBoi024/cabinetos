#!/usr/bin/env bash
#
# Stands up the overlay experiment on the reference console's own television
# and then asks you to LOOK AT IT.
#
# THE QUESTION. PCSX2 and every other emulator that is not a libretro core makes
# its own graphics device and presents its own picture. CabinetOS copies that
# picture off the GPU and back on so it can draw the pause menu over it — 6.1 ms
# of a 16.7 ms frame at 4x. If gamescope will composite our menu on top of a
# window the emulator owns, none of that copying is needed. This script is how
# that was answered on 2026-09-21. The answer is yes; see docs/PROJECT.md, open
# question 24.
#
# IT TAKES OVER THE TELEVISION for as long as it runs, by pointing the session's
# CABINETOS_APP at a test program instead of the frontend. It puts the machine
# back on every exit path, including failure and Ctrl-C.
#
# WHY THERE IS NO CAPTURE IN HERE. `gamescopectl screenshot` does not capture
# either overlay plane — not with type 1, and not with type 2 "all_real_layers".
# A capture taken with a working overlay on screen comes back showing only the
# game. Most of a session went into chasing an overlay that was on the
# television the whole time. If you want a record, photograph the screen.
#
# Usage, on the A9 itself:
#   tools/gamescope-overlay-test.sh external   the HUD slot: composites, no input
#   tools/gamescope-overlay-test.sh steam      the Steam slot: composites AND takes the pad
#   tools/gamescope-overlay-test.sh gears      glxgears over vkcube, the zero-code version
set -uo pipefail

MODE="${1:-steam}"
DROPIN=/etc/systemd/system/cabinetos-session.service.d/90-overlay-test.conf
HERE="$(cd "$(dirname "$0")" && pwd)"
# A directory of its own: podman refuses to relabel /tmp itself (`SELinux
# relabeling of /tmp is not allowed`), so the build cannot bind-mount it.
WORK="${CABINETOS_OVERLAY_WORK:-$HOME/.cache/cabinetos-overlay-test}"
PROBE="$WORK/overlay-probe"
SUDO() { echo "${CABINETOS_SUDO_PW:-cabinet}" | sudo -S "$@" 2>/dev/null; }

restore() {
    echo
    echo "putting the console back..."
    pkill -x overlay-probe 2>/dev/null
    pkill -x glxgears 2>/dev/null
    SUDO rm -f "$DROPIN"
    SUDO systemctl daemon-reload
    SUDO systemctl restart cabinetos-session
    sleep 8
    echo "session: $(systemctl is-active cabinetos-session)"
    ps -eo args | grep '[c]abinetos-frontend' | head -1
}
trap restore EXIT INT TERM

# --- the probe -------------------------------------------------------------
# The console has no X11 headers. The PCSX2 builder container does, and a binary
# built in it runs on the host unchanged.
if [[ "$MODE" != "gears" ]]; then
    echo "building the probe..."
    mkdir -p "$WORK"
    cp "$HERE/overlay-probe.c" "$WORK/overlay-probe.c"
    podman run --rm -v "$WORK":/w:Z -w /w localhost/cabinetos-pcsx2-builder \
        bash -c 'gcc -O2 -Wall -o overlay-probe overlay-probe.c $(pkg-config --cflags --libs x11)' \
        || { echo "probe did not build"; exit 1; }
fi

# --- put a game on the television -----------------------------------------
# vkcube stands in for PCSX2: it makes its own Vulkan device and presents its
# own swapchain, which is the whole shape of the problem. A libretro core does
# NOT do this and does not need any of it.
mkdir -p "$WORK"
printf '[Service]\nEnvironment="CABINETOS_APP=/usr/bin/vkcube"\n' > "$WORK/90-overlay-test.conf"
SUDO cp "$WORK/90-overlay-test.conf" "$DROPIN"
SUDO systemctl daemon-reload
SUDO systemctl restart cabinetos-session
sleep 12

VP=$(pgrep -x vkcube | head -1)
[[ -z "$VP" ]] && { echo "the test game never started"; exit 1; }
XD=$(tr '\0' '\n' < "/proc/$VP/environ" | grep '^DISPLAY=' | cut -d= -f2)
GSD=$(tr '\0' '\n' < "/proc/$VP/environ" | grep '^GAMESCOPE_WAYLAND_DISPLAY=' | cut -d= -f2)
# The output's real size: the overlay is painted NoScale, at its own pixel size.
read -r OW OH < <(ps -eo args \
                  | grep -oE -- '--output-width [0-9]+ --output-height [0-9]+' \
                  | head -1 | awk '{print $2, $4}')
OW=${OW:-1920}; OH=${OH:-1080}
echo "game on $XD via $GSD, output ${OW}x${OH}"

GAME_WID=$(DISPLAY=$XD xwininfo -root -children 2>/dev/null \
           | grep -i vkcube | grep -oE '0x[0-9a-f]+' | head -1)

# --- the overlay -----------------------------------------------------------
case "$MODE" in
  gears)
    DISPLAY=$XD glxgears >"$WORK/overlay-gears.log" 2>&1 &
    sleep 6
    OW_ID=$(DISPLAY=$XD xwininfo -root -children 2>/dev/null \
            | grep -i gears | grep -oE '0x[0-9a-f]+' | head -1)
    DISPLAY=$XD xprop -id "$OW_ID" -f GAMESCOPE_EXTERNAL_OVERLAY 32c -set GAMESCOPE_EXTERNAL_OVERLAY 1
    DISPLAY=$XD xprop -id "$OW_ID" -f GAMESCOPE_NO_FOCUS 32c -set GAMESCOPE_NO_FOCUS 1
    # THE ATOMS WERE SET AFTER THE WINDOW MAPPED, so nothing has re-examined it.
    # Poking opacity forces the rescan: handle_property_notify's opacity branch
    # walks ctx->list and reassigns focus.externalOverlayWindow unconditionally.
    DISPLAY=$XD xprop -id "$OW_ID" -f _NET_WM_WINDOW_OPACITY 32c -set _NET_WM_WINDOW_OPACITY 4294967295
    EXPECT="spinning gears in the TOP LEFT, over the cube — small, because glxgears' window is 300x300 and the overlay is painted unscaled"
    ;;
  external)
    DISPLAY=$XD "$PROBE" "$OW" "$OH" >"$WORK/overlay-probe.log" 2>&1 &
    sleep 6
    OW_ID=$(DISPLAY=$XD xwininfo -root -children 2>/dev/null \
            | grep -i 'overlay probe' | grep -oE '0x[0-9a-f]+' | head -1)
    EXPECT="a magenta bar across the top with a white tick sliding along it, and a green band across the middle WITH THE CUBE VISIBLE THROUGH IT"
    ;;
  steam|*)
    DISPLAY=$XD "$PROBE" "$OW" "$OH" --steam >"$WORK/overlay-probe.log" 2>&1 &
    sleep 6
    OW_ID=$(DISPLAY=$XD xwininfo -root -children 2>/dev/null \
            | grep -i 'overlay probe' | grep -oE '0x[0-9a-f]+' | head -1)
    EXPECT="the same magenta bar and see-through green band — AND the game keeps the screen while the overlay holds the pad"
    ;;
esac
sleep 3

echo
echo "game window    $GAME_WID"
echo "overlay window $OW_ID"
DISPLAY=$XD xprop -id "$OW_ID" 2>/dev/null | grep -iE 'GAMESCOPE|STEAM|OPACITY'

# --- what gamescope thinks -------------------------------------------------
# The one thing here that IS machine-checkable. In the steam slot the game stays
# the focus window (so it keeps presenting) while input and keyboard focus move
# to the overlay — which is exactly a pause menu.
echo
echo "=== gamescope's own view of focus ==="
GAMESCOPE_WAYLAND_DISPLAY=$GSD gamescopectl focus_info >/dev/null 2>&1
sleep 2
journalctl -t cabinetos-session --since '-20 sec' \
    | grep -iE 'Global (focus|input focus|keyboard focus|overlay) window' | tail -5

echo
echo "=== NOW LOOK AT THE TELEVISION ==="
echo "expect: $EXPECT"
echo
echo "Nothing below this line can tell you whether it worked. Screenshots do not"
echo "capture the overlay planes. Press Enter when you have looked."
read -r _
