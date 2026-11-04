# Lessons: the image, CI and releases

Read before touching `build_files/`, `system_files/`, `ci/`, the workflows, the testing channel or the installer.

## The image

- **`/var` IN A BOOTC IMAGE IS UNPACKED FROM THE FIRST IMAGE ONLY.** Anything a
  build writes there lands on machines installed after it and **never** on
  machines that upgraded into it. The build is green throughout. This bit open
  question 21 (flatpaks) and it nearly bit the storage root — which is why
  `/var/lib/cabinetos` is created every boot by
  `system_files/usr/lib/tmpfiles.d/cabinetos.conf` rather than by a `mkdir`.
  **Everything the image installs goes in `/usr`.**

- **A SYSFS SETTING A UDEV RULE MAKES CAN BE UNDONE BY TUNED AND BY THE
  DRIVER, AND TRIGGERING THE RULE BY HAND WILL NOT SHOW IT.** The rule for
  #228 ("Sound can die until a restart: the TV's HDMI audio chip fails to
  power down") set
  `power/control=on` on the sound controllers; triggered on a running console
  it worked, and after a real reboot it was gone: the driver turns runtime PM
  back on after its probe, and tuned's audio plugin sets
  `power_save_controller=Y` on every profile load. Before writing a rule for a
  device, read which tuned plugins touch it (`/usr/lib/python3*/site-packages/
  tuned/plugins/`), and **prove it across a real reboot**. tuned's own way in
  without copying Bazzite's profiles is the post-loaded profile
  (`/etc/tuned/post_loaded_profile`), laid over whatever profile is active.

- **Without that directory the console writes to `/`.** `storage::root()` tries
  `/var/lib/cabinetos`, and the session user cannot create it, so it falls back
  to the working directory — which for a systemd service is the root of the
  filesystem. `WorkingDirectory=/var/home/cabinet` in the unit makes the
  fallback visible rather than catastrophic, but the tmpfiles rule is the fix.

- **`ldd` prints "not found" and exits 0.** `build_files/install-frontend.sh`
  reads its output, not its status, for the frontend and all 23 cores.
  **Run the control** — the same script into a bare `fedora:44` names all six
  libraries the frontend needs, plus `libGL` for melonDS and **`libX11` and
  `libXext` for PPSSPP**, which nothing had written down anywhere.

- **`mesa-libGLES` is not in the base image, and `libGLESv2.so.2` is there
  anyway** — `libglvnd-gles` provides it. Do not "fix" a missing package that
  is not missing.

- **DO NOT PIN GAMESCOPE'S SIZE; LOOK AT HOW BAZZITE'S SESSION DOES IT
  FIRST.** From 2026-09-19 to 2026-10-13 `cabinetos-session` read the
  display's size once and passed it as `--output-width/--output-height`,
  because gamescope once chose 1080p by itself. Each fault that followed was
  patched around the pin (a 1080p guess, then a wait for the display, then a
  proposed longer wait) until MMagTech asked how everything else handles a
  TV that is off. Desktops and gamescope re-read the display on every change;
  the pin stopped gamescope doing it. Bazzite's own
  `/usr/share/gamescope-session-plus/gamescope-session-plus`, on our base,
  gives no size and turns on gamescope's memory of the last display. Read the
  base image's own way before writing one. (#268, "Booting with no display
  answering leaves the console at a guessed 1080p"; PROJECT.md.)

- **A DISPLAY SWITCHED OFF AND ON THROUGH SYSFS SHOWS A STOPGAP MODE LIST.**
  After `echo detect > /sys/class/drm/card1-HDMI-A-1/status` the connector
  lists five sizes topped by 1024x768, with the EDID already there, for 2 s
  once and 20 s another time, until something asks the kernel to check again
  (another `echo detect`, or a DRM client). Never seen without the software
  switch. After a test, `echo detect` and wait for the full list (52 lines on
  the C1) before restarting the session, or the console comes back at
  1024x768.

- **AND THE SYSFS SWITCH IS NOT A TV COMING BACK; `trigger_hotplug` IS.**
  With the display forced off and back, a running gamescope never logged
  taking the TV and the HDMI sound stayed gone (ELD not valid, only "Dummy
  Output"): a test that looked like a fault in #268 and was the test. The
  driver's own switch, `echo 0 >` then `echo 1 >
  /sys/kernel/debug/dri/1/HDMI-A-1/trigger_hotplug` (as root), goes through
  the real detection: gamescope logs `selecting connector HDMI-A-1`, the ELD
  is valid again and the app's sound moves to the TV (2026-10-13). Use it for
  any "TV off, TV on" test with nobody at the TV, and check the sound output
  (`wpctl status`, `/proc/asound/card0/eld#0.0`), not only the picture size.

- **A LAYER WHOSE CONTENT DID NOT CHANGE CAN STILL GET A NEW DIGEST, AND
  THE CONSOLE DOWNLOADS IT AGAIN** (#511, "Every update downloads about 310
  MB that did not change"). Two causes, found 2026-10-18: the initramfs's
  copy of os-release carried the version (the 254 MB boot splash layer),
  and rpm stamps each package this build installs with the time it ran
  (the 70 MB OS layer). To find the file: the console keeps every layer of
  its two deployments as an ostree ref, so on the A9 `ostree refs
  --repo=/sysroot/ostree/repo | grep container/blob`, then `ostree diff`
  the two refs of the layer (digests from `skopeo inspect --raw`). It reads
  only what is already there and downloads nothing. **And a `podman build`
  on the A9 FROM a local image needs `--pull=never`:** without it, it began
  pulling 12 GB over his slow line.

## Workflows and checks

- **An artifact's internal root is the least common ancestor of the files it
  actually found.** `build-core.yml` uploaded `cores/build/*.so` and
  `cores/system/`, so the twenty cores with no system files got an artifact
  rooted one directory deeper than PPSSPP's. Two layouts under one naming
  scheme, invisible until something downloaded them. Both jobs now stage into a
  fixed `artifact/` directory and upload that.

- **`just build` and `just generate-build-tags` decide whether to stamp the
  image by asking whether `git status -s` is empty.** An untracked directory in
  the working tree silently costs the image its labels and its git-sha tags.
  `image_payload/` and `.artifacts/` are gitignored for that reason, not for
  tidiness.

- **A called workflow's concurrency group can collide with its caller's**, and
  `cancel-in-progress` then has a run cancelling itself — a red build with no
  failing step. `build-core.yml` and `build-frontend.yml` therefore have **no**
  `concurrency:` at all, and `build.yml`'s group is a literal prefix rather
  than `${{ github.workflow }}`. If that ever needs to change, the fix is a
  `workflow_call` input used in the group string — but test it, because an
  invalid `concurrency` expression is a workflow that does not parse.

- **A workflow that build.yml CALLS must not also trigger itself where
  build.yml runs**, or one change queues the same 23 core builds
  twice. That is not merely wasteful: the runner concurrency cap means the
  duplicate starves the image build of the runners it is waiting for, and it
  doubled the wall clock of the change that introduced it. Cancelling one by
  hand leaves a red cross on a pull request whose code is fine, which is how a
  check stops meaning anything. So both called workflows now use
  **`pull_request: branches-ignore: [main]`** and have no `push:` trigger at
  all — build.yml covers main in both directions, and its path filter skips
  documentation only so it can never skip a change under `cores/`.
  **Do not narrow them to `branches: [main]`**: that is the 2026-09-16 hole
  where a stack of branches slipped past every check.

- **MEASURE THE CASE SOMEBODY IS WAITING ON, NOT THE CONVENIENT ONE.** On
  2026-09-22 this entry said a pull request builds in 6 minutes and a merge in
  16, having said 23 minutes an hour before; both were right for something and
  wrong for what a person was waiting on. The shape changed since: a pull
  request's run builds no image (under 2 minutes), a push to `testing` builds
  and publishes one (measured 2026-09-27: 11 minutes, and 24 when the cores
  rebuilt), and a merge to `main` promotes the tested image in under a minute.
  Read the current figure off `gh run list --workflow build.yml` rather than
  this line.

- **Only a push to `testing` or `main` builds an image.** A pull request's run
  checks and stops. To build another branch, push it to `testing`, or
  `gh workflow run build.yml --ref <branch>`.

- **Retargeting a pull request does not re-run CI.** Close and reopen it.

- **A `pull_request` EVENT CAN SIMPLY NOT FIRE, AND NOTHING SAYS SO.** Seen
  2026-09-20 on a pull request that changed `frontend/src/catalog.cpp` as well as
  docs, so `paths-ignore` did not apply: zero workflow runs were created, the
  branch showed *"no checks reported"*, and the pull request sat at
  **`MERGEABLE / CLEAN` with nothing having built it.** A green-looking pull
  request that ran no checks at all is the most dangerous state this repository
  can be in. **`gh pr checks` printing nothing is not the same as passing** —
  count them. The documented remedy works: close the pull request and reopen it.

- **A DOCUMENTATION-ONLY PULL REQUEST CORRECTLY RUNS ONLY THE CHANGELOG
  CHECK**, because `build.yml`'s path filter leaves out `**.md` and `docs/**`
  (all but `docs/LICENCES.md`), while `changelog.yml` runs on every pull
  request to `main` and passes it as "Documentation only". One check and no
  build is expected, and is a different thing from the fault above: check
  WHAT the pull request touches before deciding which one you are looking
  at.

- **AND THERE IS A THIRD CAUSE, WHICH IS JUST A RACE.** `gh pr checks` says
  *"no checks reported"* for the first few seconds after a push, between the
  workflow run being created and its jobs registering against the new commit.
  It is indistinguishable from the real fault by that command alone. **Tell them
  apart with `gh run list --branch <branch>`**: a run in `in_progress` means
  wait, and no run at all for the new head means the event did not fire — which
  is the one that needs the pull request closed and reopened. Seen 2026-09-21,
  where it briefly looked like the dangerous case and was not.

- **A PUSH TO `testing` CAN TAKE TWO MINUTES TO SHOW A RUN.** 2026-10-05 the
  run for a push appeared about 100 s after it, by which time a manual
  `gh workflow run` had been started; the push's run then cancelled the
  manual one (one image build per branch at a time). Wait three minutes
  before deciding a push fired nothing, and do not dispatch on top of it.

- **`ci/base-watch.txt` now watches the CORES' libraries too**, added
  2026-09-19 off a real `ldd` sweep rather than guessed at — including
  `libX11` and `libXext`, which are PPSSPP's and which nothing had written
  down. Three of the frontend's own were missing from that list as well. The
  image build would now fail rather than ship broken, but it would fail with
  no obvious cause; this is what makes the base-bump pull request say "read
  this" first.

- **The monthly base bump needs two clicks, not none.** It opens a pull request,
  but the build on it lands as `action_required` and waits for approval —
  `gh api -X POST /repos/MMagTech/cabinetos/actions/runs/<id>/approve`. And
  **read the relevant list**: when something breaks on real hardware, look at
  what `ci/base-watch.txt` does not watch.

- **A core build failing is not always the core.** `Curl error (28)` is the
  network. Re-run before reading anything into a single red core. This matters
  more now: 23 core builds gate every image build.

- **GitHub serves some repositories at 55 KB/s over git and 9.8 MB/s over
  HTTPS.** `build-core.sh` clones `--filter=blob:none` — but **NOT for
  submodules**, where the lazy blob fetch is thirty times slower than cloning
  them whole. The comment in the script says so; do not "tidy" it.

- **`core-manifest.json` IS on GitHub**, at `docs/core-manifest.json` in
  Cabinet, and has been since `37ca75d`.

- **A MANUAL RUN ON A SIDE BRANCH WOULD HAVE PUBLISHED IT AS `latest`**
  (found 2026-10-12, never happened). The push steps checked only "not a
  pull request", and every branch but `testing` takes the `latest` tag. Now
  only main and testing publish, so `gh workflow run build.yml --ref
  <branch>` is the safe way to prove a CI change without touching `testing`.

- **The outside downloads come from the Actions cache** (RPCS3, Xenia Edge,
  xemu's drive; `ci/fetch-pinned.sh`, keyed on the install scripts' own
  pins). Proved 2026-10-12 with three manual runs: no cache, downloaded and
  saved; cached, nothing fetched; one pin moved, only that file fetched and
  the old one dropped. **The cache is per branch**: a new branch reads only
  its own and main's, so test a pin move on the branch that holds the cache.
- **EACH CORE'S CACHE IS KEYED ON ITS OWN PART OF `cores/build-core.sh`**
  (`ci/core-cache-key.sh`, since 2026-10-20). Until then the key hashed the
  whole file, so adding the 3DS's recipe (#524) rebuilt all twenty-three
  cores, eighteen minutes before the image could start. Now a change inside
  one core's case arm or its `if [ "$CORE" = <core> ]` patch block rebuilds
  that core alone, and any other line of the file, a comment or a count of
  cores included, rebuilds them all. So everything about one core goes
  inside its arm or block, and the lines outside carry no count or list of
  cores. Proved with two manual runs of `build-core.yml`: every core built
  under the new keys, then a comment in gambatte's arm rebuilt gambatte and
  the other 21 came from the cache.
- **A core's start-up chatter is not an option.** PCSX ReARMed's stored
  option list held its memory-map lines, which vary with the runner's
  processor, and failed a build that changed no core; the check compares
  only `name<TAB>default<TAB>values` lines now.

## Promotion, the testing channel and updates

- **PROMOTION COMPARES THE FILES THE IMAGE IS BUILT FROM** (`ci/promote-tested.sh`).
  Until #156 ("CI: docs changes should not force an image build, except
  LICENCES.md, which is in the image") it compared whole trees, docs
  included (learned 2026-09-27): a
  handover committed after the testing push made the merge BUILD a new image
  instead of promoting the judged one. Now `.md` files and `docs/` are
  ignored, the same files `build.yml`'s trigger ignores, except
  `docs/LICENCES.md`, which the image carries. Any other file committed
  after the testing push still makes main build.

- **THE CHANNEL TAG MOVES LAST, TO A SIGNED AND CHECKED DIGEST** (#398,
  SCRIPTS-02 and 48). Until 2026-10-17 `build.yml` pushed `testing` or
  `latest` first and signed after, so a cosign error, a Sigstore outage or
  a cancel in between left the channel on an image every installed console
  refuses, and promotion would carry such a `testing` to `latest`. Nothing
  checked that the secret key matched the public key the image ships. Now:
  push the other tags, sign, `cosign verify` against
  `system_files/etc/pki/containers/cabinetos.pub`, then copy the digest to
  the channel and read it back. Do not fold the channel back into the push
  loop.

- **CREATING `testing` AT A COMMIT GITHUB ALREADY HAS BUILDS NOTHING.** The
  first push to it carried no new commits (the same commit had gone up on
  `system-update`), so the trigger's path filter saw no changed files and
  no run started, silently. Start it by hand: `gh workflow run build.yml
  --ref testing`. Any later push with a new commit builds on its own.

- **A test on the TV that fakes an update state writes the REAL
  `settings.json`.** `--update-dir <dir>` makes the frontend read a status
  file you write, but what it concludes (`update_pending`, `_boot`,
  `_checked`, `_found`) goes into `/var/lib/cabinetos/config/settings.json`.
  A fake "ready" left there means the next real boot says "Update didn't
  apply". Copy the file aside first and put it back after. Headless
  captures should set `CABINETOS_STORAGE` to a scratch root.

- **Show a state, then wait for "next".** Running eight states on a timer
  went past faster than MMagTech could judge them, and the last one stayed
  up looking like a real failure. One state per message worked.

- **A CHANGE TO THE UPDATE SCREENS NEEDS TWO `testing` BUILDS TO TEST.**
  The check is done by the frontend the console is RUNNING, so the first
  build only delivers the new screens; they can only be seen answering a
  check once there is a second, newer build to find. Found 2026-09-25
  sending MMagTech to test the Update available panel against the very
  build that carried it: the old frontend answered, with no panel.

- **Anything in `/tmp` on the A9 is gone after a reboot**, including a log
  you started to watch an update. Read the previous boot with
  `journalctl -b -1`.

## The installer

- **Try every installer change in the VM first: `tools/installer-vm.sh`.**
  QEMU in podman on the A9 (`/dev/kvm` is open to all), an empty NVMe disk,
  no network unless `--net`, and a picture of the screen after each step.
  `repack` puts this checkout's `disk_config/iso.toml` into an existing ISO
  (kickstart as `/osbuild.ks`, the module list as `images/updates.img`) and
  then its look, with `ci/brand-installer-iso.sh` as CI does, for a try in
  minutes; the ISO that ships is always CI's. Open question 5, *Built
  2026-10-07*, has what the installer does and why; its look is
  decisions/2026-11-02-installer-iso-repacked.md, and the setup it no longer
  does decisions/2026-11-03-first-time-setup-in-the-os.md.

- **The ISO builder points a new console at the tag the ISO was built
  from, unsigned.** Its `osbuild-base.ks` (read off the 2026.11.02.4 ISO)
  ends in a `%post` of its own, `bootc switch --mutate-in-place --transport
  registry ghcr.io/mmagtech/cabinetos:<tag>`, before anything of ours runs.
  So a new console's source is "ours, unsigned" whatever the ISO, and
  keeping its tag would pin a console installed from a version-built ISO to
  that version for good: `cabinetos-update-source` sends every new console
  to `latest`, as the installer's own step did.

- **On these images `systemd-sysusers` runs at the first start of every
  deployment, not only the first boot.** `/usr` is dated 1970 on ostree, so
  `ConditionNeedsUpdate=/etc` could never hold again, but ostree leaves
  `/etc/.updated` out of each new deployment: on the A9 sysusers ran on the
  first boot after each update of 2026-11-02 and was skipped on the next. A
  unit gated on that condition is therefore not "once per machine"; it must
  tell a new console from an updated one itself (one deployment, nothing
  beside it), and it must run before `systemd-update-done.service`, so with
  `DefaultDependencies=no`, as ldconfig.service does.

- **`bootc switch --mutate-in-place` cannot write on a running console.**
  It is meant for an installer's `%post`; it skips the mount namespace
  bootc's other commands enter (bootc 1.17.1's `cli.rs`), and `/sysroot` is
  read-only once booted (`[sysroot] readonly = true`). Remount it read-write
  in a namespace of its own (`unshare --mount --propagation slave`), as
  bootc itself does; that is how `cabinetos-update-source` wrote the VM's
  `.origin` on 2026-11-03, in about a second. It rewrites the newest
  deployment's `.origin`, so it is safe only with one.

- **A new console restarts itself once, seconds into its first start.**
  `bazzite-hardware-setup.service` adds kernel arguments (Bluetooth's among
  them), which makes a second deployment of the same image with the booted
  one's `.origin` copied, and reboots into it. Seen in the VM on
  2026-11-03: two boots before first run, two deployments on the drive. So
  "one deployment" means "new" only before that service runs; anything that
  tells a new console by it must be ordered before it.

- **After an install the VM boots the ISO again**, because QEMU's
  `bootindex` overrides the boot order the installer wrote. Real firmware
  boots the new drive. Switch the VM off and `boot` the disk alone.

- **The installer runtime has no `clear`, `stty`, `tput` or fb0.** Clear with
  escape codes; take the screen's width from
  `/sys/class/drm/card*-*/modes` of the connected output. Its only console
  font is `Lat2-Terminus16`; `setfont -d` doubles it.

- **Switching an Anaconda module off does not always hide its screen.** The
  Network module was off and the installer said so, and Network & Host Name
  was still drawn. Users really did go. Check the screen, not the config.
  The screen goes with `hidden_spokes = NetworkSpoke` in a conf.d file in
  the product image (`disk_config/installer.conf`).

- **THE UEFI BOOT MENU IS NOT IN THE ISO'S FILE TREE.** UEFI boots a FAT
  image appended after the ISO9660 tree (GPT partition 2), and
  `xorriso -boot_image any replay` copies that partition from the old ISO,
  so a new `/EFI/BOOT/grub.cfg` in the tree changes nothing. The FAT image
  is edited with mtools and given back with `-append_partition 2`;
  `ci/brand-installer-iso.sh` reads the partition back to prove it.

- **GRUB'S SCRIPT HAS NO `&&`.** `if [ a ] && b; then` is a syntax error,
  the block is skipped (or half run) and the menu still appears, so it
  looked as if it worked. `grub2-script-check` (Ubuntu: `grub-script-check`)
  catches it; the branding script runs it.

- **Fedora's signed EFI GRUB has no `background_image`**, only themes
  (gfxmenu), and a theme always opens a terminal box of at least 80 by 24
  characters over the picture when an entry starts, which the kernel then
  keeps on screen until something prints. `fbcon=nodefer` clears it. Under
  UEFI, GRUB's graphical screen keeps the firmware's mode (efi_gop.c, "Keep
  current mode if possible"), so a theme does not change the installer's
  resolution.

- **`quiet` is not quiet in the installer.** systemd turns its status lines
  on once a step is slow (finding the stick is), Plymouth has only its text
  themes and repeats them, and dracut's warnings come through journald. The
  installer's line has `loglevel=0 systemd.show_status=error
  systemd.journald.max_level_console=crit rd.plymouth=0 plymouth.enable=0`.

- **A LINE WRITTEN TO THE CONSOLE EARLY IN THE INITRD STOPS MOVING.** The
  console's setup runs a second time when the kernel's console moves to the
  graphics driver, which cuts off a console opened before it and puts the
  normal font back. `disk_config/installer-wait.sh` opens the console again
  for every frame. And a unit wanted by `sysinit.target` runs in the initrd
  only: after the switch to the installer's root that target is already
  reached, so the second copy is wanted by `anaconda.target`.

- **Keys typed into the VM go to whatever has focus.** An `n` in a shell
  command typed before the console switch landed answered the erase question
  and switched the VM off (correctly, with nothing written). Take a picture
  before typing; type slowly (very fast keys jam a key down); and read a disk
  with guestfish rather than by typing commands.

- **The media check passes on a perfect copy.** Run in the VM on the ISO
  itself, it counted to 100% and went on. The 2026-09-19 failure (`Supported
  ISO: no`, aborting at 4.8%) was reading the USB stick, not the ISO.

- **The installer runtime carries NO firmware**, which is why the GPU and
  Wi-Fi die in it and why neither matters. The tell: it also failed to load
  `gc_11_5_0_pfp.bin`, a file that *is* in Fedora's package and *is* in our
  image. **One thing that should not have failed was worth more than all the
  things that did.** Its kernel is stock Fedora's, not ours.

- **AN APPIMAGE UNPACKS WITH ITS OWN PERMISSIONS.** Xenia Edge's
  `squashfs-root/usr` is 0700, `cp -a` kept it, and the first testing image
  had the emulator readable by root alone: the console runs as `cabinet` and
  called it "not installed" (2026-09-30). The UI loop never saw it, because
  there the test copy belonged to `cabinet`. **An emulator is judged from the
  image, not only from a copy.** `install-xenia.sh`, `install-cemu.sh` and
  `install-rpcs3.sh` now make everything readable and fail the build on any
  file others cannot read.

- **A GITHUB ARTIFACT LOSES ITS FILES' EXECUTE BIT.** `actions/upload-artifact`
  does not keep permissions, so Cemu's program reached the image build as a
  plain file, and `install-cemu.sh`, which checked `-x` before copying, said
  there was no Cemu at all (2026-10-02). PCSX2 never showed it, because a
  shared library is never executed directly. **Check that a payload file
  exists, then set its mode in the image**, as `install-cemu.sh` now does.


- **A package can set a file capability with no script at all.** Sunshine's
  RPM marks `/usr/bin/sunshine` `cap_sys_admin,cap_sys_nice=p` in its file
  list; `rpm -qp --scripts` showed nothing of it. The dry run in the base
  image caught it only because the install script checked `getcap`. Check
  the installed result, not the package's description, and act on the
  package's own file list (`rpm -ql`), so a version that moves a file is
  still covered.

- **Building Sunshine ourselves (#304, "Stream into Steam: leaving Steam back
  to Home turns the phone's picture black"): three traps, each found on the
  A9.**
  1. *Git must trust the build tree.* Sunshine's CMake asks git which
     build-deps tag its submodule is at and downloads that tag's prebuilt
     FFmpeg. rpmbuild unpacks the tree as another user than the container's
     root, git refuses to answer ("dubious ownership", silently), and
     Sunshine falls back to build-deps' NEWEST FFmpeg: the link then fails
     with undefined `swr_*` in libavcodec. `safe.directory` fixes it, and
     `cores/build-sunshine.sh` checks the log names the right tag.
  2. *A run's files belong to a mapped user.* Remove them with
     `podman unshare rm -rf`, or the next run cannot start.
  3. *The spec fetches tools unpinned* (nvm from master, the newest Node.js).
     Pinned by edits that check their anchors.
  Built and checked like this, LizardByte's package and ours differ only in
  the compiler's point release and our lines: every other string in the two
  binaries, FFmpeg's included, is identical.

- **A dry run catches what reading does not.** The new
  `install-sunshine.sh` read fine and passed `bash -n`; run in the testing
  image on the A9, its last line named a variable that no longer existed and
  the image build would have failed at the very end. Five minutes, against a
  wasted image build.

## Running from a USB drive (learned 2026-11-03, the portable install)

- **The builder's disks boot with `console=tty0 console=ttyS0`**, so
  `/dev/console` is the serial port: systemd's messages, and anything that asks
  on the console, go there, and the screen sits on the last kernel line as if
  frozen. `tools/installer-vm.sh` writes the serial port to `serial.log` in
  the VM's folder; read that first on a qcow2. An ISO install has no serial
  console, and neither has the portable file, which takes it out
  (`ci/make-portable-image.sh`).
- **A QCOW2'S SIZE SAYS NOTHING OF WHAT A DRIVE NEEDS.** The builder's qcow2
  artifact was 6.6 GB and the plan said a portable would write about 7 GB; the
  qcow2 is compressed. The same image as a raw disk is 28.9 GB, twice the
  image's size whatever the settings ask, holding about 10 GB. Measure the raw
  file.
- **SHIM ON A DRIVE'S FALLBACK PATH WRITES TO THE PC'S FIRMWARE.** With
  `EFI/BOOT/fbx64.efi` there, the VM's firmware gained `\EFI\fedora\shimx64.efi`
  on its first boot. `strings -el vars.fd` shows it. Without that file and with
  grub copied beside shim, the firmware gained no entry and the VM booted the
  same, from a drive with no other change.
- **A `journalctl --follow` started before the journal moves to the drive stops
  at the move.** The first boot check's log ended at
  `systemd-journal-flush.service` on both starts and looked like a hang, while
  the console went on to first run. Order the unit after that service.
- **A RAW FILE LEFT FOR QEMU TO GUESS CANNOT BE WRITTEN IN ITS FIRST SECTOR.**
  That is where a partition table grows. Say `format=raw`, as
  `tools/installer-vm.sh portable` does for a `.img`.
- **A GPT CUT SHORT IS NOT READ AGAIN, BY SFDISK.** Its last usable sector is
  then past the end. Dump the table, truncate the file and write the dump back,
  without `device:` and `last-lba:`: every id stays, and nothing warns.
  `sgdisk -e` also copes, with warnings and a gap for alignment.
- **GitHub's Ubuntu runner has `/dev/kvm`**, group `kvm`, so `chmod 666` it
  first (measured 2026-11-03): a portable's first start took about 4 minutes
  there, and the next start 45 seconds to first run.
- **A new console's first start is slow in the VM and nothing is wrong:**
  Bazzite's hardware setup took 3.5 minutes from a USB-attached disk image
  (it adds kernel arguments, then restarts), and the Flatpak setup downloads
  the emulators meanwhile; the session starts only after the hardware setup.
  A hard power-off in that time loses the journal's unsynced tail: press the
  power button (`system_powerdown`) and wait.
- **The kernel writes the hardware clock in local time only when the first
  time zone it is told at a start is not UTC** (`persistent_clock_is_local`,
  set once per start). The journal says which: "RTC configured in
  localtime, applying delta of 0 minutes to system time" on a start with no
  zone. So a start without a zone must not let chrony's `rtcsync` write the
  clock, and the zone chosen later cannot change that until the next start.
- **`hwclock --hctosys` waits for the clock's once-a-second interrupt** and
  fails after ten seconds ("select() to /dev/rtc0 to wait for clock tick
  timed out") where it never comes: QEMU's clock, and some boards.
  `/sys/class/rtc/rtc0/since_epoch` reads the clock's face with no wait.
- **systemd's first-boot wizard is on in the base, and the image has no
  language, keyboard or zone file**, so a first boot would stop at a text
  prompt asking for all three. A disk image's first start is not a first
  boot to systemd: its `/etc/machine-id` is empty, not "uninitialized", so
  the wizard was skipped (the journal says "unmet condition check
  ConditionFirstBoot=yes"). A disk build that writes "uninitialized" would
  bring the prompt.
- **An ISO install keeps the PC's clock in local time, written by the
  installer**: its `/etc/adjtime` says LOCAL with the install's own time on
  it (the image's copy is dated 1970), as the A9's does from 2026-09-19. The
  kickstart has no timezone line. The installer's own Time & Date screen
  still read the clock as UTC (04:26 at 09:26 Eastern, in the VM with a
  local-time clock); the installed console's first start was right.
- **A disk image written to a bigger drive keeps its GPT's backup at the
  image's end.** sfdisk says so ("GPT PMBR size mismatch", "The backup GPT
  table is not on the end of the device") and moves it as it writes, so one
  `sfdisk -N` grows the last partition to the drive's real end.

## The release download (learned 2026-10-10, the first release)

- **A GitHub Release takes at most 2 GB per file, and the ISO is about 6.7 GB.**
  Nobody checked the size when the release was planned; it surfaced on
  release day. The installer lives in Cloudflare R2 (`download.mmagtech.com`,
  MMagTech's account), uploaded by **Build disk images** with `publish`.
  Why R2: the comment on that workflow's publish step (#308, "At the first
  release: the download goes on a GitHub Release, and the page and wiki
  point to it", whose plan the 2 GB limit overturned).
- **A new DNS name is "not found" for a while on any machine that asked
  before it existed.** The Mac and the A9 both cached the miss for
  `download.mmagtech.com`; public resolvers had it at once. Check with
  `dig @1.1.1.1` and `curl --resolve` before concluding a domain is broken.
- **The A9 has no IPv6 route** but resolves AAAA records first, so `curl`
  there fails on a dual-stack host: use `curl -4`. Browsers fall back by
  themselves.
- **GitHub Pages tells browsers to keep pages for about 10 minutes.** Right
  after a site change, MMagTech's browser showed the old page; a private
  window showed the new one. Check the live page with `curl` before
  assuming a change did not land.
- **A custom domain on Pages means no base path.** `_config.yml`'s
  `baseurl: /cabinetos` made every stylesheet miss at
  `cabinetos.mmagtech.com`, and the site loaded unstyled.
