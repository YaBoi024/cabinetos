// Games RomM no longer has (#625). What this does and why is gonegames.h.

#include "gonegames.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

#include "accounts.h"
#include "cache.h"
#include "covercache.h"
#include "fileio.h"
#include "pending.h"
#include "romm.h"
#include "standalone.h"
#include "storage.h"

namespace gone {
namespace {

std::atomic<int> gPlaying{0};
std::atomic<int> gLaunching{0};

std::mutex gMu;          // guards the three below
bool gAsked = false;     // RomM answered this start
std::set<int> gGone;     // what it said
std::set<int> gSaid;     // games already said to be left, so each is said once

bool inUse(int romId) { return romId == gPlaying.load() || romId == gLaunching.load(); }

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

bool removeCopy(int romId) {
    const std::vector<cache::Placement> copies = cache::findAll(romId);
    auto leave = [&](const char* because) {
        std::lock_guard<std::mutex> lk(gMu);
        if (gSaid.insert(romId).second)
            std::fprintf(stderr, "[gone] %d left on the drive for now: RomM no longer has "
                                 "it, but %s\n", romId, because);
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

int forgetOwed(int romId) {
    int n = 0;
    for (const storage::User& u : storage::knownUsers())
        for (const cache::Owed& o : cache::owed(u))
            if (o.romId == romId) {
                cache::clearPending(u, romId, o.fileName);
                ++n;
            }
    if (n)
        std::fprintf(stderr, "[gone] %d: %d upload(s) no longer owed, RomM no longer has the "
                             "game; the files stay on the drive\n", romId, n);
    return n;
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
    // An owed save of 705 and one of 100, each with its file.
    auto owe = [&](int id, const std::string& file) {
        cache::Owed o;
        o.romId = id;
        o.emulator = "gone-test";
        o.fileName = file;
        o.localPath = storage::savesDir(a, "FBNEO", id, "fbneo") + "/" + file;
        put(o.localPath);
        cache::markPending(a, o, 4096);
        return o.localPath;
    };
    const std::string goneSave = owe(705, "gonefight.nv");
    owe(100, "live.nv");

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
    check(forgetOwed(705) == 1 && exists(goneSave), "a save owed for it stops being owed, its file kept");
    const std::vector<cache::Owed> still = cache::owed(a);
    check(still.size() == 1 && still[0].romId == 100, "and a live game's save is still owed");
    setInUse(0, 0);
    check(removeCopy(3017) && !exists(running), "the played game goes once it is left");

    storage::removeEntry(root);
    std::printf("\ngone games: %d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

}  // namespace gone
