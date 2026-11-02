# Roadmap

**Released: CabinetOS 2026.10.10**, the first public release, on GitHub with
the installer. Since then work comes from what MMagTech raises and what
people report; `docs/NEXT-SESSION.md` holds what is being worked on now.

Anything with no date is in the **Later** milestone, kept so it is not lost
and worked on only when MMagTech raises it (named "After first release" until
2026-10-12). A new issue with no date goes into it when it is filed. The
release-gate tests, **8. Testing at the TV**, are done.

**How the release was reached**, below: the order set with MMagTech on
2026-09-28, one numbered milestone per step (all closed by 2026-10-12, all
done).

Milestones: https://github.com/MMagTech/cabinetos/milestones

The reasoning behind any step lives in `docs/PROJECT.md`; what the next
session picks up is `docs/NEXT-SESSION.md`.

## In order

1. **Emulators. Done 2026-09-28.** PS2 on a fresh install (#153) was fixed;
   the Dreamcast crash (#151), full CPU speed during games (#150, measured: no
   visible benefit on the A9) and rewind's snapshot cost (#168) moved to After
   first release.
2. **New systems**, in MMagTech's order: the shared foundation for emulators
   that run as their own programs (#169), then Switch (#170), PS3 (#171), Xbox
   (#172), Xbox 360 (#192), Wii (#173) and Wii U (#174). Xbox 360 moved ahead
   of the Wiis on 2026-09-29, after it ran well on the A9. Before picture quality, because that
   is one setting across every system.
3. **Settings: picture**, in this order (set with MMagTech 2026-10-02):
   1. ~~**#63 phase 1**, with #73, #204, #209 and #217.~~ **Merged in #225,
      2026-10-02.**
   2. ~~**Wii Remotes (#200)**.~~ **Merged in #232, 2026-10-03**: pair once,
      the Wii bridge in the image, judged on the A9 TV.
   3. ~~**Steam (#223)**~~ **Built on `steam-handoff` and judged on the A9
      TV, 2026-10-03/04**: "Switch to Steam" in the Start menu, its own
      slice of the drive (PROJECT.md open question 37).
   4. ~~**PS2's picture without the copy (#226)**~~ **Merged in #239,
      2026-10-04**: its own window, as the separate emulators are shown;
      Quality 4x with 16x filtering. Upstream declined the other route
      (PCSX2/pcsx2#15040).
   5. ~~**ext4 for extra drives, shared with Steam (#236)**~~ **Merged in
      #240, 2026-10-04.**
   6. ~~**#63 phase 2**, the machine class, with #209 (vsync on).~~
      **Built on `machine-class` and judged on the A9 TV, 2026-10-04**: the
      starting level from the graphics chip, N64 Balanced 2x, PS3 Quality
      1080p, Xbox 360 720p. An Ultra level for stronger machines is #245.
   7. **Shaders and CRT looks (#122)**, moved ahead of phase 3 (MMagTech,
      2026-10-04: "at least that's something fun"). Discussed before
      anything is built; not a copy of Cabinet's.
   8. **Before phase 3:** the slowed A9 (#210), the drop rule ignoring rewind
      snapshot frames (#168), whether built-in cores repeat or drop frames
      (#221).
   9. **#63 phase 3**, the drop rule, record-only first.
   Also in the milestone: the gamescope flag check on base updates (#227).
4. **Offline play.** Kept games play with no server (#88).
5. **RetroAchievements** (#74). Each account signs in with its own login.
   After offline, because achievements earned offline must wait and send.
6. **UI polish.** The wording pass and notification length (#110), then
   sound dying until a restart (#228). Sleep (#132) moved to After first
   release.
7. **Ready to ship.** Done: the Mii Channel and Miis (#275), rumble kept
   across Wii games (#274), the installer down to one question (#105 to #107,
   #136; what the ISO builder cannot change is #284, after first release) and
   Developer access, one image for everyone (#134). The Nintendo DS stays
   (#248). Remote Play, at home and away through Tailscale (#286, pull
   request #292), into Steam (#296, #298). The controller battery on Home
   (#293). CabinetOS named in the system's version info (#137, #252). A
   stream started while the screen is asleep (#294), built on
   `asleep-stream`. The Bazzite base 44.20261006.1 with base-update pull
   requests listing their own checks (#301, #302) and the controller list
   (#266). Then, in the order MMagTech set on 2026-10-09, all done by
   2026-10-10: the stream into Steam going black on the way back to Home
   (#304, "Stream into Steam: leaving Steam back to Home turns the phone's
   picture black"); signed images (#135, "Sign the images"), first because a
   mistake there breaks every console's updates and it needs several image
   updates to prove itself (`build_files/require-signed-updates.sh`); the
   diagnostic report (#195, "Settings: create a diagnostic report, with a QR
   code"), a feature everyone installing can use to report bugs; licences
   checked and shipped (#120, "Licences: check every line against its
   source, and ship the full texts").
8. **Testing at the TV.** The release gate: covers and art on the TV (#112), a
   PS2 save reaching the server (#113), Cabinet on Apple TV loading our states
   (#114), players 3 and 4 (#115), the owed hardware tests (#140), and last,
   the first-hour walk-through as a new user on the real installer (#111):
   **the last step, on the release stick, after Remote Play** (MMagTech,
   2026-10-07: the release image may need things added), wiping the A9.
   Done; the milestone closed on 2026-10-12. #114 ("Cabinet on Apple TV
   loads save states made on CabinetOS") was closed by decision instead:
   since 2026-10-11 Cabinet on Apple platforms is no longer a constraint.

**Later** (was After first release): everything in that milestone on
GitHub. The items this page once named there are all closed. Shipped:
#150 ("Use Bazzite's performance profile during a game, balanced on Home"),
#163 ("Keep the emulators on the fast CPU cores") and #154 ("PS2: ship
PCSX2's per-game fixes (patches.zip)"). Built in a milestone of its own:
#162 ("Multi-disc games: Change disc in the pause menu, one save per game
(Saturn first)"). Closed by MMagTech's decision, most of them to be reopened
on a report: #151 ("Dreamcast crashed on load after an arcade game with a
143 MB save state"), #168 ("Rewind: measure the snapshot cost per system,
and a setting to turn rewind off"), the system tuning ideas #164 ("Explore:
hold background system jobs while a game runs"), #165 ("Consider turning
off the kernel's lockup watchdog (nmi_watchdog)"), #166 ("Consider
real-time priority for gamescope") and #167 ("Check audio latency: what the
console asks PipeWire for"), #230 ("Controller batteries: the Wii HOME menu
shows an empty battery, and the console shows none") and #231 ("Wii U: real
Wii Remotes in Cemu"). Declined or superseded, the RomM 5.2 and 5.3
features: #158 ("Surprise me: play a random game (RomM 5.2)"), #159
("Similar games and recommendations from RomM (5.3)"), #160 ("Use RomM's
title_id and save_target for save matching (5.3)") and #161 ("Ask RomM only
for BIOS files it still has (5.3)"). Each issue says which.

Done: Settings (#57 to #72, except #63), the emulator block (#82 to #90), the
in-game features (#76 to #80).

## Decided against

Kept here so nobody proposes them again: Atari Jaguar and ColecoVision (their
number-pad controllers), HDMI-CEC (shelved 2026-09-24), static IP, screen
recording (encoding cost on integrated graphics, and clips would compete with
the game cache), an Options level in the pause menu (2026-10-25: the
setting rows stay on its top level, changed with Left and Right; worth another
look only when a system has three or more), and a download queue (2026-10-28,
#600: the console is about finding a game and enjoying it; a game is fetched
from RomM when it is played and kept in the cache).
