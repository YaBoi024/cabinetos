#include "accountremove.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "cache.h"
#include "fileio.h"
#include "playtime.h"
#include "romm.h"
#include "storage.h"
#include "uploader.h"

// EVERYTHING `who` STILL OWES RomM, sent now with their own login, before their
// folder is deleted (#194). Synchronous: called off the frame thread. Stops at
// the first failure, because a server that refused one or did not answer will
// do the same for the rest, and each try can take curl's whole timeout.
// States are not rotated to three here (the uploader's rotate): a person being
// removed may keep a fourth on RomM, which their next save elsewhere tidies.
// Returns how much is still owed afterwards: refused, unanswered, a marker
// from before 2026-09-26 that cannot be sent at all, or a game's owed
// snapshot (cache::OwedPush), which this does not send: it sends files.
static int sendEverythingOwed(const romm::Client& c, const storage::User& who,
                              const std::atomic<bool>* stop) {
    for (const cache::Owed& o : cache::owed(who)) {
        if (stop && stop->load()) break;
        const std::vector<uint8_t> data = cab::readBytes(o.localPath);
        if (data.empty()) {
            // Its file is gone, so nothing is left to send or to lose.
            cache::clearPending(who, o.romId, o.fileName);
            continue;
        }
        const std::vector<uint8_t> shot =
            o.shotPath.empty() ? std::vector<uint8_t>() : cab::readBytes(o.shotPath);
        std::string err;
        const bool ok = uploadOne(c, o.romId, o.isState, o.isScreenshot, o.emulator,
                                  o.fileName, data, o.shotName, shot, &err);
        std::fprintf(stderr, "[remove] %s %s for %s%s%s\n", o.fileName.c_str(),
                     ok ? "sent" : "not sent", who.name.c_str(), ok ? "" : ": ",
                     ok ? "" : err.c_str());
        if (!ok) break;
        cache::clearPending(who, o.romId, o.fileName);
    }
    if (!playtime::owed(who).empty() && !(stop && stop->load())) {
        std::string err;
        if (!sendPlaySessionsAs(c, who, &err))
            std::fprintf(stderr, "[remove] play time not sent for %s: %s\n", who.name.c_str(),
                         err.c_str());
    }
    return cache::pendingCount(who) + static_cast<int>(playtime::owed(who).size());
}

// THE SENDING HALF OF REMOVE (#194): what `who` owes, with their own login
// (they are never the person signed in; accounts::remove refuses that).
// Returns how much is still owed afterwards. Blocks: off the frame thread.
int sendOwedBeforeRemoving(const accounts::Account& who, const std::string& addr,
                           const std::atomic<bool>* stop) {
    storage::User u;
    u.id = who.id;
    u.name = who.name;
    u.avatar = who.avatar;
    int left = cache::pendingCount(u) + static_cast<int>(playtime::owed(u).size());
    std::fprintf(stderr, "[remove] %s owes RomM %d thing(s)\n", who.name.c_str(), left);
    if (left == 0) return 0;
    romm::Client c;   // ITS OWN, with THEIR login
    std::string err;
    if (addr.empty() || !c.setAddress(addr, &err))
        std::fprintf(stderr, "[remove] no server to send %s's saves to\n", who.name.c_str());
    else if (!c.loadToken(accounts::tokenPath(who.id)))
        std::fprintf(stderr, "[remove] %s's login would not load\n", who.name.c_str());
    else
        left = sendEverythingOwed(c, u, stop);
    std::fprintf(stderr, "[remove] %s: %d still owed\n", who.name.c_str(), left);
    return left;
}

// THE DELETING HALF OF REMOVE (#194), once what is owed has been sent or the
// person removing has said Remove anyway. THE ORDER IS THE SAFETY: their
// downloads are released (a game nobody else downloaded drops to the cache:
// still playable, cleared when room is needed), then their folder goes, then
// the login LAST, so a power cut at any point leaves them listed and Remove
// can simply be run again. Nothing on RomM is touched.
bool removePersonNow(const accounts::Account& who, std::string* err) {
    if (!accounts::canRemove(who.id, err)) return false;
    storage::User u;
    u.id = who.id;
    u.name = who.name;
    u.avatar = who.avatar;
    int released = 0;
    for (int romId : cache::keptRoms(u))
        if (cache::unkeep(u, romId, /*keepTheBytes=*/true)) ++released;
    const std::string dir = storage::userDir(u);
    const bool had = storage::exists(dir);
    const int64_t bytes = had ? storage::treeBytes(dir) : 0;
    if (had && !storage::removeEntry(dir)) {
        if (err) *err = "could not delete all of " + dir;
        return false;
    }
    if (!accounts::remove(who.id, err)) {
        if (err) *err = "folder deleted but the login stayed: " + *err;
        return false;
    }
    std::fprintf(stderr,
                 "[accounts] removed %d - %s: %d download(s) released, %.1f MB of their "
                 "folder deleted%s, now %zu accounts\n",
                 who.id, who.name.c_str(), released, bytes / 1048576.0,
                 had ? "" : " (there was none)", accounts::all().size());
    return true;
}
