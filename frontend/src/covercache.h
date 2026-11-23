// Cover art kept on disk, so a console does not re-download its own artwork
// every time it is switched on.
//
// WHY THIS EXISTS. Nothing about a library survives a boot — the whole of what
// a console remembers is three files in config/ — so every start re-fetches
// every cover it draws. On a LAN that is a few megabytes nobody notices. Open
// question 29 established that RomM is not always on a LAN, and on a hosted
// server it is the same few megabytes over somebody's uplink, every time.
//
// IT IS NOT A SNAPSHOT OF THE LIBRARY, and that distinction is what keeps it
// clear of open question 22. Nothing here records which games exist, what they
// are called or which platform they belong to. It stores image BYTES under the
// server's own path for them, and a path this console was not told about by a
// live server is never read.
//
// --- THE PATH IS THE VERSION ------------------------------------------------
//
// RomM hands out cover paths with a timestamp on them, the game row's
// updated_at (so it moves when anything about the game changes, the art
// included):
//
//   /assets/romm/resources/roms/1/2/cover/small.png?ts=2025-01-01 00:00:00
//
// So changed art is a changed path, which becomes a different file here, and
// the stale one is simply never asked for again. **That is why there is no TTL
// and no revalidation.** MMagTech raised the TTL question — art that sits on
// disk forever after a platform is deleted — and the answer is a sweep against
// the server's own list rather than a clock (sweep, below, run at start-up;
// docs/PROJECT.md question 30); an old version of a changed picture ages out
// by least recent use (evict).
//
// --- WHAT IS DELIBERATELY NOT CACHED ----------------------------------------
//
// Only rom cover art. An avatar comes from /api/users/<id>/avatar, which
// carries no version in its path and changes when somebody changes their
// picture, so caching it would show a stale face with no way to notice. Any
// key this does not recognise is passed through uncached rather than guessed
// at.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace covercache {

// Which server's art this is. The cache is keyed by it because pointing a
// console at a different RomM makes every path meaningless at once, and
// switching back should not mean downloading everything again.
//
// Until this is called, nothing is read and nothing is written: a cache with
// no server to attribute it to is not safe to serve from.
void setServer(const std::string& address);

// The bytes for a key, or empty if they are not here. Safe to call from the
// ImageCache's worker threads.
std::vector<uint8_t> read(const std::string& key);

// Stores bytes under a key. Best effort in every direction: a failure to write
// is not reported, because a console that cannot cache its art still draws it.
//
// IT REFUSES TO WRITE INTO THE SYSTEM RESERVE. cache.h's rule is that a disk
// full of games is a console that cannot update itself, and artwork is no more
// entitled to that space than a ROM is.
void write(const std::string& key, const std::vector<uint8_t>& bytes);

// Where it all lives, for the storage report and for anything that later
// sweeps it.
std::string dir();

// How many covers were served from disk and how many had to be fetched, since
// the process started. Printed once at shutdown beside the image cache's own
// figures: without it "the cover cache works" is an inference from a directory
// listing rather than something the console says.
struct Stats { int fromDisk = 0; int fetched = 0; int stored = 0; };
Stats stats();

// --- The tile map -----------------------------------------------------------
//
// Which picture a platform's TILE shows, remembered between boots.
//
// WHY IT IS NOT A LIBRARY SNAPSHOT, since that rule is open question 22's and
// it matters: this records "platform 1's tile shows this picture". It does
// not record which games exist, what they are called, or which platform they
// are on, and nothing can be browsed from it. It is also only ever READ on a
// boot where the platform list came back — the console exits before reaching
// it otherwise — so it cannot change what an offline console shows.
//
// WITHOUT IT the console spends 1.07 s and about 0.33 MB at every single start
// asking thirty-six times which cover a tile should use, and getting the same
// thirty-six answers it got last time.
//
// VALIDATED, NOT EXPIRED. Each row carries the platform's `updated_at` and
// `rom_count` as the server reported them when the row was written. Both come
// free with the platform list that boot already fetches, so checking costs no
// request. A platform whose either value has moved is re-asked; the rest are
// not. There is no TTL here for the same reason there is none anywhere else in
// this file.
struct Tile {
    std::string cover;      // the path to draw
    std::string updatedAt;  // the platform's, when this was written
    int romCount = 0;
};
std::map<int, Tile> loadTiles();
void saveTiles(const std::map<int, Tile>& tiles);

// --- Keeping it from growing forever ----------------------------------------

// Deletes everything belonging to a platform the server no longer lists.
//
// THIS IS THE ANSWER TO "WHAT STOPS DELETED ART SITTING ON DISK FOREVER", and
// it is a sweep rather than a clock: a platform that is gone stops appearing in
// the platform list, so it is orphaned the FIRST time the console sees the
// server without it, not whenever a timer happens to expire.
//
// CALL IT ONLY WITH A LIST THAT CAME BACK. Absence is not deletion when the
// server could not be reached, and a sweep on a failed fetch would wipe the
// cache on exactly the boot that most needs it.
void sweep(const std::vector<int>& livePlatformIds);

// Deletes one game's covers, every size and version, under whichever platform
// they were filed by: a game RomM no longer has (gonegames.h, #625), whose
// platform may still be listed. True when something went.
bool forgetGame(int romId);

// Deletes least-recently-used files until the cache is under `budgetBytes`.
// The reserve check on write is a backstop that keeps the console updatable;
// this is the actual policy.
void evict(int64_t budgetBytes);

// What the cache is allowed to grow to. A library's whole art is about 332 MB
// on the reference server and roughly 4 GB at twenty thousand games, so this
// bites only on a large library or a small disk — which is the point, because
// the alternative is that it bites on neither until the disk is full.
inline constexpr int64_t kBudgetBytes = 1LL << 30;   // 1 GB

}  // namespace covercache
