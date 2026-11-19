// LEAVING A GAME (#346, save channels): what a session left goes up to RomM
// as one snapshot, once, after the game has closed, and RomM's answer is
// settled here. Its own file because it is neither the queue's job
// (uploader.h, which runs it on its thread) nor the gathering's (saves.cpp,
// which reads each kind of save and writes this console's copy first). The
// words are RomM's, in romm_types.h.
//
// WHAT GOES UP. One save, in the form a CabinetOS console's start reads a
// snapshot's save (saves.h): a battery as it is; the one file a core writes
// as it is; several files as a zip, each under the path its core reads it
// from; a save folder as the zip syncDirSave makes. Under the tag the core's
// saves travel under (catalog::saveTag), which is what lets another console
// use it as stored (savepick.h), and named after the game. A snapshot's
// states are carried from the one before it. Nothing changed means nothing
// sent.
//
// WHERE IT GOES (decisions 3 and 9 of 2026-11-15). Into the channel the save
// it was made from is in, naming that snapshot as the current it builds on:
// the snapshot this console holds of the game (savemirror::Held), which a
// start from a channel sets and every push that lands moves on. A console
// that holds none saves into the person's channel named "default", RomM's
// own and where every other app looks, made when there is none. Every
// console is one device on RomM, so the console says which current it builds
// on itself, every time.
//
// WHEN RomM SAYS THE CHANNEL MOVED ON (409), it has already kept the push
// beside the channel's current, as a branch. If this console's save was made
// later than that current, it is put on top by a second push that names the
// branch and sends no bytes; if not, it is left where RomM kept it, and the
// next start takes the newer one. "Made later" is this console's own note of
// when it wrote the save against the time RomM took the other device's push:
// a push carries no time of its own. A SESSION THAT COULD NOT USE THE NEWEST
// SAVE (GameSession::couldNotUseNewest) IS ALWAYS LEFT AS RomM KEPT IT: the
// newer save stays on top for the device that made it, and this console goes
// on from its own (savepick.h).
//
// A HARDCORE CHANNEL IS NEVER SAVED INTO (decision 10). A session that
// started from one, or whose channel has turned hardcore since, saves into
// the person's channel named "softcore", RomM's own word for a save that is
// not hardcore, made from the snapshot it started from. RomM refuses an
// ordinary save over a hardcore current in any case (409
// `hardcore_downgrade`), and the console never approves that.
//
// THE ORDER THAT KEEPS A SAVE (pending.h). The marker is written before the
// push is queued, and cleared only by the push that sent it, after the note
// of the snapshot held is written, so a crash or a power cut at any point
// leaves the save owed and it goes again; RomM answers a push it already has
// with "unchanged". While it is owed, a start takes nothing from RomM
// (savepick.h).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "gamesession.h"
#include "pending.h"
#include "romm.h"
#include "storage.h"

class Uploader;

namespace savepush {

// The channel a console that holds no snapshot of a game saves into: RomM's
// own name for a game's ordinary channel (its `DEFAULT_CHANNEL_LABEL`).
inline constexpr char kDefaultChannel[] = "default";
// The console's own channel beside a hardcore one (MMagTech, 2026-11-17):
// the word RomM's website uses for a save that is not hardcore. Shown only
// on RomM's website.
inline constexpr char kSoftcoreChannel[] = "softcore";

// --- The frame thread's half ---------------------------------------------------

// WHAT THE SESSION LEFT, MARKED AS OWED: when one of the gatherings found
// the save changed (GameSession::saveChanged), the game's owed push is
// written (cache::OwedPush), replacing whole the one an earlier session left
// unsent, and any save file of the game still owed from before save channels
// stops being owed: the snapshot carries the game's whole save. False when
// there is nothing to send: no change, no tag the save may travel under, or
// nobody signed in. Asks nothing of the network.
bool mark(GameSession& sess, const storage::User& user);

// mark, then the push queued (Uploader::pushSnapshot). Called once a game has
// closed and its saves are gathered: the end of App::finishExit and of
// App::finishStandalone, a save folder a power cut left unsent
// (finishInterruptedSave, App::recoverDriveSaves), and the key that gathers
// a battery mid-game (input.cpp).
void leave(GameSession& sess, const storage::User& user, Uploader& up);

// --- The upload queue's half ---------------------------------------------------

// What a push asks of RomM, so the test answers in its place: whether the
// server has save channels (romm::heardServerChannels); the file a game's
// channels are keyed to (false when RomM could not be asked, 0 when it names
// none); this person's channels on that file (`*gone` when RomM no longer
// knows the file); one push; and a game's saves, for the console's copy.
struct Server {
    std::function<bool()> hasChannels;
    std::function<bool(int romId, int* fileId, std::string* err)> channelFile;
    std::function<bool(int fileId, std::vector<romm::Channel>* out, std::string* err,
                       bool* gone)> channels;
    std::function<bool(const romm::SnapshotPush& push, romm::PushAnswer* answer,
                       std::string* err)> push;
    std::function<bool(int romId, std::vector<romm::Asset>* rows, std::string* err)> saves;
};
// The real one: RomM, as whoever `client` is signed in as.
Server serverOf(romm::Client& client);

enum class Sent {
    Landed,        // RomM holds it as its channel's current
    Beside,        // RomM holds it beside the current, where it stays
    NothingOwed,   // no marker, or the file it named is gone: nothing to send
    NotSent,       // no answer, or not asked: still owed, tried again later
    Refused,       // RomM answered and will not take it: still owed
};

// ONE GAME'S OWED PUSH, SENT AND SETTLED (the head of this file), as `who`.
// Reads the marker and the save's bytes off the disk, so what goes is what
// the newest session left. When RomM holds it: the game's copy of RomM's
// saves is forgotten first, so a start already fetching sees its list is
// stale (saves.cpp, fetchIsStale); the snapshot becomes the one this console
// holds; the marker is cleared; and the copy is fetched again for offline.
// `inPlay`, when given, is asked once the bytes are read: true leaves the
// push owed and unsent (uploader.cpp). `*why` says what happened when it did
// not land. Blocks: off the frame thread.
Sent send(const Server& server, const storage::User& who, int romId, std::string* why,
          const std::function<bool()>& inPlay = {});

// `--save-push-test` (savepush_test.cpp): what a session left for each kind
// of save, where each push is aimed, and every answer RomM gives, settled
// against a stand-in Server in a scratch root. 0 when everything passed.
int test();

}  // namespace savepush
