// LEAVING A GAME (#346, save channels): what a session left goes up to RomM
// as one snapshot, once, after the game has closed, and RomM's answer is
// settled here. Its own file because it is neither the queue's job
// (uploader.h, which runs it on its thread) nor the gathering's (saves.cpp,
// which reads each kind of save and writes this console's copy first). The
// words are RomM's, in romm_types.h.
//
// WHAT GOES UP. One save, in the form a CabinetOS console's start reads a
// snapshot's save (saves.h): a battery as it is, or zipped with the
// cartridge's clock (Game Boy) or the cartridge (Saturn) beside it (#346,
// step 11), a cartridge with no save on it never sent and a change to the
// clock alone no new save; the one file a core writes as it is; several
// files as a zip, each under the path its core reads it from; a save folder
// as the zip syncDirSave makes. Under the tag the core's
// saves travel under (catalog::saveTag), which is what lets another console
// use it as stored (savepick.h), with the core's own name in Sigil's spelling
// beside it (catalog::SaveForm; answer 7 of 2026-11-17), from which RomM
// converts the save for another app (#346, step 8), and named after the
// game. Nothing changed
// means nothing sent. WITH ITS PICTURE, the game the moment it saved, when
// the sitting took one (savepicture.h): RomM's `save_screenshot` part, which
// RomM keeps with the save's row and its website shows on the channel's
// tile. A push of states alone sends none: the save it carries keeps its own.
//
// AND THE STATES (#346, step 6; decisions 1 and 8 of 2026-11-15). A state is
// written on this console with its picture when it is made, and marked owed
// in the game's push at once (markState), so a power cut cannot strand it; it
// goes up inside the snapshot when the game is left. THE GAME OFFERS THE THREE
// NEWEST OF ITS OWN STATES, WHEREVER THEY ARE ON RomM (MMagTech, 2026-11-19,
// as RomM's own web player chooses): of RomM's list of the game's states,
// every snapshot's (a channel's current, its history, a branch) and the ones
// from before channels, the ones under the console's own name, the core's
// state tag (catalog::emulatorTag), with the ones made here and still owed
// (offeredStates). A push names those same three (kStatesKept), so the
// snapshot on top holds what the TV shows, in slots "1", "2" and "3", newest
// first, RetroArch's and RomM's numbered slots (MMagTech, 2026-11-19: not
// "auto", which other apps keep for a state made by itself on quitting). A
// state made since is always among them, so none made on this console is let
// go before RomM has it. Only that core is named, so every other app's states
// in the bank are carried as they are. A state RomM already holds is named by
// its content hash and its bytes are not sent again; RomM's list carries no
// hash, so it is read off this console's copy, else the channels' currents,
// else the history of the channel the state was pushed into. One no snapshot
// holds (from before channels) goes up from this console's copy, or is left
// out. The console deletes no state on RomM: RomM's own history keeps the
// older ones. A push with no save of its own carries the save of the snapshot
// it builds on; a push with a save and no new state names none, so RomM
// carries the bank as it is.
//
// WHERE IT GOES (decisions 3 and 9 of 2026-11-15). Into the channel the save
// it was made from is in, naming that snapshot as the current it builds on:
// the snapshot this console holds of the game (savemirror::Held), which a
// start from a channel sets and every push that lands moves on. A console
// that holds none saves into the person's channel named "default", RomM's
// own and where every other app looks, made when there is none. Every
// console is one device on RomM, so the console says which current it builds
// on itself, every time. A PUSH INTO A CHANNEL NAMES THAT CHANNEL'S OWN FILE
// (#346, step 12): RomM refuses any other (422), and a file added to the
// game's folder can move the one RomM makes new channels on (a `.cue` beside
// a CHD) while the channel stays where it was made. So the person's channels
// are listed on every game file of the game, as a start lists them, and
// RomM's file for new channels (`channel_file_id`) is named only for a
// channel the push makes (romm::ChannelFiles).
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
// A PUSH OF STATES ALONE GOES ONTO THE CHANNEL'S CURRENT AS RomM LISTS IT
// when it is sent (MMagTech, 2026-11-19): states made while another device's
// newer save went on top go into that snapshot, beside that save, which the
// push carries; when RomM says the channel moved on meanwhile, they go again
// onto the current it names. Not for a session that could not use the newest
// save, nor onto a save of another emulator's (answer 3 of 2026-11-17): then
// they are aimed as a save is, and stay where RomM keeps them, beside. The
// note of the snapshot held keeps the save this console last had, so its next
// start still says "Continuing from another device" (savepick.h).
//
// A PUSH OF STATES ALONE NEEDS A SNAPSHOT TO BUILD ON when the game has a
// save anywhere else: one this console owes from before save channels, or
// one RomM holds from before them. With nothing to build on it would make a
// snapshot with no save in front of that one, so it waits, owed, until the
// game's save is in a channel; the game's next start brings it in, and the
// states go with it. A game with no save at all sends its states in a
// snapshot with none.
//
// A SAVE FROM BEFORE SAVE CHANNELS COMES IN AT THE GAME'S FIRST START AFTER
// THE UPDATE (#346, step 7; decision 2 of 2026-11-15). A start that put
// RomM's in place (savepick.h, oldSaveToCopy) owes the game's push of it
// (markOldSave), aimed where this console saves into, and queued at once:
// RomM copies the row in itself (`copy_of`), no byte sent, and leaves the
// row as it is for a console still on the old version. The game's states
// from before channels go in with it, from this console's copies, as their
// bytes, since RomM holds them in no snapshot. "Made" is RomM's time
// for the row, so a channel that moved on meanwhile keeps the newer save on
// top. A save this console still owes from before save channels, one marker
// a file, becomes its game's push the same way (adoptOldSaves).
//
// A HARDCORE CHANNEL IS NEVER SAVED INTO (decision 10). A session that
// started from one, or whose channel has turned hardcore since, saves into
// the person's channel named "softcore", RomM's own word for a save that is
// not hardcore, made from the snapshot it started from. RomM refuses an
// ordinary save over a hardcore current in any case (409
// `hardcore_downgrade`), and the console never approves that.
//
// A RomM TOO OLD FOR SAVE CHANNELS IS NEITHER READ NOR WRITTEN (#346, step
// 9; decisions 6 and 7 of 2026-11-15, answer 4 of 2026-11-17): a push waits,
// owed, with nothing asked (Sent::Waits), and the console sends by itself
// once the server says it has them (App::followServer). Not refused: nothing
// RomM said is in the way.
//
// THE ORDER THAT KEEPS A SAVE (pending.h). The marker is written before the
// push is queued, and cleared only by the push that sent it, after the note
// of the snapshot held is written, so a crash or a power cut at any point
// leaves the save owed and it goes again; RomM answers a push it already has
// with "unchanged". While a save is owed, a start takes nothing from RomM
// (savepick.h); states alone owed do not hold a start back.
//
// THIS CONSOLE'S COPIES OF ITS STATES. Once RomM holds a push that named
// states, a state file of the game that push did not name and no push still
// owes is deleted with its picture, as the three per game always were: RomM
// keeps the others in the game's history.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "gamesession.h"
#include "pending.h"
#include "romm.h"
#include "savepick.h"
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

// How many states a game offers and a push names: the console's three
// newest, as it always kept (docs/wiki/saves.md, "Save states").
inline constexpr size_t kStatesKept = 3;

// --- The frame thread's half ---------------------------------------------------

// WHAT THE SESSION LEFT, MARKED AS OWED: when one of the gatherings found
// the save changed (GameSession::saveChanged), the game's owed push is
// written (cache::OwedPush), replacing the one an earlier session left
// unsent but for its states, which it carries, and any save file of the game
// still owed from before save channels stops being owed: the snapshot
// carries the game's whole save. False when there is no save to send: no
// change, no tag the save may travel under, or nobody signed in. Asks
// nothing of the network.
bool mark(GameSession& sess, const storage::User& user);

// A STATE JUST MADE, MARKED AS OWED (the head of this file): the game's owed
// push gains it, newest first, and keeps the newest kStatesKept; one pushed
// out of them is never sent. A push already owed keeps where it was aimed
// when it carries a save, and is aimed again from this session when it
// carries states alone. False when it could not be marked: no state tag,
// nobody signed in, or the marker could not be written. Asks nothing of the
// network.
struct StateMade {
    std::string fileName;      // the name it travels under
    std::string path;          // its bytes on this console
    std::string pictureName;   // its picture; empty for none
    std::string picturePath;
    int64_t madeMs = 0;        // when it was made, milliseconds since 1970
};
bool markState(const GameSession& sess, const storage::User& user, const StateMade& made);

// mark, then the push queued (Uploader::pushSnapshot) when the game owes one:
// a save just marked, or states. Called once a game has closed and its saves
// are gathered: the end of App::finishExit and of App::finishStandalone, a
// save folder a power cut left unsent (finishInterruptedSave,
// App::recoverDriveSaves), and the key that gathers a battery mid-game
// (input.cpp).
void leave(GameSession& sess, const storage::User& user, Uploader& up);

// A START THAT TOOK RomM'S SAVE FROM BEFORE CHANNELS (savepick.h,
// oldSaveToCopy) MARKED AS THE GAME'S OWED PUSH that brings it into a
// channel (#346, step 7; decision 2 of 2026-11-15): aimed as the session's
// own save would be (WHERE IT GOES, above), carrying the states an earlier
// marker holds and this console's own copies of the game's newest states
// (answer 5 of 2026-11-17: they go into its first snapshot), and made when
// RomM stored the newest of its rows. One row is
// named for RomM to copy; the rows of a save that was several are zipped
// from `bytes` (each row's, in `copy.rows`' order) beside this console's own
// copy of the save. Not a save this console owes (cache::saveOwed). A push
// of the session's own save marked at the leave takes its place. False when
// nothing was marked. Asks nothing of the network.
bool markOldSave(const GameSession& sess, const storage::User& user,
                 const savepick::OldSaveCopy& copy,
                 const std::vector<std::vector<uint8_t>>& bytes);

// A SAVE'S MARKER FROM BEFORE SAVE CHANNELS, one per file (cache::Owed): a
// save this console made and RomM never had, so it still wins at a start.
// Each game's become its owed push (#346, step 7), its file as the save,
// made when the console wrote it, into the person's "default", or onto the
// snapshot this console holds; a game's several files (Sega CD's two) go as
// one zip of them, each under its own name, written beside them. A push
// already owed with a save of the console's own carries the whole save, so
// the old markers only go. The old markers go once the push is marked. On
// the frame thread (Uploader::resendOwed). How many games gained one.
int adoptOldSaves(const storage::User& who);

// A STATE'S MARKER FROM BEFORE SAVE CHANNELS, one per file (cache::Owed):
// RomM takes a state from this console only inside a snapshot now, so each
// becomes a state of its game's owed push, aimed as a push of states alone
// is, and its old marker goes. On the frame thread (Uploader::resendOwed),
// and for a person being removed (accountremove.cpp). How many games gained
// one.
int adoptOldStates(const storage::User& who);

// THE STATES THIS CONSOLE OFFERS FOR A GAME, newest first, at most
// kStatesKept (the head of this file): of `rows`, RomM's list of the game's
// states (Client::fetchStates), the ones filed under `tag` (the console's own
// name), with the ones its owed push carries; one per name, RomM's own time
// taken off it. Null `rows` (RomM away or not answering, or a core whose
// states never leave the console): this console's own files, the owed ones
// and every state in `stateDir`. A state comes off this console when its
// file is in `stateDir` (by name), else down from RomM by its id when it is
// picked. Reads the disk only; the game's page, Load latest state and Resume
// go by it.
struct OfferedState {
    int id = 0;             // RomM's; 0 for one only on this console
    std::string fileName;
    std::string path;       // this console's copy; empty when it has none
    std::string picture;    // this console's copy, else RomM's path; may be empty
    int64_t madeMs = 0;     // 0 when not known
};
std::vector<OfferedState> offeredStates(const storage::User& who, int romId,
                                        const std::string& tag, const std::string& stateDir,
                                        const std::vector<romm::Asset>* rows);

// --- The upload queue's half ---------------------------------------------------

// What a push asks of RomM, so the test answers in its place: whether the
// server has save channels (romm::heardServerChannels); the files a game's
// channels can be on (romm::ChannelFiles), as this console remembers them
// (`ask` false; none when it never learned them) or asked of RomM, which
// this console then remembers (`ask` true; false when RomM could not be
// asked); this person's channels on those files (`*gone` when RomM no
// longer knows one of them); one push; a game's saves, for the console's
// copy; and for the states a push names, the game's states and a channel's
// history.
struct Server {
    std::function<bool()> hasChannels;
    std::function<bool(int romId, bool ask, romm::ChannelFiles* out, std::string* err)> files;
    std::function<bool(const std::vector<int>& fileIds, std::vector<romm::Channel>* out,
                       std::string* err, bool* gone)> channels;
    std::function<bool(const romm::SnapshotPush& push, romm::PushAnswer* answer,
                       std::string* err)> push;
    std::function<bool(int romId, std::vector<romm::Asset>* rows, std::string* err)> saves;
    std::function<bool(int romId, std::vector<romm::Asset>* rows, std::string* err)> states;
    std::function<bool(const std::string& channelId, std::vector<romm::Snapshot>* out,
                       std::string* err)> history;
};
// The real one: RomM, as whoever `client` is signed in as.
Server serverOf(romm::Client& client);

enum class Sent {
    Landed,        // RomM holds it as its channel's current
    Beside,        // RomM holds it beside the current, where it stays
    NothingOwed,   // no marker, or the file it named is gone: nothing to send
    NotSent,       // no answer, or not asked: still owed, tried again later
    Refused,       // RomM answered and will not take it: still owed
    Waits,         // RomM is too old for save channels: still owed, nothing asked
};
// RomM holds it, or nothing is owed: the push is done with.
bool settled(Sent sent);
// Still owed and tried again on the upload queue's timer: no answer, or a
// RomM too old for save channels. A refusal waits for the next start or a
// new save of the game instead (uploader.h, refused_).
bool triedAgain(Sent sent);

// ONE GAME'S OWED PUSH, SENT AND SETTLED (the head of this file), as `who`.
// Reads the marker and the save's bytes off the disk, so what goes is what
// the newest session left. When RomM holds it: the game's copy of RomM's
// saves is forgotten first, so a start already fetching sees its list is
// stale (saves.cpp, fetchIsStale); the snapshot becomes the one this console
// holds; the marker is cleared; and the copy is fetched again for offline.
// `inPlay`, when given, is asked once the bytes are read: true leaves the
// push owed and unsent (uploader.cpp). `*why` says what happened when it did
// not land. Blocks: off the frame thread. ONE PUSH OF A GAME AT A TIME: the
// upload queue's and a start's (settleBeforeStart) are never in flight
// together, so the second finds the marker the first cleared and sends
// nothing twice.
Sent send(const Server& server, const storage::User& who, int romId, std::string* why,
          const std::function<bool()>& inPlay = {});

// A START SENDS WHAT THIS CONSOLE STILL OWES OF THE GAME BEFORE IT PICKS
// (#346, phase 5, 2026-11-21), when RomM answered its lists: a console back
// from a trip, its save made there still owed, while another console saved
// the game since. Until this the owed save won whatever RomM held
// (savepick.h), so the console played on from the older save and its next
// save went on top of the newer one: the fault phase 2 recorded, still
// there whenever Play came before the queue had sent it (a start right after
// the console noticed RomM again). Sent, the push settles the usual way
// (the head of this file): on top when this console's save was made later,
// else kept beside, and the start lists again and picks by the one rule.
// True when nothing is owed any more; false leaves it owed, and this
// console's own save stands, as before. On the launch worker (saves.cpp,
// fetchSaves).
bool settleBeforeStart(const Server& server, const storage::User& who, int romId);

// `--save-push-test` (savepush_test.cpp): what a session left for each kind
// of save, where each push is aimed, every answer RomM gives, and the states
// a push carries, settled against a stand-in Server in a scratch root. 0
// when everything passed.
int test();

}  // namespace savepush
