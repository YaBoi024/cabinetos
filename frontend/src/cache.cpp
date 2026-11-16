#include "cache.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include <json-c/json.h>

#include "fileio.h"

namespace cache {
namespace {

std::string keepPath(const storage::User& u, int romId) {
    return storage::keepsDir(u) + "/" + std::to_string(romId) + ".json";
}

// Walks one half of one location — `roms/` or `cache/` — calling `fn` with the
// platform segment and the entry name. Both halves have exactly the same shape,
// which is the property that makes a demotion a rename.
template <typename F>
void walkHalf(const std::string& half, F fn) {
    DIR* top = ::opendir(half.c_str());
    if (!top) return;   // nothing kept yet, or nothing cached. Both ordinary.
    while (struct dirent* plat = ::readdir(top)) {
        if (plat->d_name[0] == '.') continue;
        const std::string platform = plat->d_name;
        const std::string pdir = half + "/" + platform;
        DIR* pd = ::opendir(pdir.c_str());
        if (!pd) continue;
        while (struct dirent* e = ::readdir(pd)) {
            if (e->d_name[0] == '.') continue;
            fn(platform, std::string(e->d_name));
        }
        ::closedir(pd);
    }
    ::closedir(top);
}

int64_t newestMtime(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_mtime);
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

// --- A game's manual (#364): the folder beside its game, found and moved ---

// `<half>/<platform>/Manuals`, for a game whose entry is `gameEntryPath`.
std::string manualsDirBeside(const std::string& gameEntryPath) {
    const size_t slash = gameEntryPath.find_last_of('/');
    return (slash == std::string::npos ? std::string(".") : gameEntryPath.substr(0, slash)) +
           "/Manuals";
}

// The game's manual in one `Manuals` folder, or empty. By the number, as a
// game is found: the words after it are for a person.
std::string manualIn(const std::string& dir, int romId) {
    std::string out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        const size_t len = std::strlen(e->d_name);
        if (e->d_name[0] == '.' || len < 5 || strcasecmp(e->d_name + len - 4, ".pdf") != 0)
            continue;
        if (storage::romIdFromEntry(e->d_name) != romId) continue;
        out = dir + "/" + e->d_name;
        break;
    }
    ::closedir(d);
    return out;
}

// The game went: its manual goes with it, and the folder when it is empty.
void dropManual(const std::string& gameEntryPath, int romId) {
    const std::string dir = manualsDirBeside(gameEntryPath);
    const std::string pdf = manualIn(dir, romId);
    if (pdf.empty()) return;
    if (::unlink(pdf.c_str()) == 0)
        std::fprintf(stderr, "[manual] removed with its game: %s\n", pdf.c_str());
    ::rmdir(dir.c_str());   // only if it is empty
}

// The game moved from `fromEntry` to `toEntry`: its manual follows, unless
// one is already there.
void moveManual(const std::string& fromEntry, const std::string& toEntry, int romId) {
    const std::string fromDir = manualsDirBeside(fromEntry);
    const std::string pdf = manualIn(fromDir, romId);
    if (pdf.empty()) return;
    const std::string toDir = manualsDirBeside(toEntry);
    if (toDir == fromDir) return;
    if (!manualIn(toDir, romId).empty()) {
        dropManual(fromEntry, romId);
        return;
    }
    const std::string target = toDir + pdf.substr(pdf.find_last_of('/'));
    const storage::MoveResult r = storage::moveEntry(pdf, target);
    if (r.ok)
        std::fprintf(stderr, "[manual] followed its game to %s\n", target.c_str());
    else
        std::fprintf(stderr, "[manual] could not follow its game: %s\n", r.error.c_str());
    ::rmdir(fromDir.c_str());
}

// Moves a game between `roms/` and `cache/` on the location it already sits on.
//
// THIS IS WHY THE TWO DIRECTORIES REPEAT ON EVERY LOCATION. With one `cache/`
// at the root and kept games on a second drive, this would be a copy of
// gigabytes because somebody changed their mind about a game. moveEntry reports
// a crossed filesystem rather than hiding it, so if that ever happens here it
// is a fault in the layout and it says so.
bool relocate(const Placement& p, bool toKept) {
    if (!p.present || p.kept == toKept) return true;
    // THE NAME DOES NOT CHANGE — only which half it is under. Rebuilding it
    // from the game's title here would rename a game as a side effect of
    // keeping it, and the name on disk is what a person recognises.
    const std::string entryName = p.entryPath.substr(p.entryPath.find_last_of('/') + 1);
    const std::string target = (toKept ? storage::romsDir(p.location)
                                       : storage::cacheDir(p.location)) +
                               "/" + p.platform + "/" + entryName;
    if (target == p.entryPath) return true;
    const storage::MoveResult r = storage::moveEntry(p.entryPath, target);
    if (!r.ok) {
        std::fprintf(stderr, "[keep] could not %s %s: %s\n",
                     toKept ? "promote" : "demote", p.entryPath.c_str(), r.error.c_str());
        return false;
    }
    if (r.crossedFilesystem) {
        std::fprintf(stderr,
                     "[keep] %s CROSSED A FILESYSTEM and had to be copied: %s -> %s. "
                     "roms/ and cache/ are supposed to repeat per location so this "
                     "is a rename; something is wrong with the layout.\n",
                     toKept ? "promotion" : "demotion", p.entryPath.c_str(),
                     target.c_str());
    }
    std::fprintf(stderr, "[keep] %s %s\n", toKept ? "promoted" : "demoted to the cache",
                 target.c_str());
    moveManual(p.entryPath, target, storage::romIdFromEntry(entryName));
    return true;
}

}  // namespace

int64_t freeBytes(const std::string& path) {
    struct statvfs vfs;
    if (::statvfs(path.c_str(), &vfs) != 0) return 0;
    // f_bavail, not f_bfree: the blocks available to us, excluding what the
    // filesystem reserves for root. Using f_bfree would promise space that a
    // non-root write cannot actually have.
    return static_cast<int64_t>(vfs.f_bavail) * static_cast<int64_t>(vfs.f_frsize);
}

std::vector<Placement> findAll(int romId) {
    std::vector<Placement> out;
    for (const std::string& loc : storage::locations()) {
        // `roms/` before `cache/`, so that where there IS only one answer it is
        // the kept one.
        const std::pair<std::string, bool> halves[2] = {
            {storage::romsDir(loc), true},
            {storage::cacheDir(loc), false},
        };
        for (const auto& [half, kept] : halves) {
            walkHalf(half, [&](const std::string& platform, const std::string& entry) {
                if (storage::romIdFromEntry(entry) != romId) return;
                Placement p;
                p.present = true;
                p.kept = kept;
                p.location = loc;
                p.platform = platform;
                p.entryPath = half + "/" + platform + "/" + entry;
                struct stat st;
                p.isDirectory = ::lstat(p.entryPath.c_str(), &st) == 0 &&
                                S_ISDIR(st.st_mode);
                out.push_back(std::move(p));
            });
        }
    }
    return out;
}

Placement find(int romId) {
    const std::vector<Placement> all = findAll(romId);
    return all.empty() ? Placement{} : all.front();
}

namespace {

// Does this entry hold the payload RomM describes?
//
// A single-file entry answers with its own size. A directory entry cannot —
// it holds the archive that came down AND whatever was unpacked out of it, so
// its total is always larger — so the question becomes whether any one file
// inside it is exactly the size the server reports. That file is the download,
// and its presence at the right size is the same test the launch path already
// trusts to decide a game need not be fetched again.
bool holdsPayloadOfSize(const std::string& entryPath, int64_t expectedBytes) {
    if (expectedBytes <= 0) return false;
    struct stat st;
    if (::lstat(entryPath.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return st.st_size == expectedBytes;
    DIR* d = ::opendir(entryPath.c_str());
    if (!d) return false;
    bool found = false;
    // A GAME ON SEVERAL DISCS (#162) is its discs together: RomM's size is
    // the folder's, and no one file is that size. The console's own list
    // is not counted; RomM never had it.
    int64_t together = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        struct stat f;
        if (::stat((entryPath + "/" + e->d_name).c_str(), &f) == 0 && S_ISREG(f.st_mode)) {
            if (f.st_size == expectedBytes) {
                found = true;
                break;
            }
            const size_t len = std::strlen(e->d_name);
            if (len < 4 || strcasecmp(e->d_name + len - 4, ".m3u") != 0) together += f.st_size;
        }
    }
    ::closedir(d);
    return found || together == expectedBytes;
}

}  // namespace

Placement dedupe(int romId, int64_t expectedBytes) {
    std::vector<Placement> all = findAll(romId);
    if (all.size() <= 1) return all.empty() ? Placement{} : all.front();

    // WHICH COPY STAYS. A kept game's kept copy (under roms/), wherever it
    // is: the other is the stand-in fetched into the cache while its drive was
    // away. A game nobody keeps stays on the main drive, where the cache
    // lives. Between two equal copies, the main drive's. The keep record never
    // leaves the main drive, so this answer is there even when a drive is not.
    const bool kept = isKeptByAnyone(romId);
    auto rank = [&](const Placement& p) {
        return (kept && !p.kept ? 2 : 0) + (p.location != storage::primaryLocation() ? 1 : 0);
    };

    std::vector<const Placement*> usable;
    for (const Placement& p : all)
        if (holdsPayloadOfSize(p.entryPath, expectedBytes)) usable.push_back(&p);

    if (usable.empty()) {
        // NOTHING IS DELETED. Every copy is the wrong size, so there is no
        // known-good one to fall back on and picking a survivor would be a
        // guess — the one move here that could actually cost something. The
        // launch re-fetches, which fixes it.
        std::fprintf(stderr,
                     "[storage] rom %d is here %zu times and NONE is %lld bytes — "
                     "leaving all of them alone and fetching a clean copy\n",
                     romId, all.size(), static_cast<long long>(expectedBytes));
        return all.front();
    }

    const Placement* winner = usable.front();
    for (const Placement* p : usable)
        if (rank(*p) < rank(*winner)) winner = p;

    for (const Placement& p : all) {
        if (&p == winner) continue;
        // The manual stays with the copy that stays (#364).
        moveManual(p.entryPath, winner->entryPath, romId);
        if (removeTree(p.entryPath)) {
            std::fprintf(stderr,
                         "[storage] rom %d was here twice; kept %s and removed %s\n",
                         romId, winner->entryPath.c_str(), p.entryPath.c_str());
        }
    }
    return *winner;
}

std::string entryPathFor(const std::string& location, const std::string& platform,
                         int romId, const std::string& title, bool kept) {
    const std::string half = kept ? storage::romsDir(location) : storage::cacheDir(location);
    return half + "/" + storage::platformSegment(platform) + "/" +
           storage::entryName(romId, title);
}

std::string manualPathFor(const std::string& gameEntryPath, int romId,
                          const std::string& title) {
    return manualsDirBeside(gameEntryPath) + "/" + storage::entryName(romId, title) + ".pdf";
}

std::string findManual(int romId) {
    for (const Placement& p : findAll(romId)) {
        const std::string pdf = manualIn(manualsDirBeside(p.entryPath), romId);
        if (!pdf.empty()) return pdf;
    }
    return {};
}

std::vector<Entry> candidates(const std::string& location) {
    std::vector<Entry> out;
    walkHalf(storage::cacheDir(location),
             [&](const std::string& platform, const std::string& entry) {
                 const int romId = storage::romIdFromEntry(entry);
                 if (romId <= 0) return;
                 const std::string path =
                     storage::cacheDir(location) + "/" + platform + "/" + entry;
                 // A GAME WHOSE SAVES ARE STILL ONLY IN ITS FOLDER: an Xbox
                 // game's drive between the start of a game and the saves
                 // coming off it (standalone::beforeStart). Deleting it would
                 // take somebody's save, so it is not a candidate until they
                 // are off, which the next start of the console sees to.
                 if (::access((path + "/.cabinetos-playing").c_str(), F_OK) == 0) return;
                 out.push_back({path, storage::treeBytes(path), newestMtime(path), romId,
                                platform});
             });
    // Oldest first, which is the entire eviction order.
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return a.lastUsed < b.lastUsed;
    });
    return out;
}

namespace {

// ONE CACHED GAME OUT, the one way eviction removes anything: the entry and
// its manual. The space is not committed here; each caller syncs once.
bool removeCached(const Entry& e) {
    if (!removeTree(e.path)) return false;
    dropManual(e.path, e.romId);
    return true;
}

}  // namespace

int64_t evictGame(int romId) {
    int64_t freed = 0;
    for (const std::string& loc : storage::locations()) {
        int64_t here = 0;
        for (const Entry& e : candidates(loc))
            if (e.romId == romId && removeCached(e)) here += e.bytes;
        if (here > 0) cab::syncDrive(loc);
        freed += here;
    }
    return freed;
}

int64_t evictUntilFree(const std::string& location, int64_t needBytes,
                       int protectRomId) {
    const int64_t target =
        needBytes + static_cast<int64_t>(static_cast<double>(needBytes) * kMarginFraction);
    int64_t have = freeBytes(location);
    if (have >= target) return 0;

    // `have + freed` rather than re-measuring each time round, which is both
    // cheaper and — see the sync below — the only thing that would work.
    int64_t freed = 0;
    for (const Entry& e : candidates(location)) {
        if (have + freed >= target) break;
        // The game being launched or running. Its file is open or about to
        // be, and deleting it would be the one
        // eviction a person could actually notice.
        if (protectRomId != 0 && e.romId == protectRomId) continue;
        if (!removeCached(e)) continue;
        freed += e.bytes;
        std::fprintf(stderr, "[cache] evicted %s (%lld bytes)\n", e.path.c_str(),
                     static_cast<long long>(e.bytes));
    }
    if (freed > 0) {
        // WITHOUT THIS, EVERY EVICTION LOOKS LIKE A FAILURE. CabinetOS runs on
        // btrfs, where unlink returns immediately but the space stays invisible
        // to statvfs until a transaction commits — so a caller that deletes and
        // then measures sees exactly what it saw before and concludes there was
        // nothing to gain.
        //
        // Measured on the test machine rather than inferred from a symptom:
        // delete a 50 MB file and the free-space figure moves by 0 bytes
        // immediately, 0 after three seconds, and the full 50,003,968 the
        // moment a sync is forced. Waiting is not an option; committing is.
        //
        // syncfs rather than sync, so this is the one filesystem rather than
        // every mounted one. It happens once per eviction, on a path that is
        // about to move hundreds of megabytes over a network.
        cab::syncDrive(location);
        std::fprintf(stderr, "[cache] freed %lld bytes to make room for %lld\n",
                     static_cast<long long>(freed), static_cast<long long>(needBytes));
    }
    return freed;
}

int64_t saveFloorBytes(const std::string& path) {
    // storage::spaceOf, so the size is the console's part of the drive and
    // Steam's slice (#223) is not counted.
    const storage::Space sp = storage::spaceOf(path);
    if (!sp.ok) {
        // An unreadable disk looks full elsewhere in this file, and the same
        // pessimism applies: take the whole 2 GB rather than a percentage of a
        // size we do not know.
        return 2LL << 30;
    }
    return std::min<int64_t>(2LL << 30, sp.totalBytes / 20);   // 5%
}

bool isKeptBy(const storage::User& u, int romId) {
    if (!u.valid()) return false;
    struct stat st;
    return ::stat(keepPath(u, romId).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<int> keepers(int romId) {
    std::vector<int> out;
    for (const storage::User& u : storage::knownUsers())
        if (isKeptBy(u, romId)) out.push_back(u.id);
    return out;
}

bool isKeptByAnyone(int romId) { return !keepers(romId).empty(); }

std::vector<int> keptRoms(const storage::User& u) {
    std::vector<int> out;
    if (!u.valid()) return out;
    DIR* d = ::opendir(storage::keepsDir(u).c_str());
    if (!d) return out;   // nothing kept yet is the ordinary case, not a fault
    while (struct dirent* f = ::readdir(d)) {
        if (f->d_name[0] == '.') continue;
        char* end = nullptr;
        const long id = std::strtol(f->d_name, &end, 10);
        if (id > 0 && end && std::strcmp(end, ".json") == 0)
            out.push_back(static_cast<int>(id));
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> allKeptRoms() {
    std::set<int> ids;
    for (const storage::User& u : storage::knownUsers())
        for (int id : keptRoms(u)) ids.insert(id);
    return std::vector<int>(ids.begin(), ids.end());
}

KeepVerdict mayKeep(const std::string& location, int romId, int64_t gameBytes) {
    KeepVerdict v;
    v.floorBytes = saveFloorBytes(location) + kSystemReserveBytes;

    // Everything the console could still delete if it had to, with this game
    // excluded — it is about to stop being evictable.
    int64_t reclaimable = freeBytes(location);
    for (const Entry& e : candidates(location)) {
        if (e.romId == romId) continue;
        reclaimable += e.bytes;
    }

    // The part of this game still to fetch comes OUT of that, because keeping
    // an undownloaded game is a promise to spend the space.
    //
    // A game SOMEBODY ELSE ALREADY KEEPS costs nothing at all: the bytes are on
    // the machine and are already not evictable, so the second keep changes
    // nothing about the disk. That is the whole of what "one kept game, two
    // people" means here.
    const Placement p = find(romId);
    const int64_t onDisk = p.present ? storage::treeBytes(p.entryPath) : 0;
    const int64_t stillToFetch = p.kept ? 0 : std::max<int64_t>(0, gameBytes - onDisk);
    reclaimable -= stillToFetch;

    // An upload that has not reached the server is the one thing here that
    // cannot be re-fetched, so its bytes are spoken for and are not reclaimable
    // by anybody.
    reclaimable -= pendingBytes();

    v.reclaimableBytes = reclaimable;
    v.allowed = reclaimable >= v.floorBytes;
    v.shortfallBytes = v.allowed ? 0 : (v.floorBytes - reclaimable);
    return v;
}

bool keep(const storage::User& u, int romId, const std::string& record,
          std::string* err) {
    if (!u.valid()) {
        if (err) *err = "no user, so there is nobody to keep this for";
        return false;
    }
    storage::makeDirs(storage::keepsDir(u));
    const std::string path = keepPath(u, romId);
    // Written through a temporary and renamed, so a keep is either recorded or
    // it is not. A half-written record is a kept game that cannot be read back,
    // which is the worst of both: the space is protected and the game is not
    // launchable offline.
    if (!cab::writeBytes(path, record)) {
        if (err) *err = "could not write the keep record";
        return false;
    }
    // And out of the cache, if it is here. A game that is not here yet is
    // promoted by the download, which writes it straight into `roms/`.
    relocate(find(romId), /*toKept=*/true);
    return true;
}

bool unkeep(const storage::User& u, int romId, bool keepTheBytes, Release* out) {
    Release scratch;
    Release& r = out ? *out : scratch;
    r = Release{};

    if (!u.valid()) return false;
    const std::string path = keepPath(u, romId);
    struct stat st;
    const bool had = ::stat(path.c_str(), &st) == 0;
    if (had && ::unlink(path.c_str()) != 0) return false;

    // SOMEBODY ELSE MAY STILL BE KEEPING IT, and then nothing happens to the
    // file at all. This is the whole reason keeping stopped being a boolean.
    const std::vector<int> rest = keepers(romId);
    if (!rest.empty()) {
        std::fprintf(stderr, "[keep] %d released by user %d, still kept by %zu other(s)\n",
                     romId, u.id, rest.size());
        r.what = Release::What::StillKept;
        r.otherKeepers = static_cast<int>(rest.size());
        return true;
    }

    const Placement p = find(romId);
    if (!p.present) return true;

    if (keepTheBytes) {
        // See cache.h: the game is being played, or a keep failed. Neither is
        // somebody asking for their space back, so the game drops into the
        // cache and eviction takes it in the ordinary way.
        relocate(p, /*toKept=*/false);
        std::fprintf(stderr, "[keep] %d released to the cache\n", romId);
        r.what = Release::What::Demoted;
        return true;
    }

    // THE LAST KEEP DELETES, because the row says "Remove download" and
    // reclaiming the space is why anybody presses it.
    const int64_t bytes = storage::treeBytes(p.entryPath);
    if (!removeTree(p.entryPath)) {
        std::fprintf(stderr, "[keep] %d released but %s could not be removed\n", romId,
                     p.entryPath.c_str());
        r.what = Release::What::DeleteFailed;
        return true;
    }
    dropManual(p.entryPath, romId);
    // WITHOUT THIS THE SPACE DOES NOT APPEAR TO COME BACK, which is the exact
    // complaint that changed this behaviour. btrfs unlinks immediately and
    // leaves statvfs reporting the old figure until a transaction commits, so a
    // Storage screen refreshed a second later would show no change at all. Same
    // reason evictUntilFree does it.
    cab::syncDrive(p.location);
    std::fprintf(stderr, "[keep] %d released and removed, %lld bytes back\n", romId,
                 static_cast<long long>(bytes));
    r.what = Release::What::Deleted;
    r.bytesFreed = bytes;
    return true;
}

bool unkeepForEveryone(int romId, bool keepTheBytes, Release* out) {
    Release scratch;
    Release& r = out ? *out : scratch;
    r = Release{};
    bool ok = true;
    bool any = false;
    // One person at a time through unkeep, so the last release is the one that
    // deletes and says so, exactly as it would from the game's own screen.
    for (const storage::User& u : storage::knownUsers()) {
        if (!isKeptBy(u, romId)) continue;
        any = true;
        ok = unkeep(u, romId, keepTheBytes, &r) && ok;
    }
    if (!any) {
        // NOBODY KEEPS IT: a game left in `roms/` on a drive that came back
        // after its download was removed. Removing it is deleting it.
        const Placement p = find(romId);
        if (!p.present || !p.kept) return true;
        const int64_t bytes = storage::treeBytes(p.entryPath);
        if (!removeTree(p.entryPath)) {
            r.what = Release::What::DeleteFailed;
            return true;
        }
        dropManual(p.entryPath, romId);
        cab::syncDrive(p.location);
        std::fprintf(stderr, "[keep] %d was kept by nobody; removed, %lld bytes back\n", romId,
                     static_cast<long long>(bytes));
        r.what = Release::What::Deleted;
        r.bytesFreed = bytes;
    }
    return ok;
}

std::vector<Download> downloads() {
    std::map<int, Download> by;
    // The records first: title and platform, and the games whose drive is away.
    for (const storage::User& u : storage::knownUsers()) {
        for (int id : keptRoms(u)) {
            if (by.count(id)) continue;
            Download d;
            d.romId = id;
            if (json_object* o = json_object_from_file(keepPath(u, id).c_str())) {
                json_object* v = nullptr;
                if (json_object_object_get_ex(o, "name", &v)) d.title = json_object_get_string(v);
                if (json_object_object_get_ex(o, "platform_display_name", &v))
                    d.platform = json_object_get_string(v);
                if (d.platform.empty() && json_object_object_get_ex(o, "platform_name", &v))
                    d.platform = json_object_get_string(v);
                if (json_object_object_get_ex(o, "platform_id", &v))
                    d.platformId = json_object_get_int(v);
                json_object_put(o);
            }
            by[id] = d;
        }
    }
    // Then what is actually under `roms/`, on every drive that is here.
    for (const std::string& loc : storage::locations()) {
        walkHalf(storage::romsDir(loc), [&](const std::string& platform, const std::string& entry) {
            const int id = storage::romIdFromEntry(entry);
            if (id <= 0) return;
            Download& d = by[id];
            d.romId = id;
            if (d.present) return;   // twice on the machine; dedupe's business
            d.present = true;
            d.location = loc;
            d.bytes = storage::treeBytes(storage::romsDir(loc) + "/" + platform + "/" + entry);
            if (d.platform.empty()) d.platform = platform;
            if (d.title.empty()) {
                // "<id> - <title>", with any extension a single file carries.
                const size_t dash = entry.find(" - ");
                d.title = dash == std::string::npos ? entry : entry.substr(dash + 3);
                const size_t dot = d.title.rfind('.');
                if (dot != std::string::npos && dot > 0 && d.title.size() - dot <= 5)
                    d.title.resize(dot);
            }
        });
    }
    std::vector<Download> out;
    out.reserve(by.size());
    for (auto& [id, d] : by) out.push_back(std::move(d));
    return out;
}

void rememberGame(int romId, const std::string& record) {
    if (romId <= 0 || record.empty()) return;
    storage::makeDirs(storage::gamesDir());
    const std::string path = storage::gamesDir() + "/" + std::to_string(romId) + ".json";
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    const bool ok = std::fwrite(record.data(), 1, record.size(), f) == record.size();
    if (std::fclose(f) != 0 || !ok || ::rename(tmp.c_str(), path.c_str()) != 0)
        ::unlink(tmp.c_str());
}

namespace {
std::string readText(const std::string& path) {
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}
}  // namespace

std::string gameRecord(int romId) {
    std::string r = readText(storage::gamesDir() + "/" + std::to_string(romId) + ".json");
    if (!r.empty()) return r;
    for (const storage::User& u : storage::knownUsers()) {
        r = readText(keepPath(u, romId));
        if (!r.empty()) return r;
    }
    return {};
}

std::vector<OnDrive> onDrive() {
    std::map<int, OnDrive> by;
    for (const std::string& loc : storage::locations()) {
        for (const bool kept : {true, false}) {
            const std::string half = kept ? storage::romsDir(loc) : storage::cacheDir(loc);
            walkHalf(half, [&](const std::string& platform, const std::string& entry) {
                const int id = storage::romIdFromEntry(entry);
                if (id <= 0) return;
                const int64_t used = newestMtime(half + "/" + platform + "/" + entry);
                OnDrive& d = by[id];
                if (d.romId == 0) {
                    d.romId = id;
                    d.kept = kept;
                    d.lastUsed = used;
                } else {
                    d.kept = d.kept || kept;
                    d.lastUsed = std::max(d.lastUsed, used);
                }
            });
        }
    }
    std::vector<OnDrive> out;
    out.reserve(by.size());
    for (auto& [id, d] : by) {
        d.record = gameRecord(id);
        out.push_back(std::move(d));
    }
    std::sort(out.begin(), out.end(), [](const OnDrive& a, const OnDrive& b) {
        return a.lastUsed != b.lastUsed ? a.lastUsed > b.lastUsed : a.romId < b.romId;
    });
    return out;
}

void touch(const std::string& entryPath) {
    // The entry's own mtime is the record, so there is no sidecar to keep in
    // step and nothing to migrate. utimensat with nullptr means "now", and it
    // works on a directory entry exactly as it does on a file one.
    if (::utimensat(AT_FDCWD, entryPath.c_str(), nullptr, 0) != 0) {
        // Not worth failing a launch over: the cost is that this game looks
        // older than it is and may be evicted sooner than it deserves.
        std::fprintf(stderr, "[cache] could not touch %s\n", entryPath.c_str());
    }
}

std::string keepLocation(int64_t gameBytes) {
    const std::vector<std::string> all = storage::locations();
    const std::string& main = all.front();
    if (all.size() == 1) return main;

    // What is on the main drive that is not the cache: the cache is the
    // console's to clear, so it does not count towards the 80%.
    const storage::Space m = storage::spaceOf(main);
    if (m.ok && m.totalBytes > 0) {
        int64_t cached = 0;
        for (const Entry& e : candidates(main)) cached += e.bytes;
        const int64_t settled = m.totalBytes - m.freeBytes - cached;
        if (settled + gameBytes <= static_cast<int64_t>(m.totalBytes * kMainKeepShare))
            return main;
    }
    std::string best;
    int64_t bestFree = -1;
    for (size_t i = 1; i < all.size(); ++i) {
        const storage::Space sp = storage::spaceOf(all[i]);
        if (sp.ok && sp.freeBytes >= gameBytes && sp.freeBytes > bestFree) {
            best = all[i];
            bestFree = sp.freeBytes;
        }
    }
    return best.empty() ? main : best;
}

bool almostFull() {
    int64_t room = 0, total = 0;
    for (const std::string& loc : storage::locations()) {
        const storage::Space sp = storage::spaceOf(loc);
        if (!sp.ok) continue;
        room += sp.freeBytes;
        for (const Entry& e : candidates(loc)) room += e.bytes;
        total += sp.totalBytes;
    }
    return total > 0 && room < static_cast<int64_t>(total * kAlmostFullShare);
}

}  // namespace cache
