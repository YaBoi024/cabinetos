#include "storage.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "storage_internal.h"

namespace storage {
namespace {

std::string gRoot;

// Read once, on the first call that needs it, and remembered. Resolving this
// repeatedly would mean a console that changed its mind about where it lives
// halfway through a launch.
bool gRootResolved = false;

}  // namespace

// isDir and writable are locations.cpp's and users.cpp's too
// (storage_internal.h).
bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Whether we could actually write there, asked by writing rather than by
// reading a permission bit — a read-only mount and a directory owned by root
// both look fine to access(2) in ways that differ by filesystem.
//
// A NAME NO OTHER CALL SHARES (#483, FILES-01): two threads asking at once
// made the same probe with O_EXCL, and the one that lost was told a good
// drive could not be written. A probe a crash left behind no longer
// answers for the drive either.
bool writable(const std::string& dir) {
    static std::atomic<unsigned> n{0};
    const std::string probe = dir + "/.cabinetos-write-test." + std::to_string(::getpid()) +
                              "." + std::to_string(n.fetch_add(1));
    const int fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    ::close(fd);
    ::unlink(probe.c_str());
    return true;
}

namespace {

void resolveRoot() {
    if (gRootResolved) return;
    gRootResolved = true;
    if (!gRoot.empty()) return;
    if (const char* env = std::getenv("CABINETOS_STORAGE"); env && *env) {
        gRoot = env;
        return;
    }
    // The console's own answer. It is tried rather than assumed, because the
    // development VM runs the frontend out of a home directory and must not
    // start scattering a person's saves into /var/lib on a machine where that
    // is nobody's idea of where they went.
    const char* kConsoleRoot = "/var/lib/cabinetos";
    if (makeDirs(kConsoleRoot) && writable(kConsoleRoot)) {
        gRoot = kConsoleRoot;
        return;
    }
    gRoot = ".";
}

// The files a core needs that SHIP INSIDE THE IMAGE — PPSSPP's fonts and lookup
// tables, 13 MB of them, which came out of a core build rather than off the
// server. They belong in /usr/share/cabinetos: replaced wholesale on every
// update, never written to, and not the person's data.
//
// A core still wants them in its one system directory, so they are LINKED in
// rather than copied. A name already present as a real file or directory is
// left alone, which is what keeps a development machine working — there the
// assets sit in `bios/` where the core build put them and the image directory
// does not exist at all.
void linkImageAssets() {
    const std::string from = imageAssetsDir();
    const std::string to = biosDir();
    DIR* d = ::opendir(from.c_str());
    if (!d) return;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string dest = to + "/" + e->d_name;
        struct stat st;
        if (::lstat(dest.c_str(), &st) == 0) continue;
        if (::symlink((from + "/" + e->d_name).c_str(), dest.c_str()) == 0)
            std::fprintf(stderr, "[storage] linked %s into the system directory\n",
                         e->d_name);
    }
    ::closedir(d);
}

// Copies one file, byte for byte, preserving nothing but the contents and the
// mode. Only reached when a move crossed a filesystem, which the layout exists
// to prevent — see moveEntry.
bool copyFile(const std::string& from, const std::string& to, std::string* err) {
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) { if (err) *err = "cannot read " + from; return false; }
    FILE* out = std::fopen(to.c_str(), "wb");
    if (!out) { std::fclose(in); if (err) *err = "cannot write " + to; return false; }
    char buf[1 << 16];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) {
        if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (std::ferror(in)) ok = false;
    std::fclose(in);
    if (std::fclose(out) != 0) ok = false;
    if (ok) {
        struct stat st;
        if (::stat(from.c_str(), &st) == 0) ::chmod(to.c_str(), st.st_mode & 07777);
    } else {
        ::unlink(to.c_str());
        if (err) *err = "copy of " + from + " did not complete";
    }
    return ok;
}

bool copyTree(const std::string& from, const std::string& to, std::string* err) {
    struct stat st;
    if (::lstat(from.c_str(), &st) != 0) { if (err) *err = "cannot stat " + from; return false; }
    if (!S_ISDIR(st.st_mode)) return copyFile(from, to, err);
    if (!makeDirs(to)) { if (err) *err = "cannot create " + to; return false; }
    DIR* d = ::opendir(from.c_str());
    if (!d) { if (err) *err = "cannot open " + from; return false; }
    bool ok = true;
    while (struct dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        if (!copyTree(from + "/" + e->d_name, to + "/" + e->d_name, err)) { ok = false; break; }
    }
    ::closedir(d);
    return ok;
}

bool removeTree(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return ::unlink(path.c_str()) == 0;
    DIR* d = ::opendir(path.c_str());
    if (!d) return false;
    while (struct dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        removeTree(path + "/" + e->d_name);
    }
    ::closedir(d);
    return ::rmdir(path.c_str()) == 0;
}

}  // namespace

void setRoot(const std::string& path) {
    gRoot = path;
    gRootResolved = true;
}

const std::string& root() {
    resolveRoot();
    return gRoot;
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return false;
    if (isDir(path)) return true;
    std::string built;
    if (path[0] == '/') built = "/";
    size_t i = 0;
    while (i < path.size()) {
        const size_t slash = path.find('/', i);
        const std::string part =
            path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) {
            if (built.size() > 1 || (built.size() == 1 && built[0] != '/')) built += "/";
            built += part;
            if (::mkdir(built.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    return isDir(path);
}

bool exists(const std::string& path) {
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0;
}

bool ensureTree(std::string* err) {
    const std::string r = root();
    if (!makeDirs(r)) {
        if (err) *err = "cannot create the storage root at " + r;
        return false;
    }
    if (!writable(r)) {
        if (err) *err = "the storage root at " + r + " is not writable";
        return false;
    }
    // Every location gets the same shape, which is the whole reason a demotion
    // is a rename. A location that is missing — an unplugged drive — is skipped
    // rather than being an error: docs/PROJECT.md, open question 14, "a missing
    // drive degrades; it never errors".
    for (const std::string& loc : locations()) {
        if (!isDir(loc)) continue;
        makeDirs(romsDir(loc));
        makeDirs(cacheDir(loc));
    }
    makeDirs(biosDir());
    makeDirs(configDir());
    makeDirs(logsDir());
    makeDirs(reportsDir());
    makeDirs(r + "/users");
    linkImageAssets();
    return true;
}

std::string romsDir(const std::string& location) { return location + "/roms"; }
std::string cacheDir(const std::string& location) { return location + "/cache"; }
std::string biosDir() { return root() + "/bios"; }
std::string configDir() { return root() + "/config"; }
std::string logsDir() { return root() + "/logs"; }
std::string reportsDir() { return root() + "/reports"; }
std::string emulatorsDir() { return root() + "/emulators"; }
std::string gamesDir() { return root() + "/games"; }

std::string imageAssetsDir() {
    // Inside the bootc image, replaced wholesale on every update and never
    // written to. On a development machine it is simply absent, and the assets
    // sit in the system directory where the core build put them.
    //
    // `/system` AND NOT `/usr/share/cabinetos` ITSELF. That directory already
    // held things that have nothing to do with a core — a DEVELOPMENT-IMAGE
    // marker and two package inventories — and the first version of this linked
    // all three into the console's system directory, where a core would go
    // looking for its fonts. Found by running it; the directory listing is what
    // said so.
    if (const char* env = std::getenv("CABINETOS_ASSETS"); env && *env) return env;
    return "/usr/share/cabinetos/system";
}

std::string safeSegment(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Everything above ASCII passes through: this is UTF-8 and those bytes
        // are the tail of a multi-byte character. Replacing them turns a
        // Japanese title into a row of underscores, which sanitisedStem
        // (fileio.cpp) already learned the hard way.
        if (u >= 0x80 || std::isalnum(u) || std::strchr(" -_().,'!&+[]", c) != nullptr)
            out += c;
        else
            out += '_';
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.empty() || out == "." || out == "..") out = "_";
    return out;
}

std::string platformSegment(const std::string& fsSlug) {
    return fsSlug.empty() ? "unknown" : safeSegment(fsSlug);
}

std::string entryName(int romId, const std::string& title) {
    return std::to_string(romId) + " - " + safeSegment(title);
}

int romIdFromEntry(const std::string& entry) {
    // The number is the identity and the words are decoration, so this reads
    // the number and stops. It deliberately accepts "321 - anything" and
    // "321.chd" alike: a rename is cosmetic and must not lose a game.
    char* end = nullptr;
    const long id = std::strtol(entry.c_str(), &end, 10);
    if (id <= 0 || end == entry.c_str()) return 0;
    return static_cast<int>(id);
}

std::string userDir(const User& u) { return root() + "/users/" + u.dirName(); }

std::string savesDir(const User& u, const std::string& platform, int romId,
                     const std::string& core) {
    return userDir(u) + "/saves/" + platformSegment(platform) + "/" +
           std::to_string(romId) + "/" + safeSegment(core);
}

std::string statesDir(const User& u, const std::string& platform, int romId,
                      const std::string& core) {
    return userDir(u) + "/states/" + platformSegment(platform) + "/" +
           std::to_string(romId) + "/" + safeSegment(core);
}

std::string screenshotsDir(const User& u) { return userDir(u) + "/screenshots"; }
std::string keepsDir(const User& u) { return userDir(u) + "/keeps"; }
std::string pendingDir(const User& u) { return userDir(u) + "/pending"; }
std::string controlsDir(const User& u) { return userDir(u) + "/controls"; }

std::string scratchSavesDir(const std::string& core) {
    // `--core` is given a path, not a name. Taking the whole thing would make a
    // directory called `cores_build_gambatte_libretro.so`.
    const size_t slash = core.find_last_of('/');
    const std::string name =
        slash == std::string::npos ? core : core.substr(slash + 1);
    return root() + "/users/0 - local/saves/local/0/" +
           safeSegment(name.empty() ? "core" : name);
}

MoveResult moveEntry(const std::string& from, const std::string& to) {
    MoveResult r;
    if (!exists(from)) {
        r.error = from + " is not there";
        return r;
    }
    const size_t slash = to.find_last_of('/');
    if (slash != std::string::npos) makeDirs(to.substr(0, slash));

    // THE FAST PATH IS THE ONLY PATH THE CONSOLE SHOULD EVER TAKE. A rename is
    // instant whatever the game weighs, and it is why `roms/` and `cache/`
    // repeat on every location instead of living once at the root.
    if (::rename(from.c_str(), to.c_str()) == 0) {
        r.ok = true;
        return r;
    }
    if (errno != EXDEV) {
        r.error = std::string("could not move ") + from + ": " + std::strerror(errno);
        return r;
    }

    // EXDEV: the two ends are on different filesystems. Reported, not hidden —
    // if this ever fires in the product, the layout has a fault in it and
    // somebody should know rather than wonder why a demotion took four minutes.
    r.crossedFilesystem = true;
    std::string err;
    if (!copyTree(from, to, &err)) {
        removeTree(to);
        r.error = err;
        return r;
    }
    if (!removeTree(from)) {
        r.error = "copied to " + to + " but could not remove " + from;
        return r;
    }
    r.ok = true;
    return r;
}

int64_t treeBytes(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return S_ISREG(st.st_mode) ? st.st_size : 0;
    int64_t total = 0;
    DIR* d = ::opendir(path.c_str());
    if (!d) return 0;
    while (struct dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        total += treeBytes(path + "/" + e->d_name);
    }
    ::closedir(d);
    return total;
}

bool removeEntry(const std::string& path) {
    removeTree(path);
    return !exists(path);
}

}  // namespace storage
