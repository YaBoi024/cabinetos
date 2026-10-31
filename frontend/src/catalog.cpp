#include "catalog.h"
#include "arcadematch.h"
#include "catalog_internal.h"
#include "ncch.h"
#include "players.h"

#include "standalone.h"
#include "wii.h"
#include "wiiremote.h"
#include "wiiu.h"

#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <vector>

#include <cstring>
#include <string>

namespace catalog {
namespace {

// EVERY ROW BELOW IS POSITIONAL, so a field inserted in the middle would
// silently re-assign the ones that follow it.
struct Entry {
    const char* slug;
    Support support;
    const char* core;
    const char* reason;
};

// Started from Cabinet's core-manifest.json, 2026-09-14. Since 2026-10-11
// Cabinet is a reference, not a constraint: this table is this console's
// own, and a core's pin moves by docs/PROJECT.md, "Constraints and
// principles", 8 (only for something a player gets).
const Entry kTable[] = {
    {"3do",                  Support::Playable, "opera",           nullptr},
    // Nintendo 3DS in Azahar's own core (docs/PROJECT.md, open question 50).
    // Decrypted cartridge dumps play: .3ds, .cci, .cxi and .3dsx, and the
    // compressed .zcci, .zcxi and .z3dsx, which the core reads as they are
    // (it reports them as its own extensions). An encrypted dump or a .cia
    // is refused before the core sees it, with the reason (ncch.h).
    {"3ds",                  Support::Playable, "azahar",          nullptr},
    // THE ARCADE EMULATORS, in the order a game is asked of them. Which one
    // plays a game is decided by its zip, never by its folder
    // (arcadematch.h); these rows say only that they are arcade emulators,
    // for every slug arcadematch::isArcadeSlug names. Flycast's own row is
    // Dreamcast's, below.
    {"arcade",               Support::Playable, "flycast",         nullptr},
    {"arcade",               Support::Playable, "fbneo_libretro",  nullptr},
    {"arcade",               Support::Playable, "mame2003_plus",   nullptr},
    // sm2-emu, a program of its own (standalone.cpp), for every Model 2 game
    // its list marks working (docs/decisions/2026-10-28-model-2-and-model-3.md).
    {"arcade",               Support::Playable, "sm2emu",          nullptr},
    // Supermodel, a program of its own (standalone.cpp), for every Model 3
    // game (docs/decisions/2026-10-28-model-2-and-model-3.md).
    {"arcade",               Support::Playable, "supermodel",      nullptr},
    // MAME 0.289, libretro's own build, for what nothing else plays
    // (docs/decisions/2026-10-28-prebuilt-mame-core.md).
    {"arcade",               Support::Playable, "mame_libretro",   nullptr},
    {"atari2600",            Support::Playable, "stella2014",      nullptr},
    {"atari7800",            Support::Playable, "prosystem",       nullptr},
    {"dc",                   Support::Playable, "flycast",         nullptr},
    {"gamegear",             Support::Playable, "genesis_plus_gx", nullptr},
    {"gb",                   Support::Playable, "gambatte",        nullptr},
    {"gba",                  Support::Playable, "mgba",            nullptr},
    {"gbc",                  Support::Playable, "gambatte",        nullptr},
    {"genesis",              Support::Playable, "genesis_plus_gx", nullptr},
    {"n64",                  Support::Playable, "mupen64plus",     nullptr},
    {"nds",                  Support::Playable, "melonds",         nullptr},
    {"neo-geo-pocket-color", Support::Playable, "beetle_ngp",      nullptr},
    {"nes",                  Support::Playable, "fceumm",          nullptr},
    {"ngc",                  Support::Playable, "dolphin",         nullptr},
    {"ps2",                  Support::Playable, "pcsx2",           nullptr},
    {"psp",                  Support::Playable, "ppsspp",          nullptr},
    {"psx",                  Support::Playable, "pcsx_rearmed",    nullptr},
    {"saturn",               Support::Playable, "beetle_saturn",   nullptr},
    {"segacd",               Support::Playable, "genesis_plus_gx", nullptr},
    {"sega32",               Support::Playable, "picodrive",       nullptr},
    {"sms",                  Support::Playable, "genesis_plus_gx", nullptr},
    {"snes",                 Support::Playable, "snes9x",          nullptr},
    {"tg16",                 Support::Playable, "beetle_pce_fast", nullptr},
    {"turbografx-cd",        Support::Playable, "beetle_pce_fast", nullptr},
    {"vectrex",              Support::Playable, "vecx",            nullptr},
    {"virtualboy",           Support::Playable, "beetle_vb",       nullptr},
    // Wii on the same Dolphin as GameCube, which plays both from one core.
    // Which games play on a pad is per game, in coverageFor below (wii.h).
    {"wii",                  Support::Playable, "dolphin",         nullptr},

    // NOT A LIBRETRO CORE: a whole emulator, from Flathub or in the image, run
    // as a separate program. The name is a row in standalone.cpp rather than
    // a file in the core directory, and `answer` asks standalone::installed
    // rather than stat'ing a .so. docs/PROJECT.md, open questions 19 and 32.
    {"switch",               Support::Playable, "eden",            nullptr},
    {"ps3",                  Support::Playable, "rpcs3",           nullptr},
    {"xbox",                 Support::Playable, "xemu",            nullptr},
    {"xbox360",              Support::Playable, "xenia",           nullptr},
    // PlayStation Vita in Vita3K, played as a PlayStation TV (docs/PROJECT.md,
    // open question 49): every game shows, with no greying.
    {"psvita",               Support::Playable, "vita3k",          nullptr},
    // Wii U in Cemu. Which games play on a pad is per game, in coverageFor
    // below (wiiu.h), as for Wii.
    {"wiiu",                 Support::Playable, "cemu",            nullptr},

    // A core exists and Cabinet did not ship it. The manifest says so in as
    // many words, "iOS-only by decision", and there was no tvOS build, which
    // was the platform CabinetOS most resembles. Not carried over without a
    // deliberate decision, because whatever made it wrong for a television
    // has not changed.
    // NOT A GAP, A DECISION — MMagTech, 2026-09-20: this console will not build
    // it, "as the games are too small on a tv". The reason a person reads has
    // to be about THIS console, not about what Cabinet chose on a phone: 171
    // games is the largest excluded row in the reference library, so it is the
    // tile most likely to be asked about.
    {"Game & Watch",         Support::Excluded, "gw",
     "Not built here. These games are too small to play on a television"},
};

bool eq(const char* a, const std::string& b) { return b == a; }

}  // namespace

namespace {

// The table's row for a platform. An arcade slug has several, one per arcade
// emulator, and is answered before it gets here (coverageFor).
const Entry* lookup(const std::string& slug) {
    for (const Entry& e : kTable)
        if (eq(e.slug, slug)) return &e;
    return nullptr;
}

// The arcade row for one of the arcade emulators, by manifest name.
const Entry* arcadeRow(const char* core) {
    for (const Entry& e : kTable)
        if (eq(e.slug, "arcade") && std::strcmp(e.core, core) == 0) return &e;
    return nullptr;
}

}  // namespace

// Shared with coreoptions.cpp and savetable.cpp through catalog_internal.h.
//
// The manifest's name for a core, given either that name or the file it was
// built into. Resolved against the table, so the answer is a name the table
// actually uses rather than one produced by trimming a string and hoping.
//
// Unrecognised input comes back unchanged. That is the honest answer: a caller
// holding a name this table has never heard of has a problem no normalisation
// can fix.
std::string manifestName(const std::string& core) {
    std::string stem = core;
    // A path is one of the things a caller may hold — --core takes one — and a
    // directory prefix would defeat every comparison below.
    if (const size_t slash = stem.find_last_of('/'); slash != std::string::npos)
        stem.erase(0, slash + 1);
    const std::string dotSo = ".so";
    if (stem.size() > dotSo.size() &&
        stem.compare(stem.size() - dotSo.size(), dotSo.size(), dotSo) == 0)
        stem.erase(stem.size() - dotSo.size());

    auto known = [](const std::string& name) {
        for (const Entry& e : kTable)
            if (e.core && name == e.core) return true;
        return false;
    };
    if (known(stem)) return stem;

    const std::string suffix = "_libretro";
    if (stem.size() > suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const std::string shorter = stem.substr(0, stem.size() - suffix.size());
        if (known(shorter)) return shorter;
    }
    return stem;
}

namespace {
std::string gCoreDir = "cores/build";
}  // namespace

std::string coreFileName(const std::string& manifestCoreName) {
    // PLAYSTATION 2 IS NOT A LIBRETRO CORE AND ITS FILE IS NOT NAMED LIKE ONE.
    //
    // PCSX2 is a whole emulator, embedded from upstream and built by
    // cores/build-pcsx2.sh rather than cores/build-core.sh. The name says so
    // deliberately: a file called `pcsx2_libretro.so` would be the libretro
    // core, which this console DELIBERATELY DOES NOT SHIP — its source
    // repository does not exist, so it can never be pinned, built in CI or
    // audited. Seeing the two names side by side is how somebody tells at a
    // glance which one a console is running. docs/PROJECT.md, open question 12b.
    if (manifestCoreName == "pcsx2") return "cabinetos-ps2.so";

    // See the header. cores/build-core.sh has the same three lines and the
    // same comment; the two must agree.
    std::string stem = manifestCoreName;
    const std::string suffix = "_libretro";
    if (stem.size() > suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        stem.erase(stem.size() - suffix.size());
    return stem + "_libretro.so";
}

std::map<std::string, std::vector<std::string>> libretroCores() {
    static const std::string kLibretro = "_libretro.so";
    std::map<std::string, std::vector<std::string>> out;
    for (const Entry& e : kTable) {
        if (e.support != Support::Playable || !e.core || cab::standalone::find(e.core)) continue;
        const std::string file = coreFileName(e.core);
        if (file.size() <= kLibretro.size() ||
            file.compare(file.size() - kLibretro.size(), kLibretro.size(), kLibretro) != 0)
            continue;
        std::vector<std::string>& slugs = out[e.core];
        if (std::find(slugs.begin(), slugs.end(), e.slug) == slugs.end()) slugs.push_back(e.slug);
    }
    return out;
}

namespace {

// Turns a table row into the answer for THIS console. The table is a fact about
// the emulators; everything here is a fact about the machine it is running
// on, which is why the two are kept apart.
//
// Two ways a Playable row stops being playable, and they are different work:
// the core has not been built, or it has been built and cannot be driven.
Coverage answer(const Entry* e) {
    if (!e) return {Support::NoCore, nullptr, "No core for this system"};

    Coverage c{e->support, e->core, e->reason};
    if (c.support != Support::Playable || !c.core) return c;

    // Two emulators that are their own application, xemu and Eden, are
    // installed by first boot from Flathub, not shipped in the image, so
    // "not here" is an ordinary state for a while on a new machine, and a
    // permanent one on a console that has never reached the internet (open
    // question 21). RPCS3, Xenia and Cemu are in the image.
    if (const cab::standalone::Emulator* emu = cab::standalone::find(c.core)) {
        if (!cab::standalone::installed(*emu)) {
            c.support = Support::NotInstalled;
            c.reason = "Its emulator is not installed on this console yet";
        }
        return c;
    }

    const std::string path = gCoreDir + "/" + coreFileName(c.core);
    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        c.support = Support::NotInstalled;
        c.reason = "The core for this system is not built on this console yet";
        return c;
    }

    // A core that draws through a graphics context rather than handing back
    // pixels (Flycast, Mupen64Plus, PPSSPP, Dolphin) plays like any other: the
    // host serves it (core_hwrender.cpp). One asking for a context this
    // machine cannot serve is refused as it loads, and the launch says so as
    // for any core that will not load.
    return c;
}

// AN ARCADE GAME, by its zip's name (arcadematch.h): the emulator the
// first catalogue that has the set working names, a light-gun game for the
// light gun it needs, or not supported here, which is also a game made for
// anything but a controller. Which emulator
// really plays it is asked again of its files at Play (launch_routes.cpp).
//
// A GAME WHOSE RECORD KNOWS WHAT ITS ZIP HOLDS (Game::members, a loose zip a
// launch read) is asked of its files when they fit, so a game offline names
// the emulator it plays on, and the save tags it is mirrored under.
Coverage arcadeGame(const romm::Game& g, bool dreamcast) {
    arcadematch::Answer a = arcadematch::byName(g.fsName, dreamcast);
    if (a.verdict == arcadematch::Verdict::Plays && !g.members.empty() &&
        g.membersFile == g.fsName && g.membersSize == g.sizeBytes) {
        const arcadematch::Answer f = arcadematch::byFiles(g.fsName, g.members, {}, dreamcast);
        if (f.verdict == arcadematch::Verdict::Plays) a = f;
    }
    switch (a.verdict) {
        case arcadematch::Verdict::Plays: {
            Coverage c = answer(arcadeRow(a.core));
            c.arcade = true;
            return c;
        }
        case arcadematch::Verdict::NeedsGun:
            return {Support::NeedsController, nullptr, "Needs a light gun"};
        default:
            return {Support::UnsupportedGame, nullptr, "Not supported"};
    }
}
}  // namespace

void setCoreDirectory(const char* dir) {
    if (!dir) return;
    gCoreDir = dir;
    arcadematch::setDirectory(dir);
}

Coverage coverageFor(const romm::Platform& p) {
    // An arcade folder is enterable when any arcade emulator is here; which
    // one a game needs is answered per game.
    if (arcadematch::isArcadeSlug(p.slug)) {
        Coverage first;
        for (const Entry& e : kTable) {
            if (!eq(e.slug, "arcade")) continue;
            const Coverage c = answer(&e);
            if (c.support == Support::Playable) return c;
            if (!first.core) first = c;
        }
        return first;
    }
    return answer(lookup(p.slug));
}
// The one platform whose CIA games are Hidden (coverageFor, below).
static bool hidesCias(const std::string& slug) { return slug == "3ds"; }

Coverage coverageFor(const romm::Game& g) {
    // AN ARCADE GAME'S EMULATOR COMES FROM ITS ZIP, in every arcade folder
    // and for a zip in Dreamcast's (arcadematch.h).
    const bool dreamcastZip =
        g.platformSlug == "dc" && !g.multiFile && arcadematch::asksDreamcast(g.fsName);
    Coverage c = arcadematch::isArcadeSlug(g.platformSlug) || dreamcastZip
                     ? arcadeGame(g, dreamcastZip)
                     : answer(lookup(g.platformSlug));
    // A Wii game that takes neither a Classic Controller nor a GameCube pad
    // needs a real Wii Remote, and plays while any Wii Remote is paired, as
    // on a Wii (#200, PROJECT.md question 35). Remove every Remote and they
    // grey out again. AND NOT WHILE A STREAM PLAYS (#286): the paired Remote
    // is by the TV, and the person streaming is not. ONE THAT REQUIRES
    // MOTIONPLUS plays only while a Remote whose MotionPlus works here is
    // paired, never one of Nintendo's own (wiiremote.h; MMagTech, 2026-10-22).
    if (c.support == Support::Playable && g.platformSlug == "wii") {
        const std::string code = wii::codeFromTitleId(g.titleId);
        if (wii::padDevice(code) == 0) {
            if (!wiiremote::anyPaired() || players::streaming()) {
                c.support = Support::NeedsController;
                c.reason = "Needs a Wii Remote";
            } else if (wii::needsMotionPlus(code) && !wiiremote::anyWithMotionPlus()) {
                c.support = Support::NeedsController;
                c.reason = "Needs MotionPlus";
            }
        }
    }
    // A Wii U game that takes neither a Pro nor a Classic Controller was made
    // for the GamePad or the Wii Remote (wiiu.h).
    if (c.support == Support::Playable && g.platformSlug == "wiiu") {
        switch (wiiu::needs(g.productCode)) {
            case wiiu::Needs::Nothing: break;
            case wiiu::Needs::WiiRemote:
                c.support = Support::NeedsController;
                c.reason = "Needs a Wii Remote";
                break;
            case wiiu::Needs::GamePad:
                c.support = Support::NeedsController;
                c.reason = "Needs a Wii U GamePad";
                break;
        }
    }
    // A 3DS GAME THAT IS A CIA SHOWS NOWHERE (MMagTech, 2026-10-20): Azahar
    // plays no CIA. By the name RomM gives the game in every list
    // (ncch::namesCia), since a list carries no file's bytes. A folder game's
    // files are not in RomM's lists, so a folder shows, and Play starts its
    // playable file (Azahar lists no `cia`); a folder of only CIAs, or an
    // archive whose name hides one, is refused after its download (ncch.h).
    if (c.support == Support::Playable && hidesCias(g.platformSlug) && !g.multiFile &&
        ncch::namesCia(g.fsName)) {
        c.support = Support::Hidden;
        c.reason = "3DS CIA files can't be played";
    }
    return c;
}

bool hidesSomeGames(const romm::Platform& p) { return hidesCias(p.slug); }

const char* shortReason(Support s) {
    switch (s) {
        case Support::Playable: return "";
        // Measured against the tile that shows them, not guessed: a library
        // tile's second line holds about twenty-three characters at Footnote,
        // and anything longer comes back as an ellipsis where the explanation
        // was meant to be.
        case Support::NoCore: return "No core for this system";
        case Support::Excluded: return "Not shipped here";
        case Support::NotInstalled: return "Core not built yet";
        case Support::NeedsController: return "Needs a Wii Remote";
        case Support::UnsupportedGame: return "Not supported";
        case Support::Hidden: return "Not shown here";
    }
    return "Not playable here";
}

bool shownGreyed(Support s) {
    return s == Support::NeedsController || s == Support::UnsupportedGame;
}

const char* shortReason(const Coverage& c) {
    if (c.support == Support::NotInstalled && c.core && cab::standalone::find(c.core))
        return "Emulator not installed";
    // Which controller: every one of these reasons fits the tile.
    if (c.support == Support::NeedsController && c.reason) return c.reason;
    return shortReason(c.support);
}

std::string displayName(const romm::Platform& p) {
    // EVERY ARCADE FOLDER IS ONE SYSTEM, "Arcade" (#611): its games share one
    // tile, as they share one look and one achievements system (looks.cpp,
    // achievements.cpp). RomM calls MAME's, FinalBurn Neo's, Naomi's and
    // Atomiswave's folders "Arcade" and CPS1's "Capcom Play System"; the
    // folder itself is named only inside that tile (folderName).
    if (arcadematch::isArcadeSlug(p.slug)) return "Arcade";
    return p.name.empty() ? p.slug : p.name;
}

std::string systemName(const romm::Game& g) {
    if (arcadematch::isArcadeSlug(g.platformSlug)) return "Arcade";
    return g.platformName;
}

std::string folderName(const romm::Game& g) {
    return g.platformFsSlug.empty() ? g.platformName : g.platformFsSlug;
}

}  // namespace catalog
