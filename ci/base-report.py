#!/usr/bin/env python3
"""
What a Bazzite base update changes for CabinetOS, and what to check for it.

Reads two package manifests (`NAME VERSION-RELEASE` per line, as
ci/check-base-update.sh writes them) and ci/base-watch.txt, and sorts every
change to a watched package into the AREA it belongs to. From that it writes
the parts of the base-update pull request a person reads (MMagTech,
2026-10-09):

  * update notes, written so they also serve as the public notes for the
    update: one line per area that changed, in plain words;
  * the checks for those areas only, split into what the Deck's log answers
    and what needs a person by hand, each listed once;
  * the kernel's fixes for the parts the console uses, from kernel.org's
    stable changelogs and the OGC kernel's own patches, filtered by the
    watch list's changelog: lines;
  * the watched changes themselves, by area, for us.

  base-report.py report OLD NEW --md OUT.md --json OUT.json --notes NOTES.md
  base-report.py checks OLD NEW       tab-separated, for tools/base-checks.sh:
                                      area<TAB>name, log<TAB>check<TAB>command,
                                      tv<TAB>check
  base-report.py expect OLD NEW       NAME VERSION-RELEASE of every watched
                                      package the update brings, which the Deck
                                      must then have (not those CabinetOS
                                      removes)

`report` reads the changelogs over the network (GH_TOKEN, when set, for the
OGC kernel's tags on GitHub); one it cannot read is said so in the body, and
never fails the run. `checks` needs no network.
"""

import fnmatch
import json
import os
import re
import sys
import urllib.error
import urllib.request

WATCH = "ci/base-watch.txt"
KEYS = ("area", "log", "tv", "changelog", "changelog-skip", "series", "kind")


# --- The watch list -----------------------------------------------------------

class Area:
    def __init__(self, name):
        self.name = name
        self.patterns = []
        self.log = []          # (check, command)
        self.tv = []           # check
        self.stripped = False
        # What changed, filled by classify().
        self.updated = []      # (name, old, new)
        self.added = []        # (name, version)
        self.removed = []      # (name, version)
        self.replaced = []     # (old name, old version, new name, new version)

    def changed(self):
        return bool(self.updated or self.added or self.removed or self.replaced)

    def count(self):
        return (len(self.updated) + len(self.added) + len(self.removed)
                + len(self.replaced))


def read_watch(path=WATCH):
    """(areas, parts, skips, series)."""
    areas, parts, skips, series = [], [], [], []
    area = None
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            key, sep, value = line.partition(":")
            if sep and key in KEYS:
                value = value.strip()
                if key == "area":
                    area = Area(value)
                    areas.append(area)
                    continue
                if area is None:
                    sys.exit(f"{path}:{n}: '{key}:' before any 'area:'")
                if key == "log":
                    check, sep2, command = value.partition(" :: ")
                    if not sep2:
                        sys.exit(f"{path}:{n}: log: needs '<check> :: <command>'")
                    area.log.append((check.strip(), command.strip()))
                elif key == "tv":
                    area.tv.append(value)
                elif key == "changelog":
                    part, sep2, rx = value.partition(" :: ")
                    if not sep2:
                        sys.exit(f"{path}:{n}: changelog: needs '<part> :: <regex>'")
                    parts.append((part.strip(), re.compile(rx.strip())))
                elif key == "changelog-skip":
                    skips.append(re.compile(value))
                elif key == "series":
                    part, sep2, paths = value.partition(" :: ")
                    if not sep2 or not paths.split():
                        sys.exit(f"{path}:{n}: series: needs '<part> :: <path> ...'")
                    series.append((part.strip(), paths.split()))
                elif key == "kind":
                    if value != "stripped":
                        sys.exit(f"{path}:{n}: unknown kind '{value}'")
                    area.stripped = True
                continue
            if area is None:
                sys.exit(f"{path}:{n}: package pattern before any 'area:'")
            area.patterns.append(line)
    return areas, parts, skips, series


def area_of(name, areas):
    """The first area with a pattern matching NAME, or its Terra build's
    name without the prefix; with the pattern's position, for ordering."""
    names = [name]
    if name.startswith("terra-"):
        names.append(name[len("terra-"):])
    for area in areas:
        for i, pattern in enumerate(area.patterns):
            if any(fnmatch.fnmatchcase(n, pattern) for n in names):
                return area, i
    return None, None


# --- The manifests ------------------------------------------------------------

def read_manifest(path):
    packages = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                packages[parts[0]] = parts[1]
    return packages


def classify(old, new, areas):
    order = {}

    def place(name):
        area, i = area_of(name, areas)
        if area is not None:
            order[name] = i
        return area

    added = sorted(set(new) - set(old))
    removed = sorted(set(old) - set(new))
    # A package Terra now builds in Fedora's place: the same thing, moved.
    moved = {n for n in removed if "terra-" + n in added}
    for name in sorted(moved):
        area = place(name)
        if area:
            area.replaced.append((name, old[name], "terra-" + name, new["terra-" + name]))
    for name in added:
        if name.startswith("terra-") and name[len("terra-"):] in moved:
            continue
        area = place(name)
        if area:
            area.added.append((name, new[name]))
    for name in removed:
        if name in moved:
            continue
        area = place(name)
        if area:
            area.removed.append((name, old[name]))
    for name in sorted(set(old) & set(new)):
        if old[name] == new[name]:
            continue
        area = place(name)
        if area and not area.stripped:
            area.updated.append((name, old[name], new[name]))
    for area in areas:
        area.updated.sort(key=lambda u: (order[u[0]], u[0]))
    return [a for a in areas if a.changed()]


# --- Plain versions, for the notes --------------------------------------------

def plain(version):
    """The version without its release: 7.2.7-ogc1.1.fc44 -> 7.2.7. shown()
    keeps the release when only the release moved (steam 1.0.0.87-2 -> -5)."""
    return version.split("-", 1)[0]


def release(version):
    rel = version.split("-", 1)[1] if "-" in version else ""
    return re.sub(r"\.fc\d+.*$", "", rel)


def shown(old, new):
    if plain(old) != plain(new):
        return plain(old), plain(new)
    return f"{plain(old)}-{release(old)}", f"{plain(new)}-{release(new)}"


def note_for(area):
    """One plain line for the update notes."""
    bits = []
    # Updates with the same version change are one thing (kernel, kernel-core,
    # kernel-modules...): name the shortest, and count the unrelated ones.
    groups = {}
    for name, old, new in area.updated:
        groups.setdefault(shown(old, new), []).append(name)
    for (old, new), names in groups.items():
        # mesa-libGL, mesa-dri-drivers... are "mesa"; linux-firmware and
        # amd-gpu-firmware share no name, so the shortest leads.
        common = os.path.commonprefix([n.split("-") for n in names])
        if len(names) > 1 and common:
            lead, more = "-".join(common), ""
        else:
            lead = min(names, key=len)
            others = [n for n in names if not n.startswith(lead)]
            more = f" and {len(others)} more" if others else ""
        bits.append(f"{lead}{more} {old} → {new}")
    moved = {}
    for old_name, old_v, _, new_v in area.replaced:
        moved.setdefault(shown(old_v, new_v), []).append(old_name)
    for (old, new), names in moved.items():
        bits.append(f"{min(names, key=len)} {old} → {new}, now Terra's build")
    if area.added:
        bits.append("added " + ", ".join(n for n, _ in area.added))
    if area.removed:
        bits.append("removed " + ", ".join(n for n, _ in area.removed))
    return "; ".join(bits)


# --- The kernel's changelogs --------------------------------------------------

def fetch(url, token=None):
    headers = {"User-Agent": "cabinetos-base-report"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode("utf-8", "replace")


def kernel_version(full):
    """7.2.7-ogc1.1.fc44 -> ((7, 2, 7), 'v7.2.7-ogc1'); a Fedora kernel has
    no OGC tag."""
    up, _, rel = full.partition("-")
    nums = tuple(int(x) for x in re.findall(r"\d+", up)[:3])
    while len(nums) < 3:
        nums += (0,)
    m = re.match(r"ogc(\d+)", rel)
    tag = f"v{up}-ogc{m.group(1)}" if m else None
    return nums, tag


def stable_releases(old, new):
    """Every stable release after OLD up to NEW. Across a series (7.2 to 7.3)
    only the new series' point releases: its first release is the whole merge
    window, thousands of changes, which kernelnewbies.org summarises."""
    (oa, ob, oc), (na, nb, nc) = old, new
    if (oa, ob) == (na, nb):
        return [(na, nb, c) for c in range(oc + 1, nc + 1)]
    return [(na, nb, c) for c in range(1, nc + 1)]


def changelog_subjects(text):
    """The subject of each commit in a kernel.org ChangeLog (git log format)."""
    subjects = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        if lines[i].startswith("commit "):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("Date:"):
                j += 1
            j += 1
            while j < len(lines) and not lines[j].strip():
                j += 1
            if j < len(lines):
                subjects.append(lines[j].strip())
            i = j
        i += 1
    return subjects


def part_of(subject, parts, skips):
    s = re.sub(r"^\[[A-Z-]+\]\s*", "", subject)       # OGC's [FROM-ML] etc.
    m = re.match(r'^Revert "(.*)"$', s)
    if m:
        s = re.sub(r"^\[[A-Z-]+\]\s*", "", m.group(1))
    if any(rx.search(s) for rx in skips):
        return None
    for name, rx in parts:
        if rx.search(s):
            return name
    return None


def ogc_patches(tag, upstream, token):
    """The OGC kernel's own commits on TAG: everything above the upstream
    "Linux x.y.z" commit, merges left out. GitHub lists them newest first, and
    OGC's rebased patches are newer than the stable release they sit on."""
    subjects = []
    for page in range(1, 11):
        url = (f"https://api.github.com/repos/OpenGamingCollective/linux/commits"
               f"?sha={tag}&per_page=100&page={page}")
        commits = json.loads(fetch(url, token))
        if not commits:
            break
        for c in commits:
            subject = c["commit"]["message"].split("\n", 1)[0].strip()
            if subject == f"Linux {upstream}":
                return subjects
            # Merges, and a patch carried twice, are one change or none.
            if (len(c.get("parents", [])) == 1 and not subject.startswith("Merge ")
                    and subject not in subjects):
                subjects.append(subject)
    raise RuntimeError(f"no 'Linux {upstream}' commit within 1000 of {tag}")


def series_commits(path, old_series, new_series, token):
    """The subjects of the commits to PATH that Linux NEW_SERIES has and
    OLD_SERIES does not (torvalds/linux on GitHub, merges left out). Each
    tag's latest hundred for the file: far more than one series adds to one
    driver."""
    def history(tag):
        url = (f"https://api.github.com/repos/torvalds/linux/commits"
               f"?sha={tag}&path={path}&per_page=100")
        return [(c["sha"], c["commit"]["message"].split("\n", 1)[0].strip())
                for c in json.loads(fetch(url, token))]
    had = {sha for sha, _ in history(f"v{old_series}")}
    return [s for sha, s in history(f"v{new_series}")
            if sha not in had and not s.startswith("Merge ")]


def kernel_report(old_full, new_full, parts, skips, series, token):
    """Markdown for the kernel's fixes, and counts by part for the notes."""
    old, old_tag = kernel_version(old_full)
    new, new_tag = kernel_version(new_full)
    dotted = lambda v: ".".join(str(x) for x in v)
    out, counts, problems = [], {}, []
    by_part = {}
    upstream_subjects = set()

    if new <= old:
        out.append(f"The kernel's upstream version did not move ({dotted(old)}); "
                   "only Bazzite's build of it did.")
    else:
        if new[:2] != old[:2]:
            out.append(f"**A new kernel series, {new[0]}.{new[1]}.** Its new features are "
                       f"summarised at https://kernelnewbies.org/Linux_{new[0]}.{new[1]}; "
                       "below are its stable fixes, and the whole series' changes only "
                       "for the files the `series:` lines name.")
            # NEW HARDWARE ARRIVES IN A SERIES, NOT IN ITS FIXES. Support for
            # the 2026 Steam Controller went into Linux on 2026-08-14 as
            # "HID: steam: Initial 2026 Steam Controller support", and would
            # have reached the console in a series jump with no line in these
            # notes: the stable changelogs carry fixes only (#318).
            os_, ns = f"{old[0]}.{old[1]}", f"{new[0]}.{new[1]}"
            for part, paths in series:
                lines = []
                for p in paths:
                    try:
                        subs = series_commits(p, os_, ns, token)
                    except (urllib.error.URLError, OSError, ValueError, KeyError) as e:
                        problems.append(f"Could not read {p}'s changes in {ns} ({e}); see "
                                        f"https://github.com/torvalds/linux/commits/v{ns}/{p}.")
                        continue
                    lines.extend(s for s in subs if s not in lines)
                if lines:
                    name = f"{part}, new in {ns}"
                    counts[name] = len(lines)
                    out.append(f"<details open><summary>{name} ({len(lines)})</summary>\n")
                    out.extend(f"- {line}" for line in lines)
                    out.append("\n</details>\n")
        for rel in stable_releases(old, new):
            v = dotted(rel)
            url = f"https://cdn.kernel.org/pub/linux/kernel/v{rel[0]}.x/ChangeLog-{v}"
            try:
                subjects = changelog_subjects(fetch(url))
            except (urllib.error.URLError, OSError) as e:
                problems.append(f"Could not read {url} ({e}); read it there.")
                continue
            for s in subjects:
                upstream_subjects.add(s)
                p = part_of(s, parts, skips)
                if p:
                    by_part.setdefault(p, []).append(f"{v}: {s}")

    ogc_new, ogc_gone = [], []
    if new_tag:
        try:
            now = ogc_patches(new_tag, dotted(new), token)
            before = ogc_patches(old_tag, dotted(old), token) if old_tag else []
            # Compared without OGC's tag: a patch re-tagged [FROM-ML] is
            # the same patch.
            bare = lambda s: re.sub(r"^\[[A-Z-]+\]\s*", "", s)
            had, has = {bare(s) for s in before}, {bare(s) for s in now}
            ogc_new = [s for s in now if bare(s) not in had]
            ogc_gone = [s for s in before if bare(s) not in has]
        except (urllib.error.URLError, OSError, RuntimeError, ValueError, KeyError) as e:
            problems.append(f"Could not read the OGC kernel's patches for {new_tag} ({e}); "
                            "see https://github.com/OpenGamingCollective/linux/tags.")

    for name, _ in parts:
        if name in by_part:
            counts[name] = len(by_part[name])
            lines = by_part[name]
            out.append(f"<details><summary>{name} ({len(lines)})</summary>\n")
            out.extend(f"- {line}" for line in lines)
            out.append("\n</details>\n")

    if new_tag:
        ours_new = [(s, part_of(s, parts, skips)) for s in ogc_new]
        ours_new = [(s, p) for s, p in ours_new if p]
        ours_gone = [(s, part_of(s, parts, skips)) for s in ogc_gone]
        ours_gone = [(s, p) for s, p in ours_gone if p]
        if ours_new or ours_gone:
            out.append(f"**The OGC kernel's own patches** ({old_tag or 'none'} → {new_tag}), "
                       "for the same parts:\n")
            for s, p in ours_new:
                out.append(f"- new, {p}: {s}")
            for s, p in ours_gone:
                plain_s = re.sub(r"^\[[A-Z-]+\]\s*", "", s)
                why = " (now in the upstream kernel)" if plain_s in upstream_subjects else ""
                out.append(f"- dropped, {p}: {s}{why}")
            out.append("")
        others = (len(ogc_new) - len(ours_new)) + (len(ogc_gone) - len(ours_gone))
        if others:
            out.append(f"{others} other OGC patch changes touch none of these parts.\n")
        if ours_new:
            counts["the OGC kernel's own patches"] = len(ours_new)

    out.extend(problems)
    return "\n".join(out), counts


# --- Checks -------------------------------------------------------------------

ALWAYS = "The Deck runs this base: the changed packages have their new versions"


def checks(changed):
    """(log, tv) for the changed areas, each check once, in watch order, with
    the areas that asked for it."""
    log, tv = {}, {}
    for area in changed:
        for check, command in area.log:
            log.setdefault((check, command), []).append(area.name)
        for check in area.tv:
            tv.setdefault(check, []).append(area.name)
    return log, tv


# --- Commands -----------------------------------------------------------------

def cmd_checks(old_path, new_path):
    areas, *_ = read_watch()
    changed = classify(read_manifest(old_path), read_manifest(new_path), areas)
    log, tv = checks(changed)
    for area in changed:
        print(f"area\t{area.name}")
    for (check, command), _ in log.items():
        print(f"log\t{check}\t{command}")
    for check in tv:
        print(f"tv\t{check}")


def cmd_expect(old_path, new_path):
    areas, *_ = read_watch()
    for area in classify(read_manifest(old_path), read_manifest(new_path), areas):
        if area.stripped:
            continue
        for name, _, new in area.updated:
            print(name, new)
        for name, version in area.added:
            print(name, version)
        for _, _, name, version in area.replaced:
            print(name, version)


def cmd_report(old_path, new_path, md_path, json_path, notes_path, versions):
    areas, parts, skips, series = read_watch()
    old, new = read_manifest(old_path), read_manifest(new_path)
    changed = classify(old, new, areas)
    token = os.environ.get("GH_TOKEN")
    old_version, new_version = versions

    kernel_md, kernel_counts = "", {}
    ko, kn = old.get("kernel"), new.get("kernel")
    if ko and kn and ko != kn:
        kernel_md, kernel_counts = kernel_report(ko, kn, parts, skips, series, token)

    # The update notes: plain, one line per area, readable by anyone.
    notes = [f"CabinetOS moves to Bazzite {new_version} (from {old_version}).", ""]
    if not changed:
        notes.append("Nothing the console relies on changed.")
    for area in changed:
        if area.stripped:
            continue
        line = note_for(area)
        if area.name == "Kernel and startup" and kernel_counts:
            fixes = ", ".join(f"{p} ({n})" for p, n in kernel_counts.items())
            line += f". Kernel fixes for the parts a console uses: {fixes}"
        notes.append(f"- **{area.name}:** {line}.")
    stripped = [a for a in changed if a.stripped]
    if stripped:
        notes.append("- Apps CabinetOS removes changed names in Bazzite; "
                     "see the pull request.")

    md = ["### Update notes", ""] + notes + [""]

    log, tv = checks(changed)
    md += ["### Checks for this update", "",
           "Only for the areas above. Put the testing image on the Deck, then "
           "`tools/base-checks.sh <this branch>` runs the log checks there.", ""]
    if changed:
        md += ["**From the Deck's log** (the assistant):", ""]
        md.append(f"- [ ] {ALWAYS}")
        for (check, _), names in log.items():
            md.append(f"- [ ] {check} ({', '.join(names)})")
        md += ["", "**By hand** (on the Deck, and on a TV when docked):", ""]
        if tv:
            md += [f"- [ ] {check} ({', '.join(names)})" for check, names in tv.items()]
        else:
            md.append("- Nothing: the log answers all of it.")
        md.append("")

    if kernel_md:
        md += ["### Kernel fixes for the parts the console uses", "",
               f"Linux {plain(ko)} → {plain(kn)}. Only the changes to the drivers the "
               "console uses, by part (the `changelog:` lines in `ci/base-watch.txt`); "
               "the full lists are at kernel.org.", "", kernel_md, ""]

    if changed:
        md += ["### Changes CabinetOS depends on, by area", "",
               "Matched against `ci/base-watch.txt`. **Read these before merging.**", ""]
        for area in changed:
            md.append(f"<details open><summary>{area.name} ({area.count()})</summary>\n")
            md.append("```")
            md += [f"{n} {o} -> {v}" for n, o, v in area.updated]
            md += [f"{o} {ov} -> {n} {nv}  (Terra's build replaces Fedora's)"
                   for o, ov, n, nv in area.replaced]
            md += [f"added   {n} {v}" for n, v in area.added]
            md += [f"removed {n} {v}" for n, v in area.removed]
            md.append("```\n</details>\n")

    with open(md_path, "w", encoding="utf-8") as f:
        f.write("\n".join(md) + "\n")
    with open(notes_path, "w", encoding="utf-8") as f:
        f.write("\n".join(notes) + "\n")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump({
            "relevant_count": sum(a.count() for a in changed),
            "areas": [a.name for a in changed],
        }, f)


def main(argv):
    if len(argv) >= 3 and argv[0] == "checks":
        return cmd_checks(argv[1], argv[2])
    if len(argv) >= 3 and argv[0] == "expect":
        return cmd_expect(argv[1], argv[2])
    if len(argv) >= 3 and argv[0] == "report":
        opts = dict(zip(argv[3::2], argv[4::2]))
        need = ("--md", "--json", "--notes", "--old-version", "--new-version")
        if all(k in opts for k in need):
            return cmd_report(argv[1], argv[2], opts["--md"], opts["--json"], opts["--notes"],
                              (opts["--old-version"], opts["--new-version"]))
    sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
