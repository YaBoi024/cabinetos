---
name: check-security-fixes
description: Say whether the exact version CabinetOS pins of anything that reads outside data (Sunshine, rcheevos, every emulator and core) has a publicly known security hole, vulnerability or CVE, and whether a newer version fixes it. Returns No, Look closer or Yes for each pin. Use when asked about security, vulnerabilities or CVEs in a pin, and with the monthly base update.
argument-hint: [pin]
---

# Check security fixes

For every part of the image we pin ourselves that reads data from outside,
this skill says whether our exact version has a publicly known security hole,
and whether a newer version fixes it. It changes nothing and files nothing: a
Yes is a proposal, and the maintainer decides.

`check-emulator-update` answers a different question: would an update fix
something players see. The rule a Yes is weighed against is
`docs/decisions/2026-10-23-when-an-emulator-pin-moves.md`, reason (E): "a
security fix for a hole that outside data can reach (a request over the
network, or a game file, save or state)".

With one pin named, check that one. With none, check them all.

## What it reads, and what it leaves out

The pins are the ones `.claude/skills/check-emulator-update/SKILL.md`, "The
list, when none is named", finds, and rcheevos and Sigil, whose versions and
commits are in `frontend/third_party/rcheevos/VERSION` and
`frontend/third_party/sigil/VERSION` (Sigil's names its libchdr, zlib, zstd,
LZMA SDK and tiny-AES-c too).

| Pin | The outside data it reads | Faces the network |
|---|---|---|
| Sunshine (`cores/build-sunshine.sh`, `VERSION` and `COMMIT`) | Requests from devices on the local network, while Remote Play is on | Yes |
| rcheevos | RetroAchievements' answers; also the game files it hashes and the progress kept in a state | Yes |
| Sigil, with the libraries it carries | A Switch game's file, decrypted with the player's keys, and another app's Switch save from RomM | No: it runs inside the console's own program, which holds the RomM sign-in |
| Every emulator and core, the Flathub apps (Eden, xemu) among them | Game files, saves and states, from RomM | No (`docs/decisions/2026-10-26-emulators-get-no-network.md`) |

Where a hole lands matters to what it reaches. The libretro cores and PS2 run
inside the console's own program, which holds the RomM sign-in. The other
emulators run as programs of their own with no network: under `bwrap`, or
`flatpak run --unshare=network`.

Left out, each for a reason:

- **What comes from the Bazzite base.** Whatever the frontend and the system
  use from the base moves with the monthly base update
  (`ci/check-base-update.sh`, `ci/base-watch.txt`): the image decoders,
  json-c, libarchive, libcurl, GStreamer and libnice, Tailscale, BlueZ and
  the kernel. So does `libnice-gstreamer1`, which
  `build_files/install-linkup.sh` adds from Fedora at each image build. None
  of it is ours to pin. A flaw being exploited in the wild in one of them is
  a reason to run `base-update.yml` by hand and cut an image before the month
  is up (docs/WORKING.md, "Monthly, on the 1st").
- **The Flathub runtimes** (the `runtime` lines of
  `system_files/usr/share/cabinetos/flatpaks.list`, Flathub's Mesa among
  them), which Eden and xemu read their files through. They publish no
  advisories, and freedesktop-sdk's release notes list updated packages
  without naming a CVE, so none of the sources below can say whether a build
  has a hole. Their point releases are their security updates, and Flathub
  keeps only the last few builds of each, so `ci/check-flatpak-pins.sh` makes
  these pins move every few releases anyway. Say how many point releases
  behind each one is (its version from the reference console, as for an
  app), and nothing more.
- **The LinkUp server** (`ci/linkup-server.txt`). It is not in the image: the
  file names the one release CI tests the console against. The server's holes
  are MMagTech/cabinet-linkup's to watch, and that repository checks its own
  (govulncheck, in its check-known-holes.sh, and Dependabot).
- **What only builds something:** the builder containers, and Sunshine's
  Node.js pins, which build its web page and are not in the image.
- **Data, not code:** xemu's hard disk image
  (`build_files/install-xemu-drive.sh`) and MAME's list of games.

## The sources

Read the first three for every pin, and the fourth for the two that face the
network; each finds what the others miss.

1. **GitHub's repository advisories**, of the repository we pin and, for a
   fork (most `libretro/` ones), of its upstream too
   (`gh api repos/<owner>/<repo> --jq .parent.full_name`, or the project the
   fork's README names):

       gh api --paginate repos/<owner>/<repo>/security-advisories --jq '.[] | [.ghsa_id, .cve_id, .published_at, .severity, (.vulnerabilities | map(.vulnerable_version_range + " fixed in " + (.patched_versions // "nothing")) | join("; ")), .summary] | @tsv'

2. **OSV, by our exact commit**, for CVE records that name the project's
   commits:

       curl -s -X POST https://api.osv.dev/v1/query -H 'Content-Type: application/json' -d '{"commit":"<full commit>"}'

   OSV holds no advisory that has no CVE (most of Sunshine's), and it does not
   know a fork's own commits, so it never stands in for source 1.
3. **NVD, by name**, for a CVE that no repository published and OSV could not
   tie to commits (Snes9x's of 2026-06 was found only here):

       curl -s "https://services.nvd.nist.gov/rest/json/cves/2.0?keywordSearch=<name>"

   Five requests every 30 seconds without a key. A name is shared with other
   products (Dolphin, Eden, Sunshine and Xenia all are), so keep only the
   records whose text or links name the emulator.
4. **For the two pins facing the network, the fixes nobody announced:**
   upstream's commit titles since our commit, read for a bounds check, an
   overflow, a crash on bad input or a security report, and the pull request
   behind each likely one:

       gh api --paginate "repos/<owner>/<repo>/compare/<our commit>...<default branch>?per_page=100" --jq '.commits[] | .commit.committer.date[0:10] + " " + (.commit.message | split("\n")[0])'

   A fix with no advisory is not a publicly known hole, so it is at most Look
   closer.

## For each pin

1. **Our exact commit, and its date.** Most pins name a commit. A downloaded
   build names it in its version or on its release page: RPCS3's in its URL,
   Xenia Edge's tag is its commit, a Vita3K build's release says
   "Corresponding commit", MAME's is `MAME_COMMIT` in `cores/fetch-mame.sh`. A
   Flathub pin's version comes from the reference console
   (`check-emulator-update`, step 1). A project with no advisory ever
   published and no OSV or NVD record needs no version: it is No. The date is
   `gh api repos/<owner>/<repo>/commits/<commit> --jq .commit.committer.date`;
   anything published after it may be about code we carry.
2. **Is our version affected?** For each advisory, whenever it was published:
   - Against a release:
     `gh api repos/<owner>/<repo>/compare/<first fixed tag>...<our commit> --jq .status`.
     `ahead` or `identical`: we have the fix. `behind` or `diverged`: we do
     not, unless the fix was picked into our commit (below). An advisory's range is sometimes
     written loosely (a commit, a release that never had the bug); judge by
     the fix commit when it can be found.
   - A fork carries upstream only up to where it last merged, plus what it
     picked. Find where it meets upstream,
     `gh api "repos/<upstream>/compare/<upstream branch>...<fork owner>:<fork repo>:<our commit>" --jq .merge_base_commit.sha`,
     and where that lands among upstream's releases
     (`compare/<tag>...<merge base>`, whose `ahead_by` gives a development
     number such as 2606-282). Then look for the fix itself, by the file the
     advisory names:
     `gh api "repos/<fork>/commits?path=<file>&sha=<our commit>&since=<a date before the fix>"`.
     A fork can pick a fix long before it merges; when the history is unclear,
     read the fixed lines in our commit's copy of the file.
3. **The first fixed version:** upstream's release, and for a fork the fork's
   own branch, checked the same way, with the commit that brought it. If
   nothing fixes it yet, say so.
4. **What outside data reaches the flaw on CabinetOS.** Start from the
   advisory's own text (a game image, a save, a state, a request on the
   network, a mod file), then check the console takes that path: the core's
   own defaults (`cores/options/<core>.txt`), what the console sets over them
   (`frontend/src/coreoptions.cpp`, `quality.cpp`, `sysopts.cpp`), what it
   writes before a launch (the core's prep code in `frontend/src/`, such as
   `dolphinprep.cpp`), and what RomM carries (games, saves, states). A flaw
   that only a file the console never puts there can reach, or one behind a
   setting that is off, is not reached; name the file that shows it.

## The verdict

For each pin, one of:

- **No:** no advisory affects our version, or none that outside data can
  reach on CabinetOS.
- **Look closer:** a hole may affect us, but the evidence is not in hand: a
  fork whose contents could not be placed, a version that could not be read,
  a fix with no advisory, or a hole nothing fixes yet. Name the one thing that
  would settle it, and who can get it.
- **Yes:** our version is affected, outside data reaches the flaw, and a newer
  version fixes it.

A Yes meets reason (E), for an emulator or core as much as for a pin facing
the network, and the proposal is to move the pin to the smallest step that
carries the fix, not the newest build. Its cost is `check-emulator-update`,
steps 6 and 7: where the fix exists only in a large jump, the move still goes
through the full check, with saves and states tried first. Security is a
reason to move, never a reason to rush.

## The report

First, in one line, every pin with no advisory or record at all. Then, for each
other pin:

- the advisories published since our commit, and any older one that still
  affects us;
- whether our version is affected, with the evidence (the comparison, or the
  fix commit found or missing in our copy);
- the first fixed version;
- what outside data reaches the flaw, and where it lands (inside the console's
  program, or an emulator on its own with no network);
- No, Look closer or Yes, and for a Yes, the smallest step that carries the
  fix and how far it is from our pin.

Say it in plain words, so it can be pasted on an issue as it is, and give it to
whoever asked.

**Worked example: Sunshine, 2026-11-14.** Our pin is 2026.914.233613.
CVE-2026-32253 (GHSA-ph75-mgxh-mv57), a critical bypass of how Sunshine checks
a client's certificate, was fixed in 2026.516.143833; ours is later, so it is
not affected. One advisory was published after our commit,
GHSA-fp6g-27w5-489j on 2026-09-15, and it is fixed in our own version. No
CVE record ties to our commit. Of the commits upstream since ours, the likely
fixes are a crash on a long hex value in Sunshine's own config file and two
repairs to pairing, none a hole a device on the network can reach. Verdict:
No.
