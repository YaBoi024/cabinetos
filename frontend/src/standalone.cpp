#include "standalone.h"

#include "proc.h"
#include "standalone_internal.h"
#include "storage.h"
#include "xtitles.h"

#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace cab::standalone {
namespace {

// Eden and xemu are pinned in system_files/usr/share/cabinetos/flatpaks.list.
// RPCS3, Xenia, Cemu and Vita3K are in the image (build_files/install-*.sh).
const Emulator kEmulators[] = {
    // Eden's words for a game it cannot load (yuzu/main_window.cpp, before its
    // "Error while loading ROM!" box).
    {"eden", "dev.eden_emu.eden", nullptr, "eden", "user/log/eden_log.txt",
     {"Failed to load ROM", "Failed to obtain loader"},
     // Eden's loader codes (core/loader/loader.h) for a key the file needs and
     // the keys do not hold: 21 is a master key newer than the file has, which
     // is what Metroid Prime 4 Beyond gave against RomM's v19 keys on
     // 2026-09-28; 18 and 19 are its title key and title key key.
     {"(Error 21)", "(Error 18)", "(Error 19)", "(Error 13)"},
     nullptr, nullptr,
     // "Eden | <version> | <compiler>" idle, and the game's name, its version
     // and the GPU after that while one runs (main_window.cpp,
     // UpdateWindowTitle).
     "Eden |", 5, nullptr,
     // notesPlaying (#213): Eden writes its saves straight into the person's
     // folder (user/save/), as Xenia and Cemu do, so a session cut off by a
     // crash, a power cut or an app restart is sent at the next start or
     // launch instead of being overwritten by the older copy on RomM.
     "nsp|xci", "keys", false, false, false, false, false, true, false},

    // PLAYSTATION 3. Its log sits beside its cache, in the game's folder
    // (XDG_CACHE_HOME, rpcs3/ appended: Utilities/File.cpp, get_cache_dir).
    // A load that fails is a FATAL line, `·F ` in its log, and RPCS3 then
    // opens its error box as a second process (rpcs3.cpp,
    // report_fatal_error). The licence line is Crypto/unself.cpp's. No title
    // check: started with --no-gui, RPCS3 quits by itself when the game
    // ends (gui_application.cpp, try_to_quit).
    {"rpcs3", nullptr, "/usr/lib/cabinetos/rpcs3/usr/bin/rpcs3", "rpcs3", "rpcs3/RPCS3.log",
     {"\xC2\xB7" "F ", nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     "Failed to locate the game license file",
     "Emulation has been frozen",
     nullptr, 0, nullptr,
     // notesPlaying (#213): its savedata is in the person's folder too
     // (hdd0/home/<user>/savedata), the same case as Eden's.
     "iso|pkg", "firmware", true, true, true, false, false, true, false},

    // ORIGINAL XBOX. xemu keeps no log of its own; what it prints is it. A
    // game is a folder, as a PS3 game is, because its hard drive lives beside
    // the disc (`installs`: fetched file by file, then installGame). No title
    // check: xemu's window title never names the game (ui/xemu.c). SIGTERM
    // closes it cleanly: qemu drains and closes the drive first
    // (system/runstate.c, qemu_cleanup). Issue #172.
    {"xemu", "app.xemu.xemu", nullptr, "xemu", nullptr,
     {nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     nullptr, nullptr,
     nullptr, 0, nullptr,
     "iso|xiso", "BIOS", true, false, false, true, true, false, false},

    // XBOX 360. Xenia Edge's own Linux build, in the image
    // (build_files/install-xenia.sh). Its log is where the console tells it
    // (`--log_file`); a load that fails, and any fatal error later, is an
    // `x>` line there, then an error box nobody can press (base/logging.cc,
    // FatalError). SIGTERM does nothing: SDL takes it and Edge never reads
    // SDL's quit (measured on the A9, 2026-09-29), so it is closed by its
    // window, which exits 0 at once. A game that quits to the dashboard
    // leaves Edge open on its own list, and its window title loses the
    // `| [TITLEID vVER] Name` part: `Xenia-edge (...) <x64>` idle,
    // `Xenia-edge (...) | [4D530AA4 v0.0.0.12] Forza Horizon 2 <...>` in a
    // game (emulator_window.cc). Issue #192; open question 34.
    {"xenia", nullptr, "/usr/lib/cabinetos/xenia/usr/bin/xenia_edge", "xenia_edge", "xenia.log",
     {"Failed to launch target", "x> "},
     {nullptr, nullptr, nullptr, nullptr},
     nullptr,
     "x> ",
     "Xenia-edge", 2, nullptr,
     "iso|xex|zar", "", false, false, true, false, false, true, false},

    // WII U. Cemu main, built from source at a pin (cores/build-cemu.sh) and in
    // the image at /usr/lib/cabinetos/cemu. Its log is under its home
    // (XDG_DATA_HOME/Cemu). Started with `-g`, it builds no game list and
    // CLOSES ITSELF when the game ends, returning the game's own exit status
    // (OnRequestGameExit, CemuApp::OnExit), so no title watch. A game it
    // cannot open is a desktop box titled "Error" over its empty window, and
    // only some of those reach the log, so both are watched. SIGTERM is
    // `_Exit(0)`, no clean shutdown; the window close saves and stops the
    // game (measured on the A9, 2026-10-01: exit 0, save intact). Mid-game,
    // an unrecoverable Vulkan error is logged and thrown. Issue #174; open
    // question 36.
    {"cemu", nullptr, "/usr/lib/cabinetos/cemu/bin/Cemu", "Cemu", "Cemu/log.txt",
     {"Mounting failed", "Unable to find RPX executable"},
     {nullptr, nullptr, nullptr, nullptr},
     nullptr,
     "Unrecoverable error in Vulkan",
     nullptr, 0, "Error",
     "wua", "", false, false, true, false, false, true, false},

    // PLAYSTATION VITA. Vita3K's own build, in the image
    // (build_files/install-vita3k.sh), started through its SDL frontend: a
    // plain window that runs the one game and quits with it, with the game's
    // own exit status, so no title watch and no error box. A game it cannot
    // start is one of these two lines in its log (sdl-frontend/session.cpp),
    // and it exits by itself, 255. SIGTERM is SDL's quit, which stops the
    // game and exits (measured on the A9 2026-10-20: 0.1 s). Its log is under
    // its home (XDG_CACHE_HOME/Vita3K). A game is installed before it runs,
    // as a PS3 package is (`installs`): its zip, with the licence inside, is
    // extracted and decrypted into the game's folder by Vita3K itself.
    // Issue #198; open question 49.
    {"vita3k", nullptr, "/usr/lib/cabinetos/vita3k/bin/Vita3K", "Vita3K", "Vita3K/vita3k.log",
     {"Failed to load or start the app session", "in apps list"},
     {nullptr, nullptr, nullptr, nullptr},
     nullptr,
     nullptr,
     nullptr, 0, nullptr,
     // notesPlaying: Vita3K writes the saves straight into the person's
     // folder, through the link standalone_vita3k.cpp makes, as Xenia does.
     "zip|vpk", "firmware", true, false, false, false, false, true, true},
};

// How long a program that was asked to close gets before it is made to.
// Eden took 0.8 s on the A9; ten is room for a slow disk flushing a save.
constexpr int64_t kStopGraceMs = 10 * 1000;
// A failed load shows the emulator's own error box and waits for a click
// nobody can make, so its log is watched for this long after a start.
constexpr int64_t kLoadWatchMs = 60 * 1000;

}  // namespace

// The helpers, declared in standalone_internal.h for the emulators' files.
namespace detail {

// WHERE AN EMULATOR IN THE IMAGE IS, or, for development only,
// `CABINETOS_BINARY_<core>` (CABINETOS_BINARY_rpcs3=...), so a build of the
// frontend can be tried against an emulator before any image carries it. The
// console's session sets none.
std::string binaryOf(const Emulator& e) {
    const std::string var = std::string("CABINETOS_BINARY_") + e.core;
    if (const char* v = std::getenv(var.c_str()); v && *v) return v;
    return e.binary ? e.binary : "";
}

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFile(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// Copies `from` to `to` unless `to` already holds the same bytes.
bool placeFile(const std::string& from, const std::string& to) {
    const std::string bytes = readFile(from);
    if (bytes.empty()) return false;
    if (readFile(to) == bytes) return true;
    return writeFile(to, bytes);
}

// Sets `key=value` in `[section]` of an INI file, keeping everything else the
// emulator wrote. Eden rewrites the whole file on exit, so the console's
// choices are laid over whatever is there before every start rather than
// written once.
//
// AND FOR EDEN EACH ONE NEEDS A `key\default=false` BESIDE IT. Eden reads a
// setting's value only when that flag says it is not the default; a value on
// its own is silently ignored (frontend_common/config.cpp,
// ReadSettingGeneric). `withDefaults` off for anything else (RPCS3's Qt
// settings file).
std::string applyIni(const std::string& text, const std::vector<IniSet>& sets,
                     bool withDefaults) {
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }
    auto sectionOf = [&](size_t upto) {
        std::string s;
        for (size_t i = 0; i < upto; ++i)
            if (!lines[i].empty() && lines[i].front() == '[' && lines[i].back() == ']')
                s = lines[i].substr(1, lines[i].size() - 2);
        return s;
    };
    auto setOne = [&](const std::string& section, const std::string& key,
                      const std::string& value) {
        const std::string prefix = key + "=";
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].compare(0, prefix.size(), prefix) != 0) continue;
            if (sectionOf(i) != section) continue;
            lines[i] = prefix + value;
            return;
        }
        // Not there: at the end of its section, or a new section at the end.
        const std::string header = "[" + section + "]";
        auto at = std::find(lines.begin(), lines.end(), header);
        if (at == lines.end()) {
            if (!lines.empty() && !lines.back().empty()) lines.push_back("");
            lines.push_back(header);
            lines.push_back(prefix + value);
            return;
        }
        auto end = at + 1;
        while (end != lines.end() && !(!end->empty() && end->front() == '[')) ++end;
        while (end != at + 1 && (end - 1)->empty()) --end;
        lines.insert(end, prefix + value);
    };
    for (const IniSet& s : sets) {
        if (withDefaults) setOne(s.section, std::string(s.key) + "\\default", "false");
        setOne(s.section, s.key, s.value);
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

bool isFile(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool removeTree(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return ::unlink(path.c_str()) == 0;
    if (DIR* d = opendir(path.c_str())) {
        while (dirent* e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
            removeTree(path + "/" + e->d_name);
        }
        closedir(d);
    }
    return ::rmdir(path.c_str()) == 0;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
            names.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool endsWithNoCase(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    return true;
}

// A YAML scalar that means exactly this string: a path can hold ": " or
// start with something YAML reads as syntax.
std::string yamlQuote(const std::string& v) {
    std::string out = "\"";
    for (const char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

}  // namespace detail

// What the emulators' files share (standalone_internal.h), used here bare.
using namespace detail;

const Emulator* find(const std::string& core) {
    for (const Emulator& e : kEmulators)
        if (core == e.core) return &e;
    return nullptr;
}

std::vector<const Emulator*> all() {
    std::vector<const Emulator*> out;
    for (const Emulator& e : kEmulators) out.push_back(&e);
    return out;
}

bool installed(const Emulator& e) {
    if (e.binary) return ::access(binaryOf(e).c_str(), X_OK) == 0;
    // The system installation, where cabinetos-flatpak-setup puts it.
    return isDir(std::string("/var/lib/flatpak/app/") + e.flatpak + "/current/active/files");
}

std::string home(const Emulator& e) { return storage::emulatorsDir() + "/" + e.core; }

bool installedFile(const std::string& entryPath, const std::string& fileName,
                   int64_t sizeBytes) {
    const std::string line = std::to_string(sizeBytes) + " " + fileName;
    std::istringstream in(readFile(entryPath + "/" + kInstalledNote));
    for (std::string l; std::getline(in, l);)
        if (l == line) return true;
    return false;
}

std::string bootPath(const std::string& entryPath) {
    // An installed game is a title directory with USRDIR/EBOOT.BIN; anything
    // else in the folder is the emulator's (rpcs3/) or the console's (dot).
    for (const std::string& name : listDir(entryPath)) {
        if (name[0] == '.') continue;
        const std::string eboot = entryPath + "/" + name + "/USRDIR/EBOOT.BIN";
        if (isFile(eboot)) return eboot;
    }
    // An installed Vita game, in the folder's own Vita storage
    // (standalone_vita3k.cpp): what Vita3K is started on, by its title ID.
    {
        const std::string apps = entryPath + "/" + kVitaGameFs + "/ux0/app";
        for (const std::string& id : listDir(apps))
            if (isFile(apps + "/" + id + "/eboot.bin")) return apps + "/" + id;
    }
    // A stamped decrypted disc image, which RPCS3 opens as it is (open
    // question 19). The largest, should there ever be more than one.
    std::string pick;
    int64_t pickSize = -1;
    for (const std::string& name : listDir(entryPath)) {
        struct stat st;
        const std::string p = entryPath + "/" + name;
        if (endsWithNoCase(name, ".iso") && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            st.st_size > pickSize) {
            pick = p;
            pickSize = st.st_size;
        }
    }
    return pick;
}

bool installGame(const Emulator& e, const std::string& entryPath,
                 const std::atomic<bool>& cancel, std::string* romPath,
                 std::string* message) {
    if (std::strcmp(e.core, "xemu") == 0)
        return installXbox(entryPath, cancel, romPath, message);
    if (std::strcmp(e.core, "vita3k") == 0)
        return installVita3k(e, entryPath, cancel, romPath, message);
    if (std::strcmp(e.core, "rpcs3") != 0) {
        *message = "Couldn't install this game";
        return false;
    }
    return installRpcs3(e, entryPath, cancel, romPath, message);
}

bool prepare(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
             const std::string& player, int players, quality::Level level, bool* missingKeys,
             std::string* err) {
    *missingKeys = false;
    storage::makeDirs(saveDir);
    if (std::strcmp(e.core, "eden") == 0)
        return prepareEden(e, saveDir, player, players, level, missingKeys, err);
    if (std::strcmp(e.core, "rpcs3") == 0)
        return prepareRpcs3(e, entryPath, saveDir, player, players, level, missingKeys, err);
    if (std::strcmp(e.core, "xemu") == 0)
        return prepareXemu(e, entryPath, players, level, missingKeys, err);
    if (std::strcmp(e.core, "xenia") == 0) return prepareXenia(e, saveDir, err);
    if (std::strcmp(e.core, "cemu") == 0) return prepareCemu(e, saveDir, players, err);
    if (std::strcmp(e.core, "vita3k") == 0)
        return prepareVita3k(e, entryPath, saveDir, player, players, level, missingKeys, err);
    *err = std::string("nothing prepares ") + e.core;
    return false;
}

std::string saveRoot(const Emulator& e, const std::string& saveDir) {
    // Xbox: a copy of the drive's E: as far as saves go, `UDATA/<title ID>/`
    // and `TDATA/<title ID>/`, so the zip drops straight onto any xemu drive.
    if (std::strcmp(e.core, "xemu") == 0) return saveDir + "/E";
    // Xbox 360: Edge's content root, `<XUID>/` and `0000000000000000/`.
    if (std::strcmp(e.core, "xenia") == 0) return xeniaContent(saveDir);
    // Wii U: the games' save folders in this person's mlc, one per title,
    // `<title low>/user/` and `<title low>/meta/`.
    if (std::strcmp(e.core, "cemu") == 0) return cemuMlc(saveDir) + "/usr/save/00050000";
    if (std::strcmp(e.core, "rpcs3") == 0)
        return saveDir + "/hdd0/home/" + kPs3User + "/savedata";
    if (std::strcmp(e.core, "eden") == 0) return edenSaveRoot(e, saveDir);
    // Vita: the person's `savedata/`, linked in as Vita3K's ux0/user/00/savedata.
    if (std::strcmp(e.core, "vita3k") == 0) return vita3kSaveRoot(saveDir);
    return {};
}

bool beforeStart(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                 const std::string& player, const std::string& note, std::string* err) {
    // The note first, in the person's folder: from here until the save is
    // zipped, the emulator writes there and nothing has been sent.
    if (e.notesPlaying && !writePlayingNote(saveDir, note)) {
        *err = "could not mark " + saveDir + " as playing";
        return false;
    }
    if (std::strcmp(e.core, "xenia") == 0) return xeniaBeforeStart(saveDir, player, err);
    if (std::strcmp(e.core, "xemu") != 0) return true;
    return xemuBeforeStart(e, entryPath, saveDir, note, err);
}

bool afterEnd(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
              std::string* err) {
    if (std::strcmp(e.core, "xemu") != 0) return true;
    return xemuAfterEnd(e, entryPath, saveDir, err);
}

// The note in the person's folder goes, whichever wrote it: beforeStart
// (`notesPlaying`), or another person's start that brought this person's
// Xbox saves off the drive for them (App::recoverDriveSaves). Nothing else
// is ever there, and one that is not there costs nothing.
void finished(const Emulator&, const std::string& saveDir) {
    removePlayingNote(saveDir);
}

std::string playingNote(const std::string& folder) {
    return readFile(folder + "/" + kPlayingNote);
}

bool hasPlayingNote(const std::string& folder) {
    return isFile(folder + "/" + kPlayingNote);
}

bool writePlayingNote(const std::string& folder, const std::string& note) {
    return writeFile(folder + "/" + kPlayingNote, note);
}

void removePlayingNote(const std::string& folder) {
    ::unlink((folder + "/" + kPlayingNote).c_str());
}

bool Run::start(const Emulator& e, const std::string& romPath, const std::string& entryPath,
                const std::string& saveDir, quality::Level level, std::string* err) {
    emu_ = &e;
    ended_ = End::None;
    program_ = -1;
    stopAtMs_ = 0;
    failedLoad_ = false;
    oldKeys_ = false;
    noLicence_ = false;
    froze_ = false;
    frozen_ = false;
    sawGameTitle_ = false;
    titleCheckMs_ = 0;
    errorCheckMs_ = 0;
    logCheckMs_ = 0;
    if (qmpFd_ >= 0) ::close(qmpFd_);
    qmpFd_ = -1;
    qmpCheckMs_ = 0;
    xboxTitle_ = 0;
    const std::string dir = home(e);
    // A fresh log each start, so a failure from the last game is not read as
    // this one's.
    const std::string out = storage::logsDir() + "/" + e.core + ".log";
    logPath_ = e.log ? (e.cacheInGame ? entryPath : dir) + "/" + e.log : out;
    storage::makeDirs(logPath_.substr(0, logPath_.find_last_of('/')));
    ::unlink(logPath_.c_str());
    logRead_ = 0;

    // THE SANDBOX SEES THREE THINGS OF OURS: the emulator's own home, this
    // person's save folder for this game, and the folder the game is in,
    // read-only. Nothing else of the console's and nobody else's saves. The
    // game may be on an external drive, which is why its folder is named per
    // launch rather than granted once.
    std::string gameDir = romPath;
    if (const size_t slash = gameDir.find_last_of('/'); slash != std::string::npos)
        gameDir.erase(slash);
    storage::makeDirs(storage::logsDir());

    // A PROGRAM IN THE IMAGE has no `flatpak run` to carry its settings, so
    // `env` does, and then becomes it, under bwrap: the emulator is bwrap's
    // child, found as a Flatpak's is. Its settings are in its home
    // (XDG_CONFIG_HOME); what it builds per game goes in the game's folder
    // (XDG_CACHE_HOME, when `cacheInGame`), which is how a removed game takes
    // its cache with it.
    //
    // NO EMULATOR REACHES THE NETWORK (#557; the ones inside the console's
    // program, netblock.h). bwrap gives it a network of its own with nothing
    // in it, and hides systemd-resolved's folder, whose Unix socket otherwise
    // still answers a name lookup there: measured on the A9, the Mario Kart
    // Wii server's name resolved in 0.1 s; hidden, a lookup failed in 8 ms
    // and a connection in 1 ms. Everything else is the console's own,
    // devices included, and gamescope's X11 socket is a path, so it is
    // reached as before. A Flatpak is given --unshare=network instead
    // (below, and startXemu), whose sandbox already hides the folder.
    if (e.binary) {
        // THE GRAPHICS DRIVER'S MEMORY STAYS SHARED, where the console's own
        // is, even though RPCS3's cache moves into the game's folder: Mesa
        // would otherwise follow XDG_CACHE_HOME there too. The first thing
        // RPCS3 does on every start is build its shader interpreter's
        // pipelines, which are the same for every game: 3 min 25 s on the
        // A9 the first time (Super Stardust HD, 2026-09-28), all 24 threads.
        // Shared, that is paid once per console (and again when an update
        // brings a new Mesa), not once per game. Mesa caps the folder (1 GB,
        // its default) and ages out the oldest itself. MMagTech, 2026-09-28.
        const char* xdgCache = std::getenv("XDG_CACHE_HOME");
        const char* userHome = std::getenv("HOME");
        const std::string mesaCache = xdgCache && *xdgCache
                                          ? std::string(xdgCache)
                                          : std::string(userHome ? userHome : "") + "/.cache";
        const std::vector<std::string> args = {
            "bwrap", "--dev-bind", "/", "/", "--unshare-net",
            "--tmpfs", "/run/systemd/resolve", "--",
            "env",
            "XDG_CONFIG_HOME=" + dir,
            "XDG_CACHE_HOME=" + (e.cacheInGame ? entryPath : dir),
            // Mesa adds mesa_shader_cache/ itself (util/disk_cache_os.c).
            "MESA_SHADER_CACHE_DIR=" + mesaCache,
            // gamescope's Xwayland, as for Eden below.
            "QT_QPA_PLATFORM=xcb",
            "SDL_JOYSTICK_HIDAPI=0",
            binaryOf(e),
        };
        std::vector<std::string> full = args;
        if (std::strcmp(e.core, "xenia") == 0) {
            xeniaArgs(full, dir, saveDir, logPath_, level, romPath);
        } else if (std::strcmp(e.core, "cemu") == 0) {
            cemuArgs(full, dir, romPath);
        } else if (std::strcmp(e.core, "vita3k") == 0) {
            vita3kArgs(full, e, dir, romPath);
        } else {
            full.insert(full.end(), {"--no-gui", "--fullscreen", romPath});
        }
        root_ = proc::spawn(full, out);
        if (root_ <= 0) {
            *err = "could not start " + binaryOf(e);
            root_ = -1;
            return false;
        }
        startMs_ = nowMs();
        std::fprintf(stderr, "[standalone] started %s (pid %d) on %s\n", e.program, root_,
                     romPath.c_str());
        return true;
    }

    if (std::strcmp(e.core, "xemu") == 0) return startXemu(e, romPath, dir, gameDir, out, err);

    const std::vector<std::string> args = {
        "flatpak", "run",
        "--unshare=network",
        "--cwd=" + dir,
        "--filesystem=" + dir,
        "--filesystem=" + saveDir,
        "--filesystem=" + gameDir + ":ro",
        // gamescope's Xwayland. Eden also offers Wayland, and there it puts up
        // a warning box first; the console runs on X11 anyway (lessons,
        // "THE CONSOLE RUNS ON X11").
        "--env=QT_QPA_PLATFORM=xcb",
        // Its SDL keeps its hands off the real pads' HID devices. It is given
        // virtual controllers instead (vpad.h), and two programs driving one
        // Switch or PlayStation pad's HID protocol at once fight over it.
        "--env=SDL_JOYSTICK_HIDAPI=0",
        std::string("--command=") + e.program,
        e.flatpak,
        "-f", "-g", romPath,
    };
    root_ = proc::spawn(args, out);
    if (root_ <= 0) {
        *err = "could not start flatpak";
        root_ = -1;
        return false;
    }
    startMs_ = nowMs();
    std::fprintf(stderr, "[standalone] started %s (pid %d) on %s\n", e.flatpak, root_,
                 romPath.c_str());
    return true;
}

// The emulator itself, below what was started. A program in the image is
// bwrap's one child, whatever it calls itself (Cemu names its main thread
// "cemu"); a Flatpak's is found by its name.
int Run::findProgram() const {
    return proc::findDescendant(root_, emu_->binary ? "" : emu_->program);
}

void Run::freeze() {
    if (!active() || frozen_ || stopping()) return;
    if (program_ <= 0) program_ = findProgram();
    if (program_ <= 0) return;
    ::kill(program_, SIGSTOP);
    frozen_ = true;
    std::fprintf(stderr, "[standalone] %s frozen\n", emu_->program);
}

void Run::thaw() {
    if (!frozen_) return;
    if (program_ > 0) ::kill(program_, SIGCONT);
    frozen_ = false;
    std::fprintf(stderr, "[standalone] %s thawed\n", emu_->program);
}

void Run::stop() {
    if (!active() || stopping()) return;
    thaw();
    stopAtMs_ = nowMs();
    if (program_ <= 0) program_ = findProgram();
    if (program_ > 0 && emu_->closesByWindow && gTitles.closeWindowsOf(program_) > 0) {
        std::fprintf(stderr, "[standalone] asked %s's window (pid %d) to close\n",
                     emu_->program, program_);
    } else if (program_ > 0) {
        ::kill(program_, SIGTERM);
        std::fprintf(stderr, "[standalone] asked %s (pid %d) to close\n", emu_->program,
                     program_);
    } else {
        // Not up yet, so there is nothing that would close politely.
        std::fprintf(stderr, "[standalone] %s is not up yet; stopping the sandbox\n",
                     emu_->program);
        kill9();
    }
}

void Run::kill9() {
    if (program_ <= 0) program_ = findProgram();
    if (program_ > 0) ::kill(program_, SIGKILL);
    ::kill(root_, SIGKILL);
    // And whatever it started: RPCS3 shows a fatal error as a second RPCS3
    // (`--error`), in the session spawn gave it, which would otherwise stay
    // on the television.
    if (emu_->binary) ::kill(-root_, SIGKILL);
}

bool Run::poll() {
    if (!active()) return false;

    int status = 0;
    const pid_t got = waitpid(root_, &status, WNOHANG);
    if (got == root_) {
        const int64_t ran = nowMs() - startMs_;
        // Anything it left behind, its error box included (kill9).
        if (emu_->binary) ::kill(-root_, SIGKILL);
        if (failedLoad_ && oldKeys_) {
            ended_ = End::KeysTooOld;
        } else if (failedLoad_ && noLicence_) {
            ended_ = End::NoLicence;
        } else if (froze_) {
            ended_ = End::Crashed;
        } else if (failedLoad_) {
            ended_ = End::CouldNotLoad;
        } else if (stopping()) {
            ended_ = End::Asked;
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            ended_ = End::Quit;
        } else {
            ended_ = End::Crashed;
        }
        std::fprintf(stderr, "[standalone] %s ended after %.1f s (%s %d)\n", emu_->program,
                     ran / 1000.0, WIFEXITED(status) ? "exit" : "signal",
                     WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
        root_ = -1;
        program_ = -1;
        stopAtMs_ = 0;
        frozen_ = false;
        gTitles.close();
        if (qmpFd_ >= 0) ::close(qmpFd_);
        qmpFd_ = -1;
        return false;
    }

    const int64_t now = nowMs();
    if (program_ <= 0 && now - startMs_ > 200) {
        program_ = findProgram();
        if (program_ > 0)
            std::fprintf(stderr, "[standalone] %s is pid %d\n", emu_->program, program_);
    }
    const bool watchLoad = !failedLoad_ && now - startMs_ < kLoadWatchMs;

    if (stopping() && now - stopAtMs_ > kStopGraceMs) {
        std::fprintf(stderr, "[standalone] %s did not close in %lld s; forcing it\n",
                     emu_->program, static_cast<long long>(kStopGraceMs / 1000));
        kill9();
        stopAtMs_ = now;   // and again after another grace, should even that not take
    }

    // A LOAD THAT FAILED is an error box nobody can close, so the emulator's
    // log is read, as it grows, for its words for that in the first minute,
    // and the console closes it instead. Errors are written at once, unlike
    // the rest of the log.
    //
    // AND ONE THAT FROZE, for as long as the game runs (`froze`): RPCS3 holds
    // the last picture with its window up and waits for somebody to close it,
    // which is a crash to the person holding the controller. Read twice a
    // second, only what was added since the last read.
    const bool watchFreeze = emu_->froze && !froze_ && now - logCheckMs_ >= 500;
    if (!stopping() && (watchLoad || watchFreeze)) {
        logCheckMs_ = now;
        if (FILE* f = std::fopen(logPath_.c_str(), "r")) {
            std::fseek(f, logRead_, SEEK_SET);
            char line[1024];
            while (std::fgets(line, sizeof line, f)) {
                if (watchLoad && ((emu_->loadFailed[0] && std::strstr(line, emu_->loadFailed[0])) ||
                                  (emu_->loadFailed[1] && std::strstr(line, emu_->loadFailed[1])))) {
                    std::fprintf(stderr, "[standalone] %s could not load the game: %s",
                                 emu_->program, line);
                    failedLoad_ = true;
                    for (const char* k : emu_->keysTooOld)
                        if (k && std::strstr(line, k)) oldKeys_ = true;
                    if (emu_->noLicence && std::strstr(line, emu_->noLicence)) noLicence_ = true;
                    break;
                }
                if (emu_->froze && std::strstr(line, emu_->froze)) {
                    std::fprintf(stderr, "[standalone] %s froze: %s", emu_->program, line);
                    froze_ = true;
                    break;
                }
            }
            logRead_ = std::ftell(f);
            std::fclose(f);
        }
        // Not asked to close: its error box is modal and holds the close
        // back (measured on Contra, which waited out the whole grace), and a
        // game that never started has nothing to write.
        if (failedLoad_ || froze_) {
            stopAtMs_ = now;
            kill9();
        }
    }

    // ITS OWN ERROR BOX for a game it could not open (`errorBox`), which only
    // some of its failures put in the log. Twice a second, in the same first
    // minute.
    if (!stopping() && watchLoad && emu_->errorBox && program_ > 0 &&
        now - errorCheckMs_ >= 500) {
        errorCheckMs_ = now;
        if (gTitles.hasWindowTitled(program_, emu_->errorBox)) {
            std::fprintf(stderr, "[standalone] %s could not open the game: its \"%s\" box is up\n",
                         emu_->program, emu_->errorBox);
            failedLoad_ = true;
            stopAtMs_ = now;
            kill9();
        }
    }

    // THE XBOX'S DASHBOARD (watchesDashboard): the game quit, or never ran.
    watchDashboard(now);

    // A GAME THAT STOPPED, by itself or through the emulator's own exit
    // shortcut, leaves the emulator's window up: its title loses the game.
    // Twice a second is plenty, and a frozen emulator's title does not move.
    if (!stopping() && !frozen_ && emu_->titlePrefix && now - titleCheckMs_ >= 500) {
        titleCheckMs_ = now;
        const int parts = gTitles.parts(emu_->titlePrefix);
        if (parts >= emu_->titleParts) {
            sawGameTitle_ = true;
        } else if (sawGameTitle_ && parts > 0) {
            std::fprintf(stderr, "[standalone] emulation stopped; closing %s\n",
                         emu_->program);
            stop();
        }
    }
    return true;
}

}  // namespace cab::standalone
