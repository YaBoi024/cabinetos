// Games RomM no longer has (#625). What this does and why is gonegames.h.

#include "gonegames.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

#include <dirent.h>
#include <sys/stat.h>

#include "accounts.h"
#include "cache.h"
#include "covercache.h"
#include "fileio.h"
#include "pending.h"
#include "romm.h"
#include "savemirror.h"
#include "standalone.h"
#include "storage.h"

namespace gone {
namespace {

std::atomic<int> gPlaying{0};
std::atomic<int> gLaunching{0};
std::mutex gMu;                // guards the four below
Queue gQueue;                  // the upload queue's two questions
bool gAsked = false;           // RomM answered this start
std::set<int> gGone;           // what it said
std::set<std::string> gSaid;   // what was said to be left, so each is said once

bool inUse(int romId) { return romId == gPlaying.load() || romId == gLaunching.load(); }

// Says once a run why a game, or a person's files of it, were left.
void sayLeft(const std::string& what, const std::string& line) {
    std::lock_guard<std::mutex> lk(gMu);
    if (gSaid.insert(what).second) std::fprintf(stderr, "%s\n", line.c_str());
}

// The folders named `<romId>` one level under `dir`: `saves/<platform>/<id>`.
std::vector<std::string> gameFolders(const std::string& dir, int romId) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string p = dir + "/" + e->d_name + "/" + std::to_string(romId);
        if (storage::exists(p)) out.push_back(p);
    }
    ::closedir(d);
    return out;
}

// A folder anywhere under `dir` holding an emulator's note: saves not yet
// off it (standalone.h, notesPlaying).
bool holdsNote(const std::string& dir) {
    if (cab::standalone::hasPlayingNote(dir)) return true;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return false;
    bool found = false;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string p = dir + "/" + e->d_name;
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (holdsNote(p)) { found = true; break; }
    }
    ::closedir(d);
    return found;
}

std::string accountName(const std::vector<accounts::Account>& all, int id) {
    for (const accounts::Account& a : all)
        if (a.id == id) return a.name;
    return "user " + std::to_string(id);
}

}  // namespace

bool decide(const std::vector<int>& ids, const std::vector<int>& people, const Asker& ask,
            std::set<int>* gone, std::string* why) {
    gone->clear();
    if (people.empty()) {
        if (why) *why = "nobody is signed in on this console";
        return false;
    }
    std::set<int> left;
    for (int id : ids)
        if (id > 0) left.insert(id);
    for (int p : people) {
        if (left.empty()) break;
        std::vector<int> seen;
        if (!ask.list(p, &seen)) {
            if (why) *why = "the game list of RomM user " + std::to_string(p) + " did not come";
            return false;
        }
        for (int id : seen) left.erase(id);
        for (auto it = left.begin(); it != left.end();)
            it = ask.notFound(p, *it) ? std::next(it) : left.erase(it);
    }
    *gone = std::move(left);
    return true;
}

std::set<int> thisStart(const std::string& baseUrl, const std::vector<int>& ids) {
    {
        std::lock_guard<std::mutex> lk(gMu);
        if (gAsked) return gGone;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<accounts::Account> all = accounts::all();
    std::vector<int> people;
    for (const accounts::Account& a : all) people.push_back(a.id);

    romm::Client c;
    std::string why;
    int calls = 0, loaded = 0;
    std::string listErr;
    // The token is the person's own; it never leaves this client.
    auto as = [&](int person) {
        if (loaded == person) return true;
        loaded = c.loadToken(accounts::tokenPath(person)) ? person : 0;
        return loaded == person;
    };
    Asker ask;
    ask.list = [&](int person, std::vector<int>* out) {
        ++calls;
        if (!as(person)) {
            listErr = accountName(all, person) + " has no sign-in on this console";
            return false;
        }
        if (c.fetchGameIds(out, &listErr)) return true;
        listErr = accountName(all, person) + ": " + listErr;
        return false;
    };
    ask.notFound = [&](int person, int romId) {
        ++calls;
        romm::Game g;
        std::string e;
        bool nf = false;
        return as(person) && !c.fetchGame(romId, &g, &e, &nf) && nf;
    };
    std::set<int> found;
    const bool answered = c.setAddress(baseUrl, &why) && decide(ids, people, ask, &found, &why);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
    std::lock_guard<std::mutex> lk(gMu);
    if (!answered) {
        // Said once a start; the next backfill asks again.
        static bool said = false;
        if (!said)
            std::fprintf(stderr, "[gone] not asked this time, nothing removed: %s%s%s\n",
                         why.c_str(), listErr.empty() ? "" : ": ", listErr.c_str());
        said = true;
        return {};
    }
    gAsked = true;
    gGone = found;
    std::fprintf(stderr, "[gone] asked RomM about %zu game(s) as %zu person(s): %zu gone, "
                         "%d request(s), %lld ms\n",
                 std::set<int>(ids.begin(), ids.end()).size(), people.size(), found.size(), calls,
                 ms);
    return gGone;
}

void setInUse(int playing, int launching) {
    gPlaying = playing;
    gLaunching = launching;
}

void setQueue(Queue q) {
    std::lock_guard<std::mutex> lk(gMu);
    gQueue = std::move(q);
}

bool uploadsPending() {
    std::function<int()> pending;
    {
        std::lock_guard<std::mutex> lk(gMu);
        pending = gQueue.pending;
    }
    return pending && pending() > 0;
}

bool removeCopy(int romId) {
    const std::vector<cache::Placement> copies = cache::findAll(romId);
    auto leave = [&](const char* because) {
        sayLeft("copy " + std::to_string(romId),
                "[gone] " + std::to_string(romId) + " left on the drive for now: RomM no "
                "longer has it, but " + because);
        return false;
    };
    if (inUse(romId)) return leave("it is being played");
    for (const cache::Placement& p : copies)
        if (p.isDirectory && cab::standalone::hasPlayingNote(p.entryPath))
            return leave("its saves are still in its folder");

    int64_t bytes = 0;
    for (const cache::Placement& p : copies) bytes += storage::treeBytes(p.entryPath);
    // Every person's keep, then whatever is left in a cache. One release
    // deletes one kept copy (cache.h, unkeep), so a game kept on two drives
    // takes one round each.
    for (size_t round = 0; round < copies.size(); ++round) {
        const cache::Placement p = cache::find(romId);
        if (!p.present || !p.kept) break;
        cache::Release r;
        cache::unkeepForEveryone(romId, false, &r);
        if (r.what != cache::Release::What::Deleted) break;
    }
    cache::evictGame(romId);
    const std::vector<cache::Placement> after = cache::findAll(romId);

    const std::string record = storage::gamesDir() + "/" + std::to_string(romId) + ".json";
    const bool hadRecord = ::access(record.c_str(), F_OK) == 0;
    if (hadRecord) cab::removeFile(record);
    const bool hadCovers = covercache::forgetGame(romId);

    if (!after.empty()) {
        std::fprintf(stderr, "[gone] %d: RomM no longer has it, and %s would not be removed\n",
                     romId, after.front().entryPath.c_str());
        return false;
    }
    if (copies.empty() && !hadRecord && !hadCovers) return true;
    const std::string others =
        copies.size() > 1 ? " and " + std::to_string(copies.size() - 1) + " more copy(ies)" : "";
    const bool gb = bytes >= (1LL << 30);
    std::fprintf(stderr, "[gone] %d removed, RomM no longer has it: %s%s (%.1f %s)%s%s\n", romId,
                 copies.empty() ? "no copy on the drive" : copies.front().entryPath.c_str(),
                 others.c_str(), bytes / (gb ? 1073741824.0 : 1048576.0), gb ? "GB" : "MB",
                 hadRecord ? ", its record" : "", hadCovers ? ", its covers" : "");
    return true;
}

bool removeSaves(int romId, const storage::User& signedIn) {
    const std::string id = std::to_string(romId);
    if (inUse(romId)) {
        sayLeft("saves " + id, "[gone] " + id + "'s saves left for now: it is being played");
        return false;
    }
    Queue q;
    {
        std::lock_guard<std::mutex> lk(gMu);
        q = gQueue;
    }
    int people = 0, files = 0, owedGone = 0;
    int64_t bytes = 0;
    bool left = false;
    for (const storage::User& u : storage::knownUsers()) {
        const std::string home = storage::userDir(u);
        std::vector<std::string> folders = gameFolders(home + "/saves", romId);
        for (const std::string& f : gameFolders(home + "/states", romId)) folders.push_back(f);
        if (storage::exists(storage::screenshotsDir(u) + "/" + id))
            folders.push_back(storage::screenshotsDir(u) + "/" + id);
        std::vector<cache::Owed> owed;
        for (cache::Owed& o : cache::owed(u))
            if (o.romId == romId) owed.push_back(std::move(o));
        // NOT YET TRIED: the signed-in person's owed file before RomM has
        // refused it this run, or saves still in an emulator's folder.
        std::string wait;
        for (const cache::Owed& o : owed)
            if (u.id == signedIn.id && !(q.refused && q.refused(u, romId, o.fileName)))
                wait = o.fileName + " has not been tried by the upload queue yet";
        for (const std::string& f : folders)
            if (wait.empty() && holdsNote(f)) wait = f + " still holds an emulator's note";
        if (!wait.empty()) {
            sayLeft("saves " + id + " " + home,
                    "[gone] " + id + ": " + u.name + "'s saves left for now: " + wait);
            left = true;
            continue;
        }
        bool any = !folders.empty() || !owed.empty();
        for (const std::string& f : folders) {
            bytes += storage::treeBytes(f);
            if (storage::removeEntry(f)) ++files;
        }
        for (const cache::Owed& o : owed) {
            for (const std::string& p : {o.localPath, o.shotPath}) {
                if (p.empty() || !storage::exists(p)) continue;
                bytes += storage::treeBytes(p);
                if (cab::removeFile(p)) ++files;
            }
            cache::clearPending(u, romId, o.fileName);
            ++owedGone;
        }
        if (storage::exists(home + "/server-saves/" + id)) {
            savemirror::forget(u, romId);
            any = true;
        }
        if (any) ++people;
    }
    if (people)
        std::fprintf(stderr, "[gone] %d: saves, states and screenshots removed for %d person(s), "
                             "%d item(s), %.1f KB, %d of them owed; RomM deleted its own with "
                             "the game\n", romId, people, files, bytes / 1024.0, owedGone);
    return !left;
}

std::vector<int> owedIds() {
    std::set<int> ids;
    for (const storage::User& u : storage::knownUsers())
        for (const cache::Owed& o : cache::owed(u)) ids.insert(o.romId);
    return std::vector<int>(ids.begin(), ids.end());
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto exists = [](const std::string& p) { return storage::exists(p); };

    // 1. The rule, against a stand-in RomM: `sees` is each person's list, and
    // `answers` what each game's own entry answers them (404, 200, 500, or 0
    // for no answer at all); a game in neither answers 200.
    std::map<int, std::vector<int>> sees;
    std::map<std::pair<int, int>, int> answers;
    std::set<int> listFails;
    int asked = 0;
    Asker ask;
    ask.list = [&](int p, std::vector<int>* out) {
        if (listFails.count(p)) return false;
        *out = sees[p];
        return true;
    };
    ask.notFound = [&](int p, int id) {
        ++asked;
        const auto it = answers.find({p, id});
        return it != answers.end() && it->second == 404;
    };
    std::set<int> g;
    std::string why;

    sees[1] = {1, 2, 3};
    answers[{1, 705}] = 404;
    answers[{1, 706}] = 500;
    answers[{1, 707}] = 0;
    answers[{1, 708}] = 403;
    bool ok = decide({2, 3, 705, 706, 707, 708}, {1}, ask, &g, &why);
    check(ok && g == std::set<int>{705}, "one person: only the game whose entry answered 404 is gone");
    check(g.count(706) == 0 && g.count(707) == 0 && g.count(708) == 0,
          "a 5xx, no answer and a 403 are not gone");
    check(asked == 4, "a game in the person's list is not asked about by its entry");

    sees[2] = {705};
    ok = decide({705}, {1, 2}, ask, &g, &why);
    check(ok && g.empty(), "a game hidden from one person and seen by another stays");

    sees[2] = {};
    answers[{2, 705}] = 200;
    ok = decide({705}, {1, 2}, ask, &g, &why);
    check(ok && g.empty(), "404 to one person and found by another's own entry: it stays");

    answers[{2, 705}] = 404;
    ok = decide({705, 706}, {1, 2}, ask, &g, &why);
    check(ok && g == std::set<int>{705}, "404 to every person on the console: gone");

    listFails.insert(2);
    ok = decide({705}, {1, 2}, ask, &g, &why);
    check(!ok && g.empty(), "a person whose list does not come (the server away): nothing gone");
    ok = decide({705}, {}, ask, &g, &why);
    check(!ok && g.empty(), "nobody to ask: nothing gone");

    // 2. The removal, on a scratch drive.
    char root[] = "/tmp/cabinetos-gone-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[gone] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[gone] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("\nscratch root  %s\n\n", root);
    const std::string loc = storage::primaryLocation();
    storage::User a, b;
    a.id = 1;
    a.name = "gone-test-a";
    b.id = 2;
    b.name = "gone-test-b";
    storage::makeDirs(storage::userDir(a));
    storage::makeDirs(storage::userDir(b));
    auto put = [](const std::string& p) { cab::writeBytes(p, std::string(4096, 'g')); };

    // 705 played (cache), with its manual; 2268 kept by both people (roms);
    // 100 still on RomM; 3017 being played; 900 an Xbox game whose saves are
    // still in its folder.
    const std::string fb = storage::cacheDir(loc) + "/FBNEO";
    const std::string played = fb + "/705 - Gone Fighter";
    put(played + "/gonefight.zip");
    put(fb + "/Manuals/705 - Gone Fighter.pdf");
    const std::string live = fb + "/100 - Live Game.zip";
    put(live);
    const std::string kept = storage::romsDir(loc) + "/Sony Playstation 3/2268 - BioShock";
    put(kept + "/PS3_GAME/USRDIR/EBOOT.BIN");
    const std::string record = "{\"id\":2268,\"name\":\"BioShock\"}";
    cache::keep(a, 2268, record, &serr);
    cache::keep(b, 2268, record, &serr);
    const std::string running = storage::cacheDir(loc) + "/MAME2003/3017 - Playing.zip";
    put(running);
    const std::string xbox = storage::cacheDir(loc) + "/Microsoft Xbox/900 - Xbox Gone";
    put(xbox + "/game.iso");
    cab::standalone::writePlayingNote(xbox, "1\nsaves\nXbox Gone\nxemu\n");
    for (int id : {705, 100, 2268})
        cache::rememberGame(id, "{\"id\":" + std::to_string(id) + "}");
    covercache::setServer("gone-test");
    const std::string covers = covercache::dir() + "/" + storage::safeSegment("gone-test");
    put(covers + "/22/705/small.none.img");
    put(covers + "/55/100/small.none.img");
    // Saves, states and screenshots. Person a is signed in: an owed save of
    // 705 with its file, a sent state, a screenshot and RomM's saves as last
    // seen; an owed save of 100, which RomM has; a sent save of 2268. Person b:
    // a sent save of 705 and one owed; 2268's saves still in an emulator's
    // folder; a save of 3017.
    auto owe = [&](const storage::User& u, int id, const std::string& file) {
        cache::Owed o;
        o.romId = id;
        o.emulator = "gone-test";
        o.fileName = file;
        o.localPath = storage::savesDir(u, "FBNEO", id, "fbneo") + "/" + file;
        put(o.localPath);
        cache::markPending(u, o, 4096);
        return o.localPath;
    };
    const std::string aOwed = owe(a, 705, "gonefight.nv");
    const std::string aState = storage::statesDir(a, "FBNEO", 705, "fbneo") + "/1.state";
    put(aState);
    const std::string aShot = storage::screenshotsDir(a) + "/705/shot.png";
    put(aShot);
    const std::string aMirror = storage::userDir(a) + "/server-saves/705/saves.json";
    put(aMirror);
    owe(a, 100, "live.nv");
    const std::string aKept = storage::savesDir(a, "Sony Playstation 3", 2268, "rpcs3") + "/x";
    put(aKept);
    const std::string bSent = storage::savesDir(b, "FBNEO", 705, "fbneo") + "/sent.nv";
    put(bSent);
    const std::string bOwed = owe(b, 705, "b.nv");
    const std::string bNoted = storage::savesDir(b, "Sony Playstation 3", 2268, "rpcs3");
    put(bNoted + "/hdd0/save.dat");
    cab::standalone::writePlayingNote(bNoted, "2268\n");
    const std::string bPlaying = storage::savesDir(b, "MAME2003", 3017, "mame2003") + "/hi";
    put(bPlaying);

    setInUse(3017, 0);
    check(owedIds() == std::vector<int>({100, 705}), "the games owed for are listed for asking");
    check(removeCopy(705) && !exists(played) && !exists(fb + "/Manuals/705 - Gone Fighter.pdf"),
          "a played copy goes, and its manual with it");
    check(!exists(storage::gamesDir() + "/705.json") && !exists(covers + "/22/705"),
          "and its record and covers, under a platform the server may still list");
    check(removeCopy(2268) && !exists(kept) && !cache::isKeptBy(a, 2268) &&
              !cache::isKeptBy(b, 2268),
          "a download kept by two people goes, and both keeps with it");
    check(!removeCopy(3017) && exists(running), "a game being played is left");
    check(!removeCopy(900) && exists(xbox + "/game.iso"),
          "a folder still holding saves not yet off it is left");
    check(exists(live) && exists(storage::gamesDir() + "/100.json") &&
              exists(covers + "/55/100/small.none.img"),
          "a game RomM still has is untouched");

    std::set<std::string> refused;
    int queued = 2;
    setQueue({[&] { return queued; },
              [&](const storage::User& u, int id, const std::string& f) {
                  return refused.count(u.name + "/" + std::to_string(id) + "/" + f) > 0;
              }});
    check(uploadsPending(), "the upload queue still holding uploads is waited for");
    queued = 0;
    check(!uploadsPending(), "and an empty one is not");
    check(!removeSaves(705, a) && exists(aOwed) && exists(aState) && exists(aShot) &&
              cache::owed(a).size() == 2,
          "the signed-in person's owed save before the queue has tried it: all theirs left");
    check(!exists(bSent) && !exists(bOwed) && cache::owed(b).empty(),
          "another person's, sent and owed, go: RomM answered 404 to them");
    refused.insert(a.name + "/705/gonefight.nv");
    check(removeSaves(705, a) && !exists(aOwed) && !exists(aState) && !exists(aShot) &&
              !exists(aMirror),
          "once RomM refused it this run: the save, the state, the screenshot and RomM's "
          "copy go");
    const std::vector<cache::Owed> still = cache::owed(a);
    check(still.size() == 1 && still[0].romId == 100, "and a live game's owed save stays owed");
    check(!removeSaves(2268, a) && !exists(aKept) && exists(bNoted + "/hdd0/save.dat"),
          "a person's saves still in an emulator's folder are left, the others go");
    check(!removeSaves(3017, a) && exists(bPlaying), "a game being played keeps its saves");
    setInUse(0, 0);
    check(removeCopy(3017) && !exists(running) && removeSaves(3017, a) && !exists(bPlaying),
          "the played game and its saves go once it is left");
    setQueue({});

    storage::removeEntry(root);
    std::printf("\ngone games: %d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

}  // namespace gone
