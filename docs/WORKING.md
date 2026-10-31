# Working on CabinetOS

How MMagTech works on this project: the loop, the testing branch, merges,
and how he wants it done. This changes rarely. New here? Start at
[CONTRIBUTING.md](../CONTRIBUTING.md). What to work on next is
`docs/NEXT-SESSION.md`; what cost hours before is `docs/lessons/`.

## The loop

- **Nothing builds on the Mac.** Frontend changes: `tools/ui-loop.sh` builds on
  the A9 and shows it on the TV in about 30 s (`--restore` puts the console
  back on the image). Anything in the image (cores, units, `tmpfiles.d`, the
  session script) needs an image build: push to `testing`.
- **Anything that runs at every start gets a smoke check on the testing
  image**, whatever was judged in the loop: a menu click is heard, a game
  plays with sound, a pad moves (lessons/testing.md, "The silent day").
- **A merge promotes the tested image; it does not build.** Push the branch's
  final commit to `testing`, have MMagTech judge it on the A9, merge on his
  explicit go. Anything committed after the testing push makes main build
  instead, except documentation: `.md` files and `docs/` are not in the image
  and do not count (#156, "CI: docs changes should not force an image
  build, except LICENCES.md, which is in the image"), apart from
  `docs/LICENCES.md`, which is. Write `NEXT-SESSION.md` inside the work's
  own branch, before or after the push.
- **Every session starts with only `main` and `testing`.** Delete a branch
  once it merges, on GitHub and locally.
- **Check CI yourself** (`gh run list --branch <b>`); never ask him to look at
  Actions. "No checks reported" can be a race, or an event that never fired.
- **PR bodies** lead with what the change does and why, say "Closes #N", and
  each finished issue gets a closing comment.
- **Every change that reaches consoles goes in the changelog**,
  `docs/wiki/changelog.md` (on the site as *What's new*), in the same pull
  request (MMagTech, 2026-10-12). Once its testing image is built, add a
  heading with that image's version (`## 2026.10.12.9 {#v2026-10-12-9}`),
  newest first, and no date line: the version is the date (UTC). **Write it for a player, not for us:** what they
  would have noticed, then what happens now, and the issue number; no
  internal names (`**Fixed:** GameCube saves kept on your server loaded as an
  empty card on a new console. They load now.`). **A New entry says how to
  use it**, not only how to open it: the buttons, briefly, and a link to its
  wiki page (MMagTech: the manuals entry said R1 and nothing about turning or
  zooming). A pushed-again `testing`
  image moves the heading to the new version; it is a docs change, so it
  does not stop the promotion. **A contributor's pull request** puts its
  entry at the top under `## Next` (CONTRIBUTING.md, "Pull requests and
  issues"), which `changelog.yml` accepts because it checks only that the
  file changed; once that change's testing image is built, MMagTech turns
  `## Next` into the version heading, as above. A change a player
  cannot notice (CI, the build) says `Changelog: none` in its PR body
  instead. The pull request check `changelog.yml` enforces it.
- **The TV**: say every step and what success looks like, then wait for "go".
- **Logs**: `sudo journalctl -o cat --since "-40 min" | grep -aE "\[launch\]|\[core\]"`;
  `-u cabinetos-session` shows almost nothing.

## Machines

The reference console (the A9 Max), the test VM and MMagTech's RomM server,
their addresses, and the hand-made state on them are kept off this public
repository (#398, "Audit the code and restructure it for contributors
(main.cpp is 20,000 lines)", 2026-10-17): they are in a notes file in his
home folder that CLAUDE.md loads. CLAUDE.local.example.md shows what it
holds.

Recovery if port 2222 on a console ever fails: at the TV with a keyboard,
Ctrl+Alt+F3, log in as `cabinet` (the password File access and Developer
access show, kept in `/var/lib/cabinetos-files/password`), `sudo bootc
rollback`, `sudo reboot`.

## Going back to an earlier version

Moved to CONTRIBUTING.md, "Going back to an earlier version" (#398,
2026-10-17). The last image before the restructure is 2026.10.14.5, tagged
in git on 72c0c4b.

## How MMagTech wants this done

Plain answers. **Lead with what a change does and why it exists, in terms of
what breaks for the product, not the subsystem.** Keep the detail, after the
plain statement. **Explain how something works as steps.** When he asks how
something happens, answer with the steps in the order a person would see them,
one per line, each saying who does it (RomM or the console), in plain words
with no code names; and say first when it applies only to other people's
setups, not his. Check the running machine before theorising, and say what is
verified and what is assumed. **Use the `MMagTech` handle, never a personal
name**: the repo is public.

## Keeping the cost down

The 3DS (PR #524) cost far more than any core before it, for reasons a
session controls (MMagTech, 2026-10-20). From now on:

- **Settle the choices before building.** Every decision the work needs
  (layout, levels, controls, what plays) is put to MMagTech and answered
  before the build session starts. A change after that is a new piece of
  work, said as such.
- **One fresh session per phase**: research, build, TV run, testing push,
  each with a short brief that names the files and what done looks like. No
  session carries a feature from start to finish.
- **The lead session stays small.** It reads, decides, briefs and reports.
  Past about 300,000 tokens of context it hands over to a fresh lead with a
  written brief, between phases, when no session it started is running.
- **TV runs read logs before pictures.** A screenshot only where only a
  picture can answer, at the smallest scale that answers it.
- **Research only what decides something.** A survey that will not change
  what gets built is offered as a later card, not run.
- **Side findings go to a card or the pull request's notes**, not into the
  running session's work.
- **Sonnet for mechanical pieces** (docs edits, tables, checks, watching
  CI); Opus for code and research; Fable only when MMagTech names it.

## Patches to an emulator's code

MMagTech's rule of 2026-10-20, for any change to an emulator's own source (a
patch applied in `cores/build-core.sh` or any other build script):

- **Named before it is built.** The patch is put to MMagTech first, in plain
  words: what it changes, why, and what happens without it. A condition
  inside research does not count as naming it.
- **Tracked on an issue.** Each patch he approves gets a CabinetOS issue that
  links the bug or request filed upstream (filed on his go), names where the
  patch lives in the recipe, and says what to check when the emulator's pin
  moves: fixed upstream, so drop the patch and close the issue; or adjust
  the patch to the new code. Every replacement in a patch is asserted, so a
  pin move never quietly ships without it.

## The image, the build and CI

How the image is built, what it carries and what CI checks on a pull
request are in CONTRIBUTING.md ("Building", "What CI checks on your pull
request", "How a change reaches a console") and docs/ARCHITECTURE.md (the
map), corrected against the workflows on 2026-10-17 (#398). What stays here
is what only the maintainer runs.

**Monthly, on the 1st.** `base-update.yml` checks for a new Bazzite base and
opens a pull request listing what moved. A player's fault that a newer
Bazzite fixes, or a security hole, is taken by hand in between
(`gh workflow run base-update.yml`; docs/PROJECT.md, "Staying current with
Bazzite").

**Weekly, on Mondays.** `pad-db-update.yml` refreshes the controller list
(`frontend/data/gamecontrollerdb.txt`); `terra-fallback-update.yml` keeps the
stored spare of Steam's session (`build_files/terra-fallback/`) current.

None of the three merges anything.

**`build-disk.yml`**, by hand only: the installer ISO (`anaconda-iso`, an
artifact kept 90 days) or a qcow2. With `publish` ticked it also replaces the
download, `cabinetos.iso` and its SHA-256 in Cloudflare R2
(`download.mmagtech.com`, the link the README and the wiki's Install page
give), and reads the size back to check it. Build it once per release; an
installed console updates itself. Try installer changes in a throwaway VM
first with `tools/installer-vm.sh`.

**Repository settings the build depends on.** Settings, Actions, General,
Workflow permissions: **Read and write**, or the push to GHCR fails with a 403.
The GHCR package must be **public**, or installed consoles cannot pull updates.

**The site.** `docs/index.html` (the showcase page), `docs/wiki/` and
`docs/media/` are served by GitHub Pages from `main`'s `docs/` folder;
`docs/_config.yml` keeps these working notes off the site. Media is captured on
the A9: stills with the frontend's own `SIGUSR1` capture, video from
gamescope's PipeWire node with `gst-launch-1.0` (both on the image), encoded to
720p60 H.264 with the image's `ffmpeg`.
