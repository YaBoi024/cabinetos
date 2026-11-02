// Uploads, off the thread that draws: the queue that sends saves, states,
// screenshots, play time and favourites to RomM, and the one-file send that
// removing a person uses too. Moved out of main.cpp in the restructure (#398).
//
// JOB'S FIELD ORDER IS FROZEN. The saves and states code builds a Job by
// position ({romId, emulator, fileName, data, isState}); emulator and fileName
// are both strings, so reordering them compiles and silently swaps them.

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

bool uploadOne(const romm::Client& c, int romId, bool isState, bool isScreenshot,
               const std::string& emulator, const std::string& fileName,
               const std::vector<uint8_t>& data, const std::string& shotName,
               const std::vector<uint8_t>& shot, std::string* err);
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
        bool isState = false;
        // The file on disk these bytes came from: what a retry reads, since
        // the bytes in memory do not outlive the console being switched off.
        std::string localPath;
        // A state's picture (#99): its name, its bytes, and where it is.
        std::string shotName;
        std::vector<uint8_t> shot;
        std::string shotPath;
        // Sent again from a marker, not asked for by a person just now: the
        // pause menu is not told how it went.
        bool resend = false;
        // A screenshot (#79): `data` is the PNG, and it goes to the gallery.
        bool isScreenshot = false;
        // Time played (#128): every play session this person owes RomM, read
        // off the disk when it runs. No bytes, no marker; playtime.json is
        // its own record of what is owed.
        bool isPlay = false;
        // Favourites (#267): every one owed, read off favorites-owed.json.
        bool isFavorite = false;
        // WHOSE IT IS, set on the frame thread when it is queued (push, and
        // the resends), so the worker never reads storage::currentUser(),
        // which a switch of person assigns (#483, FILES-09). Last, so the
        // positional builds above leave it empty and push fills it.
        storage::User who{};
    };

    // An upload that has not reached the server is the ONE irreplaceable thing
    // on this machine, and until this existed nothing recorded that one was
    // outstanding — so a crash between writing a state and sending it left no
    // trace that anything was owed. The marker makes it a fact on disk, and its
    // bytes count against the save floor when somebody asks to keep a game.
    //
    // The marker belongs to a PERSON now, because the thing it is tracking is a
    // save. It lands in `users/<id> - <name>/pending/`.
    void start(romm::Client* client);

    void shutdown();

    void push(Job job);

    // SEND AGAIN EVERYTHING STILL OWED (2026-09-26). Until this, an upload
    // that failed was never tried again: a state saved while the server was
    // away stayed on this console for good, and never reached the Apple TV.
    // Called at start, after an upload a person made that worked, and every
    // few minutes while something is owed (the frame loop's `owing()`). The
    // bytes are read off the disk on the worker; anything already queued is
    // not queued twice, and nothing RomM refused this run (refused_). On the
    // frame thread, for the person signed in.
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

    // THE OUTCOME OF THE MOST RECENT STATE UPLOAD, so the pause menu can say
    // what happened rather than what was attempted. 0 nothing new, 1 it reached
    // the server, 2 it did not and is still on the disk.
    //
    // Cabinet awaited its upload and reported "Saved to RomM." or "Waiting for
    // signal to upload." This console queues on a worker instead, which is the
    // right shape for a machine that must not stall its frame loop — but it
    // meant Save said nothing at all, and then "load latest" went looking on a
    // server that had not received it yet. One atomic closes that gap.
    std::atomic<int> stateOutcome{0};

private:
    static std::string key(const Job& j) { return std::to_string(j.romId) + "/" + j.fileName; }

    // THREE STATES PER GAME, ROTATING (docs/PROJECT.md, "The in-game
    // shortcuts, and three states per game"). Once a state has reached RomM,
    // this person's states for the game UNDER THE SAME EMULATOR TAG are cut to
    // the newest three, on RomM and on this console. Other tags, which is
    // every other app's states, are never looked at. A state Cabinet saved
    // on the Apple TV under the same tag counts, and can be the one that goes:
    // decided, and the price of three being true everywhere.
    //
    // After the upload, not before: an offline save waits in the queue, and
    // counting it before it exists on RomM would delete a good state to make
    // room for one that might never arrive. Several saved offline come down to
    // three as each lands.
    //
    // Deletes nothing unless the state just sent is among the three kept. If
    // RomM's times disagree with ours, a wrong guess would delete the newest
    // state someone has; doing nothing leaves one too many, which the next
    // save tidies.
    static constexpr size_t kStatesKept = 3;
    void rotate(const Job& job);

    void enqueue(Job job);

    // resendOwed, sendFavorites and sendPlay for a named person: the worker
    // resends for the person whose upload just worked.
    void resendOwedFor(const storage::User& who);
    void sendFavoritesFor(const storage::User& who);
    void sendPlayFor(const storage::User& who);

    void run();

    // THE COPY OF RomM's SAVES FOR THIS GAME (#88, savemirror.h), after a
    // save of ours has landed: forgotten FIRST, so a copy older than what was
    // just sent can never be used offline, then asked for again, with the
    // bytes just sent kept as that row's. Should the asking fail there is no
    // copy, and offline uses the console's own save, which is this one.
    void refreshSaveCopy(const Job& job);

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
