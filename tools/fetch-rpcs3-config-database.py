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

Run it with each move of RPCS3's pin and with the monthly base update
(CONTRIBUTING.md, "Duties that come with some changes") and commit what it
changed: it prints the games added, removed and changed against the
committed file, and the day the RPCS3 team's server made the file. The file
is kept only when it is what RPCS3 itself requires of it (read_json): JSON,
an object, `return_code` not negative, `games` an object, every game a
`config` string. An entry the pinned build cannot read RPCS3 rejects
itself, whole, at every start; the pull request counts those from a
console's RPCS3.log, as the duty says.
"""

import datetime
import json
import os
import sys
import urllib.request

URL = "https://api.rpcs3.net/config/?api=v1"
HERE = os.path.dirname(os.path.abspath(__file__))
SNAPSHOT = os.path.join(HERE, "..", "build_files", "rpcs3-config-database.json")


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


def main():
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    request = urllib.request.Request(URL, headers={"User-Agent": "CabinetOS fetch-rpcs3-config-database"})
    with urllib.request.urlopen(request, timeout=60) as r:
        data = r.read()
    stamp, new = games_of(data, URL)

    old = {}
    if os.path.exists(SNAPSHOT):
        with open(SNAPSHOT, "rb") as f:
            had = f.read()
        if had == data:
            print(f"unchanged: {len(new)} games, as committed")
            return
        _, old = games_of(had, SNAPSHOT)

    with open(SNAPSHOT, "wb") as f:
        f.write(data)

    added = sorted(set(new) - set(old))
    removed = sorted(set(old) - set(new))
    changed = sorted(s for s in set(new) & set(old) if new[s]["config"] != old[s]["config"])
    made = ""
    if isinstance(stamp, int):
        made = ", made " + datetime.datetime.fromtimestamp(stamp, datetime.timezone.utc).strftime("%Y-%m-%d")
    print(f"wrote {os.path.relpath(SNAPSHOT)}: {len(new)} games{made}")
    for what, serials in (("added", added), ("removed", removed), ("changed", changed)):
        if serials:
            print(f"  {what} {len(serials)}: {' '.join(serials[:20])}{' ...' if len(serials) > 20 else ''}")
    if not old:
        print("  (no committed file to compare with)")


if __name__ == "__main__":
    main()
