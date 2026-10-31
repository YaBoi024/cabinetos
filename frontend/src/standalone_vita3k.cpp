// PlayStation Vita, through Vita3K in the image: the system software and the
// font package installed once per console, and each game installed into its
// own folder, both on the download worker (installVita3k, for installGame);
// the settings, the game and this person's saves laid out where Vita3K looks
// before every start (prepareVita3k); and its command line (vita3kArgs).
// docs/PROJECT.md, open question 49, "Decided, 2026-10-20: the build plan";
// issue #198.

#include "standalone_internal.h"

#include "proc.h"
#include "storage.h"
#include "vpad.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>
#include <vector>

namespace cab::standalone::detail {
namespace {

// --- Vita3K ----------------------------------------------------------------
//
// WHERE EVERYTHING GOES, measured on the A9 2026-10-20 with the build the
// image pins (build_files/install-vita3k.sh):
//
//   emulators/vita3k/Vita3K/   its settings (config.yml), its log, and the
//                              shader cache (shaders/<title ID>), shared by
//                              every game and person as Xenia's is
//   emulators/vita3k/fs/       the Vita's own storage: the system software
//                              (os0/, vs0/, 112 MB) and the fonts (sa0/),
//                              once per console; and ux0/, where the game and
//                              the person's saves are linked in before every
//                              start
//   <the game's folder>/vita/  the installed game (ux0/app/<title ID>) and
//                              its licence (ux0/license/<title ID>): removed
//                              with the game
//   <the person's folder>/savedata/  the game's saves, one folder for each
//                              save directory the game names
//   <the person's folder>/trophy/    the game's trophies, which stay on the
//                              console, as a PS3 game's do in its hdd0
//
// So two people and two games never share a save or a trophy, the zip that
// travels is Vita3K's own `savedata/` layout, and a removed game takes its
// install with it. Vita3K reads them all through links in fs/ux0, which it
// follows as it would folders (measured: Shovel Knight booted from its
// linked folder and made its save folder through the link).

// The Vita's storage, its two install markers, and the note of what RomM's
// files were when they went in.
std::string vitaFs(const Emulator& e) { return home(e) + "/fs"; }
// What Vita3K installs from each of Sony's two files, which both carry the
// name PSP2UPDAT.PUP from Sony and are told apart by this, never by name:
// the system software fills vs0/ (141 modules in sys/external) and os0/, the
// font package fills sa0/ (measured: 1.2 s and 1.1 s on the A9).
bool hasSystemSoftware(const Emulator& e) { return isDir(vitaFs(e) + "/vs0/sys/external"); }
bool hasFonts(const Emulator& e) { return isDir(vitaFs(e) + "/sa0/data/font/pvf"); }
std::string firmwareNote(const Emulator& e) { return vitaFs(e) + "/.cabinetos-firmware"; }

// Where an install's settings and log go while it runs, deleted after.
constexpr const char* kScratch = ".install";

// THE SETTINGS, WRITTEN WHOLE BEFORE EVERY RUN, install or game: Vita3K reads
// the default config.yml (XDG_CONFIG_HOME/Vita3K/) for `-r` before anything
// else, and the very first run with none there crashes a firmware install
// (measured: create_directories("") on its empty storage path, exit 134).
// Keys are Vita3K's (config/config.h); everything not here is its default,
// which it writes back in full at start.
//
//   pstv-mode      each controller its own player, and L2, R2, L3 and R3 to
//                  games that ask for the PS TV's pad (ctrl.cpp)
//   front/back-camera-type 0, a plain colour: a PS TV has no camera, and 2,
//                  Vita3K's default, is the machine's own webcam
//   disable-motion no gyro, as on the PS TV (the virtual pads have none)
//   http-enable, discord-rich-presence  nothing leaves the console
//   show-compile-shaders  no notice over the game while shaders build
//   validation-layer  Vulkan's debugging layer, only ever a cost
//   log-level 2    INFO, which still has every error the console watches for
std::string configText(const std::string& prefPath, quality::Level level) {
    return "---\n"
           "backend-renderer: Vulkan\n"
           "pref-path: " + yamlQuote(prefPath) + "\n"
           "user-id: \"00\"\n"
           "pstv-mode: true\n"
           "front-camera-type: 0\n"
           "back-camera-type: 0\n"
           "disable-motion: true\n"
           "http-enable: false\n"
           "discord-rich-presence: false\n"
           "show-compile-shaders: false\n"
           "validation-layer: false\n"
           "performance-overlay: false\n"
           "show-welcome: false\n"
           "check-for-updates: false\n"
           "log-level: 2\n" +
           quality::vita3k(level) + "...\n";
}

// The environment every Vita3K run needs, whatever it is for. Vita3K's
// AppImage is run unpacked, as Xenia's is, so two things its own launcher
// would have done are said here: APPDIR, where its static assets are (without
// it, it crashes at boot looking for its built-in shaders), and its own Vulkan
// loader for the SDL inside it. THE LOADER IS A FIX FOR A CRASH: the
// AppImage's loader has no `libvulkan.so.1` name, so SDL loads the system's
// while Vita3K made its instance with its own, and every Vulkan start crashed
// in the first call between them (measured on the A9 2026-10-20; OpenGL ran).
// Named, it runs on Vulkan. The name is a link the image's install makes
// (build_files/install-vita3k.sh). Only the console's virtual controllers, and none
// of a real pad's HID protocol, as for the others.
std::vector<std::string> vitaEnv(const Emulator& e) {
    const std::string bin = binaryOf(e);
    // <root>/bin/Vita3K
    const std::string root = bin.substr(0, bin.rfind("/bin/"));
    return {
        "APPDIR=" + root,
        "SDL_VULKAN_LIBRARY=" + root + "/lib/libvulkan.so.1",
        "SDL_JOYSTICK_HIDAPI=0",
        "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0",
    };
}

// VITA3K WITHOUT A WINDOW, to install something, and its log afterwards.
// `scratch` holds the settings and the log for this run only and is deleted
// after, so an install never touches a running game's. A game's install is a
// start whose window cannot open (SDL's dummy video, which has no Vulkan):
// Vita3K installs the archive, then tries to boot it and gives up at the
// window, exit 255, with nothing on the screen and no sound or controller
// opened. So the log decides, never the exit code.
std::string vitaHeadless(const Emulator& e, const std::string& scratch, const std::string& prefPath,
                         const std::vector<std::string>& what, const std::atomic<bool>& cancel) {
    removeTree(scratch);
    storage::makeDirs(scratch + "/Vita3K");
    storage::makeDirs(prefPath);
    writeFile(scratch + "/Vita3K/config.yml", configText(prefPath, quality::Level::Performance));
    std::vector<std::string> args = {
        "prlimit", "--core=0", "--",
        "env",
        "XDG_CONFIG_HOME=" + scratch, "XDG_CACHE_HOME=" + scratch, "XDG_DATA_HOME=" + scratch,
        "SDL_VIDEO_DRIVER=dummy", "SDL_AUDIO_DRIVER=dummy", "SDL_CAMERA_DRIVER=dummy",
    };
    for (const std::string& v : vitaEnv(e)) args.push_back(v);
    args.push_back(binaryOf(e));
    args.insert(args.end(), what.begin(), what.end());
    // Half an hour, as for a PS3 install: the largest Vita game is 3.2 GB.
    const proc::Result r = proc::run(args, 30 * 60, &cancel);
    std::fprintf(stderr, "[standalone] vita3k %s: exit %d%s\n", what.back().c_str(), r.status,
                 r.timedOut ? " (stopped)" : "");
    std::string log = readFile(scratch + "/Vita3K/vita3k.log");
    removeTree(scratch);
    return log;
}

// THE VITA'S SYSTEM SOFTWARE AND FONTS, once per console and again only when
// RomM's files change, as the PS3's are (installPs3Firmware). Every `.pup` in
// `bios/` but the PS3's is offered to Vita3K, and what it installed from each
// says which it was. docs/PROJECT.md question 49, decision 7.
bool installVitaFirmware(const Emulator& e, const std::atomic<bool>& cancel,
                         std::string* message) {
    std::vector<std::string> pups;
    std::string stamp;
    for (const std::string& name : listDir(storage::biosDir())) {
        std::string lower = name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (!endsWithNoCase(lower, ".pup") || lower.compare(0, 8, "ps3updat") == 0) continue;
        const std::string p = storage::biosDir() + "/" + name;
        struct stat st;
        if (::stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        pups.push_back(p);
        stamp += name + " " + std::to_string(st.st_size) + "\n";
    }
    const auto missing = [&](bool somethingFailed) {
        if (!hasSystemSoftware(e))
            *message = somethingFailed ? "Couldn't install the PlayStation Vita firmware"
                                       : "No PlayStation Vita firmware on the server";
        else
            *message = "No PlayStation Vita font package on the server";
        return false;
    };
    // Nothing on the server, or no server: whatever is installed stands.
    if (pups.empty()) return (hasSystemSoftware(e) && hasFonts(e)) || missing(false);
    if (readFile(firmwareNote(e)) == stamp && hasSystemSoftware(e) && hasFonts(e)) return true;

    bool somethingFailed = false;
    for (const std::string& pup : pups) {
        const bool hadSystem = hasSystemSoftware(e), hadFonts = hasFonts(e);
        const std::string log =
            vitaHeadless(e, home(e) + "/" + kScratch, vitaFs(e), {"--firmware", pup}, cancel);
        if (cancel.load()) return false;
        const bool done = log.find("Firmware installation progress: 100%") != std::string::npos;
        const char* what = !hadSystem && hasSystemSoftware(e) ? "the system software"
                           : !hadFonts && hasFonts(e)        ? "the font package"
                           : done                            ? "nothing new"
                                                             : "nothing; it did not install";
        std::fprintf(stderr, "[standalone] Vita firmware %s: %s\n", pup.c_str(), what);
        if (!done) somethingFailed = true;
    }
    if (!hasSystemSoftware(e) || !hasFonts(e)) return missing(somethingFailed);
    writeFile(firmwareNote(e), stamp);
    return true;
}

// EVERY ARCHIVE IN THE GAME'S FOLDER, installed into the folder's own Vita
// storage, largest first (a game before a patch or add-on for it, which
// Vita3K installs only on top of the game), then deleted. Vita3K extracts it
// and decrypts it with the licence inside (`work.bin`), about twice the
// archive's size on the disk while it works (measured: Shovel Knight, 138 MB,
// 0.8 s, 265 MB at the peak), and logs "<title> [<title ID>] installed
// successfully!" when it is in.
bool installArchives(const Emulator& e, const std::string& entryPath,
                     const std::atomic<bool>& cancel, std::string* message) {
    std::vector<std::pair<int64_t, std::string>> archives;
    for (const std::string& name : listDir(entryPath)) {
        struct stat st;
        const std::string p = entryPath + "/" + name;
        if (name[0] != '.' && (endsWithNoCase(name, ".zip") || endsWithNoCase(name, ".vpk")) &&
            ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            archives.emplace_back(st.st_size, name);
    }
    if (archives.empty()) return true;
    std::sort(archives.begin(), archives.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    const std::string gameFs = entryPath + "/" + kVitaGameFs;
    std::string note = readFile(entryPath + "/" + kInstalledNote);
    for (const auto& [bytes, name] : archives) {
        const std::string archive = entryPath + "/" + name;
        const std::string log = vitaHeadless(e, entryPath + "/" + kScratch, gameFs,
                                             {"--frontend", "sdl", archive}, cancel);
        if (cancel.load() || log.find("installed successfully!") == std::string::npos) {
            std::fprintf(stderr, "[standalone] %s did not install\n", archive.c_str());
            *message = "Couldn't install this game";
            return false;
        }
        note += std::to_string(bytes) + " " + name + "\n";
        std::fprintf(stderr, "[standalone] installed %s (%lld bytes)\n", name.c_str(),
                     static_cast<long long>(bytes));
    }
    // Only now: an archive is deleted once the game it became is in place.
    writeFile(entryPath + "/" + kInstalledNote, note);
    for (const auto& a : archives) ::unlink((entryPath + "/" + a.second).c_str());
    return true;
}

// XML's five, for the name a game shows.
std::string xmlEscape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace

std::string vita3kSaveRoot(const std::string& saveDir) { return saveDir + "/savedata"; }

bool prepareVita3k(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                   const std::string& player, int players, quality::Level level, bool* missing,
                   std::string* err) {
    // The download installs them; without them Vita3K starts and the game
    // stops at its first system call, so the console refuses first.
    if (!hasSystemSoftware(e) || !hasFonts(e)) {
        *missing = true;
        return false;
    }
    const std::string app = bootPath(entryPath);
    if (app.empty()) {
        *err = "no installed Vita game in " + entryPath;
        return false;
    }
    const std::string id = app.substr(app.rfind('/') + 1);
    const std::string fs = vitaFs(e);
    const std::string ux0 = fs + "/ux0";
    for (const char* d : {"/app", "/license", "/user/00"}) storage::makeDirs(ux0 + d);
    // ONLY THIS GAME IN ITS LIST, so the last game's link never leads to a
    // folder that has since been removed.
    for (const char* d : {"/app", "/license"})
        for (const std::string& name : listDir(ux0 + d)) removeTree(ux0 + d + "/" + name);
    const std::string licence = entryPath + "/" + kVitaGameFs + "/ux0/license/" + id;
    const std::string saves = vita3kSaveRoot(saveDir);
    const std::string trophies = saveDir + "/trophy";
    storage::makeDirs(saves);
    storage::makeDirs(trophies);
    if (!placeLink(app, ux0 + "/app/" + id) ||
        (isDir(licence) && !placeLink(licence, ux0 + "/license/" + id)) ||
        !placeLink(saves, ux0 + "/user/00/savedata") ||
        !placeLink(trophies, ux0 + "/user/00/trophy")) {
        *err = "could not link the game and its saves into " + ux0;
        return false;
    }
    // THE NAME A GAME SHOWS for the person playing, Vita3K's one user named
    // for them, cut to the sixteen bytes an online ID may have (longer, and
    // Vita3K refuses the ID to the game: SceNpManager.cpp) at a whole
    // character.
    std::string name = player.empty() ? std::string("Player") : player;
    if (name.size() > 16) {
        size_t cut = 16;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
        name.resize(cut);
    }
    const std::string user =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<user id=\"00\" name=\"" + xmlEscape(name) + "\">\n"
        "\t<theme use-background=\"true\">\n\t\t<content-id>default</content-id>\n\t</theme>\n"
        "\t<start-screen type=\"default\">\n\t\t<path />\n\t</start-screen>\n"
        "\t<backgrounds />\n</user>\n";
    // EACH PLAYER'S CONTROLLER, BY NAME, as for Xenia: SDL inside Vita3K
    // reads this list (SDL_GAMECONTROLLERCONFIG_FILE, vita3kArgs) and sees
    // only the virtual controllers, in the console's order, which pstv-mode
    // makes one player each.
    std::string db;
    for (int p = 0; p < std::min(players, cab::vpad::kMaxPlayers); ++p)
        db += cab::vpad::sdlMapping(p) + "\n";
    const std::string config = home(e) + "/Vita3K";
    storage::makeDirs(config);
    if (!writeFile(ux0 + "/user/00/user.xml", user) ||
        !writeFile(home(e) + "/gamecontrollerdb.txt", db) ||
        !writeFile(config + "/config.yml", configText(fs, level))) {
        *err = "could not write Vita3K's settings in " + home(e);
        return false;
    }
    return true;
}

bool installVita3k(const Emulator& e, const std::string& entryPath,
                   const std::atomic<bool>& cancel, std::string* romPath, std::string* message) {
    if (!installVitaFirmware(e, cancel, message)) return false;
    if (!installArchives(e, entryPath, cancel, message)) return false;
    *romPath = bootPath(entryPath);
    if (romPath->empty()) {
        std::fprintf(stderr, "[standalone] nothing to start in %s\n", entryPath.c_str());
        *message = "Couldn't open this game's file";
        return false;
    }
    return true;
}

void vita3kArgs(std::vector<std::string>& full, const Emulator& e, const std::string& dir,
                const std::string& romPath) {
    for (const std::string& v : vitaEnv(e)) full.insert(full.end() - 1, v);
    full.insert(full.end() - 1, "XDG_DATA_HOME=" + dir);
    full.insert(full.end() - 1, "SDL_GAMECONTROLLERCONFIG_FILE=" + dir + "/gamecontrollerdb.txt");
    // gamescope's Xwayland, whose touch is what reaches the game from the
    // console's pointer (vtouch.h).
    full.insert(full.end() - 1, "SDL_VIDEO_DRIVER=x11");
    // The SDL frontend: a plain window that runs the one game and quits with
    // it, never Vita3K's own interface (question 49, decision 1). The game by
    // its title ID, the name of its installed folder; never its path, which
    // would install it again at every start.
    full.insert(full.end(), {"--frontend", "sdl", "-r", romPath.substr(romPath.rfind('/') + 1)});
}

}  // namespace cab::standalone::detail
