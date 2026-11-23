#include "uploader.h"

#include <algorithm>
#include <cstdio>

#include "cache.h"
#include "favorites.h"
#include "fileio.h"
#include "gonegames.h"
#include "playtime.h"
#include "savepush.h"

// RomM ANSWERED AND REFUSED IT FOR GOOD: an HTTP 4xx (a permission unticked
// at pairing, a role without write access, a file it will not take), read
// off the client's error, "HTTP <status>: ..." (Client::postMultipart). Not
// 408 or 429, which ask to be asked again. A server away or a 5xx is not a
// refusal and is tried again as before.
bool refusedForGood(const std::string& err) {
    if (err.size() < 8 || err.compare(0, 6, "HTTP 4") != 0) return false;
    return err.compare(5, 3, "408") != 0 && err.compare(5, 3, "429") != 0;
}

// Every play session `who` owes, a hundred at a time, RomM's most. Whatever
// RomM has now (new, or a copy of one it already had) or has refused for good
// stops being owed.
bool sendPlaySessionsAs(const romm::Client& c, const storage::User& who,
                        std::string* err) {
    std::vector<playtime::Session> list = playtime::owed(who);
    constexpr size_t kBatch = 100;
    for (size_t at = 0; at < list.size(); at += kBatch) {
        const size_t end = std::min(list.size(), at + kBatch);
        std::vector<romm::Client::PlaySession> out;
        for (size_t i = at; i < end; ++i)
            out.push_back({list[i].romId, playtime::iso(list[i].startMs),
                           playtime::iso(list[i].endMs), list[i].playedMs});
        std::vector<int> outcome;
        if (!c.postPlaySessions(out, &outcome, err)) return false;
        std::vector<playtime::Session> done;
        for (size_t i = at; i < end; ++i)
            if (outcome[i - at] != 0) done.push_back(list[i]);
        playtime::sent(who, done);
        std::fprintf(stderr, "[playtime] sent %zu session(s), %zu taken\n", end - at,
                     done.size());
        if (done.size() < end - at) {
            if (err) *err = "RomM did not say what became of some";
            return false;
        }
    }
    return true;
}

void Uploader::start(romm::Client* client) {
    client_ = client;
    worker_ = std::thread([this] { run(); });
}

void Uploader::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Uploader::push(Job job) {
    // Recorded BEFORE it is queued, so the window in which the console owes
    // the server something and does not know it is zero.
    cache::Owed o;
    o.romId = job.romId;
    o.isScreenshot = job.isScreenshot;
    o.emulator = job.emulator;
    o.fileName = job.fileName;
    o.localPath = job.localPath;
    if (!job.who.valid()) job.who = storage::currentUser();
    cache::markPending(job.who, o, static_cast<int64_t>(job.data.size()));
    {
        // A new save of a file RomM refused is asked about once more.
        std::lock_guard<std::mutex> lock(mutex_);
        refused_.erase(whoKey(job));
    }
    enqueue(std::move(job));
}

void Uploader::pushSnapshot(int romId, const storage::User& who) {
    Job j;
    j.isPush = true;
    j.romId = romId;
    j.fileName = cache::kOwedPushName;
    j.who = who;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        refused_.erase(whoKey(j));
    }
    enqueue(std::move(j));
}

bool Uploader::refusedThisRun(const storage::User& who, int romId,
                              const std::string& fileName) const {
    Job j;
    j.romId = romId;
    j.fileName = fileName;
    j.who = who;
    std::lock_guard<std::mutex> lock(mutex_);
    return refused_.count(whoKey(j)) > 0;
}

void Uploader::resendOwed() {
    savepush::adoptOldSaves(storage::currentUser());
    savepush::adoptOldStates(storage::currentUser());
    resendOwedFor(storage::currentUser());
}
void Uploader::sendFavorites() { sendFavoritesFor(storage::currentUser()); }
void Uploader::sendPlay() { sendPlayFor(storage::currentUser()); }

void Uploader::resendOwedFor(const storage::User& who) {
    const std::vector<cache::Owed> list = cache::owed(who);
    for (const cache::Owed& o : list) {
        // A save's or a state's marker from before save channels (uploader.h).
        if (!o.isScreenshot) continue;
        Job j;
        j.romId = o.romId;
        j.isScreenshot = true;
        j.emulator = o.emulator;
        j.fileName = o.fileName;
        j.localPath = o.localPath;
        j.resend = true;
        j.who = who;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (refused_.count(whoKey(j))) continue;
        }
        enqueue(std::move(j));
    }
    for (const cache::OwedPush& p : cache::owedPushes(who)) {
        Job j;
        j.isPush = true;
        j.romId = p.romId;
        j.fileName = cache::kOwedPushName;
        j.resend = true;
        j.who = who;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (refused_.count(whoKey(j))) continue;
        }
        enqueue(std::move(j));
    }
    if (!playtime::owed(who).empty()) sendPlayFor(who);
    if (!favOwed(who).empty()) sendFavoritesFor(who);
}

void Uploader::sendFavoritesFor(const storage::User& who) {
    Job j;
    j.isFavorite = true;
    j.fileName = "favourites";
    j.resend = true;
    j.who = who;
    enqueue(std::move(j));
}

void Uploader::sendPlayFor(const storage::User& who) {
    Job j;
    j.isPlay = true;
    j.fileName = "play sessions";
    j.resend = true;
    j.who = who;
    enqueue(std::move(j));
}

void Uploader::enqueue(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string k = key(job);
        if (queued_.count(k)) {
            // Already on its way. A person's own save replaces the bytes
            // of a resend of the same file, which is only ever older.
            if (job.resend) return;
            for (Job& q : queue_)
                if (key(q) == k) { q = std::move(job); return; }
            // In flight right now: queue it behind, it is newer.
        }
        queued_.insert(k);
        queue_.push_back(std::move(job));
        ++pending_;
    }
    wake_.notify_one();
}

void Uploader::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            // Drain on the way out rather than dropping: a queued upload is
            // a save someone has already made, and quitting is not a reason
            // to discard it.
            if (queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        if (job.isFavorite) {
            std::string err;
            const bool ok = sendFavoritesAs(*client_, job.who, &err);
            if (!ok) std::fprintf(stderr, "[favorite] not sent, kept locally: %s\n", err.c_str());
            finished(job, ok, /*leftSomething=*/!ok);
            continue;
        }
        if (job.isPlay) {
            std::string err;
            const bool ok = sendPlaySessionsAs(*client_, job.who, &err);
            if (!ok) std::fprintf(stderr, "[playtime] not sent, kept locally: %s\n", err.c_str());
            finished(job, ok, /*leftSomething=*/!ok);
            continue;
        }
        if (job.isPush) {
            // A GAME BEING PLAYED KEEPS ITS OWED SNAPSHOT UNTIL IT IS LEFT.
            // A snapshot's save can be the file a core writes, and one read
            // while the game runs can be half written. Only a resend can
            // meet a game in play: the push a person's own exit queues comes
            // after the game has closed, and is tried here at once. Asked
            // again after the read, in case the game started during it.
            std::string why;
            savepush::Sent sent = savepush::Sent::NotSent;
            if (job.resend && gone::inUse(job.romId)) {
                why = "the game is being played";
            } else {
                sent = savepush::send(savepush::serverOf(*client_), job.who, job.romId, &why,
                                      [&job] { return job.resend && gone::inUse(job.romId); });
            }
            const bool ok = savepush::settled(sent);
            if (sent == savepush::Sent::Refused) {
                std::lock_guard<std::mutex> lock(mutex_);
                refused_.insert(whoKey(job));
                std::fprintf(stderr, "[save] %sgame %d's snapshot refused by RomM, kept on this "
                                     "console; not sent again until the next start or a new save "
                                     "of it: %s\n",
                             job.resend ? "resent, " : "", job.romId, why.c_str());
            } else if (sent == savepush::Sent::NotSent) {
                std::fprintf(stderr, "[save] %sgame %d's snapshot not sent, kept on this "
                                     "console: %s\n",
                             job.resend ? "resent, " : "", job.romId, why.c_str());
            } else if (sent == savepush::Sent::Waits && !job.resend) {
                // ON A RomM TOO OLD FOR SAVE CHANNELS (savepush.h) it waits,
                // said once, at the push a leave queued: the five-minute
                // retry asks nothing of the server for it. A server never
                // heard since the console started is away, not too old.
                const bool heard = !romm::heardServerVersion().empty() && !romm::serverAway();
                std::fprintf(stderr, heard ? "[save] game %d's snapshot waits on this console "
                                             "until RomM has save channels\n"
                                           : "[save] game %d's snapshot not sent, kept on this "
                                             "console: the server is away\n",
                             job.romId);
            }
            // What waits stays owed, so the five-minute retry goes on behind
            // the notice of the server gaining channels (App::followServer).
            finished(job, ok, /*leftSomething=*/savepush::triedAgain(sent));
            continue;
        }
        // A SCREENSHOT (#79), the one file that still goes up by itself. A
        // resend reads its bytes now, off the disk.
        if (job.data.empty() && !job.localPath.empty()) job.data = cab::readBytes(job.localPath);
        bool ok = false;
        std::string err;
        if (job.data.empty()) {
            // The file it was owed from is gone (deleted by hand, or the
            // person removed). Nothing left to send, so nothing is owed.
            err = "its file is gone";
            cache::clearPending(job.who, job.romId, job.fileName);
        } else {
            ok = client_->uploadScreenshot(job.romId, job.fileName, job.data, &err);
            // Cleared only on success. A failed upload leaves the marker,
            // which is the point: the file is still on disk, it still has
            // not reached RomM, and the console still owes it.
            if (ok) cache::clearPending(job.who, job.romId, job.fileName);
        }
        const bool refused = !ok && !job.data.empty() && refusedForGood(err);
        if (refused) {
            std::lock_guard<std::mutex> lock(mutex_);
            refused_.insert(whoKey(job));
        }
        if (refused)
            std::fprintf(stderr, "[screenshot] %s%s refused by RomM, kept locally; not sent "
                                 "again until the next start or a new save of it: %s\n",
                         job.resend ? "resent, " : "", job.fileName.c_str(), err.c_str());
        else
            std::fprintf(stderr, "[screenshot] %s%s %s%s\n", job.resend ? "resent, " : "",
                         job.fileName.c_str(), ok ? "uploaded" : "upload failed, kept locally: ",
                         ok ? "" : err.c_str());
        finished(job, ok, /*leftSomething=*/!ok && !job.data.empty() && !refused);
    }
}

void Uploader::finished(const Job& job, bool ok, bool leftSomething) {
    bool resendNow = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queued_.erase(key(job));
        if (leftSomething) failed_.insert(whoKey(job));
        else failed_.erase(whoKey(job));
        // THE SERVER ANSWERED AN UPLOAD A PERSON MADE, so anything a
        // failure left behind can go now rather than at the next timer.
        // Not after a resend (favourites and time played go as resends
        // too): one that always works (favourites RomM will not make a
        // collection for, which favorites.cpp counts as done) beside one
        // that always fails queued both again, for ever. The timer and the
        // server coming back still send the rest.
        if (ok && !job.resend && !failed_.empty()) resendNow = true;
        owing_ = !failed_.empty();
    }
    // THE RESEND IS QUEUED BEFORE THIS ONE IS COUNTED OFF (#483, FILES-09):
    // in between, pending() read 0, and a switch of person could start while
    // the worker was still listing what this person owes.
    if (resendNow) resendOwedFor(job.who);
    --pending_;
}
