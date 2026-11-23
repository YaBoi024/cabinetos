// Which games are here because somebody asked for them, and which are here
// because somebody played them — and making room for the next one.
//
// A library of 1644 games does not fit on a console. Until this existed the
// disk simply filled and stayed full, which is the state docs/PROJECT.md
// described as the biggest hole in the product.
//
// THE POLICY IS IN docs/PROJECT.md, Phase 4, and the whole of it is:
//
//   The games you have played on this console are on the disk. They stay until
//   the disk needs the room, and then the ones you have not played for longest
//   go first. Nothing that is running, nothing you marked as keep, and nothing
//   still waiting to reach RomM is ever touched.
//
// NEVER ON A TIMER. A cached game on a half-empty disk costs nothing, and
// deleting it only buys a re-download. Eviction happens when a download needs
// the room and at no other moment. The one other removal is a game RomM no
// longer has, at a start with the server there (gonegames.h, #625): there is
// nothing left to re-download.
//
// OLDEST FIRST, AND THAT IS THE WHOLE ORDER. No size tiers, no exemptions for
// small systems. Both were considered and are recorded as deferred.
//
// AND IT IS INVISIBLE. Nothing tells the person this happened. They press play,
// and the game either starts or downloads. See the policy for why: the feedback
// that matters already exists, at the only moment it is useful.
//
// --- WHAT THE FOLDER LAYOUT CHANGED HERE, AND IT IS MOST OF THIS FILE --------
//
// This used to be a walk over one directory called `romcache/` that held kept
// games, cached games, everybody's saves and the upload queue, with a `kept/`
// folder of marker files deciding which of them eviction was allowed to touch.
// Two rules had to be enforced in code because the disk could not express them:
// "do not delete a kept game" and "do not delete a save".
//
// Now the disk expresses both. A kept game is in `roms/` and a cached one is in
// `cache/`, on the same filesystem so that moving between them is a rename;
// saves are somewhere else entirely, under the person they belong to.
//
//   EVICTION ONLY EVER DELETES INSIDE `cache/`.
//
// That is a rule you can check by listing a directory rather than by reading
// this file, and it is the whole point of the split. Two consequences worth
// saying out loud:
//
//   THE UNIT IS NOW THE WHOLE GAME, not "the files in its directory that do not
//   look like saves". The old code carried a list of extensions it must not
//   delete — `.srm`, `.state`, `.brm` — because a game's directory held its
//   saves beside the ROM, and one wrong entry in that list would have taken the
//   only irreplaceable thing on the machine. There is nothing irreplaceable
//   inside `cache/` any more, so that list is gone.
//
//   KEEPING IS A SET OF PEOPLE, NOT A FLAG. One kept game is one file however
//   many people play it, so releasing must not take it from somebody else. When
//   the LAST person releases it, the game is DELETED.
//
//   That last part was the other way round until 2026-09-19, and the row is why
//   it changed: it says "Remove download", and it removed nothing. MMagTech —
//   *"most users would assume unkeeping a chosen game would free up space"* —
//   and they would be right, because that is why anybody presses it.
//
//   The old reasoning was that demoting made un-keep a safe button. Look at
//   what that safety actually bought: every game here is a copy of RomM, so the
//   worst a mis-press costs is a download this console is built to make
//   invisible. That is a very small thing to protect, and it was paid for with
//   a button that appeared to do nothing.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pending.h"
#include "storage.h"

namespace cache {

// Bytes free on the filesystem holding `path`. Zero when it cannot be read,
// which is deliberately the pessimistic answer: an unreadable disk should look
// full rather than infinite.
int64_t freeBytes(const std::string& path);

// --- Where a game is ---------------------------------------------------------

// One game on the disk, on one storage location.
//
// `entryPath` is a FILE when the game is a single payload and a DIRECTORY when
// its archive unpacked into several — a `.cue` needs its `.bin` beside it, and
// an arcade set is a folder. Both are named `<romId> - <title>`, both are
// renamed the same way, and nothing outside this file has to care which it is.
struct Placement {
    bool present = false;
    bool kept = false;          // it is under roms/ rather than cache/
    std::string location;       // the storage location it sits on
    std::string platform;       // the platform segment it sits under
    std::string entryPath;
    bool isDirectory = false;
};

// Finds a game wherever it is, on any location. Cheap enough to call per
// launch: it is a readdir over a few platform folders.
//
// READ OFF THE DISK EVERY TIME, never remembered. That is the whole of what
// stops the fault the Steam Deck has: pull its card out and it still lists the
// games on it as installed, offering a Play button that does nothing, because
// it trusts a list instead of looking. Here an unplugged drive means the game
// is simply not found, and not found already means fetch it.
Placement find(int romId);

// Every copy of this game on the machine. More than one is possible and it is
// nobody's mistake — see dedupe.
std::vector<Placement> findAll(int romId);

// ONE GAME, ONE COPY. Deletes every redundant copy and returns the survivor.
//
// THE CASE THIS EXISTS FOR, which MMagTech found: keep a game while the drive
// is plugged in and it lands in `roms/` on the drive. Unplug the drive, press
// Play, and the console cannot see it — so it fetches it from RomM into the
// cache on the internal disk, which is right. Plug the drive back in and the
// game is on the machine twice.
//
// Leaving both is not acceptable. Two copies of a 40 GB title sitting there
// until something happens to need the room is exactly what a console should not
// do, so the redundant one goes immediately.
//
// WHICH ONE WINS IS DECIDED BY WHAT THE GAME IS, NOT BY WHICH DISK IT IS ON:
// a game somebody still keeps belongs on the games drive, and one nobody keeps
// belongs in the cache on the internal disk. The console knows which even with
// the drive in a drawer, because the keep record never leaves the internal disk.
//
// `expectedBytes` is RomM's `fs_size_bytes`. A copy that is not that size never
// wins, and **if NO copy is the right size nothing is deleted at all** — two
// suspect files and a guess is the one move here that could actually cost
// something, so the launch re-fetches instead.
//
// Deleting is safe rather than probably safe: the copies are the same game at
// the same size, the survivor has just been read, and losing every copy would
// still only cost a download.
Placement dedupe(int romId, int64_t expectedBytes);

// Where a game SHOULD go. `kept` picks `roms/` over `cache/`; everything else
// follows the naming convention — the number identifies, the words are for you.
std::string entryPathFor(const std::string& location, const std::string& platform,
                         int romId, const std::string& title, bool kept);

// --- A game's manual (#364) -------------------------------------------------
//
// THE GAME'S ONE MANUAL FROM RomM, a PDF, kept in a `Manuals` folder beside
// the games of its platform:
//
//   cache/<platform>/Manuals/<romId> - <title>.pdf    a game somebody played
//   roms/<platform>/Manuals/<romId> - <title>.pdf     a game somebody keeps
//
// NOT BESIDE THE GAME AS `852 - name.pdf`: the console knows a game by the
// number at the front of an entry, so that would read as a second copy of it.
// `Manuals` has no number, so everything that walks the games passes it by.
//
// IT FOLLOWS ITS GAME, in this file, wherever the game moves or goes: cleared
// from the cache with it, moved with it when it is kept or released, deleted
// with it when its download is removed, and kept with the survivor when a
// second copy is cleaned away. Nothing else has to remember it exists.

// Where a game's manual goes, beside a game whose entry is `gameEntryPath`.
std::string manualPathFor(const std::string& gameEntryPath, int romId,
                          const std::string& title);

// The manual on a drive that is here now, beside any copy of the game, or
// empty. Read off the disk every time, like find().
std::string findManual(int romId);

// --- Eviction ----------------------------------------------------------------

// One cached game, and when it was last used.
//
// `lastUsed` is the entry's own mtime, which needs no new bookkeeping: the
// download sets it and a launch touches it. A game downloaded but never played
// therefore sorts oldest, which is right — nobody has come back to it.
//
// "Last used" is a fact about THIS CONSOLE'S COPY, deliberately not RomM's play
// history, which belongs to the household and spans every device. It is also
// THE MACHINE'S rather than any one person's: reading it per user would evict a
// game because *you* have not touched it while somebody else plays it daily.
struct Entry {
    std::string path;
    int64_t bytes = 0;
    int64_t lastUsed = 0;   // seconds since the epoch
    int romId = 0;
    std::string platform;
};

// Everything in `cache/` on this location, oldest first. There is nothing to
// exclude and no rule to remember: if it is in there, it may go.
std::vector<Entry> candidates(const std::string& location);

// Deletes least-recently-used games until `needBytes` are free, plus a margin so
// the next few launches cost nothing. Returns the number of bytes freed.
//
// `protectRomId` is the game being launched or running, which is never a
// candidate however old its entry looks. Pass 0 when there is none.
//
// It stops when it runs out of candidates rather than failing: the caller
// checks whether there is now enough room, because "not enough space" is a
// different answer from "nothing left to delete" only in what it says to the
// person.
int64_t evictUntilFree(const std::string& location, int64_t needBytes,
                       int protectRomId = 0);

// ONE GAME OUT OF THE CACHE, on every drive that is here, the way eviction
// removes one: its entry and its manual, and never a folder holding saves not
// yet off it (candidates). Returns the bytes freed. For a game RomM no longer
// has (gonegames.h); a game somebody keeps is released instead (unkeep).
int64_t evictGame(int romId);

// The margin above, as a fraction. Freeing exactly enough means evicting on
// every single launch once a disk sits near full, until the cache holds nothing
// but the running game — the person never sees it happen and never benefits
// from the cache again.
inline constexpr double kMarginFraction = 0.10;

// Slack on top of a download's declared size. Landing on exactly zero free
// bytes is where filesystems begin to fail in interesting ways, and staying off
// it costs nothing.
inline constexpr double kOverheadFraction = 0.05;

// Marks a game as used now, so that oldest-first means anything at all.
void touch(const std::string& entryPath);

// --- Keeping, and the two floors that guard it ------------------------------
//
// A KEPT game is the one thing on the disk that is deliberate. Everything else
// here is a copy of RomM that the console may reclaim whenever it needs the
// room; a kept game is a promise that it will not.
//
// THE PROMISE IS WHY KEEPING CAN BE REFUSED, and it is the only place in this
// whole subsystem where the console says no to anything. Kept games are never
// evicted, so without a check a person can keep enough of them to leave the
// machine with nothing it is permitted to delete — at which point it can
// neither write a save nor update itself, and the only way out is to go and
// un-keep something. Two floors, and the console refuses a keep that would
// cross either:
//
//   THE SAVE FLOOR protects the upload queue and room to write one more state.
//   2 GB, or 5% of the disk, whichever is smaller — a fixed size rather than a
//   percentage because saves do not scale with the disk. The same person plays
//   the same games on a 4 TB drive as on a 32 GB one.
//
//   THE SYSTEM RESERVE protects the machine's ability to update itself, which
//   needs room for a whole image. 5 GB, and the larger of the two. The cache is
//   the system's to take, so this is checked when KEEPING and never against the
//   cache.
//
// Neither floor can protect data from itself: somebody offline for a week fills
// the disk with their own upload queue and no reserve helps. That is recorded
// in docs/PROJECT.md as a conversation rather than a number.

// Room for a whole system image. The reserve is a constant because an image is
// a constant; it does not grow with the disk it sits on.
inline constexpr int64_t kSystemReserveBytes = 5LL << 30;

// 2 GB, or 5% of this filesystem, whichever is smaller.
int64_t saveFloorBytes(const std::string& path);

// What a keep would cost and whether the console can afford it.
//
// `gameBytes` is what the kept game will occupy IN TOTAL, whether or not any of
// it is on the disk yet — keeping a game nobody has downloaded is the case this
// has to get right, because that is a promise to fetch it and hold it.
struct KeepVerdict {
    bool allowed = false;
    // What the console would be short by. Zero when allowed, and the number the
    // refusal should say out loud: "the disk is full of things you asked me to
    // keep" is only useful with an amount beside it.
    int64_t shortfallBytes = 0;
    // What was measured, so a caller can explain the answer rather than just
    // report it.
    int64_t reclaimableBytes = 0;
    int64_t floorBytes = 0;
};

// The question is NOT "is there room right now" — a kept game may already be on
// the disk, in which case keeping it consumes nothing at all today. It is
// whether, AFTER this game stops being evictable, the console can still free
// its way down to both floors. So it counts what it could still reclaim if it
// deleted every evictable thing it has, and compares that with the floors.
//
// ALREADY KEPT BY SOMEBODY ELSE COSTS NOTHING, which is the whole of what the
// per-user split buys: the second person to keep a game is asking for a promise
// the machine has already made.
KeepVerdict mayKeep(const std::string& location, int romId, int64_t gameBytes);

// --- Where a kept game goes, with more than one drive ----------------------
//
// THE MAIN DRIVE FIRST, AND ANOTHER DRIVE ONLY AS OVERFLOW. MMagTech,
// 2026-09-25, replacing "the first extra drive, whenever one is plugged in".
// That rule assumed a small main drive and a big extra one; with a 2 TB main
// drive and a 1 TB stick it sent every kept game to the smaller, slower drive
// that can be pulled out, while the big one sat empty. Main first keeps kept
// games on the fastest drive, the one that never leaves, which is also the
// one offline mode can always read.
//
// Kept games fill the main drive up to 80% of it. The rest is left for the
// games people only play (the cache, which clears itself) so a new game can
// always be played without removing a kept one. Past that, the extra drive
// with the most free room that the game fits on. With no extra drive, or
// none it fits on, the main drive after all, down to the floors above:
// refusing while hundreds of gigabytes sit free would read as a bug.
// Internal or USB makes no difference. A starting value; one line.
inline constexpr double kMainKeepShare = 0.80;

// Where a kept game of `gameBytes` should be downloaded to. Decides where a
// download LANDS and nothing else: keeping a game already on a drive never
// moves it (see keep()).
std::string keepLocation(int64_t gameBytes);

// Space left across every drive, counting the cache as free because it clears
// itself. Under this share, a Download says "Storage almost full".
inline constexpr double kAlmostFullShare = 0.10;
bool almostFull();

// Marks a game kept BY THIS PERSON, and promotes it out of the cache.
//
// `record` is the game's whole library entry as JSON, not a subset: Cabinet's
// KeptGame embedded the entire Rom captured at keep time so a kept game can be
// browsed and launched with no network at all, and a subset is how that promise
// gets broken later by a field nobody thought of.
bool keep(const storage::User& u, int romId, const std::string& record,
          std::string* err);

// Releases THIS PERSON's keep. If nobody else is keeping it, THE GAME IS
// DELETED and the space comes back, which is what the row promises.
//
// `keepTheBytes` demotes the game to the cache instead, where it is evictable
// and costs nothing until something needs the room. Two callers want it, and
// neither is somebody asking for space back:
//
//   THE GAME BEING PLAYED RIGHT NOW. The running core has it open. Deleting it
//   would work on Linux — the open descriptors stay valid — right up to the
//   moment the core opens a second file it had not needed yet, which is an
//   ordinary thing for a `.cue` or a multi-disc `.m3u` to do.
//
//   A KEEP THAT FAILED. The record goes in before the first byte moves, so a
//   download that never finished has to take it out again. That is undoing a
//   promise, not reclaiming space — and the file may well be a perfectly good
//   game that was already on the disk before the keep was asked for, which
//   throwing away over a failed firmware fetch would be its own small disaster.
//
// WHAT A RELEASE ACTUALLY DID, because the caller has to tell somebody.
//
// These four outcomes have always existed and until 2026-09-21 all four went to
// stderr and nowhere else, so the screen said the same nothing for every one of
// them. Two of them are a person pressing "Remove download" and getting no
// space back, which is exactly the promise that row's wording was changed to
// make. A release that quietly does not release is the same fault as a launch
// refusal nobody can see.
struct Release {
    enum class What {
        Nothing,       // nothing was released: no such keep, or it would not go
        StillKept,     // somebody else keeps it, so the file did not move
        Demoted,       // dropped into the cache; the space comes back later
        Deleted,       // gone, and the space is back now
        DeleteFailed,  // the keep went, the bytes would not
    };
    What what = What::Nothing;
    int otherKeepers = 0;    // only meaningful for StillKept
    int64_t bytesFreed = 0;  // only meaningful for Deleted
};

// Returns false only when the record could not be removed. `out` is optional
// and says which of the outcomes above happened; pass it whenever a person is
// watching.
bool unkeep(const storage::User& u, int romId, bool keepTheBytes = false,
            Release* out = nullptr);

bool isKeptBy(const storage::User& u, int romId);
bool isKeptByAnyone(int romId);

// The user ids keeping this game. Walks every user directory on the machine,
// which is what makes one person's release safe for everybody else.
std::vector<int> keepers(int romId);

// RELEASES EVERYBODY'S KEEP, so the game goes whoever downloaded it. This is
// Settings' Downloads (#68): an admin screen behind the PIN, where the space
// belongs to the machine and not to whoever pressed Download. MMagTech,
// 2026-09-26: no names on the list, "if an admin/parent is concerned a kid or
// someone might delete a downloaded game they should turn on PIN protection".
// The game's own screen still releases only your own (unkeep).
//
// A game in `roms/` that nobody keeps (see downloads()) is simply deleted.
bool unkeepForEveryone(int romId, bool keepTheBytes = false, Release* out = nullptr);

// EVERY DOWNLOADED GAME ON THIS CONSOLE, for Settings' Downloads: one entry per
// game however many people kept it. Read off the disk each time, like find().
//
// Two sources, because each misses something the other has:
//   - the keep records, which carry the title and platform, and are the only
//     trace of a game whose drive is not connected (`present` false;
//     Settings leaves those out);
//   - `roms/` on every connected drive, which can hold a game nobody keeps
//     any more: its record released from the game's own screen while its
//     drive was away, then the drive plugged back in. Without this it would
//     sit on the drive for ever, never cleared (only `cache/` is) and listed
//     nowhere.
struct Download {
    int romId = 0;
    std::string title;
    std::string platform;       // RomM's platform name, as the record has it
    int platformId = 0;         // from the record; zero when there is none
    int64_t bytes = 0;          // on the disk; zero when not present
    std::string location;       // the storage location it is on, if present
    bool present = false;
};
std::vector<Download> downloads();

// The games this person has kept.
std::vector<int> keptRoms(const storage::User& u);

// Every game kept by anybody, which is what the machine's storage report and
// the floors care about.
std::vector<int> allKeptRoms();

// --- What is on the drive, for offline play (#88) ---------------------------
//
// Offline, the console shows every game on its drives, downloaded or cached,
// whoever downloaded or played it (MMagTech, 2026-10-05, on #88). Each needs
// its library entry, which is what `rememberGame` keeps: written at every
// launch and download, so a game that is on the drive has normally been
// remembered on the way there. A downloaded game also has its keep record,
// which is used when there is no note.

// Writes `storage::gamesDir()/<romId>.json`, through a temporary.
void rememberGame(int romId, const std::string& record);

// The note for this game, or any person's keep record for it, or nothing.
std::string gameRecord(int romId);

// One game on a drive that is here now.
struct OnDrive {
    int romId = 0;
    std::string record;      // gameRecord(romId); empty when there is none
    int64_t lastUsed = 0;    // the entry's mtime: download or last launch
    bool kept = false;       // under roms/ rather than cache/
};

// Every game on every drive that is here, one each, most recently used first.
// Read off the disk every time, like find(). Includes games with no record,
// which offline cannot show but a backfill can fetch while online.
std::vector<OnDrive> onDrive();

}  // namespace cache
