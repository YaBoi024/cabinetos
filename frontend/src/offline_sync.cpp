#include "offline_sync.h"

#include <SDL3/SDL.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "app.h"
#include "accounts.h"
#include "cache.h"
#include "catalog.h"
#include "covercache.h"
#include "gonegames.h"
#include "library.h"
#include "romm.h"
#include "savemirror.h"
#include "storage.h"

// STOPPED AT EXIT (#406). Both threads were detached and endless or long,
// so one still running at exit ran on into the program's teardown. Each now
// looks at gStop between steps, and stopOfflineSync joins them. The thread
// objects are under gThreads, because the reconnect thread starts backfills.
static std::atomic<bool> gStop{false};
static std::mutex gThreads;
static std::thread gBackfill;    // under gThreads
static std::thread gReconnect;   // under gThreads

// WHAT OFFLINE WILL NEED, FETCHED WHILE ONLINE (#88). A game on the drive
// with no record cannot be shown offline, and one whose covers were never
// drawn here would show as a colour. Both are fetched once per start, behind
// Home, on a client of their own: the records first (a game cached before
// records existed, or kept with an older one), then the two covers of every
// game on the drive that the cover cache does not hold. MMagTech, on #88:
// "fetch covers once for games already kept", and cached games the same.
// FIRST, THE GAMES RomM NO LONGER HAS (#625, gonegames.h): asked once per
// start, the first time RomM answers, and removed before anything is fetched
// for them, so their covers are not fetched back at every start. One left on
// the drive (being played, or its saves still in its folder) is tried again at
// the next backfill without asking again, and nothing is fetched for it.
void backfillDrive(const std::string& address) {
    static std::atomic<bool> running{false};
    if (gStop.load() || running.exchange(true)) return;
    const storage::User who = storage::currentUser();
    std::lock_guard<std::mutex> lk(gThreads);
    // The last one has finished (running was false), so this join is quick.
    if (gBackfill.joinable()) gBackfill.join();
    gBackfill = std::thread([address, who]() {
        romm::Client c;
        std::string err;
        if (!who.valid() || !c.setAddress(address, &err) || !accounts::loadActiveToken(c)) {
            running = false;
            return;
        }
        std::vector<int> toAsk = gone::owedIds();
        for (const cache::OnDrive& d : cache::onDrive()) toAsk.push_back(d.romId);
        const std::set<int> goneIds = gone::thisStart(c.baseUrl(), toAsk);
        for (int id : goneIds) {
            if (gStop.load()) break;
            gone::forgetOwed(id);
            gone::removeCopy(id);
        }
        int records = 0, covers = 0, missing = 0, saves = 0;
        std::map<int, std::string> saveTagOf;   // rom id -> the tag a launch restores
        for (const cache::OnDrive& d : cache::onDrive()) {
            if (gStop.load()) break;
            if (goneIds.count(d.romId)) continue;
            const std::string note =
                storage::gamesDir() + "/" + std::to_string(d.romId) + ".json";
            romm::Game g;
            if (::access(note.c_str(), F_OK) != 0) {
                if (c.fetchGame(d.romId, &g, &err)) {
                    cache::rememberGame(g.id, gameRecordJson(g));
                    ++records;
                } else {
                    ++missing;   // gone from the server, or not this person's
                    continue;
                }
            } else if (!romm::gameFromJson(cache::gameRecord(d.romId), &g)) {
                continue;
            }
            for (const std::string& key : {g.coverPath, g.coverLargePath}) {
                if (key.empty() || !covercache::read(key).empty()) continue;
                std::vector<uint8_t> bytes = c.fetchBytes(key);
                if (bytes.empty()) continue;
                covercache::write(key, bytes);
                ++covers;
            }
            if (const catalog::Coverage cov = catalog::coverageFor(g); cov.core)
                if (const char* tag = catalog::saveTag(cov.core)) saveTagOf[g.id] = tag;
        }
        // THE NEWEST SAVES, one call for every game (savemirror.h): the rows
        // for each game, and the bytes of the newest row of each kind (by
        // extension: a card per region, a battery, a zip) under the tag a
        // launch restores from. Bytes already kept at that version are not
        // fetched again.
        std::vector<int> ids;
        for (const auto& kv : saveTagOf) ids.push_back(kv.first);
        std::vector<romm::Asset> rows;
        const uint64_t asked = savemirror::mark();
        std::vector<int> skipped;
        if (!ids.empty() && !gStop.load() && c.fetchSavesFor(ids, &rows, &err, &skipped)) {
            std::map<int, std::vector<romm::Asset>> byGame;
            for (int id : ids) byGame[id];   // a game with none is listed as none
            for (int id : skipped) byGame.erase(id);   // and one not asked, not at all
            for (romm::Asset& a : rows) byGame[a.romId].push_back(std::move(a));
            for (const auto& [romId, list] : byGame) {
                if (gStop.load()) break;
                if (savemirror::forgottenSince(romId, asked)) continue;   // an upload landed
                savemirror::putList(who, romId, list);
                std::map<std::string, const romm::Asset*> newest;   // extension -> row
                for (const romm::Asset& a : list) {
                    if (a.emulator != saveTagOf[romId]) continue;
                    const size_t dot = a.fileName.rfind('.');
                    const std::string ext = dot == std::string::npos ? "" : a.fileName.substr(dot);
                    const romm::Asset*& n = newest[ext];
                    if (!n || a.updatedAt > n->updatedAt) n = &a;
                }
                for (const auto& [ext, a] : newest) {
                    if (savemirror::hasBytes(who, romId, *a)) continue;
                    std::vector<uint8_t> bytes = c.fetchAsset("saves", a->id);
                    if (bytes.empty()) continue;
                    savemirror::putBytes(who, romId, *a, bytes);
                    ++saves;
                }
            }
        } else if (!ids.empty() && !gStop.load()) {
            std::fprintf(stderr, "[offline] saves not fetched: %s\n", err.c_str());
        }
        std::fprintf(stderr, "[offline] ready for offline: %d record(s), %d cover(s) and %d "
                             "save(s) fetched%s\n", records, covers, saves,
                     missing ? (", " + std::to_string(missing) +
                                " game(s) the server would not describe").c_str() : "");
        running = false;
    });
}

// WHETHER THIS MACHINE HAS A ROUTE OUT AT ALL: a default route in the kernel's
// table. Read only to notice the network coming back (startReconnect).
static bool haveDefaultRoute() {
    FILE* f = std::fopen("/proc/net/route", "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    while (!found && std::fgets(line, sizeof line, f)) {
        char iface[64];
        unsigned long dest = 1;
        if (std::sscanf(line, "%63s %lx", iface, &dest) == 2 && dest == 0) found = true;
    }
    std::fclose(f);
    return found;
}

// THE SERVER, TRIED IN THE BACKGROUND WHILE IT IS AWAY (#88). Never on the
// frame thread. Quick tries first (every ten seconds for two minutes: a
// router coming back), then about once a minute (a server that is off, a
// hotel). A try that answers clears serverAway() by itself (noteTransport),
// and the frame loop does the rest. Its own client, so the live one is
// never touched from here. AND ONCE A MINUTE WHILE IT IS THERE, the same
// heartbeat, so a server that goes while the console sits on Home is
// noticed (the Offline chip, launches that stop asking it) without waiting
// for somebody to need it.
void startReconnect(const std::string& address) {
    std::lock_guard<std::mutex> lk(gThreads);
    if (gStop.load() || gReconnect.joinable()) return;
    gReconnect = std::thread([address]() {
        long long lastTry = 0;
        bool hadRoute = haveDefaultRoute();
        long long lastBeat = static_cast<long long>(std::time(nullptr));
        long long lastFill = lastBeat;
        while (!gStop.load()) {
            if (!romm::serverAway()) {
                const long long now = static_cast<long long>(std::time(nullptr));
                // What offline needs, again every quarter of an hour: a save
                // made on another device since is the one a trip starts from.
                if (now - lastFill >= 900) {
                    lastFill = now;
                    backfillDrive(address);
                }
                if (now - lastBeat >= 60) {
                    lastBeat = now;
                    romm::Client c;
                    std::string err;
                    c.setAddress(address, &err);
                }
            } else {
                const long long now = static_cast<long long>(std::time(nullptr));
                // THE NETWORK CAME BACK (a cable, Wi-Fi switched on): try at
                // once rather than at the next minute's try.
                const bool route = haveDefaultRoute();
                if (route && !hadRoute) lastTry = 0;
                hadRoute = route;
                const long long awayFor = now - romm::serverAwaySince();
                const long long every = awayFor < 120 ? 10 : 60;
                if (now - lastTry >= every) {
                    lastTry = now;
                    romm::Client c;
                    std::string err;
                    c.setAddress(address, &err);
                }
            }
            SDL_Delay(250);
        }
    });
}

// The reconnect thread first, since it starts backfills; once it is joined
// nothing else does, so the backfill taken after it is the last.
void stopOfflineSync() {
    gStop = true;
    std::thread r, b;
    {
        std::lock_guard<std::mutex> lk(gThreads);
        r = std::move(gReconnect);
    }
    if (r.joinable()) r.join();
    {
        std::lock_guard<std::mutex> lk(gThreads);
        b = std::move(gBackfill);
    }
    if (b.joinable()) b.join();
}

// ---- Uploads owed, and the server going and coming back (#88, #398) ----
// Members of the App (app.h), moved out of main.cpp in batch (g).

// The other half of the sentence Save started. Cabinet's own words,
// because they are better than anything invented here: it either
// reached the server or it is waiting for signal.
// WHAT IS OWED IS TRIED AGAIN EVERY FIVE MINUTES while any of it is:
// the server coming back says nothing, so asking is the only way to
// find out. A starting value; one line.
void App::retryOwedUploads(float dt) {
    owedClock = uploader.owing() ? owedClock + dt : 0.0f;
    if (owedClock >= 300.0f) {
        owedClock = 0.0f;
        uploader.resendOwed();
    }
}

// OFFLINE AND BACK (#88). One line going, one coming back. Back, what
// is owed goes at once rather than at the next five-minute try, and a
// console that started offline loads its library properly, on Home
// only and never during a game or a launch, so nothing is replaced
// under somebody's feet; the shelves come in as an account switch's do.
void App::followServer(float dt) {
    // WHAT IS IN USE, for the removal of games RomM no longer has, which runs
    // on the backfill's thread (gonegames.h): the game on the screen, in a
    // core or a program of its own, and the one whose launch is under way.
    gone::setInUse(playing || standaloneRun.active() ? session.romId : 0,
                   launchJob.busy() || launchJob.waiting() ? launchJob.romId : 0);
    if (rommAddress) {
        const bool away = romm::serverAway();
        if (away != shownAway) {
            shownAway = away;
            if (away) {
                std::fprintf(stderr, "[offline] can't reach the server; trying in the "
                                     "background\n");
            } else {
                std::fprintf(stderr, "[offline] the server answered after %llds\n",
                             static_cast<long long>(std::time(nullptr)) -
                                 romm::serverAwaySince());
                images.forgetFailed();
                if (!libFromDrive) {
                    owedClock = 0.0f;
                    uploader.resendOwed();
                }
            }
        }
        onlineAgainClock += dt;
        // THE SERVER WENT WHILE THE CONSOLE WAS ON: after a minute, so a
        // blip changes nothing, Home and the Library become the games on
        // the drive, as at an offline start (MMagTech, 2026-10-05: offline
        // looks the same however you got there). On Home only, never under
        // a game, a grid or a game's page; the same arrival as coming back.
        if (away && !libFromDrive && !playing && here() == Screen::Home && !accountsOpen &&
            !launchJob.busy() &&
            static_cast<long long>(std::time(nullptr)) - romm::serverAwaySince() >= 60) {
            lib = offlineLibrary();
            libFromDrive = true;
            libraryScreen.build(platformTiles, collectionTiles);
            homeFromTheStart();
            refreshKeeps();
            tabSince = 0.0f;
            std::fprintf(stderr, "[offline] the server has been away a minute; Home is the "
                                 "games on the drive\n");
        }
        // THE LIBRARY IS FETCHED OFF THE FRAME THREAD, on a client of its
        // own, and swapped in here once it is in hand: fetched in place,
        // the picture froze for the length of the fetch and the fade below
        // was spent during the freeze, so it read as a flash (MMagTech on
        // the TV, 2026-10-05).
        if (!away && libFromDrive && !onlineLoad.running.load() && !onlineLoad.ready &&
            onlineAgainClock >= 5.0f) {
            onlineAgainClock = 0.0f;
            onlineLoad.running = true;
            onlineLoad.forAccount = accounts::activeId();
            if (onlineLoad.th.joinable()) onlineLoad.th.join();
            const std::string address = rommAddress;
            onlineLoad.th = std::thread([this, address]() {
                romm::Client c;
                std::string e;
                Library fresh;
                bool live = false;
                if (c.setAddress(address, &e) && accounts::loadActiveToken(c)) {
                    fresh = loadLibrary(c);
                    // THE LIVE CLIENT IS POINTED AT THE SERVER HERE TOO, NOT
                    // ON THE FRAME THREAD (#481, ROMM-37): its setAddress
                    // asks /api/heartbeat, up to 5 s per scheme tried (30 if
                    // the server connects and does not answer), and it froze
                    // Home when the server went again in between. The
                    // client's address is set under its own lock.
                    if (!fresh.platformTiles.empty()) live = liveClient.setAddress(c.baseUrl(), &e);
                }
                onlineLoad.lib = std::move(fresh);
                onlineLoad.liveOk = live;
                onlineLoad.why = e;
                onlineLoad.ready = true;
                onlineLoad.running = false;
            });
        }
        if (onlineLoad.ready.load() && !playing && here() == Screen::Home && !accountsOpen &&
            !launchJob.busy() && uploader.pending() == 0) {
            onlineLoad.ready = false;
            if (onlineLoad.th.joinable()) onlineLoad.th.join();
            if (onlineLoad.forAccount != accounts::activeId()) {
                std::fprintf(stderr, "[offline] library not loaded; trying again\n");
            } else if (onlineLoad.lib.platformTiles.empty()) {
                // A SERVER BACK WHOSE LIBRARY DOES NOT LOAD (its sign-in
                // refused, or nothing here it can show) is asked again a
                // minute later, not every five seconds for the life of the
                // process, and said once (#485, FIRSTRUN-07).
                if (!onlineLoad.failing)
                    std::fprintf(stderr, "[offline] library not loaded; trying again "
                                         "every minute\n");
                onlineLoad.failing = true;
                onlineAgainClock = -55.0f;
            } else if (!onlineLoad.liveOk) {
                std::fprintf(stderr, "[offline] the server went again: %s\n",
                             onlineLoad.why.c_str());
            } else {
                onlineLoad.failing = false;
                adoptUser(liveClient);
                lib = std::move(onlineLoad.lib);
                libraryScreen.build(platformTiles, collectionTiles);
                startCoverFill();
                homeFromTheStart();
                refreshKeeps();
                libFromDrive = false;
                images.forgetFailed();
                // HOME ARRIVES AGAIN, as it does from the bar: the
                // shelves fade in and rise (#88's agreed sequence, "the
                // shelves fade in"). The change is the signal.
                tabSince = 0.0f;
                std::fprintf(stderr, "[offline] online again: %zu game(s) on Home\n",
                             cards.size());
                owedClock = 0.0f;
                uploader.resendOwed();
                backfillDrive(rommAddress);
            }
            onlineLoad.lib = Library{};
        }
    }
}
