#!/usr/bin/env python3
"""Refresh the snapshot of RPCS3's per-game settings database.

RPCS3 keeps a settings database, one entry per game by its serial (Write
Color Buffers for Lollipop Chainsaw, BLUS30917), which only its own window
downloads, from https://api.rpcs3.net/config/?api=v1, and saves as
GuiConfigs/config_database.dat; at every boot, `--no-gui` included, RPCS3
lays the game's entry over its global settings (rpcs3qt/config_database.cpp,
Emu/System.cpp, "Applying database config"). The console starts RPCS3
without its window and connects only to RomM and GitHub, so it never had
that file and no PlayStation 3 game got its entry. This fetches the file, on
a Mac or any computer, into build_files/rpcs3-config-database.json, byte for
byte what RPCS3's window would have saved; build_files/install-rpcs3.sh puts
it beside RPCS3 in the image and frontend/src/standalone_rpcs3.cpp copies it
into RPCS3's config folder before every start.
docs/decisions/2026-11-06-rpcs3-settings-database-snapshot.md is the decision.

    tools/fetch-rpcs3-config-database.py

Run it when RPCS3's pin moves, in the same pull request, after the pin has
moved, and commit what it changed (CONTRIBUTING.md, "Duties that come with
some changes"). Not between pin moves: the database follows the current
RPCS3, so a newer snapshot on the same build only drifts from what that
build needs. It prints the games added, removed and changed against the
committed file, and the day the RPCS3 team's server made the file. The file
is kept only when it is what RPCS3 itself requires of it (read_json): JSON,
an object, `return_code` not negative, `games` an object, every game a
`config` string. An entry the pinned build cannot read RPCS3 rejects
itself, whole, at every start; the pull request counts those from a
console's RPCS3.log, as the duty says.

WHICH PIN IT WAS TAKEN AGAINST goes beside it, in
build_files/rpcs3-config-database.pin: one line, the RPCS3_VERSION of
build_files/install-rpcs3.sh at the time, then the day of the server's file.
install-rpcs3.sh refuses to build an image whose pin is not that one, and
ci/check-rpcs3-snapshot.sh fails a pull request the same way, so a pin move
without this never rests on anyone remembering it.
"""

import datetime
import json
import os
import re
import sys
import urllib.request

URL = "https://api.rpcs3.net/config/?api=v1"
HERE = os.path.dirname(os.path.abspath(__file__))
BUILD_FILES = os.path.join(HERE, "..", "build_files")
SNAPSHOT = os.path.join(BUILD_FILES, "rpcs3-config-database.json")
PIN_FILE = os.path.join(BUILD_FILES, "rpcs3-config-database.pin")
INSTALL = os.path.join(BUILD_FILES, "install-rpcs3.sh")


def games_of(data, where):
    """The `games` object, checked the way RPCS3's read_json checks it."""
    try:
        parsed = json.loads(data)
    except ValueError as e:
        sys.exit(f"{where}: not JSON: {e}")
    if not isinstance(parsed, dict):
        sys.exit(f"{where}: not a JSON object")
    code = parsed.get("return_code", -255)
    if not isinstance(code, int) or code < 0:
        sys.exit(f"{where}: return_code {code!r}: RPCS3 would refuse it")
    games = parsed.get("games")
    if not isinstance(games, dict) or not games:
        sys.exit(f"{where}: no games object")
    for serial, game in games.items():
        if not isinstance(game, dict) or not isinstance(game.get("config"), str):
            sys.exit(f"{where}: {serial} has no config string")
    return parsed.get("timestamp"), games


def pinned_version():
    """RPCS3_VERSION as build_files/install-rpcs3.sh pins it."""
    with open(INSTALL) as f:
        m = re.search(r'^readonly RPCS3_VERSION="([^"]+)"', f.read(), re.M)
    if not m:
        sys.exit(f"{os.path.relpath(INSTALL)}: no readonly RPCS3_VERSION line")
    return m.group(1)


def main():
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    request = urllib.request.Request(URL, headers={"User-Agent": "CabinetOS fetch-rpcs3-config-database"})
    with urllib.request.urlopen(request, timeout=60) as r:
        data = r.read()
    stamp, new = games_of(data, URL)
    made = ""
    if isinstance(stamp, int):
        made = datetime.datetime.fromtimestamp(stamp, datetime.timezone.utc).strftime("%Y-%m-%d")

    old = {}
    had = None
    if os.path.exists(SNAPSHOT):
        with open(SNAPSHOT, "rb") as f:
            had = f.read()
        if had != data:
            _, old = games_of(had, SNAPSHOT)

    pin = pinned_version()
    pin_line = f"{pin} {made}\n".replace(" \n", "\n")
    had_pin = ""
    if os.path.exists(PIN_FILE):
        with open(PIN_FILE) as f:
            had_pin = f.read()
    with open(PIN_FILE, "w") as f:
        f.write(pin_line)

    if had == data:
        print(f"unchanged: {len(new)} games, as committed" + (f", made {made}" if made else ""))
        if had_pin != pin_line:
            print(f"  now recorded as taken against RPCS3 {pin} ({os.path.relpath(PIN_FILE)})")
        return

    with open(SNAPSHOT, "wb") as f:
        f.write(data)

    added = sorted(set(new) - set(old))
    removed = sorted(set(old) - set(new))
    changed = sorted(s for s in set(new) & set(old) if new[s]["config"] != old[s]["config"])
    print(f"wrote {os.path.relpath(SNAPSHOT)}: {len(new)} games" + (f", made {made}" if made else "")
          + f", taken against RPCS3 {pin}")
    for what, serials in (("added", added), ("removed", removed), ("changed", changed)):
        if serials:
            print(f"  {what} {len(serials)}: {' '.join(serials[:20])}{' ...' if len(serials) > 20 else ''}")
    if not old:
        print("  (no committed file to compare with)")


if __name__ == "__main__":
    main()
