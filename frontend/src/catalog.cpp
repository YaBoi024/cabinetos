#include "catalog.h"
#include "catalog_internal.h"
#include "ncch.h"
#include "players.h"

#include "standalone.h"
#include "wii.h"
#include "wiiremote.h"
#include "wiiu.h"

#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <unordered_set>
#include <vector>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace catalog {
namespace {

struct Entry {
    const char* slug;
    const char* fsSlug;   // nullptr when the slug alone is enough
    Support support;
    const char* core;
    const char* reason;
    // Set only where the slug alone is ambiguous: the name of the SYSTEM this
    // row actually serves, used to qualify a tile so two platforms with the
    // same name can be told apart. Cabinet's manifest carries the same thing in
    // its `systems` field.
    //
    // LAST IN THE STRUCT ON PURPOSE. Every row below is positional, so a field
    // inserted in the middle would silently re-assign the ones that follow it.
    const char* system = nullptr;
};

// Started from Cabinet's core-manifest.json, 2026-09-14. Since 2026-10-11
// Cabinet is a reference, not a constraint: this table is this console's
// own, and a core's pin moves by docs/PROJECT.md, "Constraints and
// principles", 8 (only for something a player gets).
const Entry kTable[] = {
    {"3do",                  nullptr,     Support::Playable, "opera",           nullptr},
    // Nintendo 3DS in Azahar's own core (docs/PROJECT.md, open question 50).
    // Decrypted cartridge dumps play: .3ds, .cci, .cxi and .3dsx, and the
    // compressed .zcci, .zcxi and .z3dsx, which the core reads as they are
    // (it reports them as its own extensions). An encrypted dump or a .cia
    // is refused before the core sees it, with the reason (ncch.h).
    {"3ds",                  nullptr,     Support::Playable, "azahar",          nullptr},
    // One slug, two platforms, two different cores. This is the case the
    // "never key on slug alone" rule exists for. The fsSlug here is the
    // folder's name with capitals, spaces, dashes, underscores, dots and "+"
    // ignored, and other spellings of the same emulator count too (#310;
    // arcadeFolder below). A generic arcade folder matches neither row and
    // picks one of them per game.
    {"arcade",               "FBNEO",     Support::Playable, "fbneo_libretro",  nullptr,
     "FinalBurn Neo"},
    {"arcade",               "MAME2003",  Support::Playable, "mame2003_plus",   nullptr,
     "MAME 2003-Plus"},
    {"atari2600",            nullptr,     Support::Playable, "stella2014",      nullptr},
    {"atari7800",            nullptr,     Support::Playable, "prosystem",       nullptr},
    {"dc",                   nullptr,     Support::Playable, "flycast",         nullptr},
    {"gamegear",             nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"gb",                   nullptr,     Support::Playable, "gambatte",        nullptr},
    {"gba",                  nullptr,     Support::Playable, "mgba",            nullptr},
    {"gbc",                  nullptr,     Support::Playable, "gambatte",        nullptr},
    {"genesis",              nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"n64",                  nullptr,     Support::Playable, "mupen64plus",     nullptr},
    {"nds",                  nullptr,     Support::Playable, "melonds",         nullptr},
    {"neo-geo-pocket-color", nullptr,     Support::Playable, "beetle_ngp",      nullptr},
    {"nes",                  nullptr,     Support::Playable, "fceumm",          nullptr},
    {"ngc",                  nullptr,     Support::Playable, "dolphin",         nullptr},
    {"ps2",                  nullptr,     Support::Playable, "pcsx2",           nullptr},
    {"psp",                  nullptr,     Support::Playable, "ppsspp",          nullptr},
    {"psx",                  nullptr,     Support::Playable, "pcsx_rearmed",    nullptr},
    {"saturn",               nullptr,     Support::Playable, "beetle_saturn",   nullptr},
    {"segacd",               nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"sega32",               nullptr,     Support::Playable, "picodrive",       nullptr},
    {"sms",                  nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"snes",                 nullptr,     Support::Playable, "snes9x",          nullptr},
    {"tg16",                 nullptr,     Support::Playable, "beetle_pce_fast", nullptr},
    {"turbografx-cd",        nullptr,     Support::Playable, "beetle_pce_fast", nullptr},
    {"vectrex",              nullptr,     Support::Playable, "vecx",            nullptr},
    {"virtualboy",           nullptr,     Support::Playable, "beetle_vb",       nullptr},
    // Wii on the same Dolphin as GameCube, which plays both from one core.
    // Which games play on a pad is per game, in coverageFor below (wii.h).
    {"wii",                  nullptr,     Support::Playable, "dolphin",         nullptr},

    // NOT A LIBRETRO CORE: a whole emulator, from Flathub or in the image, run
    // as a separate program. The name is a row in standalone.cpp rather than
    // a file in the core directory, and `answer` asks standalone::installed
    // rather than stat'ing a .so. docs/PROJECT.md, open questions 19 and 32.
    {"switch",               nullptr,     Support::Playable, "eden",            nullptr},
    {"ps3",                  nullptr,     Support::Playable, "rpcs3",           nullptr},
    {"xbox",                 nullptr,     Support::Playable, "xemu",            nullptr},
    {"xbox360",              nullptr,     Support::Playable, "xenia",           nullptr},
    // PlayStation Vita in Vita3K, played as a PlayStation TV (docs/PROJECT.md,
    // open question 49): every game shows, with no greying.
    {"psvita",               nullptr,     Support::Playable, "vita3k",          nullptr},
    // Wii U in Cemu. Which games play on a pad is per game, in coverageFor
    // below (wiiu.h), as for Wii.
    {"wiiu",                 nullptr,     Support::Playable, "cemu",            nullptr},

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
    {"Game & Watch",         nullptr,     Support::Excluded, "gw",
     "Not built here. These games are too small to play on a television"},
};

bool eq(const char* a, const std::string& b) { return b == a; }

}  // namespace

namespace {

// A folder name with capitals, spaces, dashes, underscores, dots and "+"
// ignored, so `MAME 2003-Plus`, `mame_2003_plus` and `Mame2003+` are one name.
std::string folderKey(const std::string& s) {
    std::string k;
    for (const char ch : s) {
        if (ch == ' ' || ch == '-' || ch == '_' || ch == '.' || ch == '+') continue;
        k += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return k;
}

// THE ARCADE FOLDER THAT NAMES ITS EMULATOR (#310), as the table's fsSlug for
// that emulator's row, or empty for a folder that names none. Matching
// "FBNEO" exactly and case-sensitively was Cabinet's rule, and it hid every
// arcade game from a person who named the folder the way RomM's own guide
// says (`arcade`, or `fbneo` in lower case).
std::string arcadeFolder(const std::string& fsSlug) {
    const std::string k = folderKey(fsSlug);
    for (const char* n : {"fbneo", "fba", "fbalpha", "finalburnneo", "finalburnalpha"})
        if (k == n) return "FBNEO";
    for (const char* n : {"mame2003", "mame2003plus", "mame2003p"})
        if (k == n) return "MAME2003";
    return {};
}

// A folder RomM files as arcade that names no emulator: `arcade`, `mame`,
// `cps`. The core is picked per game; see coverageFor(Game).
bool genericArcade(const std::string& slug, const std::string& fsSlug) {
    return slug == "arcade" && arcadeFolder(fsSlug).empty();
}

// Returns the table row, not a Coverage, because displayQualifier needs the
// row's `system`.
const Entry* lookup(const std::string& slug, const std::string& fsSlug) {
    // An arcade folder is matched on the emulator its name spells, not on
    // the name as typed.
    const std::string folder = slug == "arcade" ? arcadeFolder(fsSlug) : fsSlug;
    const Entry* slugOnly = nullptr;
    for (const Entry& e : kTable) {
        if (!eq(e.slug, slug)) continue;
        if (e.fsSlug) {
            if (eq(e.fsSlug, folder)) return &e;
            continue;   // right slug, wrong core — keep looking
        }
        slugOnly = &e;
    }
    // A slug that matches an entry needing an fsSlug, but whose fsSlug matched
    // none of them, falls through to nullptr. For arcade that is the generic
    // folder, which coverageFor answers per game before it gets here.
    return slugOnly;
}

// The arcade row for one of the two arcade cores, by manifest name.
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

// The sets one arcade core runs, read once from `<core file>.sets` beside the
// cores (cores/arcade-sets.sh makes it from the core's own DAT at the pinned
// commit). A missing list is said once in the log and reads as empty, so the
// games are greyed rather than handed to a core with no driver for them.
const std::unordered_set<std::string>& setsOf(const char* core) {
    auto load = [](const char* name) {
        std::unordered_set<std::string> sets;
        std::string file = coreFileName(name);
        file.replace(file.size() - 3, 3, ".sets");   // fbneo_libretro.so
        const std::string path = gCoreDir + "/" + file;
        std::ifstream in(path);
        if (!in) {
            std::fprintf(stderr, "[catalog] no arcade set list at %s\n", path.c_str());
            return sets;
        }
        for (std::string line; std::getline(in, line);)
            if (!line.empty() && line[0] != '#') sets.insert(line);
        return sets;
    };
    static const std::unordered_set<std::string> fbneo = load("fbneo_libretro");
    static const std::unordered_set<std::string> mame = load("mame2003_plus");
    return std::strcmp(core, "fbneo_libretro") == 0 ? fbneo : mame;
}

// A game in a generic arcade folder: FinalBurn Neo if its list has the set,
// else MAME 2003-Plus if its list does (#310). The set is the file's name, as
// the core is handed it (launch_routes.cpp keeps an arcade file's own name).
Coverage arcadeGame(const romm::Game& g) {
    std::string set = g.fsName;
    if (const size_t dot = set.find_last_of('.'); dot != std::string::npos) set.erase(dot);
    for (char& ch : set) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (const char* core : {"fbneo_libretro", "mame2003_plus"})
        if (setsOf(core).count(set)) return answer(arcadeRow(core));
    return {Support::UnsupportedGame, nullptr, "Not supported on this console"};
}
}  // namespace

void setCoreDirectory(const char* dir) { if (dir) gCoreDir = dir; }

Coverage coverageFor(const romm::Platform& p) {
    // A generic arcade folder is enterable when either arcade core is here;
    // which one a game needs is answered per game.
    if (genericArcade(p.slug, p.fsSlug)) {
        const Coverage fbneo = answer(arcadeRow("fbneo_libretro"));
        if (fbneo.support == Support::Playable) return fbneo;
        const Coverage mame = answer(arcadeRow("mame2003_plus"));
        return mame.support == Support::Playable ? mame : fbneo;
    }
    return answer(lookup(p.slug, p.fsSlug));
}
// The one platform whose CIA games are Hidden (coverageFor, below).
static bool hidesCias(const std::string& slug) { return slug == "3ds"; }

Coverage coverageFor(const romm::Game& g) {
    Coverage c = genericArcade(g.platformSlug, g.platformFsSlug)
        ? arcadeGame(g) : answer(lookup(g.platformSlug, g.platformFsSlug));
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
        case Support::UnsupportedGame: return "Not supported here";
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

std::string displayQualifier(const romm::Platform& p) {
    const Entry* e = lookup(p.slug, p.fsSlug);
    // Only the ambiguous rows carry a system name, so everything else comes
    // back empty. Qualifying a platform nobody can confuse would be noise.
    if (e && e->system) return e->system;
    // A generic arcade folder (#310) is still ambiguous to a person when
    // there are two, two tiles saying "Arcade", so fall back to the one field
    // that actually distinguishes them on the server: the folder's name. Not
    // when it is only the tile's own name again (`arcade` under "Arcade").
    if (!p.fsSlug.empty() && p.slug == "arcade" &&
        folderKey(p.fsSlug) != folderKey(p.name))
        return p.fsSlug;
    return {};
}

std::string displayName(const romm::Platform& p) {
    // The joined form, for the places that want one string: a grid's heading,
    // a launch screen, a line of output. A tile wants the two halves and calls
    // displayQualifier for the second.
    const std::string base = p.name.empty() ? p.slug : p.name;
    const std::string q = displayQualifier(p);
    return q.empty() ? base : base + " (" + q + ")";
}

}  // namespace catalog
