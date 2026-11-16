#!/usr/bin/env python3
"""Refresh build_files/terra-fallback/ from terra (#270).

The image build installs Steam's session (gamescope-session and
gamescope-session-steam) from terra, and falls back to the copy in
build_files/terra-fallback/ when terra will not answer. This keeps that copy
current: it reads terra's package list straight from the repository (not
through its metalink, whose checksum list is what lagged on 2026-10-05),
downloads the newest of each package, checks each against terra's key, and
replaces the stored file when the version changed.

Run by .github/workflows/terra-fallback-update.yml on the 1st of each month,
and by hand:
    tools/terra-fallback-update.py
Prints `changed=true` or `changed=false` as its last line. Needs rpmkeys and
zstd. Exits non-zero, changing nothing, if anything does not verify.
"""

import gzip
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

BASE = "https://repos.fyralabs.com/terra44/"
PACKAGES = ["gamescope-session", "gamescope-session-steam"]
HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build_files",
                    "terra-fallback")
KEY = os.path.join(HERE, "RPM-GPG-KEY-terra44")


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def primary_xml():
    repomd = fetch(BASE + "repodata/repomd.xml").decode()
    m = re.search(r'<data type="primary">.*?<location href="([^"]+)"', repomd, re.S)
    if not m:
        sys.exit("terra's repomd.xml names no primary list")
    raw = fetch(BASE + m.group(1))
    if m.group(1).endswith(".zst"):
        return subprocess.run(["zstd", "-dc"], input=raw, check=True,
                              capture_output=True).stdout.decode()
    if m.group(1).endswith(".gz"):
        return gzip.decompress(raw).decode()
    return raw.decode()


def newest(xml, name):
    """The location of the newest noarch build of `name`, by its build time."""
    best, best_time = None, -1
    for m in re.finditer(r'<package type="rpm">(.*?)</package>', xml, re.S):
        p = m.group(1)
        if f"<name>{name}</name>" not in p or "<arch>noarch</arch>" not in p:
            continue
        t = re.search(r'<time file="(\d+)"', p)
        loc = re.search(r'<location href="([^"]+)"', p)
        if t and loc and int(t.group(1)) > best_time:
            best, best_time = loc.group(1), int(t.group(1))
    return best


def stored_name(href):
    # terra's file names carry the epoch ("-0:0~"); a colon is no friend of
    # every file system, so the stored name drops "0:".
    return href.split("/")[-1].replace("-0:", "-")


def main():
    xml = primary_xml()
    wanted = {}
    for name in PACKAGES:
        href = newest(xml, name)
        if not href:
            sys.exit(f"terra lists no {name}")
        wanted[name] = href
    have = sorted(f for f in os.listdir(HERE) if f.endswith(".rpm"))
    want = sorted(stored_name(h) for h in wanted.values())
    if have == want:
        print("changed=false")
        return
    with tempfile.TemporaryDirectory() as tmp:
        for href in wanted.values():
            with open(os.path.join(tmp, stored_name(href)), "wb") as f:
                f.write(fetch(BASE + href))
        db = os.path.join(tmp, "keys")
        os.mkdir(db)
        subprocess.run(["rpmkeys", "--dbpath", db, "--import", KEY], check=True)
        rpms = [os.path.join(tmp, n) for n in want]
        check = subprocess.run(["rpmkeys", "--dbpath", db, "-K", *rpms],
                               capture_output=True, text=True)
        print(check.stdout.strip())
        if check.returncode != 0 or "signatures OK" not in check.stdout:
            sys.exit("a downloaded package does not verify against terra's key; nothing changed")
        # The new files in first, then the old ones out, so a failure part way
        # leaves a copy that still installs.
        for n in want:
            shutil.copyfile(os.path.join(tmp, n), os.path.join(HERE, n))
        for old in have:
            if old not in want:
                os.remove(os.path.join(HERE, old))
    print("now: " + ", ".join(want))
    print("changed=true")


if __name__ == "__main__":
    main()
