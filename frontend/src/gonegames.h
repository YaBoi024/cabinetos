// Games RomM no longer has (#625): the console's copies of them leave its
// drive at a start with the server there, with their saves, states and
// screenshots (docs/decisions/2026-11-01-game-gone-from-romm.md). The
// player sees nothing.
//
// WHY. A game deleted in RomM, or given a new entry by a folder RomM scanned
// again, stayed on the drive: a kept one for ever, a played one until a
// download needed the room. Its pictures were deleted at start when its whole
// platform had gone (covercache::sweep) and fetched again by the backfill
// (offline_sync.cpp), 30 pictures at every start on the A9; offline it showed
// and played, and a save made then was refused by RomM at every start
// (uploader.cpp, refusedForGood), since nothing could ever take it.
//
// WHEN A GAME IS GONE. RomM answers 404 for the game's own entry (`GET
// /api/roms/<id>`, Client::fetchGame's `notFound`) to EVERY person signed in
// on this console, each asked with their own sign-in. RomM 5.4.0 answers a
// game hidden from the person asking (a hidden platform or game, or an age
// limit; an admin sees everything) with the same 404 in the same words, so a
// game one person here can still see stays. A server away, a timeout, a 5xx,
// a 401 or 403, or any answer but a 404 is not gone; one person who cannot be
// asked, or a list that does not come, removes nothing that start.
//
// CHEAPLY, ONCE PER START, behind Home. Each person's whole list of ids comes
// in one call (Client::fetchGameIds, 10.8 KB for 2,343 games); only an id on
// the drive or owed that is in nobody's list is asked about by its own entry,
// so a start with nothing gone costs one call per person.
//
// WHAT GOES, by the paths that already remove a game: every person's keep
// (cache::unkeepForEveryone) and the cache's own removal (cache::evictGame),
// so the manual, an installed PS3 or Vita game and an emulator's cache inside
// the game's folder go with it; then its record and its covers; then every
// person's saves, states and screenshots of it, sent or not, and the copy of
// RomM's saves kept for offline. RomM 5.4.0 deletes a game's saves, states
// and screenshots with the game (`RomAsset.rom_id`, ON DELETE CASCADE), so
// nothing kept for it could ever be sent anywhere.
//
// NEVER BEFORE THE UPLOAD QUEUE HAS TRIED IT. The queue sends what the person
// signed in owes at every start (Uploader::resendOwed); their owed file for a
// game that is gone goes only once RomM has refused it this run, after the
// queue has emptied. Another person's goes once RomM has answered 404 for the
// game to them, which the rule below asks with their own sign-in. NEVER a
// game being played or launched (setInUse), and never a folder still holding
// saves not yet off it (the Xbox drive's note, a person's note for an
// emulator of its own, standalone.h): each is left for a later backfill or
// the next start. Time played and favourites need nothing here: RomM 5.4.0
// takes a play session for a game it no longer has (filed under no game) and
// ignores a missing game added to a collection, so neither is sent again.

#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "storage.h"

namespace gone {

// What one person's RomM says, for the rule below: every game id they can see
// (false when the list did not come), and whether a game's own entry answered
// 404 to them. Both ask as `person`, a RomM user id.
struct Asker {
    std::function<bool(int person, std::vector<int>* ids)> list;
    std::function<bool(int person, int romId)> notFound;
};

// THE RULE: which of `ids` RomM answers 404 to every one of `people`. False
// with `*why`, deciding nothing, when there is nobody to ask or a person's
// list did not come. A person's list holding a game ends the question for it;
// the rest are asked by their own entry, person by person.
bool decide(const std::vector<int>& ids, const std::vector<int>& people, const Asker& ask,
            std::set<int>* gone, std::string* why);

// ONCE PER START: the rule asked of the server at `baseUrl` as each person on
// this console (accounts.h). The first call RomM answers asks; every later
// one returns what it said then. Run on the backfill's thread (offline_sync.cpp).
std::set<int> thisStart(const std::string& baseUrl, const std::vector<int>& ids);

// The game on the screen and the game whose launch is under way, or 0. Set
// by the frame thread once a frame (App::followServer); read by removeCopy
// and removeSaves.
void setInUse(int playing, int launching);

// The upload queue, for removeSaves's order: how many uploads it still
// holds, and whether RomM refused a person's file this run
// (Uploader::refusedThisRun). Set by App::startSync; a stand-in in a test.
struct Queue {
    std::function<int()> pending;
    std::function<bool(const storage::User& who, int romId, const std::string& fileName)> refused;
};
void setQueue(Queue q);
// The queue holds something not yet tried.
bool uploadsPending();

// This console's copy of a game RomM no longer has, every one on every drive
// here, its record and its covers: one line per game removed. False when it
// was left, being played or launched or holding saves not yet off it.
bool removeCopy(int romId);

// Every person's saves, states and screenshots of a game RomM no longer has,
// sent or not, their markers and the copy of RomM's saves for it: one line
// per game. `signedIn` is the person the upload queue sends for, whose owed
// file waits until RomM has refused it this run; a person whose files wait,
// or whose save folder still holds an emulator's note, is left for a later
// call. False when anything was left.
bool removeSaves(int romId, const storage::User& signedIn);

// Every game anybody on this console owes a save, state or screenshot for.
std::vector<int> owedIds();

// `--gone-games-test`: the rule against a stand-in RomM, and the removal on a
// scratch drive. Prints each check; 0 when all pass.
int test();

}  // namespace gone
