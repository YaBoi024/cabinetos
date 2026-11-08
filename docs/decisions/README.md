# Decisions

What the project decided and why, one small file for each decision. The older ones are in [docs/PROJECT.md](../PROJECT.md), frozen on 2026-10-23: look there when nothing here covers the question.

## When to write one

When you or MMagTech chose between real options and a later reader would ask why it is this way. Not for a bug fix, a choice with no alternative, research, a fault or a build log. Settings decisions stay in [docs/SETTINGS.md](../SETTINGS.md) and "Decided against" stays in [docs/ROADMAP.md](../ROADMAP.md); this folder is for the rest.

## The file

Named `YYYY-MM-DD-short-name.md`, at most 60 lines, in this form:

```
# The decision in a few words

- **Decided:** what, in one to three sentences.
- **Date:** YYYY-MM-DD
- **Why:** the reason a later reader needs.
- **Numbers:** the issues and pull requests, each with its title, or "none" for a decision made at the TV.
- **Replaces:** the decision it replaces, or "none".
```

A decision moved from docs/PROJECT.md names its old question number under Numbers, and the one-line pointer left in PROJECT.md names the new file.

A decision that no longer holds is rewritten or deleted in the same pull request (CLAUDE.md, rule 5), and its line in the index with it.

## Index

One line per file, under the area it belongs to, in the form `- [name](file.md): the decision in a few words.`

### Emulators and cores

- [when-an-emulator-pin-moves](2026-10-23-when-an-emulator-pin-moves.md): a pin moves only for a major feature, a bug or slowness we have seen, more games playing, a patch fixed upstream, or a move we do not choose.
- [3ds-games-get-no-network](2026-10-20-3ds-games-get-no-network.md): a 3DS game never reaches a server named in the game; by a patch to Azahar until 2026-10-26, by the block for every emulator since.
- [emulators-get-no-network](2026-10-26-emulators-get-no-network.md): one block keeps every emulator off the network, inside the console's program by who asked and outside it by starting each with no network.
- [arcade-emulator-from-the-zip](2026-10-28-arcade-emulator-from-the-zip.md): an arcade game's emulator comes from its zip's name and files, in a fixed order, never from its folder.
- [emulator-updates-and-saves](2026-10-28-emulator-updates-and-saves.md): every emulator update is taken to break saves and states, so it needs a reason, a run first and a changelog warning.
- [prebuilt-mame-core](2026-10-28-prebuilt-mame-core.md): MAME 0.289 is libretro's own build, the one core we do not build.
- [model-2-and-model-3](2026-10-28-model-2-and-model-3.md): Model 2 plays in sm2-emu and Model 3 in Supermodel, both built into the image.
- [model-3-net-board-linked-to-itself](2026-10-29-model-3-net-board-linked-to-itself.md): Model 3's net board is on, linked to itself, inside the empty network every emulator gets.
- [xbox-360-unreal-engine-3-fix](2026-11-01-xbox-360-unreal-engine-3-fix.md): every Xbox 360 game draws at the Xenos sample positions with the window offset in the viewport, so Unreal Engine 3 games lose their black patches.
- [dirt-devils-supermodel-patch](2026-11-01-dirt-devils-supermodel-patch.md): a patch to Supermodel runs a machine linked to itself as the only cabinet, so Dirt Devils plays.
- [ps3-filtering-as-the-game-asked](2026-11-06-ps3-filtering-as-the-game-asked.md): on PS3 the anisotropic filter is the game's own, not the forced 16x, and buffers up to 320 pixels stay unscaled, so Unreal Engine 3 games draw without red.
- [rpcs3-settings-database-snapshot](2026-11-06-rpcs3-settings-database-snapshot.md): the RPCS3 team's per-game settings ride in the image as a committed snapshot, refreshed by a tool only when RPCS3's pin moves, held to that pin by the build and CI, never fetched by the console or the build.

### Controls

- [wii-remote-outside-wii-games](2026-10-24-wii-remote-outside-wii-games.md): a Wii Remote starts only Wii games, holding HOME on one opens the pause menu in every game, and a game's line says *Needs a controller* or *Needs a Wii Remote* when the controller it needs is off.
- [fight-stick-layout](2026-10-24-fight-stick-layout.md): the 8BitDo Arcade Stick over Bluetooth plays arcade games with a fixed fight-stick layout, and every other pad as before.
- [hold-start-opens-arcade-settings](2026-10-25-hold-start-opens-arcade-settings.md): holding Start in an arcade game opens the emulator's own menu, never one of the console's.
- [no-remapping](2026-09-26-no-remapping.md): no button remapping on any system but arcade games, which have Controls.
- [arcade-controls](2026-10-25-arcade-controls.md): an arcade game's pause menu has a Controls row that moves its buttons, per game, kind of controller and person.
- [model-2-and-model-3-controls](2026-10-30-model-2-and-model-3-controls.md): Model 2 and Model 3 games with buttons have Controls, numbered from their emulator's list, played from the next start.
- [rewind-only-with-shortcuts](2026-10-29-rewind-only-with-shortcuts.md): a game keeps a rewind history only while In-game shortcuts are on, and rewind has no switch of its own.
- [ds-3ds-screen-view](2026-10-26-ds-3ds-screen-view.md): L3 shows a DS or 3DS game's top screen alone, the bottom alone, then both, and the DS's lid moves to R2.
- [controls-guide-is-a-pdf](2026-10-27-controls-guide-is-a-pdf.md): the printable one-page controls guide is a PDF on the wiki, made from an HTML file beside it.
- [a-wake-is-not-a-choice](2026-10-27-a-wake-is-not-a-choice.md): the presses that come with a wake do nothing in a menu: the first on a dimmed screen, and every press for 3 seconds after the screen was off, after Sleep and after a device starts streaming.

### Sound and display

- [one-sound-rule](2026-11-04-one-sound-rule.md): the screen the picture is on plays the sound, an output that reports a plug takes over, nothing else; one script reads what each output reports, on every PC.
- [sound-backup-output](2026-10-26-sound-backup-output.md): the image starts pipewire-pulse at login, so a backup output catches the console's sound while the TV's is gone.
- [games-draw-on-the-screens-gpu](2026-11-06-games-draw-on-the-screens-gpu.md): on a PC with two GPUs, games draw on the one the screen is on, matched by device UUID; one GPU is unchanged; the old order is the fallback.

### LinkUp

- [linkup-streams-the-game](2026-11-02-linkup-streams-the-game.md): a friend plays by streaming, with GStreamer's webrtcbin fed by LinkUp's own capture, not by an emulator's netplay or Remote Play.
- [linkup-built-whole](2026-11-02-linkup-built-whole.md): LinkUp is designed, built and tested as one piece and ships once, in its final form.
- [linkup-needs-romm](2026-11-03-linkup-needs-romm.md): both consoles need a RomM server, theirs or a shared one, and LinkUp is greyed while the console cannot reach it.
- [linkup-own-program-and-hard-off](2026-11-03-linkup-own-program-and-hard-off.md): LinkUp is a program of its own, off on every new console, and a firewall table of its own blocks it while it is off.
- [linkup-self-run-server](2026-11-03-linkup-self-run-server.md): the server is a container anyone runs, from a repository of its own; the project runs no service.
- [linkup-server-in-go-with-pion](2026-11-03-linkup-server-in-go-with-pion.md): the server is one small Go program with Pion's relay inside, in a nearly empty container, and not coturn.
- [linkup-one-server-per-console](2026-11-03-linkup-one-server-per-console.md): a console has one server, set by the admin; friends belong to an account on the server, and only its owner makes friends.
- [linkup-servers-link-two-accounts](2026-11-03-linkup-servers-link-two-accounts.md): linking two servers connects the two accounts picked and nobody else, with no chain to a third.
- [linkup-pairing-by-the-owners-code](2026-11-03-linkup-pairing-by-the-owners-code.md): the owner makes a code that works once for 7 days, each side proves it with a slow hash and the code never travels.
- [linkup-pin-guards-what-is-new](2026-11-03-linkup-pin-guards-what-is-new.md): with a PIN set, turning LinkUp on, changing the server and Leave ask for it, and playing with friends asks nothing.
- [linkup-works-like-a-friends-house](2026-11-04-linkup-works-like-a-friends-house.md): a game starts by inviting an Online friend and waiting for them to join, and the console never chases anyone.
- [linkup-friend-sees-only-the-game](2026-11-04-linkup-friend-sees-only-the-game.md): the friend sees only the game and "Waiting for" the host whenever the host's console shows anything else.
- [linkup-friend-is-player-two](2026-11-04-linkup-friend-is-player-two.md): the friend is always player two with no third seat, and the game, its saves and its achievements stay the host's.
- [linkup-no-voice-chat](2026-11-04-linkup-no-voice-chat.md): LinkUp has no voice chat, ever.
- [linkup-one-at-a-time-with-remote-play](2026-11-04-linkup-one-at-a-time-with-remote-play.md): Sunshine is stopped for the length of a LinkUp session, and friends grey during a Remote Play stream.
- [linkup-pause-and-drops](2026-11-05-linkup-pause-and-drops.md): either console's pause stops the game for both, and a drop over about 2 seconds pauses it with "Reconnecting".
- [linkup-session-outlives-a-game](2026-11-05-linkup-session-outlives-a-game.md): quitting a game keeps the session for the next one, and a friend leaving ends it.
- [linkup-idle-time](2026-11-02-linkup-idle-time.md): after 10 minutes with no press the game pauses on both screens, and 15 more end the session.
- [linkup-game-list](2026-11-05-linkup-game-list.md): the list leaves out games a friend cannot play, and one-player games stay.
- [linkup-status](2026-11-05-linkup-status.md): Status is each account's, and an account also shows Offline after 15 minutes with no press, with the screen off, asleep or in Steam.
- [linkup-friends-list](2026-11-05-linkup-friends-list.md): friends are ordered by status, each has a colour worked out from their name, and the empty screen stays.
- [linkup-where-it-lives](2026-11-05-linkup-where-it-lives.md): a top-bar screen and rows in Settings' Streaming section, and while LinkUp is off only its switch shows.
- [linkup-versions-and-the-pinned-server](2026-11-04-linkup-versions-and-the-pinned-server.md): the protocol only adds, a console names who is behind, no update is forced, and CI runs against one pinned server image.
- [linkup-paths-tried-together](2026-11-03-linkup-paths-tried-together.md): IPv6, a reachable port, hole punching and the relay are tried together by ICE, and a player never picks one.
- [linkup-direct-game-shows-addresses](2026-11-05-linkup-direct-game-shows-addresses.md): everything is encrypted, a direct game shows each console the other's address, and the server never learns what anyone plays.
- [linkup-ports-mapped-by-the-program](2026-11-04-linkup-ports-mapped-by-the-program.md): the program maps its own port with a lease it renews, libnice's UPnP is off, and the ICE agent keeps to fixed ports.
- [linkup-stun-in-the-container](2026-11-03-linkup-stun-in-the-container.md): the STUN list lives in the server, Cloudflare then Google by default, in an order the owner sets.
- [linkup-relay-in-the-container](2026-11-03-linkup-relay-in-the-container.md): the relay is a program in the server, off until the owner turns it on, and it is the answer for carrier-grade NAT.
- [linkup-any-network](2026-11-03-linkup-any-network.md): LinkUp works over any network and is built around none, with nothing built or promised for Tailscale or WireGuard.
- [linkup-h265-up-to-1080p](2026-11-02-linkup-h265-up-to-1080p.md): the picture is H.265 at the host's own size up to 1080p, with H.264 as the fallback.
- [linkup-one-bitrate-rule](2026-11-03-linkup-one-bitrate-rule.md): one rule for every game, a steady bitrate up to 18 Mbit/s with a quality floor, and no tiers by system.
- [linkup-picture-floor](2026-11-07-linkup-picture-floor.md): a LinkUp picture never goes below 720p at 3.5 Mbit/s; below that both consoles say *Slow connection* and the game carries on.
- [linkup-friends-picture-in-opengl](2026-11-05-linkup-friends-picture-in-opengl.md): the friend's picture is drawn with OpenGL, because Vulkan costs 20 to 40 ms more delay to be as smooth; emulators keep Vulkan.
- [linkup-friend-sound-queue](2026-11-08-linkup-friend-sound-queue.md): a friend plays the sound from LinkUp's own queue at the host's pace, keeping 40 ms, more after a Wi-Fi stall, at most 100.
- [linkup-host-sound-through-pulse](2026-11-08-linkup-host-sound-through-pulse.md): a host captures its sound with pulsesrc, because pipewiresrc lost more and more of it on the mini PC.
- [linkup-server-small-surface](2026-11-03-linkup-server-small-surface.md): the server holds no logins and has no public page, runs without admin rights, and its worst case cannot reach anyone's RomM.
- [linkup-server-log-without-names](2026-11-03-linkup-server-log-without-names.md): the server's log is a capped file in its data folder and never holds codes, keys, addresses, usernames or game titles.
- [linkup-admin-page](2026-11-06-linkup-admin-page.md): the admin page has no setup code, a layout for phones and for computers, CabinetOS's look with four themes, and Copy as its only button.

### RomM and the library

- [arcade-discs-one-folder](2026-10-28-arcade-discs-one-folder.md): an arcade game with a disc is one folder with its CHD at the top, the only layout RomM documents.
- [arcade-sets-non-merged](2026-09-27-arcade-sets-non-merged.md): arcade sets are non-merged, for every emulator.
- [one-arcade-tile](2026-10-29-one-arcade-tile.md): every arcade folder is one Library tile, Arcade, whose grid names each game's folder; everywhere else an arcade game's system is Arcade.
- [romm-owns-management](2026-10-21-romm-owns-management.md): the console uses what RomM offers and never manages it: no gallery, list or manager of its own.
- [game-gone-from-romm](2026-11-01-game-gone-from-romm.md): a game deleted in RomM leaves the console at the next start with the server there, a played copy or a download, with its saves, states and screenshots.

### Screens and settings

- [focused-game-names-its-system](2026-10-25-focused-game-names-its-system.md): the focused game's line names its system on Home, in a collection and in Search, and not in a system's own grid.
- [no-ui-for-the-few](2026-10-13-no-ui-for-the-few.md): an option only a few players would use never goes on Home, a grid or a game's page.
- [stop-a-download-from-its-row](2026-11-06-stop-a-download-from-its-row.md): A on the row that is filling asks "Stop downloading?", Continue focused; no other button stops a download.

### The image and updates

- [no-help-pages](2026-11-04-no-help-pages.md): the image carries no manual pages and no job rebuilding their index, since nothing on a console reads them.
- [access-password-is-the-admin-password](2026-11-01-access-password-is-the-admin-password.md): File access and Developer access share one password that is also the console user's admin password, readable by the console's own programs; accepted for now.
- [sinden-software-in-the-image](2026-11-02-sinden-software-in-the-image.md): Sinden's light-gun software ships in the image with Sinden's permission, and source Sinden sends for its camera library is never shared.
- [first-time-setup-in-the-os](2026-11-03-first-time-setup-in-the-os.md): the account and the update source are the image's, not the installer's, so the ISO and a disk image share one copy; a new console follows `latest`, signed, and one that updated is never moved.
- [running-from-a-usb-drive](2026-11-03-running-from-a-usb-drive.md): a portable install hides the PC's own drives through udisks, fills its USB drive at its first start, asks the time zone on first run's first screen, and keeps the PC's clock in local time as Windows does.
- [portable-disk-image](2026-11-03-portable-disk-image.md): the file a tester writes is one `.img.xz`, cut to what it holds after the builder, booting as an ISO install does, adding nothing to the PC's boot list, and booted by CI before a run can pass.
- [portable-first-start](2026-11-04-portable-first-start.md): what a first start would write on a USB stick is done when the file is made, or waits: the boot setting, the update source, the filesystem's layout, and the emulators, 15 minutes in at idle priority; the log stays on.
- [portable-for-others-as-an-alpha](2026-11-04-portable-for-others-as-an-alpha.md): anyone may try the portable install from its wiki page, which calls it an alpha and lists what has and has not been tried.
- [portable-secure-boot](2026-11-04-portable-secure-boot.md): with Secure Boot on, a portable drive brings up shim's key screen by itself and the tester enrols the key once per PC; nobody is asked to change Secure Boot, and everybody is told to save their BitLocker key first.
- [wifi-scans-and-power-saving](2026-11-08-wifi-scans-and-power-saving.md): Wi-Fi scans only when the signal drops below -70 dBm, then every 6 minutes, in place of NetworkManager's every 5 minutes on a home with two access points; Wi-Fi power saving is off.

### Building and checking

- [installer-iso-repacked](2026-11-02-installer-iso-repacked.md): the installer ISO is repacked after the builder, for CabinetOS's boot menu and look, with nothing the builder made rebuilt.
- [portable-download](2026-11-05-portable-download.md): the portable's download is the file from a finished run, which was tried, not a new build; it sits beside the ISO on R2 for about 4 cents a month.

### How we work

- [comments-and-docs-held-by-a-check](2026-11-03-comments-and-docs-held-by-a-check.md): comments and docs are kept true by a CI check on what each pull request touches and a read-only skill, not by a new rule.
- [declined-patch-gets-no-issue](2026-11-01-declined-patch-gets-no-issue.md): a patch upstream declined, or one for no upstream fault, has no open issue; its row in docs/ARCHITECTURE.md section 8, its decision file and its recipe are its record.
- [wiki-side-list-groups](2026-10-29-wiki-side-list-groups.md): the wiki's side list has five groups, Set it up, Playing, Systems, Settings and Help, each with one rule for what goes in it.
