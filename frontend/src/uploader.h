// Uploads, off the thread that draws: the queue that sends a game's snapshot
// (its save and its states, savepush.h), screenshots, play time and
// favourites to RomM. Moved out of main.cpp in the restructure (#398).

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "romm.h"
#include "storage.h"

// RomM answered and refused it for good: an HTTP 4xx, read off the client's
// error (uploader.cpp).
bool refusedForGood(const std::string& err);
bool sendPlaySessionsAs(const romm::Client& c, const storage::User& who, std::string* err);

// Uploads, off the thread that draws.
//
// A game must never stop because a file is going to a server. On a LAN with a
// 60 KB Game Boy state the old blocking version was imperceptible; on a slow
// link, or with a memory card, it stutters, and a server that does not answer
// leaves curl waiting thirty seconds with the console looking dead. That is the
// same fault the download had and it gets the same treatment.
//
// THE SPLIT THAT MATTERS: the frame thread reads the core and writes the local
// copy, and only the network goes to the worker. Reading the core has to happen
// on the frame thread because a core is not thread-safe, and writing the local
// copy has to happen before the upload is even queued, or "local first" stops
// being true the moment the process dies between the two.
//
// Sixty kilobytes to a local disk is well under a millisecond. The network is
// the part that can take thirty seconds, and the network is the part that moves.
class Uploader {
public:
    struct Job {
        int romId = 0;
        std::string emulator;
        std::string fileName;
        std::vector<uint8_t> data;
        // The file on disk these bytes came from: what a retry reads, since
        // the bytes in memory do not outlive the console being switched off.
        std::string localPath;
        // Sent again from a marker, not asked for by a person just now.
        bool resend = false;
        // A screenshot (#79): `data` is the PNG, and it goes to the gallery.
        bool isScreenshot = false;
        // Time played (#128): every play session this person owes RomM, read
        // off the disk when it runs. No bytes, no marker; playtime.json is
        // its own record of what is owed.
        bool isPlay = false;
        // Favourites (#267): every one owed, read off favorites-owed.json.
        bool isFavorite = false;
        // A game's snapshot (#346, savepush.h): its owed push, read off its
        // marker when it runs (cache::OwedPush). No bytes here; `fileName` is
        // the name an owed push goes by (cache::kOwedPushName).
        bool isPush = false;
        // WHOSE IT IS, set on the frame thread when it is queued (push, and
        // the resends), so the worker never reads storage::currentUser(),
        // which a switch of person assigns (#483, FILES-09).
        storage::User who{};
    };

    // An upload that has not reached the server is the ONE irreplaceable thing
    // on this machine, and until this existed nothing recorded that one was
    // outstanding — so a crash between writing a file and sending it left no
    // trace that anything was owed. The marker makes it a fact on disk, and its
    // bytes count against the save floor when somebody asks to keep a game.
    //
    // The marker belongs to a PERSON now, because the thing it is tracking is a
    // save. It lands in `users/<id> - <name>/pending/`.
    void start(romm::Client* client);

    void shutdown();

    void push(Job job);

    // A GAME'S OWED SNAPSHOT (#346), whose marker is already on the disk
    // (savepush::leave): queued once however often it is asked for, and
    // asked of RomM once more even after a refusal this run, as a new save
    // of a file is (push).
    void pushSnapshot(int romId, const storage::User& who);

    // SEND AGAIN EVERYTHING STILL OWED (2026-09-26). Until this, an upload
    // that failed was never tried again: a state saved while the server was
    // away stayed on this console for good, and never reached the Apple TV.
    // Called at start, after an upload a person made that worked, and every
    // few minutes while something is owed (the frame loop's `owing()`). The
    // bytes are read off the disk on the worker; anything already queued is
    // not queued twice, and nothing RomM refused this run (refused_). On the
    // frame thread, for the person signed in. A SAVE'S OR A STATE'S MARKER
    // FROM BEFORE SAVE CHANNELS, one per file, is not sent as it is: RomM
    // takes a save or a state from this console only inside a snapshot. Each
    // becomes its game's owed push first (savepush::adoptOldSaves, then
    // adoptOldStates), here on the frame thread, where saves and states are
    // marked.
    void resendOwed();

    // Everything owed in favorites-owed.json, in one job, as sendPlay.
    void sendFavorites();

    // Everything owed in playtime.json, in one job. Queued once however often
    // it is asked for; a session closed while it is in flight goes next time.
    void sendPlay();

    int pending() const { return pending_.load(); }
    // Whether RomM refused this file for good this run (refused_): the
    // uploader has tried it since the start. A game RomM no longer has
    // deletes this person's owed file only then (gonegames.h).
    bool refusedThisRun(const storage::User& who, int romId, const std::string& fileName) const;
    // Something failed and is still owed: the frame loop tries again on a timer.
    bool owing() const { return owing_.load(); }

private:
    static std::string key(const Job& j) { return std::to_string(j.romId) + "/" + j.fileName; }

    void enqueue(Job job);

    // resendOwed, sendFavorites and sendPlay for a named person: the worker
    // resends for the person whose upload just worked.
    void resendOwedFor(const storage::User& who);
    void sendFavoritesFor(const storage::User& who);
    void sendPlayFor(const storage::User& who);

    void run();

    void finished(const Job& job, bool ok, bool leftSomething);


    romm::Client* client_ = nullptr;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    std::set<std::string> queued_;
    // WHAT RomM REFUSED FOR GOOD THIS RUN (an HTTP 4xx), by person and key:
    // sending it again cannot help until something changes, so no resend
    // queues it. Its marker stays, so the next start tries it once more,
    // and so does the person's own next save of that file (push), as
    // favourites RomM will not make a collection for wait for the next
    // heart or start (favorites.cpp). Under mutex_.
    std::set<std::string> refused_;
    // WHAT FAILED AND IS STILL OWED, by person and key: `owing_` is true
    // while any of it is, so a resend that works (parked favourites, time
    // played) takes only itself off and the five-minute retry goes on for
    // the rest. Under mutex_.
    std::set<std::string> failed_;
    static std::string whoKey(const Job& j) { return storage::userDir(j.who) + "|" + key(j); }
    std::atomic<int> pending_{0};
    std::atomic<bool> owing_{false};
    bool stopping_ = false;
};
