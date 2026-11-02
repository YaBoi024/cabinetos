#include "covercache.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cache.h"
#include "fileio.h"
#include "storage.h"

namespace covercache {
namespace {

std::mutex gMutex;          // guards gServer, the counters and directory creation
std::string gServer;        // sanitised, empty until setServer
Stats gStats;

// Pulls the two ids and the size out of a RomM cover path, and the version off
// the query string. Returns false for anything that is not one — an avatar, a
// platform logo, a stand-in library's file on disk — and those are then never
// cached rather than filed under a guess.
//
//   /assets/romm/resources/roms/1/2/cover/small.png?ts=2025-01-01 00:00:00
//                               ^pid ^rid    ^size      ^version
bool parse(const std::string& key, std::string* pid, std::string* rid,
           std::string* size, std::string* version) {
    const std::string marker = "/resources/roms/";
    const size_t at = key.find(marker);
    if (at == std::string::npos) return false;
    size_t i = at + marker.size();

    auto segment = [&](std::string* out) {
        const size_t start = i;
        while (i < key.size() && key[i] != '/') ++i;
        if (i == start || i >= key.size()) return false;
        out->assign(key, start, i - start);
        ++i;                       // step over the slash
        return true;
    };
    if (!segment(pid) || !segment(rid)) return false;

    // The next segment is "cover"; anything else is a different kind of asset
    // and is left alone.
    std::string kind;
    if (!segment(&kind) || kind != "cover") return false;

    const size_t q = key.find('?', i);
    const size_t end = (q == std::string::npos) ? key.size() : q;
    if (end <= i) return false;
    size->assign(key, i, end - i);            // "small.png"
    // Strip the extension; the file written here keeps its own.
    const size_t dot = size->rfind('.');
    if (dot != std::string::npos) size->erase(dot);

    // No timestamp is not a failure. It means the server did not version this
    // one, and it is filed under a constant so it is still found again.
    version->assign("none");
    if (q != std::string::npos) {
        const std::string ts = "ts=";
        const size_t t = key.find(ts, q);
        if (t != std::string::npos) *version = key.substr(t + ts.size());
    }
    return !pid->empty() && !rid->empty() && !size->empty();
}

// The file a key is stored at, or empty when the key is not cover art or no
// server has been set.
std::string pathFor(const std::string& key, std::string* dirOut) {
    std::string server;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        server = gServer;
    }
    if (server.empty()) return {};

    std::string pid, rid, size, version;
    if (!parse(key, &pid, &rid, &size, &version)) return {};

    // ORGANISED RATHER THAN HASHED, because somebody will look in here. A hash
    // of the whole key would be one flat directory of unreadable names, and the
    // first question anybody asks of a cache is "what is in it".
    const std::string d = dir() + "/" + server + "/" + storage::safeSegment(pid) +
                          "/" + storage::safeSegment(rid);
    if (dirOut) *dirOut = d;
    return d + "/" + storage::safeSegment(size) + "." +
           storage::safeSegment(version) + ".img";
}

}  // namespace

void setServer(const std::string& address) {
    std::lock_guard<std::mutex> lk(gMutex);
    gServer = storage::safeSegment(address);
}

std::string dir() { return storage::root() + "/covers"; }

std::vector<uint8_t> read(const std::string& key) {
    const std::string path = pathFor(key, nullptr);
    if (path.empty()) return {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::vector<uint8_t> out;
    uint8_t buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    // A truncated file is worse than no file: it decodes to nothing and the
    // cover silently never appears. An empty read is treated as a miss and the
    // server is asked.
    if (out.empty()) return {};
    {
        std::lock_guard<std::mutex> lk(gMutex);
        ++gStats.fromDisk;
    }
    return out;
}

Stats stats() {
    std::lock_guard<std::mutex> lk(gMutex);
    return gStats;
}

void write(const std::string& key, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return;
    std::string d;
    const std::string path = pathFor(key, &d);
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        ++gStats.fetched;
    }

    // The reserve belongs to the system image, not to artwork. Checked before
    // every write rather than once at startup, because what fills a disk is
    // usually the thing that has been running for an hour.
    if (cache::freeBytes(storage::root()) <
        cache::kSystemReserveBytes + static_cast<int64_t>(bytes.size()))
        return;

    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (!storage::makeDirs(d)) return;
    }

    // WRITTEN ASIDE AND RENAMED. Four ImageCache workers run concurrently and a
    // console loses power with no warning; a half-written cover that is found
    // later is indistinguishable from a real one, and rename is the only step
    // here that is atomic.
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    const size_t put = std::fwrite(bytes.data(), 1, bytes.size(), f);
    const bool ok = (put == bytes.size()) && (std::fflush(f) == 0);
    std::fclose(f);
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return;
    }
    std::lock_guard<std::mutex> lk(gMutex);
    ++gStats.stored;
}


// --- The tile map -----------------------------------------------------------

namespace {

std::string tilesPath() {
    std::lock_guard<std::mutex> lk(gMutex);
    if (gServer.empty()) return {};
    return dir() + "/" + gServer + "/tiles.tsv";
}

}  // namespace

std::map<int, Tile> loadTiles() {
    std::map<int, Tile> out;
    const std::string path = tilesPath();
    if (path.empty()) return out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    // Tab-separated rather than JSON: the fields are an int, two short strings
    // and an int, none of which can contain a tab, and this file is read before
    // anything else on a boot that is trying to be fast.
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t a = l.find('\t');
        if (a == std::string::npos) continue;
        size_t b = l.find('\t', a + 1);
        if (b == std::string::npos) continue;
        size_t c = l.find('\t', b + 1);
        if (c == std::string::npos) continue;
        Tile t;
        const int id = std::atoi(l.substr(0, a).c_str());
        t.updatedAt = l.substr(a + 1, b - a - 1);
        t.romCount = std::atoi(l.substr(b + 1, c - b - 1).c_str());
        t.cover = l.substr(c + 1);
        if (id > 0 && !t.cover.empty()) out.emplace(id, std::move(t));
    }
    std::fclose(f);
    return out;
}

void saveTiles(const std::map<int, Tile>& tiles) {
    const std::string path = tilesPath();
    if (path.empty() || tiles.empty()) return;
    std::string d = path.substr(0, path.rfind('/'));
    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (!storage::makeDirs(d)) return;
    }
    std::string text;
    for (const auto& [id, t] : tiles)
        text += std::to_string(id) + "\t" + t.updatedAt + "\t" + std::to_string(t.romCount) +
                "\t" + t.cover + "\n";
    cab::writeBytes(path, text);
}

// --- Sweep and eviction -----------------------------------------------------

namespace {

// Ends in `suffix`.
bool endsWith(const char* name, const char* suffix) {
    const size_t n = std::strlen(name), k = std::strlen(suffix);
    return n >= k && std::strcmp(name + n - k, suffix) == 0;
}

// Every cover under a directory, with its size and last-used time. Not the
// tile map, which is read at every start and rewritten only when a tile
// changes, so it is often the oldest file here; and not a temporary another
// thread is writing (#482, FILES-04). lstat: a link is not followed out of
// the cache (FILES-05).
void walk(const std::string& root, std::vector<std::string>* files,
          std::vector<struct stat>* stats) {
    DIR* d = ::opendir(root.c_str());
    if (!d) return;
    while (dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
            continue;
        const std::string p = root + "/" + e->d_name;
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { walk(p, files, stats); continue; }
        if (!S_ISREG(st.st_mode)) continue;
        if (std::strcmp(e->d_name, "tiles.tsv") == 0 || endsWith(e->d_name, ".part") ||
            endsWith(e->d_name, ".placing"))
            continue;
        files->push_back(p);
        stats->push_back(st);
    }
    ::closedir(d);
}

}  // namespace

void sweep(const std::vector<int>& livePlatformIds) {
    // AN EMPTY LIST IS NOT AN EMPTY SERVER. The only caller that can honestly
    // produce one is a console whose platform fetch failed, and sweeping on
    // that would delete everything on the boot that most wants it.
    if (livePlatformIds.empty()) return;
    std::string base;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (gServer.empty()) return;
        base = dir() + "/" + gServer;
    }
    DIR* d = ::opendir(base.c_str());
    if (!d) return;
    int gone = 0;
    while (dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
            continue;
        const std::string p = base + "/" + e->d_name;
        // A link is removed, never followed (storage::removeEntry lstats).
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0 || !(S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
            continue;
        const int id = std::atoi(e->d_name);
        if (id <= 0) continue;
        if (std::find(livePlatformIds.begin(), livePlatformIds.end(), id) !=
            livePlatformIds.end())
            continue;
        if (storage::removeEntry(p)) ++gone;
    }
    ::closedir(d);
    if (gone)
        std::fprintf(stderr, "[covers] swept %d platform(s) the server no longer has\n",
                     gone);
}

bool forgetGame(int romId) {
    if (romId <= 0) return false;
    std::string base;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (gServer.empty()) return false;
        base = dir() + "/" + gServer;
    }
    DIR* d = ::opendir(base.c_str());
    if (!d) return false;
    bool any = false;
    const std::string rid = std::to_string(romId);
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.' || std::atoi(e->d_name) <= 0) continue;
        const std::string p = base + "/" + e->d_name + "/" + rid;
        struct stat st;
        if (::lstat(p.c_str(), &st) == 0 && storage::removeEntry(p)) any = true;
    }
    ::closedir(d);
    return any;
}

void evict(int64_t budgetBytes) {
    std::string base;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (gServer.empty()) return;
        base = dir() + "/" + gServer;
    }
    std::vector<std::string> files;
    std::vector<struct stat> stats;
    walk(base, &files, &stats);
    int64_t total = 0;
    for (const struct stat& st : stats) total += st.st_size;
    if (total <= budgetBytes) return;

    // LEAST RECENTLY USED, by atime where the filesystem keeps one and mtime
    // otherwise. relatime gives a usable atime for this purpose: it is updated
    // when a file is read after being a day stale, which is exactly the
    // granularity a cover cache wants.
    std::vector<size_t> order(files.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const time_t ta = std::max(stats[a].st_atime, stats[a].st_mtime);
        const time_t tb = std::max(stats[b].st_atime, stats[b].st_mtime);
        return ta < tb;
    });
    int64_t freed = 0;
    int n = 0;
    for (size_t i : order) {
        if (total - freed <= budgetBytes) break;
        if (::remove(files[i].c_str()) != 0) continue;
        freed += stats[i].st_size;
        ++n;
    }
    if (n)
        std::fprintf(stderr, "[covers] evicted %d file(s), %.1f MB, to stay under %.0f MB\n",
                     n, freed / 1048576.0, budgetBytes / 1048576.0);
}

}  // namespace covercache
