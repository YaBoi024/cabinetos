#!/usr/bin/env bash
# FORK (handheld): edit on the Mac, build and run on the Deck, with no GitHub
# round trip. Based on upstream's tools/ui-loop.sh, cut down to what a Deck
# needs: no capture, no cores folder, no sound check.
#
# Run it from the repository folder on the Mac (the one with `frontend/` in it).
#
#   export CABINETOS_DECK=cabinet@<the Deck's address>   (once, see below)
#   tools/deck-loop.sh --setup      the first time only: builds the build container on the Deck
#   tools/deck-loop.sh              copy the code across, build it on the Deck, run it
#   tools/deck-loop.sh --restore    put the Deck back on the installed image
#
# BEFORE THE FIRST RUN
#   - The Deck: Settings, About, press Version seven times, then turn on Developer access.
#     That starts SSH on port 2222 and shows the password on the screen.
#   - The Mac: put the address in your shell's start-up file, for example
#       echo 'export CABINETOS_DECK=cabinet@192.168.1.213' >> ~/.zshrc
#     and open a new Terminal window. (`ip addr` on the Deck shows its address.)
#
# THE DECK RUNS YOUR BUILD UNTIL IT RESTARTS. The session is pointed at the new
# program by a file in /run, which is wiped at every boot, so a reboot always
# puts the Deck back on the installed image. --restore does the same without one.
#
# The password is asked once per run: the connection is shared between the steps.
set -uo pipefail

DECK=${CABINETOS_DECK:?set CABINETOS_DECK to cabinet@<the Deck address> (see the top of this file)}
PORT=${CABINETOS_DECK_PORT:-2222}
ROOT=${CABINETOS_REPO:-"$(cd "$(dirname "$0")/.." && pwd)"}
SOCK="/tmp/deck-loop-%C"

SSHOPT=(-p "$PORT" -o ConnectTimeout=20 -o StrictHostKeyChecking=accept-new
        -o ControlMaster=auto -o ControlPath="$SOCK" -o ControlPersist=15m)
KEYOPT=()
[ -n "${CABINETOS_KEY:-}" ] && KEYOPT=(-i "$CABINETOS_KEY")

remote() { ssh ${KEYOPT[@]+"${KEYOPT[@]}"} "${SSHOPT[@]}" "$DECK" "$@"; }
RSH="ssh ${CABINETOS_KEY:+-i $CABINETOS_KEY} -p $PORT -o ConnectTimeout=20 -o StrictHostKeyChecking=accept-new -o ControlMaster=auto -o ControlPath=$SOCK -o ControlPersist=15m"

# sudo's password is `cabinet`'s: the one File access set if it was ever on,
# otherwise the throwaway "cabinet" (as upstream's ui-loop.sh does).
SUDO='{ cat /var/lib/cabinetos-files/password 2>/dev/null || echo cabinet; } | sudo -S'
DROPIN_DIR=/run/systemd/system/cabinetos-session.service.d
DROPIN=$DROPIN_DIR/50-deck-loop.conf
DEV=/var/home/cabinet/cabinetos-frontend-dev

say() { printf '\n== %s\n' "$*"; }

if [ ! -d "$ROOT/frontend/src" ]; then
    echo "no frontend/src in $ROOT: run this from the repository folder" >&2
    exit 1
fi

# The code, copied across. Compared by content and not by time, so a file that
# did not change is left alone and the Deck compiles only what you edited.
sync_code() {
    rsync -rlzc --delete --exclude build/ --exclude '*.o' --exclude '*.d' \
        -e "$RSH" "$ROOT/frontend/" "$DECK:frontend/"
}

case "${1:-}" in
    --restore)
        say "putting the Deck back on the installed image"
        remote "$SUDO rm -f $DROPIN >/dev/null 2>&1
                $SUDO systemctl daemon-reload >/dev/null 2>&1
                $SUDO systemctl restart cabinetos-session >/dev/null 2>&1
                sleep 12
                echo \"session: \$(systemctl is-active cabinetos-session)\""
        exit 0
        ;;
    --setup)
        say "copying the code to the Deck"
        sync_code || exit 1
        say "building the build container on the Deck (a few minutes, once)"
        remote 'cd ~/frontend && podman build -t cabinetos-builder -f Containerfile .' || exit 1
        say "done: now run tools/deck-loop.sh"
        exit 0
        ;;
    "") ;;
    *) echo "unknown argument: $1 (use --setup, --restore, or nothing)" >&2; exit 1 ;;
esac

say "copying the code to the Deck"
sync_code || exit 1

# The old program goes first: a compile that fails must not leave the last
# good one to be run as if it were this one.
say "building on the Deck"
remote 'cd ~/frontend && rm -f build/cabinetos-frontend &&
        podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make -j8 >/tmp/deck-loop-build.log 2>&1'
if ! remote 'test -f ~/frontend/build/cabinetos-frontend'; then
    echo
    echo "THE BUILD FAILED. Nothing was changed on the Deck. The errors:"
    remote 'grep -n -B1 -A6 -E "error|undefined reference" /tmp/deck-loop-build.log | head -70'
    exit 1
fi

say "running it on the Deck"
remote "$SUDO systemctl stop cabinetos-session >/dev/null 2>&1"
remote "cp ~/frontend/build/cabinetos-frontend $DEV && chmod +x $DEV" || exit 1
remote "printf '[Service]\nEnvironment=\"CABINETOS_APP=$DEV\"\n' > /tmp/50-deck-loop.conf
        $SUDO mkdir -p $DROPIN_DIR >/dev/null 2>&1
        $SUDO cp /tmp/50-deck-loop.conf $DROPIN >/dev/null 2>&1
        $SUDO systemctl daemon-reload >/dev/null 2>&1
        $SUDO systemctl start cabinetos-session >/dev/null 2>&1"

cat <<'NOTE'

   It is starting on the Deck now (give it about 15 seconds).

   The Deck is running YOUR build until it restarts, or until you run:
       tools/deck-loop.sh --restore
NOTE
