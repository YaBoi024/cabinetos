#!/usr/bin/env python3
"""The full licence texts the image ships (#120).

Credits on the TV names each project and its licence in one line; the texts
themselves travel with the binaries as files, in /usr/share/licenses/cabinetos/
(MMagTech, 2026-09-25: no full texts on the television). GPL asks that every
copy handed on carries the licence, and the person most likely to hand this
image on never sees this repository.

licences/SOURCES lists each text: a file name, the upstream repository, the
exact commit or tag the image is built from, and the path of the licence file
at that revision. This fetches each one into licences/ from GitHub, at that
revision and no other, so the text is the one that applies to what ships.
Committed, so the image build needs no network for it; run again when a pin
moves. Each text's first line names where it came from, so a text added by
hand, or a pin moved without running this, shows: `--check` reads that line,
with no network, and ci/check-licences.sh runs it on every pull request.

    python3 tools/fetch-licences.py            fetch every text
    python3 tools/fetch-licences.py --check    check every text is there, fetched
                                               at the revision SOURCES names
"""

import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
DIR = os.path.join(HERE, "..", "licences")
SOURCES = os.path.join(DIR, "SOURCES")


def entries():
    out = []
    with open(SOURCES) as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 4:
                sys.exit(f"SOURCES line {n}: want 4 fields (name repo ref path), got {line!r}")
            out.append(parts)
    return out


def header(name, repo, ref, path):
    """A text's first line, as written here: where it came from."""
    if "#L" in path:
        path, rng = path.split("#L", 1)
        a, b = rng.split("-")
        path = f"{path}, lines {a} to {b}"
    return f"{name}: {path} from github.com/{repo} at {ref}\n"


def main():
    check = "--check" in sys.argv[1:]
    failed = 0
    names = set()
    for name, repo, ref, path in entries():
        if name in names:
            sys.exit(f"{name} is listed twice")
        names.add(name)
        dest = os.path.join(DIR, name + ".txt")
        first = header(name, repo, ref, path)
        if check:
            if not os.path.exists(dest) or os.path.getsize(dest) <= 100:
                print(f"  MISSING {name}")
                failed += 1
            else:
                with open(dest, encoding="utf-8", errors="replace") as f:
                    if f.readline() == first:
                        print(f"  ok   {name}")
                    else:
                        print(f"  NOT FETCHED AT {ref} {name}: run tools/fetch-licences.py")
                        failed += 1
            continue
        lines = None
        if "#L" in path:   # a range of lines: a licence that is only a header
            path, rng = path.split("#L", 1)
            a, b = rng.split("-")
            lines = (int(a), int(b))
        url = f"https://raw.githubusercontent.com/{repo}/{ref}/{path}"
        try:
            with urllib.request.urlopen(url, timeout=30) as r:
                body = r.read()
        except Exception as e:  # noqa: BLE001 - any failure is the answer
            print(f"  FAIL {name}: {url}: {e}")
            failed += 1
            continue
        if lines:
            body = b"\n".join(body.split(b"\n")[lines[0] - 1:lines[1]]) + b"\n"
        if len(body) < 100:
            print(f"  FAIL {name}: only {len(body)} bytes from {url}")
            failed += 1
            continue
        with open(dest, "wb") as f:
            f.write((first + "\n").encode() + body)
        print(f"  ok   {name} ({len(body)} bytes)")
    # A text nobody lists any more is a text for something that is not shipped.
    for f in os.listdir(DIR):
        if f.endswith(".txt") and f[:-4] not in names:
            print(f"  STALE {f}: not in SOURCES")
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
