// Where everything on this console lives, and who it belongs to.
//
// WHAT THIS IS FOR, IN ONE SENTENCE: somebody who plugs a drive in or logs in
// over SFTP should be able to find their saves without being told where they
// are, and the console should never be able to delete one person's game because
// somebody else pressed a button.
//
// The layout before this was never designed — it accumulated. There were two
// save directories and a game's save landed in whichever one the launch path
// happened to use; `system/` held replaceable BIOS next to an irreplaceable
// Dreamcast flash; every core in the machine shared one flat pile of saves, so
// working out which game wrote a file meant comparing timestamps; and the
// directory holding kept games and everybody's saves was called `romcache`.
//
// docs/PROJECT.md, open question 18, decided the shape. This is it:
//
//   <root>/
//   ├── roms/<platform>/<romId> - <name>     kept games
//   ├── cache/<platform>/<romId> - <name>    pulled games, the only thing eviction touches
//   ├── bios/                                firmware from RomM, and the cores' system directory
//   ├── games/                               what the console knows of each game on its drives
//   ├── covers/<server>/                     cover art
//   ├── emulators/<name>/                    each separate emulator's own files
//   ├── wii-remote-miis/                     the Wii bridge's Miis
//   ├── users/<id> - <name>/
//   │   ├── saves/<platform>/<romId>/<core>/
//   │   ├── states/<platform>/<romId>/<core>/
//   │   ├── screenshots/<romId>/
//   │   ├── keeps/                           which games this person kept
//   │   ├── pending/                         what has not reached RomM yet
//   │   ├── controls/<romId> - <name>.json   an arcade game's buttons moved (controls.h)
//   │   └── server-saves/<romId>/            RomM's saves as last seen, for play offline
//   ├── config/
//   ├── logs/
//   └── reports/
//
// A games drive gets its own roms/ and cache/ (frontend/README.md has the
// rest).
//
// The vocabulary is RetroArch's and RetroBat's — `roms`, `saves`, `states`,
// `bios`, `config` — because somebody who has used either already knows where
// to look, and that is most of what this question was asking for. The one name
// neither of them needs is `cache/`, because both assume the games are yours
// and permanent. This console pulls on demand, so it has two categories they do
// not: a game that is here because somebody played it, and a game that is here
// because somebody asked for it.
//
// THE PAYOFF IS A RULE YOU CAN CHECK BY LOOKING: eviction only ever deletes
// inside `cache/`. Not "eviction deletes things that are not marked kept" —
// which is a sentence about code — but a directory you can list.
//
// ONE CONVENTION, TWICE: THE NUMBER IDENTIFIES, THE WORDS ARE FOR YOU.
// `users/1 - MMagTech/` and `cache/Sony Playstation/321 - Crash Bandicoot.chd`.
// Everything after " - " is decoration and may be re-derived at any time, so
// renaming a user in RomM is cosmetic here rather than destructive. That
// failure is not hypothetical: a name-keyed console would quietly create an
// empty folder and start again, with every save still on the disk and nothing
// looking for it.
//
// AND ONE SPELLING OF A PLATFORM, EVERYWHERE — RomM's `fs_slug`. The short
// `slug` was written down first and reads better in a path, but it is NOT
// UNIQUE: the reference server has two Arcade platforms sharing `arcade`, 223
// games between them, needing different cores. Under the short name those games
// share one folder while their saves correctly split into `FBNEO/` and
// `MAME2003/` — one console, filed two ways, which is the thing this whole
// question exists to stop. `fs_slug` is also what RomM itself uses for both its
// roms and its assets, so the two trees are the same shape all the way down.
//
// TWO RomM INSTANCES BOTH HAVE A User:1. The real key is (server, user) and
// this uses the user alone, which is a documented limit rather than a mechanism
// — open question 14 established that RomM exposes no instance identity, and
// nothing here can invent one.

#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

#include "locations.h"
#include "users.h"

namespace storage {

// --- Where the root is ------------------------------------------------------
//
// `/var/lib/cabinetos` on a console, and the directory the binary was started
// in on a development machine that cannot write /var/lib (give it a scratch
// --storage-root instead: CONTRIBUTING.md, "Running and testing").
//
// Resolution order: what `--storage-root` said, then $CABINETOS_STORAGE, then
// /var/lib/cabinetos if it can be created and written, then the working
// directory. Whatever wins is printed once at startup, because a console
// writing somewhere nobody expected is a bug that otherwise takes an afternoon.
void setRoot(const std::string& path);
const std::string& root();

// Creates the tree, and returns false only if the root itself is unusable —
// which is the one storage fault that is not survivable.
bool ensureTree(std::string* err);

// Where games can be (the internal root and every drive plugged in), the room
// each has and whether it can be unplugged are in locations.h, included above.

// --- The shared half --------------------------------------------------------

std::string romsDir(const std::string& location);    // <loc>/roms
std::string cacheDir(const std::string& location);   // <loc>/cache

// The libretro system directory: BIOS fetched from RomM, and the assets a core
// ships with.
//
// KNOWN GAP, AND IT IS WRITTEN UP IN docs/PROJECT.md RATHER THAN PAPERED OVER:
// libretro gives a core exactly ONE system directory, and cores write into it —
// Flycast keeps the Dreamcast's own flash there, which is the one file in it
// that cannot be re-fetched. So `bios/` is not purely "replaceable firmware"
// the way the name promises. What it no longer holds is the 13 MB of PSP system
// files that ship inside the image; those live in /usr/share/cabinetos/system
// and are linked in rather than written to.
std::string biosDir();

std::string configDir();
std::string logsDir();
// The diagnostic reports, the last five (#195, /usr/libexec/cabinetos-report).
// File access shows it, so a report can be copied off as well as scanned.
std::string reportsDir();

// An emulator that is a whole application keeps ALL of its own state here, one
// folder each: `emulators/eden/user/` is Eden's keys, system memory, saves,
// settings and shader cache. Shared by everyone on the console, like `bios/`,
// and kept out of the emulator's default home so it is on the same disk as
// everything else the console owns and can be counted. docs/PROJECT.md, open
// question 32.
std::string emulatorsDir();

// Where the image puts the files a core needs and nobody may edit — today that
// is PPSSPP's fonts and lookup tables. Read-only on a console by construction:
// it is inside the bootc image. `ensureTree` links what it finds there into the
// system directory, and leaves alone any name that is already a real file.
std::string imageAssetsDir();

// WHAT THE CONSOLE KNOWS ABOUT EACH GAME ON ITS DRIVE (#88): one record per
// game, `games/<romId>.json`, in RomM's own field names, written whenever a
// game is played or downloaded. It is what lets Home, the Library and Search
// show the games on the drive with no server: a game's folder name carries its
// id, title and platform folder and nothing else a launch needs. Shared by
// everyone, like `bios/`; a record whose game has left the drive is ignored,
// so nothing has to tidy it. Sign out removes them all (server.h): RomM
// numbers every server's games from 1.
std::string gamesDir();

// --- The per-user paths ------------------------------------------------------
//
// Each person's folders, under users/<id> - <name>/. Who a person is (User),
// who the console is acting as and everybody with a folder here are in
// users.h, included above.

std::string userDir(const User& u);
std::string savesDir(const User& u, const std::string& platform, int romId,
                     const std::string& core);
std::string statesDir(const User& u, const std::string& platform, int romId,
                      const std::string& core);
std::string screenshotsDir(const User& u);
std::string keepsDir(const User& u);
std::string pendingDir(const User& u);
// An arcade game's buttons as this person moved them, one file per game
// (controls.h). Outside cache/, so eviction never touches it; never sent to
// RomM.
std::string controlsDir(const User& u);

// Where a save goes when there is no game and no person — `--core <so> <rom>`,
// the developer entry point that plays a file off the disk with no RomM behind
// it, and the `--core-options` audit, which loads every core in turn.
//
// It exists so that THERE IS STILL ONLY ONE SAVE DIRECTORY. The fault open
// question 18 opened with was two of them: `romcache/saves` when a game came
// from the library and `saves/` when it came from `--core`, with PSP save
// folders found in both and neither seeing the other's. This keeps the
// developer path inside the same tree, under a user id of 0 that cannot be
// mistaken for a person — `knownUsers()` skips it, so nothing can be kept or
// attributed to it.
std::string scratchSavesDir(const std::string& core);

// --- Naming -----------------------------------------------------------------

// "321 - Crash Bandicoot". The entry a game occupies under a platform, which is
// a FILE when the game is one file and a DIRECTORY when its archive unpacked
// into several. Both are renamed the same way, so promotion and demotion do not
// care which it is.
std::string entryName(int romId, const std::string& title);

// The rom id at the front of an entry name, or 0. This is the half of the
// convention that identifies; everything after " - " is ignored on purpose.
int romIdFromEntry(const std::string& entryName);

// The platform segment, and it is RomM's `fs_slug` everywhere — "Sony
// Playstation", "Game Boy", "FBNEO". The server uses it for both its own roms
// and its assets, so this tree and that one are the same shape all the way
// down and a fault is visible by eye. See the top of this file for why the
// shorter `slug` lost.
std::string platformSegment(const std::string& fsSlug);

// Anything that came off a server and is about to become a path. Slashes, NULs
// and the two dot-entries, which is the whole of what can escape a directory.
std::string safeSegment(const std::string& s);

// Makes a directory and every parent of it.
bool makeDirs(const std::string& path);

// Moves a file or a whole directory, and says whether it had to fall back to
// copying because the two ends were on different filesystems. The console
// should never need the copy — see `locations()` — so it is reported rather
// than done silently.
struct MoveResult {
    bool ok = false;
    bool crossedFilesystem = false;
    std::string error;
};
MoveResult moveEntry(const std::string& from, const std::string& to);

// Total bytes under a path, following nothing. A file answers its own size.
int64_t treeBytes(const std::string& path);

// True when the path exists at all, file or directory.
bool exists(const std::string& path);

// Deletes a file or a whole directory. True when it is gone. For signing out,
// which clears a server's games and people, and for --files-test's scratch
// root; nothing else should need it.
bool removeEntry(const std::string& path);

}  // namespace storage
