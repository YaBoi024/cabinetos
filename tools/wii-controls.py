#!/usr/bin/env python3
"""Which controllers every Wii game accepts, from GameTDB, as the console reads it.

WHAT IT IS FOR. A Wii game plays on an ordinary pad here only when it accepts
a Classic Controller or a GameCube pad; every other Wii game needs a real Wii
Remote and is greyed out until one is paired. MMagTech, 2026-09-30;
docs/PROJECT.md open question 35, issues #173 and #200. Dolphin carries no such
list (its Sys/wiitdb-*.txt are titles only), so this is GameTDB's.

WHERE IT COMES FROM. GameTDB (https://www.gametdb.com), community-kept since
2009, "for anyone to use in any Wii-related project"; credited in
docs/LICENCES.md. THE FULL FILE, wiitdb.zip with no LANG: the English-filtered
one leaves out WiiWare, Virtual Console and GameCube entries.

WHAT IT WRITES, frontend/data/wii-controls.txt: one line per ID, the ID and
then one letter per controller GameTDB lists, required or optional alike,
except MotionPlus, whose two cases the console tells apart:

    w Wii Remote   n Nunchuk   c Classic Controller   g GameCube pad
    m MotionPlus, optional   M MotionPlus, required
    b Balance Board   h wheel   z Zapper

A game that requires MotionPlus plays only on a Remote whose MotionPlus can
work here: never Nintendo's own, whose MotionPlus the kernel's driver turns
off (MMagTech, 2026-10-22; docs/PROJECT.md question 35).

GameCube, mod (CUSTOM) and homebrew entries are left out, and an entry that
lists no controls at all is left out too: the console treats a
missing ID as needing a Wii Remote, which is the same answer.

Usage: tools/wii-controls.py [wiitdb.xml]   (downloads the full file without one)
"""

import io
import sys
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

LETTERS = {
    "wiimote": "w",
    "nunchuk": "n",
    "classiccontroller": "c",
    "gamecube": "g",
    "motionplus": "m",
    "balanceboard": "b",
    "wheel": "h",
    "zapper": "z",
}



def required(control):
    letter = LETTERS[control.get("type")]
    return "M" if letter == "m" and control.get("required") == "true" else letter


OUT = Path(__file__).resolve().parent.parent / "frontend" / "data" / "wii-controls.txt"


def load(argv):
    if len(argv) > 1:
        return Path(argv[1]).read_bytes()
    req = urllib.request.Request("https://www.gametdb.com/wiitdb.zip",
                                 headers={"User-Agent": "Mozilla/5.0 (CabinetOS tools/wii-controls.py)"})
    with urllib.request.urlopen(req, timeout=120) as r:
        z = zipfile.ZipFile(io.BytesIO(r.read()))
    return z.read("wiitdb.xml")


def main(argv):
    root = ET.fromstring(load(argv))
    version = root.find("WiiTDB").get("version")
    rows = {}
    for g in root.findall("game"):
        # Only games as released. GameTDB files fan-made mods (CUSTOM) and
        # homebrew under codes that share a real game's first four letters:
        # dozens of Mario Kart Wii mods are RMCP02 to RMCPYP, some with no
        # Classic Controller, and the console looks a disc up by those four
        # letters before it is downloaded.
        if g.findtext("type") in ("GameCube", "CUSTOM", "Homebrew"):
            continue
        gid = (g.findtext("id") or "").strip()
        inp = g.find("input")
        if not gid or inp is None:
            continue
        flags = sorted({required(c) for c in inp.findall("control")
                        if c.get("type") in LETTERS})
        if flags:
            rows[gid] = "".join(flags)
    with OUT.open("w") as f:
        f.write(f"# GameTDB wiitdb.xml version {version} (https://www.gametdb.com).\n")
        f.write("# Made by tools/wii-controls.py; regenerate rather than edit.\n")
        for gid in sorted(rows):
            f.write(f"{gid} {rows[gid]}\n")
    print(f"wrote {OUT} ({len(rows)} games, GameTDB {version})")


if __name__ == "__main__":
    main(sys.argv)
