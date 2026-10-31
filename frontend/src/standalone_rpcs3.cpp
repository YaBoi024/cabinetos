// PlayStation 3, through RPCS3 in the image: its system software and a game's
// packages installed once, on the download worker (installRpcs3, for
// installGame), and the settings, controls, licence and hard drive the console
// decides for it before every start (prepareRpcs3). What every row with
// installs uses (installedFile, bootPath and the installGame dispatcher) stays
// in standalone.cpp. Moved out of standalone.cpp in the restructure (#398).

#include "standalone_internal.h"

#include "proc.h"
#include "storage.h"
#include "vpad.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace cab::standalone::detail {
namespace {

// --- RPCS3 -----------------------------------------------------------------
//
// WHERE EVERYTHING GOES, decided by MMagTech 2026-09-28 (docs/PROJECT.md, open
// question 19) and measured on the A9 the same day with the build the image
// pins:
//
//   emulators/rpcs3/rpcs3/        its settings and the PS3's system software
//                                 (dev_flash, 189 MB, once per console)
//   <the game's folder>/          the installed game (<title ID>/USRDIR/...),
//                                 its `.rap`, and rpcs3/, its compile cache
//                                 and log: removed with the game
//   <this person's save folder>/  hdd0/, the PS3's hard drive as the game
//                                 sees it: saves, the licence, trophies
//
// RPCS3 starts an installed game from OUTSIDE its hard drive by itself: it
// mounts the game's folder at /dev_hdd0/game/<title ID> (Emu/System.cpp, the
// HG category). Measured: Super Stardust HD booted from its own folder,
// decrypted with its `.rap`, and wrote its save into the save folder.

// Where an install is built before it is moved into place.
constexpr const char* kStaging = ".install";

std::string rpcs3Config(const Emulator& e) { return home(e) + "/rpcs3"; }
std::string rpcs3Flash(const Emulator& e) { return rpcs3Config(e) + "/dev_flash"; }
std::string firmwareNote(const Emulator& e) { return rpcs3Flash(e) + "/.cabinetos-firmware"; }

// The PS3 system software RomM serves: `PS3UPDAT*.PUP` in `bios/`, the highest
// version in its name when there are several (4.96 over 4.93).
std::string newestPup(const std::string& biosDir) {
    std::string pick;
    long pickVersion = -1;
    for (const std::string& name : listDir(biosDir)) {
        std::string lower = name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lower.compare(0, 8, "ps3updat") != 0 || !endsWithNoCase(lower, ".pup")) continue;
        long v = 0;
        for (const char ch : lower)
            if (std::isdigit(static_cast<unsigned char>(ch))) v = v * 10 + (ch - '0');
        if (v > pickVersion) {
            pick = biosDir + "/" + name;
            pickVersion = v;
        }
    }
    return pick;
}

// RPCS3 WITHOUT A WINDOW, to install something, and its log afterwards.
// `configHome` is a scratch folder of its own, so an install never touches the
// settings a game runs with, and the whole of it is deleted after.
//
// IT EXITS 134 AFTER A SUCCESSFUL INSTALL: an abort in a static destructor,
// after it has logged "Successfully installed" (lessons, the PS3 notes;
// measured again 2026-09-28 on the pinned build). So the log line decides,
// never the exit code. `prlimit --core=0` because every one of those aborts
// would otherwise leave a core file of a process with gigabytes mapped.
std::string rpcs3Headless(const Emulator& e, const std::string& configHome,
                          const std::vector<std::string>& what, const std::atomic<bool>& cancel) {
    std::vector<std::string> args = {
        "prlimit", "--core=0", "--",
        "env", "XDG_CONFIG_HOME=" + configHome, "XDG_CACHE_HOME=" + configHome,
        binaryOf(e), "--headless",
    };
    args.insert(args.end(), what.begin(), what.end());
    // Half an hour: 20 GB went in at 161 MB/s on the VM's slow disk; a USB
    // hard drive could be a fifth of that.
    const proc::Result r = proc::run(args, 30 * 60, &cancel);
    std::fprintf(stderr, "[standalone] rpcs3 %s: exit %d%s\n", what.front().c_str(), r.status,
                 r.timedOut ? " (stopped)" : "");
    return readFile(configHome + "/rpcs3/RPCS3.log");
}

// THE PS3'S SYSTEM SOFTWARE, once per console and again only when RomM's
// file changes, the way Eden's firmware is (installFirmware, in
// standalone_eden.cpp). 1.1 s and 189 MB on the A9.
bool installPs3Firmware(const Emulator& e, const std::atomic<bool>& cancel,
                        std::string* message) {
    const std::string pup = newestPup(storage::biosDir());
    const std::string note = firmwareNote(e);
    const std::string had = readFile(note);
    if (pup.empty()) {
        // Nothing on the server, or no server: whatever is installed stands.
        if (!had.empty()) return true;
        *message = "No PlayStation 3 firmware on the server";
        return false;
    }
    struct stat st;
    ::stat(pup.c_str(), &st);
    const std::string stamp =
        pup.substr(pup.find_last_of('/') + 1) + " " + std::to_string(st.st_size) + "\n";
    if (had == stamp) return true;

    const std::string scratch = home(e) + "/" + kStaging;
    removeTree(scratch);
    storage::makeDirs(scratch + "/rpcs3");
    storage::makeDirs(rpcs3Flash(e));
    writeFile(scratch + "/rpcs3/vfs.yml",
              "/dev_flash/: " + yamlQuote(rpcs3Flash(e) + "/") + "\n" +
              "/dev_hdd0/: " + yamlQuote(scratch + "/hdd0/") + "\n");
    const std::string log = rpcs3Headless(e, scratch, {"--installfw", pup}, cancel);
    removeTree(scratch);
    if (log.find("Successfully installed PS3 firmware") == std::string::npos) {
        std::fprintf(stderr, "[standalone] PS3 firmware from %s did not install\n", pup.c_str());
        *message = "Couldn't install the PlayStation 3 firmware";
        return false;
    }
    writeFile(note, stamp);
    std::fprintf(stderr, "[standalone] PS3 firmware installed from %s\n", pup.c_str());
    return true;
}

// A PS3 PACKAGE, BY WHAT IT IS rather than by its name: every PKG starts
// 7F 'P' 'K' 'G'. Found 2026-09-28: Zombie Apocalypse's PKG on the reference
// server has no extension at all, and a name test would have skipped it.
bool isPkg(const std::string& path) {
    char magic[4] = {};
    std::ifstream in(path, std::ios::binary);
    return in.read(magic, 4) && std::memcmp(magic, "\x7FPKG", 4) == 0;
}

// EVERY PACKAGE IN THE GAME'S FOLDER, installed into it, largest first (the
// game before an update or add-on for it), then deleted. RPCS3 installs into
// its hard drive's game/<title ID>; that drive is pointed at a staging folder
// inside the game's own, so the result is on the same disk and moves into
// place with a rename, never a copy.
//
// A GAME ALREADY INSTALLED goes back into staging first, so a later update
// lands on top of it rather than beside it. AN INTERRUPTED INSTALL leaves its
// staging behind, and nothing else would clean it (lessons, the PS3 notes),
// so it is deleted before starting.
bool installPackages(const Emulator& e, const std::string& entryPath,
                     const std::atomic<bool>& cancel, std::string* message) {
    std::vector<std::pair<int64_t, std::string>> pkgs;
    for (const std::string& name : listDir(entryPath)) {
        struct stat st;
        const std::string p = entryPath + "/" + name;
        if (name[0] != '.' && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && isPkg(p))
            pkgs.emplace_back(st.st_size, name);
    }
    if (pkgs.empty()) return true;
    std::sort(pkgs.begin(), pkgs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    const std::string staging = entryPath + "/" + kStaging;
    const std::string games = staging + "/hdd0/game";
    removeTree(staging);
    storage::makeDirs(staging + "/rpcs3");
    storage::makeDirs(games);
    writeFile(staging + "/rpcs3/vfs.yml",
              "/dev_hdd0/: " + yamlQuote(staging + "/hdd0/") + "\n" +
              "/dev_flash/: " + yamlQuote(rpcs3Flash(e) + "/") + "\n");
    std::vector<std::string> hadBefore;
    for (const std::string& name : listDir(entryPath))
        if (isFile(entryPath + "/" + name + "/PARAM.SFO") &&
            ::rename((entryPath + "/" + name).c_str(), (games + "/" + name).c_str()) == 0)
            hadBefore.push_back(name);

    std::string note = readFile(entryPath + "/" + kInstalledNote);
    bool ok = true;
    for (const auto& [bytes, name] : pkgs) {
        const std::string pkg = entryPath + "/" + name;
        const std::string log = rpcs3Headless(e, staging, {"--installpkg", pkg}, cancel);
        // "Successfully installed <path> (title_id=NPEA00014, title=..." on
        // its own line (rpcs3qt/main_window.cpp).
        const size_t at = log.find("Successfully installed " + pkg);
        if (at == std::string::npos || cancel.load()) {
            std::fprintf(stderr, "[standalone] %s did not install\n", pkg.c_str());
            ok = false;
            break;
        }
        note += std::to_string(bytes) + " " + name + "\n";
        std::fprintf(stderr, "[standalone] installed %s (%lld bytes)\n", name.c_str(),
                     static_cast<long long>(bytes));
    }

    // WHAT CAME OUT, back into the game's folder: every title directory with
    // its PARAM.SFO. RPCS3 also makes TEST12345 and a lock folder on any new
    // hard drive, which are not the game. AFTER A FAILURE only what was
    // installed before goes back: a half-installed new title in the folder
    // would be found by bootPath and started, offline, as if it were whole.
    for (const std::string& name : listDir(games)) {
        const bool keep = ok || std::find(hadBefore.begin(), hadBefore.end(), name) !=
                                    hadBefore.end();
        if (keep && isFile(games + "/" + name + "/PARAM.SFO"))
            ::rename((games + "/" + name).c_str(), (entryPath + "/" + name).c_str());
    }
    removeTree(staging);
    if (!ok) {
        *message = "Couldn't install this game";
        return false;
    }
    // Only now: a PKG is deleted once the game it became is in place.
    writeFile(entryPath + "/" + kInstalledNote, note);
    for (const auto& pkg : pkgs) ::unlink((entryPath + "/" + pkg.second).c_str());
    return true;
}

// THE CONSOLE'S SETTINGS FOR RPCS3, written whole before every start: every
// one is a box, a pause or a hint that would otherwise land on the
// television. Names from Emu/system_config.h. Everything else is RPCS3's
// default, which is what RetroArch-style "it just works" means for it.
constexpr const char* kRpcs3Settings =
    "Miscellaneous:\n"
    "  Automatically start games after boot: true\n"
    "  Exit RPCS3 when process finishes: true\n"
    "  Pause emulation on RPCS3 focus loss: false\n"
    "  Start games in fullscreen mode: true\n"
    "  Prevent display sleep while running games: true\n"
    "  Show trophy popups: false\n"
    "  Show RPCN popups: false\n"
    "  Show shader compilation hint: false\n"
    "  Show PPU compilation hint: false\n"
    "  Show autosave/autoload hint: false\n"
    "  Show pressure intensity toggle hint: false\n"
    "  Show analog limiter toggle hint: false\n"
    "  Show mouse and keyboard toggle hint: false\n"
    "  Show capture hints: false\n";

// EACH PLAYER'S CONTROLS, on their virtual controller (vpad.h), found by its
// NAME: RPCS3's evdev handler matches a device by the name it reports, so
// there are no numbers to get wrong. The buttons are RPCS3's names for the
// evdev codes the virtual controller sends (Input/evdev_joystick_handler.h),
// laid out by POSITION as a DualShock 3 is.
//
// SQUARE IS "Y" AND TRIANGLE IS "X", and that is not a slip. The virtual
// controller sends the west button as BTN_WEST and the north as BTN_NORTH,
// which are the same codes Linux also calls BTN_Y and BTN_X, and RPCS3 names
// the codes by those older words. Its own default (Square "X") suits pads
// whose drivers send by label rather than position. Players past `players`
// are unplugged, or a game would see four pads.
std::string rpcs3Controls(int players) {
    std::string out;
    for (int p = 1; p <= 7; ++p) {
        out += "Player " + std::to_string(p) + " Input:\n";
        if (p > players || p > cab::vpad::kMaxPlayers) {
            out += "  Handler: \"Null\"\n  Device: \"Null\"\n";
            continue;
        }
        out += "  Handler: Evdev\n";
        out += "  Device: " + yamlQuote("CabinetOS player " + std::to_string(p)) + "\n";
        out += "  Config:\n";
        const char* map[][2] = {
            {"Left Stick Left", "LX-"},   {"Left Stick Down", "LY+"},
            {"Left Stick Right", "LX+"},  {"Left Stick Up", "LY-"},
            {"Right Stick Left", "RX-"},  {"Right Stick Down", "RY+"},
            {"Right Stick Right", "RX+"}, {"Right Stick Up", "RY-"},
            {"Start", "Start"},           {"Select", "Select"},
            {"PS Button", "Mode"},
            {"Square", "Y"},              {"Cross", "A"},
            {"Circle", "B"},              {"Triangle", "X"},
            {"Left", "Hat0 X-"},          {"Down", "Hat0 Y+"},
            {"Right", "Hat0 X+"},         {"Up", "Hat0 Y-"},
            {"R1", "TR"},                 {"R2", "RZ+"},
            {"R3", "Thumb R"},            {"L1", "TL"},
            {"L2", "LZ+"},                {"L3", "Thumb L"},
        };
        for (const auto& m : map) out += std::string("    ") + m[0] + ": " + yamlQuote(m[1]) + "\n";
    }
    return out;
}

}  // namespace

bool prepareRpcs3(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                  const std::string& player, int players, quality::Level level, bool* missing,
                  std::string* err) {
    // The download installs it; without it RPCS3 opens its own "no firmware"
    // box over the game, so the console refuses first.
    if (readFile(firmwareNote(e)).empty()) {
        *missing = true;
        return false;
    }
    const std::string config = rpcs3Config(e);
    storage::makeDirs(config + "/GuiConfigs");
    storage::makeDirs(config + "/input_configs/global");

    // THIS PERSON'S HARD DRIVE FOR THIS GAME, as Eden's save folder is: the
    // game's saves, its licence and its trophies, and nobody else's.
    const std::string hdd0 = saveDir + "/hdd0";
    const std::string user = hdd0 + "/home/" + kPs3User;
    storage::makeDirs(user + "/savedata");
    storage::makeDirs(user + "/exdata");
    // The name some games show, cut to the PS3's sixteen bytes at a whole
    // character.
    std::string name = player.empty() ? std::string("Player") : player;
    if (name.size() > 16) {
        size_t cut = 16;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
        name.resize(cut);
    }
    writeFile(user + "/localusername", name);
    // THE LICENCE, where RPCS3 says it must be: exdata, lower-case `.rap`
    // (its own error, Crypto/unself.cpp). It stays in the game's folder too,
    // so a second person gets it.
    for (const std::string& f : listDir(entryPath))
        if (endsWithNoCase(f, ".rap"))
            placeFile(entryPath + "/" + f,
                      user + "/exdata/" + f.substr(0, f.size() - 4) + ".rap");

    const std::string vfs = "/dev_hdd0/: " + yamlQuote(hdd0 + "/") + "\n" +
                            "/dev_flash/: " + yamlQuote(rpcs3Flash(e) + "/") + "\n";
    const std::string gui = config + "/GuiConfigs/CurrentSettings.ini";
    const std::string gui_text = applyIni(readFile(gui), {
        // "Exit Game?" on the close the console sends, and the welcome.
        {"main_window", "confirmationBoxExitGame", "false"},
        {"main_window", "confirmationBoxBootGame", "false"},
        {"main_window", "infoBoxEnabledWelcome", "false"},
        {"main_window", "infoBoxEnabledInstallPKG", "false"},
        {"main_window", "infoBoxEnabledInstallPUP", "false"},
        {"main_window", "confirmationObsoleteCfg", "false"},
    }, /*withDefaults=*/false);
    // VSYNC ON (#209, MMagTech 2026-10-04), off by RPCS3's default. Under
    // gamescope a frame shown without it waits in a one-picture mailbox and
    // the next replaces it, so some never reach the screen (PS2 lost 4 in
    // 60, #226). With it they queue. On a VRR television it costs nothing.
    // The key is "VSync Mode" (Disabled, Adaptive, Full) in the pinned RPCS3
    // (Emu/system_config.h); a "VSync: true" line is ignored. Full is FIFO;
    // Adaptive asks for mailbox first (VK/vkutils/swapchain.cpp).
    const std::string video = "Video:\n  VSync Mode: Full\n" + quality::rpcs3(level);
    if (!writeFile(config + "/vfs.yml", vfs) || !writeFile(config + "/config.yml",
                   std::string(kRpcs3Settings) + video) ||
        !writeFile(gui, gui_text) ||
        !writeFile(config + "/input_configs/global/Default.yml", rpcs3Controls(players))) {
        *err = "could not write RPCS3's settings in " + config;
        return false;
    }
    // Its list of games held outside its hard drive, one line per game it
    // has started; every game here is, so it would only ever grow.
    ::unlink((config + "/games.yml").c_str());
    return true;
}

// What installGame does for a PS3 game, on the download worker: the system
// software, then every package in the folder, then what to start.
bool installRpcs3(const Emulator& e, const std::string& entryPath,
                  const std::atomic<bool>& cancel, std::string* romPath, std::string* message) {
    if (!installPs3Firmware(e, cancel, message)) return false;
    if (!installPackages(e, entryPath, cancel, message)) return false;
    *romPath = bootPath(entryPath);
    if (romPath->empty()) {
        std::fprintf(stderr, "[standalone] nothing to start in %s\n", entryPath.c_str());
        *message = "Couldn't open this game's file";
        return false;
    }
    return true;
}

}  // namespace cab::standalone::detail
