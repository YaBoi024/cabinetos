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
#include "savepick.h"
#include "storage.h"
#include "tokenrevoke.h"

// STOPPED AT EXIT (#406). Both threads were detached and endless or long,
// so one still running at exit ran on into the program's teardown. Each now
// looks at gStop between steps, and stopOfflineSync joins them. The thread
// objects are under gThreads, because the reconnect thread starts backfills.
static std::atomic<bool> gStop{false};
static std::mutex gThreads;
static std::thread gBackfill;    // under gThreads
static std::thread gReconnect;   // under gThreads

// The files a game's save channels can be on, asked of RomM and kept in the
// game's record (library.h). None when RomM lists none or did not answer.
static romm::ChannelFiles askChannelFiles(romm::Client& c, int romId) {
    romm::ChannelFiles files;
    std::string err;
    if (!c.fetchChannelFiles(romId, &files, &err)) return {};
    rememberChannelFiles(romId, files);
    return files;
}

// RomM'S SAVE CHANNELS FOR THE GAMES ON THE DRIVE (#346), on every file each
// game's record remembers its channels can be on (romm::ChannelFiles), a
// hundred files to a call; a game whose record has none yet is asked about
// once. Each channel goes to the game RomM names it under. A game RomM lists
// no channel for is in the answer with none; a game left out is one RomM
// could not be asked about. RomM refuses a whole call when one file in it is
// gone or hidden, so then each game is asked alone, and one whose files no
// longer answer is asked for its files again. Asked only of a RomM that has
// them (backfillDrive).
static std::map<int, std::vector<romm::Channel>> channelsOfGames(romm::Client& c,
                                                                 const std::vector<int>& romIds) {
    std::map<int, std::vector<romm::Channel>> out;
    std::map<int, romm::ChannelFiles> filesOf;
    std::map<int, int> romOfFile;
    std::vector<int> files;
    for (int romId : romIds) {
        if (gStop.load()) return out;
        romm::ChannelFiles known = rememberedChannelFiles(romId);
        if (known.files.empty()) known = askChannelFiles(c, romId);
        if (known.files.empty()) continue;
        for (const int file : known.files) {
            romOfFile[file] = romId;
            files.push_back(file);
        }
        filesOf[romId] = std::move(known);
    }
    // Each channel to its game: the one RomM names it under, else the one
    // whose file it is on.
    auto sort = [&](std::vector<romm::Channel>& all) {
        for (romm::Channel& ch : all) {
            auto game = out.find(ch.romId);
            if (game == out.end()) {
                const auto f = romOfFile.find(ch.romFileId);
                if (f != romOfFile.end()) game = out.find(f->second);
            }
            if (game != out.end()) game->second.push_back(std::move(ch));
        }
    };
    std::vector<romm::Channel> all;
    std::string err;
    bool gone = false;
    if (files.empty()) return out;
    if (c.fetchChannels(files, &all, &err, &gone)) {
        for (const auto& [romId, known] : filesOf) out[romId];
        sort(all);
        return out;
    }
    if (!gone) {
        std::fprintf(stderr, "[offline] save channels not fetched: %s\n", err.c_str());
        return out;
    }
    for (const auto& [romId, known] : filesOf) {
        if (gStop.load()) break;
        std::vector<romm::Channel> one;
        if (!c.fetchChannels(known.files, &one, &err, &gone)) {
            if (!gone) continue;
            const romm::ChannelFiles now = askChannelFiles(c, romId);
            if (now.files.empty() || now.files == known.files ||
                !c.fetchChannels(now.files, &one, &err))
                continue;
        }
        out[romId] = std::move(one);
    }
    return out;
}

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
// Their saves go last, once the upload queue has tried what the start owed.
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
        // NOTHING OF A GAME'S SAVE FROM A RomM TOO OLD FOR SAVE CHANNELS (#346,
        // step 9): it is neither read nor written, so the copy stays as last
        // seen, which a start goes by, as offline.
        if (!romm::heardServerChannels()) {
            if (!saveTagOf.empty())
                std::fprintf(stderr, "[offline] saves not fetched: RomM has no save channels\n");
            saveTagOf.clear();
        }
        // THE SAVE A START WOULD CONTINUE FROM, for every game (savemirror.h):
        // RomM's saves from before channels in one call, its save channels
        // in a call per hundred games, and the bytes of the save the
        // launch's own rule picks from them (savepick.h): a channel's
        // snapshot, or of the old rows the newest under the tag a launch
        // restores from, of any kind and in each region. Picked as if nothing
        // were owed, since what is owed now will have gone up by the time
        // these are read. Bytes already kept are not fetched again.
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
            std::vector<int> listed;
            for (const auto& kv : byGame) listed.push_back(kv.first);
            const std::map<int, std::vector<romm::Channel>> channelsOf =
                channelsOfGames(c, listed);
            for (const auto& [romId, list] : byGame) {
                if (gStop.load()) break;
                if (savemirror::forgottenSince(romId, asked)) continue;   // an upload landed
                savemirror::putList(who, romId, list);
                savepick::Facts facts;
                facts.tag = saveTagOf[romId];
                if (const auto ch = channelsOf.find(romId); ch != channelsOf.end()) {
                    savemirror::putChannels(who, romId, ch->second);
                    facts.channels = ch->second;
                    savepick::dateSaves(&facts.channels, list);
                }
                if (const romm::Asset* newest = savepick::newestOldRow(list, facts.tag))
                    facts.oldRowAt = newest->updatedAt;
                facts.haveHeld = savemirror::getHeld(who, romId, &facts.held);
                // The save a start with the server away goes by: it cannot
                // ask RomM for another emulator's in this console's form
                // (saves.cpp, fetchSaves), so never that one.
                const savepick::Pick pick = savepick::pick(facts, savepick::Others::Unusable);
                if (pick.from == savepick::Pick::From::Channel) {
                    const romm::SnapshotFile& save = pick.channel.current.save;
                    if (savemirror::hasSnapshotSave(who, romId, save)) continue;
                    std::vector<uint8_t> bytes = c.fetchAsset("saves", save.id);
                    if (bytes.empty()) continue;
                    savemirror::putSnapshotSave(who, romId, save, bytes);
                    ++saves;
                } else if (pick.from == savepick::Pick::From::OldRows) {
                    for (const romm::Asset* a : savepick::oldRowsAStartMayTake(list, facts.tag)) {
                        if (savemirror::hasBytes(who, romId, *a)) continue;
                        std::vector<uint8_t> bytes = c.fetchAsset("saves", a->id);
                        if (bytes.empty()) continue;
                        savemirror::putBytes(who, romId, *a, bytes);
                        ++saves;
                    }
                }
            }
        } else if (!ids.empty() && !gStop.load()) {
            std::fprintf(stderr, "[offline] saves not fetched: %s\n", err.c_str());
        }
        std::fprintf(stderr, "[offline] ready for offline: %d record(s), %d cover(s) and %d "
                             "save(s) fetched%s\n", records, covers, saves,
                     missing ? (", " + std::to_string(missing) +
                                " game(s) the server would not describe").c_str() : "");
        // LAST, A GONE GAME'S SAVES, once the upload queue has tried what the
        // start owed (gonegames.h): a minute at most, then the next backfill.
        for (int i = 0; i < 240 && !goneIds.empty() && gone::uploadsPending() && !gStop.load();
             ++i)
            SDL_Delay(250);
        if (!gone::uploadsPending())
            for (int id : goneIds) {
                if (gStop.load()) break;
                gone::removeSaves(id, who);
            }
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
// for somebody to need it. THE TOKENS THIS CONSOLE IS DONE WITH go from here
// too (tokenrevoke.h), at the start and every five seconds after: one that is
// still used waits, and one RomM did not answer for is asked again in a
// minute.
void startReconnect(const std::string& address) {
    std::lock_guard<std::mutex> lk(gThreads);
    if (gStop.load() || gReconnect.joinable()) return;
    gReconnect = std::thread([address]() {
        long long lastTry = 0;
        bool hadRoute = haveDefaultRoute();
        long long lastBeat = static_cast<long long>(std::time(nullptr));
        long long lastFill = lastBeat;
        long long lastRevoke = 0, revokeEvery = 5;
        while (!gStop.load()) {
            if (!romm::serverAway()) {
                const long long now = static_cast<long long>(std::time(nullptr));
                if (now - lastRevoke >= revokeEvery) {
                    lastRevoke = now;
                    revokeEvery = tokenrevoke::sendOwed(address, &gStop) ? 5 : 60;
                }
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
        // RomM GAINED SAVE CHANNELS (#346, step 9; decision 6 of 2026-11-15):
        // updated to 5.5 or newer while the console runs, or found so when
        // it came back. What waited on this console because the server had
        // none (savepush.h) goes now, and what offline needs is fetched
        // again, as when the server comes back; heard within a minute, from
        // the reconnect thread's heartbeat. One without them is said once
        // when first heard, and once when it loses them (put back to an
        // older version); what is owed waits. A server first heard after an
        // offline start has gained nothing: coming back sends what is owed.
        if (!away && !romm::heardServerVersion().empty()) {
            const int channels = romm::heardServerChannels() ? 1 : 0;
            if (channels != shownChannels) {
                const bool gained = channels == 1 && shownChannels == 0;
                if (channels == 0 || gained)
                    std::fprintf(stderr, gained ? "[save] RomM has save channels now; what waited "
                                                  "on this console goes\n"
                                                : "[save] RomM has no save channels; saves and "
                                                  "states stay on this console until it has\n");
                shownChannels = channels;
                if (gained && !libFromDrive) {
                    owedClock = 0.0f;
                    uploader.resendOwed();
                    backfillDrive(rommAddress);
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
                libraryLoaded();
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
