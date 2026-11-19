// The markers of what this console still owes RomM (pending.h). They hold no
// state in memory, which is what lets clearPending run on the uploader's
// worker while owed runs on the main thread; a game's owed push
// takes one lock, across marking and clearing it (gPushLock). Moved out of
// cache.cpp in the restructure (#398).

#include "pending.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#include "fileio.h"
#include "storage.h"

namespace cache {
namespace {

// A file name that cannot escape its directory or collide with a marker from
// another game. Upload file names come from game titles, which contain slashes,
// colons and worse.
std::string markerName(int romId, const std::string& fileName) {
    std::string s = std::to_string(romId) + "-";
    for (unsigned char c : fileName) {
        s += (std::isalnum(c) || c == '.' || c == '-' || c == '_')
                 ? static_cast<char>(c) : '_';
    }
    return s + ".pending";
}

// Only a marker counts: not a dotfile, and not a temporary writeBytes left
// when the power went halfway through writing one.
bool endsWith(const char* name, const char* suffix) {
    const size_t n = std::strlen(name), k = std::strlen(suffix);
    return name[0] != '.' && n > k && std::strcmp(name + n - k, suffix) == 0;
}

bool isMarker(const char* name) { return endsWith(name, ".pending"); }

// A game's owed push (OwedPush), `<romId>.push`.
const char kPushSuffix[] = ".push";
bool isPushMarker(const char* name) { return endsWith(name, kPushSuffix); }

std::string pushPath(const storage::User& u, int romId) {
    return storage::pendingDir(u) + "/" + std::to_string(romId) + kPushSuffix;
}

// MARKING AND CLEARING A GAME'S PUSH RUN ON TWO THREADS: a session's end
// writes it on the frame thread, and a push that lands clears it on the
// upload queue's. Held across clearPushOwed's read and its delete, so a
// marker written between the two is never the one deleted.
std::mutex gPushLock;
std::atomic<uint64_t> gStamps{0};

// Unique on this console: the time, the process and a count.
std::string newStamp() {
    struct timespec ts {};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return std::to_string(static_cast<long long>(ts.tv_sec)) + "." +
           std::to_string(static_cast<long>(ts.tv_nsec)) + "-" + std::to_string(::getpid()) +
           "-" + std::to_string(++gStamps);
}

// False when it names no game or carries no stamp.
bool readPush(const std::string& path, OwedPush* p) {
    *p = OwedPush{};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool first = true;
    char line[4096];
    while (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (first) { first = false; continue; }   // the byte count
        const size_t sp = l.find(' ');
        if (sp == std::string::npos) continue;
        const std::string k = l.substr(0, sp), v = l.substr(sp + 1);
        OwedPush::State* st = p->states.empty() ? nullptr : &p->states.back();
        if (k == "stamp") p->stamp = v;
        else if (k == "rom") p->romId = std::atoi(v.c_str());
        else if (k == "made") p->madeAt = std::atoll(v.c_str());
        else if (k == "romfile") p->romFileId = std::atoi(v.c_str());
        else if (k == "channel") p->channelId = v;
        else if (k == "label") p->label = v;
        else if (k == "expected") p->expectedCurrentId = std::atoi(v.c_str());
        else if (k == "parent") p->parentId = std::atoi(v.c_str());
        else if (k == "couldnotuse") p->couldNotUseNewest = v == "1";
        else if (k == "emulator") p->emulator = v;
        else if (k == "core") p->core = v;
        else if (k == "savename") p->saveName = v;
        else if (k == "savepath") p->savePath = v;
        else if (k == "saveshape") p->saveShape = v;
        else if (k == "state") p->states.push_back({v, "", "", "", "", 0});
        else if (st && k == "statemade") st->madeMs = std::atoll(v.c_str());
        else if (st && k == "statename") st->fileName = v;
        else if (st && k == "statepath") st->path = v;
        else if (st && k == "picname") st->pictureName = v;
        else if (st && k == "picpath") st->picturePath = v;
    }
    std::fclose(f);
    p->hasSave = !p->savePath.empty();
    return p->romId > 0 && !p->stamp.empty();
}

}  // namespace

const char kOwedPushName[] = "snapshot";

void markPending(const storage::User& u, const Owed& o, int64_t bytes) {
    if (!u.valid()) return;
    storage::makeDirs(storage::pendingDir(u));
    const std::string path = storage::pendingDir(u) + "/" + markerName(o.romId, o.fileName);
    // One "key value" per line after the byte count. Values are file names
    // and paths, which never hold a newline. Through writeBytes (#482): a
    // marker written in place and cut by a power cut was an empty file that
    // owed() skipped and pendingCount still counted, so the save was never
    // sent and Sign out said something was owed for good.
    std::string text = std::to_string(static_cast<long long>(bytes)) + "\n";
    text += std::string("kind ") +
            (o.isScreenshot ? "screenshot" : o.isState ? "state" : "save") + "\n";
    text += "rom " + std::to_string(o.romId) + "\n";
    text += "emulator " + o.emulator + "\n";
    text += "name " + o.fileName + "\n";
    text += "local " + o.localPath + "\n";
    if (!o.shotName.empty()) {
        text += "shotname " + o.shotName + "\n";
        text += "shot " + o.shotPath + "\n";
    }
    if (!cab::writeBytes(path, text))
        std::fprintf(stderr, "[upload] could not write the marker %s\n", path.c_str());
}

std::vector<Owed> owed(const storage::User& u) {
    std::vector<std::pair<time_t, Owed>> found;
    if (!u.valid()) return {};
    const std::string dir = storage::pendingDir(u);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return {};
    while (struct dirent* e = ::readdir(d)) {
        if (!isMarker(e->d_name)) continue;
        const std::string path = dir + "/" + e->d_name;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        Owed o;
        bool first = true, kind = false;
        char line[4096];
        while (std::fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (first) { first = false; continue; }   // the byte count
            const size_t sp = l.find(' ');
            if (sp == std::string::npos) continue;
            const std::string k = l.substr(0, sp), v = l.substr(sp + 1);
            if (k == "kind") {
                kind = true;
                o.isState = v == "state";
                o.isScreenshot = v == "screenshot";
            }
            else if (k == "rom") o.romId = std::atoi(v.c_str());
            else if (k == "emulator") o.emulator = v;
            else if (k == "name") o.fileName = v;
            else if (k == "local") o.localPath = v;
            else if (k == "shotname") o.shotName = v;
            else if (k == "shot") o.shotPath = v;
        }
        std::fclose(f);
        if (!kind || o.romId <= 0 || o.emulator.empty() || o.fileName.empty() ||
            o.localPath.empty()) {
            std::fprintf(stderr, "[upload] %s says nothing about what to send; left owed\n",
                         e->d_name);
            continue;
        }
        struct stat st;
        found.emplace_back(::stat(path.c_str(), &st) == 0 ? st.st_mtime : 0, std::move(o));
    }
    ::closedir(d);
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Owed> out;
    for (auto& [t, o] : found) out.push_back(std::move(o));
    return out;
}

void clearPending(const storage::User& u, int romId, const std::string& fileName) {
    if (!u.valid()) return;
    ::unlink((storage::pendingDir(u) + "/" + markerName(romId, fileName)).c_str());
}

std::string markPushOwed(const storage::User& u, const OwedPush& p, int64_t bytes) {
    if (!u.valid() || p.romId <= 0) return "";
    std::lock_guard<std::mutex> lk(gPushLock);
    const std::string stamp = newStamp();
    // Values are names, paths and ids, which never hold a newline. Only what
    // is set is written; what is missing reads back as unset.
    std::string text = std::to_string(static_cast<long long>(bytes)) + "\n";
    auto put = [&](const char* k, const std::string& v) {
        if (!v.empty()) text += std::string(k) + " " + v + "\n";
    };
    auto putId = [&](const char* k, long long v) {
        if (v != 0) put(k, std::to_string(v));
    };
    put("stamp", stamp);
    putId("rom", p.romId);
    putId("made", p.madeAt);
    putId("romfile", p.romFileId);
    put("channel", p.channelId);
    put("label", p.label);
    putId("expected", p.expectedCurrentId);
    putId("parent", p.parentId);
    if (p.couldNotUseNewest) put("couldnotuse", "1");
    put("emulator", p.emulator);
    put("core", p.core);
    if (p.hasSave) {
        put("savename", p.saveName);
        put("savepath", p.savePath);
        put("saveshape", p.saveShape);
    }
    for (const OwedPush::State& st : p.states) {
        text += "state " + st.core + "\n";
        putId("statemade", st.madeMs);
        put("statename", st.fileName);
        put("statepath", st.path);
        put("picname", st.pictureName);
        put("picpath", st.picturePath);
    }
    if (!cab::writeBytes(pushPath(u, p.romId), text)) {
        std::fprintf(stderr, "[upload] could not write the owed push of game %d\n", p.romId);
        return "";
    }
    return stamp;
}

std::vector<OwedPush> owedPushes(const storage::User& u) {
    std::vector<std::pair<time_t, OwedPush>> found;
    if (!u.valid()) return {};
    const std::string dir = storage::pendingDir(u);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return {};
    while (struct dirent* e = ::readdir(d)) {
        if (!isPushMarker(e->d_name)) continue;
        const std::string path = dir + "/" + e->d_name;
        OwedPush p;
        if (!readPush(path, &p)) {
            std::fprintf(stderr, "[upload] %s names no game; left owed\n", e->d_name);
            continue;
        }
        struct stat st;
        found.emplace_back(::stat(path.c_str(), &st) == 0 ? st.st_mtime : 0, std::move(p));
    }
    ::closedir(d);
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<OwedPush> out;
    for (auto& [t, p] : found) out.push_back(std::move(p));
    return out;
}

bool owedPush(const storage::User& u, int romId, OwedPush* p) {
    if (!u.valid()) {
        *p = OwedPush{};
        return false;
    }
    return readPush(pushPath(u, romId), p);
}

bool isPushOwed(const storage::User& u, int romId) {
    if (!u.valid()) return false;
    struct stat st;
    return ::stat(pushPath(u, romId).c_str(), &st) == 0;
}

bool saveOwed(const storage::User& u, int romId) {
    OwedPush p;
    if (owedPush(u, romId, &p) && p.hasSave) return true;
    for (const Owed& o : owed(u))
        if (o.romId == romId && !o.isState && !o.isScreenshot) return true;
    return false;
}

void clearPushOwed(const storage::User& u, int romId, const std::string& stamp) {
    if (!u.valid() || stamp.empty()) return;
    std::lock_guard<std::mutex> lk(gPushLock);
    OwedPush p;
    if (readPush(pushPath(u, romId), &p) && p.stamp == stamp)
        ::unlink(pushPath(u, romId).c_str());
}

int pendingCount(const storage::User& u) {
    if (!u.valid()) return 0;
    DIR* d = ::opendir(storage::pendingDir(u).c_str());
    if (!d) return 0;
    int n = 0;
    while (struct dirent* f = ::readdir(d))
        if (isMarker(f->d_name) || isPushMarker(f->d_name)) ++n;
    ::closedir(d);
    return n;
}

int64_t pendingBytes() {
    int64_t total = 0;
    for (const storage::User& u : storage::knownUsers()) {
        const std::string dir = storage::pendingDir(u);
        DIR* d = ::opendir(dir.c_str());
        if (!d) continue;
        while (struct dirent* f = ::readdir(d)) {
            if (!isMarker(f->d_name) && !isPushMarker(f->d_name)) continue;
            if (FILE* fh = std::fopen((dir + "/" + f->d_name).c_str(), "rb")) {
                long long n = 0;
                if (std::fscanf(fh, "%lld", &n) == 1 && n > 0) total += n;
                std::fclose(fh);
            }
        }
        ::closedir(d);
    }
    return total;
}

}  // namespace cache
