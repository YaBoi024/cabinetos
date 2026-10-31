// Which of a RomM library's platforms this console can actually play.
//
// A RomM server holds whatever its owner put in it. CabinetOS ships a fixed set
// of cores, so the two do not match and the gap is not small: the reference
// library has 35 platforms and six of them — Jaguar, ColecoVision, Switch, PS3,
// Vita, Wii — have no core in Cabinet's manifest at all.
//
// A console must not offer a game it cannot run. That is a promise it breaks in
// the worst possible place, after the person has chosen something and is waiting
// for it. But a library that silently drops 240 games is its own bug: someone
// who owns Switch games and sees none will reasonably conclude the scan failed.
//
// So the answer is neither "show everything" nor "hide quietly" — it is to KNOW,
// per platform, and to say so. This is the knowing part.
//
// Keyed on `slug`, because it travels between servers whereas `id` does not —
// the opposite of identity WITHIN one server, where `id` is the only unique
// field. An arcade game is the exception: its emulator comes from its zip,
// never from its folder (arcadematch.h).
//
// What each core saves, the tags a save and a state travel under and the
// firmware a core can find are in savetable.h, and the core options this
// console sets on purpose in coreoptions.h, both included below; moved out of
// this file in the restructure (#398).

#pragma once

#include <map>
#include <string>
#include <vector>

#include "coreoptions.h"
#include "romm_types.h"
#include "savetable.h"

namespace catalog {

// Where the core actually has to be for a game to start. Set once at startup;
// coverageFor answers from the manifest alone, and `installed` is the separate
// question of whether THIS console has that core built.
void setCoreDirectory(const char* dir);

enum class Support {
    // A core exists and ships. The game can be launched.
    Playable,
    // Nothing in the manifest serves this system. Switch, PS3, Vita and the
    // rest. Not a defect, just outside what this console is.
    NoCore,
    // The manifest has a core for this system, and this console does not have
    // it built. A different thing from NoCore and from Excluded: nothing is
    // wrong, the core simply has not been built yet.
    //
    // NOTHING ANSWERS THIS ON A HEALTHY CONSOLE, checked against the reference
    // A9 2026-09-22: all 23 cores this table names are in
    // /usr/lib/cabinetos/cores, 22 libretro plus PCSX2 as cabinetos-ps2.so.
    // GameCube is dolphin_libretro.so and IS a libretro core; PPSSPP is built
    // and PSP plays. An earlier version of this comment said otherwise on both
    // counts and was believed for a fortnight, which is the argument for
    // checking the disk rather than the comment.
    //
    // SO THIS IS A FAULT DETECTOR NOW, not a normal state. Cores ship in the
    // image and the image is atomic, so the only ways here are a staging
    // failure or a name that drifted out from under coreFileName — which is
    // exactly what ci/stage-image-payload.sh checks for, and why it prints any
    // .so it finds that is not on the list. Keep the stat. It is one syscall
    // and it turns a tile that fails on tap into a tile that says why.
    // Found by the hero offering an arcade game with no FBNeo on disk.
    NotInstalled,
    // A core exists but CabinetOS deliberately does not ship it. There is
    // always a reason, and `reason()` gives it, because a decision nobody can
    // recover is indistinguishable from a bug.
    Excluded,
    // The console runs this system, and THIS GAME was made for a controller
    // nobody has paired. Wii: a game that takes neither a Classic Controller
    // nor a GameCube pad needs a real Wii Remote (wii.h; docs/PROJECT.md open
    // question 35). Wii U: one that takes neither a Pro nor a Classic
    // Controller needs a Wii Remote or the GamePad (wiiu.h; open question 36).
    // An answer about one game
    // rather than its whole platform, so a platform's grid shows it, greyed
    // and saying why, rather than leaving it out.
    NeedsController,
    // The console runs this system, and no emulator here runs THIS GAME: an
    // arcade set no emulator's catalogue has working (arcadematch.h), or a
    // zip in Dreamcast's folder that is not a Flycast set. Also about one
    // game, so also shown greyed.
    UnsupportedGame,
    // The console runs this system, and THIS GAME is one it lists nowhere: a
    // 3DS game that is a CIA, by its name in RomM (ncch::namesCia; MMagTech,
    // 2026-10-20: hidden, not greyed). Not playable and not shown greyed, so
    // every list the console makes leaves it out after counting it, as the
    // grids' paging needs (#471).
    Hidden,
};

// Whether a platform's grid shows this game greyed, saying why, rather than
// leaving it out: the answers about one game, not its whole platform.
bool shownGreyed(Support s);

struct Coverage {
    Support support = Support::NoCore;
    const char* core = nullptr;     // manifest core name, when there is one
    // Why, when it is not simply playable: the line a game's page shows in
    // place of Play, so it is worded for the screen, capital first (#484).
    // Home's Resume on such a game opens that page (App::showWhyUnplayable).
    const char* reason = nullptr;
    // An arcade game, its emulator picked by its zip (arcadematch.h): what
    // the fight stick, Controls, holding Start, the saves and
    // RetroAchievements ask, since Flycast plays Dreamcast discs as well.
    bool arcade = false;
};

// The file a core was built into, given the manifest's name for it.
//
// ONE RULE, ONE PLACE, and it is here because having it in two cost a launch.
// A manifest name that already ends in `_libretro` does not get a second one:
// `fbneo_libretro` is filed as `fbneo_libretro.so`, not
// `fbneo_libretro_libretro.so`. `cores/build-core.sh` applies the same rule
// when it names the artifact and says so in a comment; coverageFor applied it
// too, and the LAUNCH path did not — so every arcade game on this console
// reported the core as playable from one code path and then failed to open it
// from the other, with a message about a file nothing ever builds. Found by
// running an FBNeo game, 2026-09-19, and it had been true since the core
// landed.
//
// THERE IS A THIRD COPY NOW and it is deliberate: ci/stage-image-payload.sh
// derives the same name to check that every core reached the image. It cannot
// call this, being a shell script that runs before anything is compiled, so
// the comment is on all three. It also PRINTS any .so it finds that is not on
// the list, which is how a name that drifts shows up as a line of output
// rather than as a platform the console says it cannot play.
std::string coreFileName(const std::string& manifestCoreName);

// Every libretro core the table plays a system on, by its manifest name, with
// the RomM platform slugs it serves: what the option check (optcheck.h) walks,
// so a core with no list in cores/options/ fails it rather than going
// unchecked. Not PCSX2, whose file is not a libretro core's, nor an emulator
// run as a program.
std::map<std::string, std::vector<std::string>> libretroCores();

Coverage coverageFor(const romm::Platform& p);

// The name to put on a tile, a grid's heading, Settings' Downloads: the
// server's name for the platform, and "Arcade" for every arcade folder, whose
// games share one tile (#611, catalog.cpp).
std::string displayName(const romm::Platform& p);

// A game's system wherever a screen names it, by the same rule: "Arcade" for a
// game in any arcade folder, RomM's name for the rest.
std::string systemName(const romm::Game& g);

// A game's RomM folder, its platform's fs_slug: what the Arcade tile's line
// names after each game, and where an arcade game's BIOS is uploaded (#611).
// RomM's name for the platform when the game carries no folder.
std::string folderName(const romm::Game& g);

// The same answer in a handful of words, for a place that has a handful of
// words' worth of room — a library tile's second line.
//
// `Coverage::reason` is a sentence, and a sentence truncated to "no core in
// the ..." tells a person strictly less than nothing: they can see the tile is
// dimmed, and the words that would explain it have been cut off. So the tile
// gets the short form and the launch screen, which has a column to itself, gets
// the sentence.
const char* shortReason(Support s);
// The same, for a platform: an emulator that is its own application is not a
// core, so it is never "not built"; it is waiting for first boot to install it.
const char* shortReason(const Coverage& c);

// The same question asked of a game. A ROM payload carries its own platform
// slug and fs_slug, so Home can decide whether the most recently played game is
// one this console can resume without fetching the platform list first.
//
// AND FOR AN ARCADE GAME IT NAMES THE EMULATOR its zip's name finds first in
// the catalogues (arcadematch.h, byName), with `arcade` set; Play asks again of
// the zip's files. A light-gun game is NeedsController, and a set no emulator
// here has working, or one made for anything but a controller, is
// UnsupportedGame.
Coverage coverageFor(const romm::Game& g);

inline bool playable(const romm::Platform& p) {
    return coverageFor(p).support == Support::Playable;
}

inline bool playable(const romm::Game& g) {
    return coverageFor(g).support == Support::Playable;
}

// Whether some of this platform's games may be Hidden (the 3DS's CIAs). RomM's
// own count includes them, so such a platform's tile is counted from its list.
bool hidesSomeGames(const romm::Platform& p);

}  // namespace catalog
