# Fork changes

A fork of [CabinetOS](https://github.com/MMagTech/cabinetos) by MMagTech,
adapted for the Steam Deck. This lists every deliberate difference from
upstream. When a sync conflicts, use it to decide which side to keep.
Files marked `FORK (handheld)` in their comments are the fork's.

## Identity and build
- **Base image: bazzite-deck**, pinned by tag and digest, for the Deck's hardware support (speaker
  tuning, built-in controls, power controls, OLED panel correction). `Containerfile`
- **Own image owner and signing key** (`ghcr.io/yaboi024/cabinetos`). Details in section 2.
- **Shown as "CabinetOS Handheld"**, with a fork watermark on the boot screen.
  `about.cpp`, `startscreen.cpp`, `build_files/assets/make-boot-watermark.py`
- **Staying in sync with CabinetOS.** A daily check keeps one issue open while upstream has commits
  the fork has not merged: a trial merge's conflicting files, the changed files on the watch list,
  and the commits. `.github/workflows/watch-upstream.yml`, `.github/upstream-watch.txt`;
  `base-update.yml` follows bazzite-deck monthly. A weekly Claude task on the Mac reviews both and
  says what is ready to merge; it never merges.
- **`tools/deck-loop.sh`**: edit on the Mac, build and run on the Deck, no GitHub round trip.

## System fixes
- **Updates from the console work** (an SELinux permission).
  `system_files/usr/lib/systemd/system/cabinetos-update-download.service.d/10-selinux.conf`
- **Sound follows the dock**: TV when docked, speakers when not.
  `system_files/usr/lib/systemd/system/cabinetos-session.service.d/10-audio.conf`
- **Power profiles work**: tuned is masked on bazzite-deck and is unmasked in the image.
  `build_files/configure-session.sh`

## Streaming: the way to play together
Games are played together by streaming: the game runs once, on the host Deck, 
and friends join with Moonlight. It's been tested on N64, PSX and a few others.
- **A device that streams joins as the next player.** The Deck's controls stay player 1 and the game
  keeps running. `CABINETOS_STREAM_SHARE=0` brings back the old takeover. `players.cpp`, `players.h`,
  `pausemenu.cpp`
- **The Deck keeps its sound while a device streams**, and the device plays it too.
  `cores/sunshine-patches/0002-keep-the-consoles-sound-on.patch` (needs an image build)
- **Heavy games drop to Performance while a stream is open**, from the next game started.
  `quality.cpp`, `quality.h`
- **Play with a friend** (Settings, Remote Play): the steps for a friend, a QR code to Moonlight, this
  console's Wi-Fi and Tailscale addresses, and the PIN. `friendflow.cpp` (new)
- **Join a friend** (Settings, Remote Play): this Deck as the guest. Type the friend's address; the
  first time, four digits appear to type on the friend's Deck; then Moonlight runs full screen with
  this Deck's controllers. Both stick clicks together leave. Needs Moonlight installed, which needs
  its lines in `flatpaks.list` (run `tools/moonlight-pin.sh`). Untested on a Deck.
  `joinflow.cpp`, `moonlight.h`, `moonlight.cpp` (new)
- Wiring for both pages: `settingsflow.cpp`, `settingsids.h`, `app.h`, `nav.cpp`, `backdrop.cpp`,
  `frame.cpp`, `main.cpp`, `remoteplayflow.cpp`, `tailscaleflow.cpp`
- Note for upstream: Join a friend connects to an address the player types, which is the same kind
  of place as the RomM server's address, but it is not on CONTRIBUTING.md's list of fixed places.

## Security and reports
- **The Play with a friend page shows "Connection: direct" or "relayed"** while a friend is
  connected over Tailscale, and says a relay adds lag. `tailscale.h`, `tailscale.cpp`, `friendflow.cpp`

## Controls and screen
- **A connected controller becomes player 1**, the Deck's controls player 2, so one-player systems
  work with a controller. `players.cpp`
- **A friend streaming in is seated before the Deck's own controls** when a controller is player 1
  (controller, friend, Deck), so the friend still has a port on two-port systems like PlayStation 2.
  The Deck alone with a friend stays Deck, friend. `players.cpp`
- **One controller is kept free for a friend** when a standalone-emulator game (Switch, PS3, ...)
  starts with Streaming on, so a friend who connects after the start still has a controller.
  `standalone_play.cpp`
- **Gyro works on the Deck in Switch games** (Eden reads the Deck's gyro server).
  `standalone_eden.cpp`. External-controller gyro is parked (section 7).
- **The left stick moves focus in the menus**, like the d-pad (it was d-pad only). Not in a game.
  `input.cpp`, `app.h`
- **Rumble works on the Deck's controls**, with a log line for the first rumble. `rumble.cpp`
- **Touchscreen in the menus**: taps, drag, swipe in from the left edge for Back; Search results.
  `touchinput.cpp`, `tapspots.h` (new), `input.cpp`, `bar.cpp`, `searchscreen.h`, `home.cpp`,
  `libraryscreen.*`, `grid.h`
- **Volume and brightness bar** when the Deck's keys are pressed. `levelbar.cpp`, `levelbar.h` (new),
  `volume.cpp`, `volume.h`

## Battery and power
- **The Deck's battery in the top bar**, a low-battery notice, and labels over each battery.
  `battery.*`, `batteryrow.*`, `players.cpp`
- **Power profile per game**: full speed for heavy systems, battery-biased for Home and light ones.
  `powerprofile.cpp`, `powerprofile.h`, `frame.cpp`
- **Sleep when idle on battery** (menus only, unplugged only). `idlesleep.h`, `idlesleep.cpp` (new)
- **Charge limit** (80, 90 or none; only on a Deck). `chargelimit.h`, `chargelimit.cpp` (new),
  `system_files/usr/libexec/cabinetos-charge-limit`, its unit and polkit rule
- **Dim screen after** 2, 3 or 5 minutes, before the screen turns off. `idle.h`, `settings.*`

## Library
- **No tile for a system with no games.** `library.cpp`
- **What's New shows two QR codes**: the original's and the fork's. `qrlink.*`

## Tested on
- Steam Deck OLED: tested.
- Steam Deck LCD: not tested.

## Known issues
- **Silent on the first boot of a new image** (after an install or a `bootc switch`); sound is back
  after one reboot. Cause unknown (section 4.2).
- First boot needs internet for about two minutes while Eden and xemu install.
- Untested on a Deck (tested on mobile devices and Windows PCs): Join a friend, Play with a friend, the streaming quality drop, the
  Sunshine sound patch (needs an image build).

## To do
- Test streaming: PS2 with the quality drop, a controller on the phone, a friend over Tailscale,
  two Moonlight guests at once, player order with a pad and a phone.
- Test Join a friend on two Decks.
- Back buttons and trackpads; Steam Deck LCD.

DETAILS:
1. Base image
The Containerfile builds on ghcr.io/ublue-os/bazzite-deck, not plain bazzite, and is pinned to a specific version.
Why: the Deck image carries the hardware support a handheld needs. Packages seen on the device: steamdeck-dsp (speaker tuning), inputplumber (built-in controls), powerstation (power and TDP), jupiter-fan-control, galileo-mura (OLED panel correction), alsa-ucm, jupiter-hw-support-btrfs, and the Deck gyro server sdgyrodsu.
The audio configuration in the image also carries hardware profiles for both Deck models (valve-jupiter for the LCD, valve-galileo for the OLED) and sound card settings for acp5x (LCD), acp6x and sof-nau8821-max (OLED). Only the OLED has been tested.
Upstream's strip-steam.sh and install-steam-session.sh are used as upstream has them. Their calls in build.sh use bash, because files created in GitHub's web editor are not executable.
ci/base-watch.txt has an added "Steam Deck hardware" area, so a base update that changes steamdeck-dsp, galileo-mura, jupiter-* or powerstation is flagged as worth reading.
2. Names, signing and the registry

CabinetOS signs every image it publishes and refuses unsigned updates, so a fork needs its own identity. Everything that names the original owner was changed:

cabinetos.env: the image owner.
build_files/require-signed-updates.sh: the repository and the policy rule.
system_files/etc/containers/registries.d/cabinetos.yaml: where signatures are found.
disk_config/iso.toml: the image an installed console updates from.
cabinetos-report: the list of names it never scrubs.
The image path is lowercase: ghcr.io/yaboi024/cabinetos.

Registry notes:

The package must be Public, so installed consoles can pull updates and the version numbering can read the existing tags.
ci/next-version.sh --strict was removed for the very first build, when no image existed yet, and restored afterwards. Strict mode refuses to publish if the tags cannot be read, which protects the version numbering.
Actions needs read and write workflow permissions to publish to the registry.
3. Console updates (SELinux)

Symptom. The console's update button downloaded the image and then failed to install it, while sudo bootc upgrade from a shell worked.

Cause. The update runs as a systemd service (cabinetos-update-download.service). With no SELinux setting of its own it runs as unconfined_service_t, which is denied the mac_admin permission. Unpacking an image has to set SELinux labels that the running policy may not know yet, and without that permission the kernel answers fsetxattr(security.selinux): Invalid argument. An administrator's shell has the permission, which is why the manual update worked.

Confirmed by. Setting SELinux to permissive for one update made the console's update succeed. Running the update script with systemd-run failed with the default context and succeeded with SELinuxContext=unconfined_u:unconfined_r:unconfined_t:s0, with SELinux enforcing.

Fix. A drop-in beside upstream's unit, so a sync cannot conflict with it:

system_files/usr/lib/systemd/system/cabinetos-update-download.service.d/10-selinux.conf

[Service]
SELinuxContext=unconfined_u:unconfined_r:unconfined_t:s0

Notes.

The service already ran with no confinement, as root; the new context matches what an administrator's shell has.
If upstream renames the unit, the drop-in silently stops applying and updates fail again. The daily watch workflow reports a drop-in whose unit disappeared from the repository.
Fallback for any machine: sudo bootc upgrade, then reboot.
The image is about 7.5 GB in total. An update only downloads the layers that changed, but mind slow Wi-Fi.
4. Audio
4.1 Docked sound stayed on the Deck's speakers

Symptom. With the dock connected and video on the TV, sound kept playing from the Deck.

Cause. The console's audio code uses SDL. SDL's PipeWire driver names a specific output when it opens the stream (target.object) and, in this setup, did not follow later changes to the default output. The stream was tied to the Deck's speaker filter (filter-chain-sink, part of the Deck's speaker tuning, which is attached to the speaker by a "smart filter" rule). The system's default output was correctly HDMI the whole time.

Fix. The session unit gets an environment setting, as a drop-in:

system_files/usr/lib/systemd/system/cabinetos-session.service.d/10-audio.conf

[Service]
Environment=SDL_AUDIODRIVER=pulseaudio,pipewire

SDL then uses its PulseAudio-compatible driver, which does not name an output, so the stream follows the default. pipewire is the fallback if that driver cannot start. Verified on the OLED: sound moves between the TV and the speakers by itself in both directions and the speakers still sound tuned.

Programs the console launches inherit the setting. The Flatpak emulators (Eden, xemu) run in their own environment. Other emulators have not all been checked on the TV.

4.2 First boot
On the very first boot after an install, audio and the Eden emulator may be missing. After one reboot they appear. Seen again on 2026-10-29 after moving a Deck onto this fork with `bootc switch`, so it is the first boot of a new image, not only of a fresh install.
Eden and xemu are installed from Flathub by cabinetos-flatpak-setup on first boot. It took about two minutes and needs internet.
Why audio is silent on the first boot is not known. Ruled out: the "Failed to enable UCM device Speaker" message (it also appears on boots where audio works), saved ALSA mixer state, and WirePlumber's saved state.
5. Controllers: player order

Symptom. A second controller (an 8BitDo, a Switch Pro, an Xbox pad) worked in the menus and in Switch games, but did nothing in one-player systems such as Game Boy Advance.

Cause. The console seats controllers in the order they connect. The Deck's built-in controls (seen by SDL as "Steam Deck Controller") are always there first, so they held player 1 and a connected controller became player 2. One-player systems listen to player 1 only.

Change. frontend/src/players.cpp:

isBuiltIn() recognises the Deck's controls (Valve vendor 0x28de, product 0x1205, or the name "Steam Deck Controller"). The LCD and OLED share these IDs.
putAheadOfBuiltIn() runs after a pad is seated and moves it ahead of any built-in pad, using the existing Seats::swap.

Behaviour.

A controller connected outside a game becomes player 1; the Deck's controls become player 2.
When it leaves, the seats close up and the Deck's controls are player 1 again.
During a game nothing is reordered (swap refuses), and a controller that drops out leaves its seat held and pauses the game. That is upstream's design for multiplayer and was left alone.
Check with: journalctl -b -t cabinetos-session -o cat | grep "\[players\]".
6. Battery

Symptom. The top bar shows a battery for each controller, but nothing for the Deck itself.

Cause. The battery code reads the kernel's power-supply entry belonging to each pad, and keeps only entries whose scope is Device. The Deck's controls have no battery of their own; the machine's battery (BAT1, no scope file) was deliberately skipped.

Change.

battery.cpp: scan() also keeps the machine's own battery (type Battery, name BAT*, scope not Device) under the fixed key "system". battery.h declares systemDir().
players.cpp: when a pad is the Deck's built-in controls, its battery directory is battery::systemDir().

Behaviour. On Home, Library, Search and Settings (never in a game) the bar shows the label (P1, P2, ...) with a four-segment battery and a bolt while charging. Green at three or four segments, amber at two, red at one (25% or less), which also raises the existing low-battery notice. The reading updates when the kernel reports a change; if it ever lags, a once-a-minute re-read for this one battery would fix it. Check with grep "\[battery\]" in the same journal.

7. Gyro
7.1 What works

The Deck's own gyro works in Switch games (Eden). Two pieces make that happen:

A gyro server from the base image. sdgyrodsu is a systemd user service (/usr/lib/systemd/user/sdgyrodsu.service, enabled) that reads the Deck's controller and serves motion over the Cemuhook (DSU) protocol on UDP port 26760. It serves the Deck only, on slot 0.
Eden is told to use it. frontend/src/standalone_eden.cpp writes these settings before every Switch game: enable_udp_controller=true, motion_enabled=true, udp_input_servers=127.0.0.1:26760, and for each player motionleft and motionright set to engine:cemuhookudp,guid:<127.0.0.1 as hex>,port:26760,pad:<player>,motion:0. Eden's default is off, which is why gyro did nothing before.
7.2 Why emulators need a server at all

The console gives emulators virtual controllers (vpad.h) so that button numbering cannot be scrambled by different SDL versions. Those virtual controllers carry buttons, sticks and rumble but, as upstream says plainly, no motion. Gyro therefore has to reach the emulator by another path.

7.3 The console's own motion server (parked)

Added so that external controllers could have gyro too. It is in the repository and does nothing while sdgyrodsu holds the port.

Files: frontend/src/motionserver.h, motionserver.cpp; one call to cab::motion::pump(...) after each of the two cab::vpad::pump(...) calls in standalone_play.cpp.
It serves a DSU server on 127.0.0.1 (default port 26760). Slot N is the controller seated as player N, if it has a gyro. It binds only if the port is free, and logs [motion] cannot bind ... when it is not.
Settings it reads from the console's environment (put in /etc/cabinetos/session.env, then restart the session): CABINETOS_MOTION_PORT, CABINETOS_MOTION_GYRO, CABINETOS_MOTION_ACCEL (three signed axes such as +x+y-z), CABINETOS_MOTION_GUID, CABINETOS_MOTION_DEBUG=1.
It works: with sdgyrodsu stopped, Eden connected to it, and an 8BitDo connected over Bluetooth (SDL reports gyro: yes; over its 2.4 GHz dongle SDL reports gyro: no) delivered gyro.
Its axis conventions were not solved. Findings, for whoever resumes:
The identity mapping gave correct up and down but wrong left and right, and reversed gravity.
Comparing a seven-step recording of both servers on the Deck, the big readings matched with gyro +x+y-z and accelerometer -x-y-z. In a Switch game aim then felt right but left and right were still swapped, so something other than the signs differs from sdgyrodsu.
A published SDL-to-DSU convention (gyro -x+y-z, accel -x-y-z) felt inverted and less accurate.
Packet rate was not the problem: about 300 packets a second, like sdgyrodsu.
Decision: leave gyro as it is. The Deck's gyro comes from sdgyrodsu; external-controller gyro is parked.
7.4 If external-controller gyro is revisited
Run both servers at once (CABINETOS_MOTION_PORT=26761 for the console's), with the Deck as the only controller and its slot in view. Recording both while the Deck is waved around and correlating the fields (the comparison scripts, section 11) gives the exact axis map instead of a guess.
Remember that player order matters: the console's server reads slot N from player N, and a held seat gives zeros.
Ship it only together with a user-unit drop-in that stops sdgyrodsu: system_files/usr/lib/systemd/user/sdgyrodsu.service.d/10-cabinetos-off.conf containing [Unit] and ConditionPathExists=!/. (Not created.)
7.5 Known quirk

sdgyrodsu serves the Deck's gyro on slot 0 only, so in Eden the Deck's gyro drives player 1 even when an external controller holds player 1.

8. Staying in sync with upstream
.github/workflows/watch-upstream.yml runs daily and on demand. It fetches upstream, compares changed files against .github/upstream-watch.txt, and opens or updates one issue, "Upstream changes to review", listing watched files that changed. It closes the issue when nothing differs. Issues must be switched on for the fork.
A second step opens "A fork drop-in has lost its target" if a .service.d folder in this repository no longer has its unit upstream. It ignores units that come from packages (for example tuned or tailscaled), which it cannot see.
