// What the console has to show: the store of cards, Home's shelves and the
// Library's tiles, loaded off RomM at start and at an account switch, kept up
// with it since (libraryrefresh.h), or off the drive with no server; and a
// kept game's record, the writer beside the library until the RomM client's
// batch (i). Moved out of main.cpp in the restructure (#398).

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "covercache.h"
#include "design.h"
#include "libraryscreen.h"
#include "romm.h"
#include "users.h"

// A game left on this console since its library was loaded: when, in seconds
// since 1970, and whether its time goes to RomM (playtime::kShortestMs: a
// minute or more). RomM's history may not have it, or may never have it.
struct PlayedHere {
    int64_t at = 0;
    bool sent = false;
};

// A platform some of whose games show nowhere (catalog::hidesSomeGames):
// RomM's count when its list was read, and how many of them show.
struct Counted {
    int listed = 0;
    int shown = 0;
};

// As many as Home's Recent row holds, and as RomM is asked for.
constexpr size_t kRecentGames = 16;

struct Library {
    std::vector<design::Card> cards;
    // The games behind the cards, same order. A card is what is drawn; this is
    // what is launched, and the launch needs the platform and the file name.
    std::vector<romm::Game> games;
    // Index into `cards`, or -1. The hero is the most recently played game THIS
    // CONSOLE CAN PLAY — not simply the most recent, because a Resume that
    // cannot run is worse than no hero at all.
    int heroIndex = -1;
    std::string heroPlatform;
    // Indices into `cards`, in recency order, with the hero removed — so the
    // shelf never shows the same cover twice and never loses one. Home shows
    // THIS, not the whole library: the library belongs to the Library screen.
    std::vector<int> shelf;
    // The second shelf. Drawn only when there are any: an empty Favorites row
    // is worse than no Favorites row.
    std::vector<int> favorites;

    // What the Library screen shows. EVERY platform the server holds, including
    // the ones this console cannot play — those carry their reason instead of a
    // count and cannot be opened. docs/PROJECT.md is explicit that the answer is
    // neither "show everything" nor "hide quietly": somebody who owns Switch
    // games and sees nothing will reasonably conclude the scan failed.
    std::vector<screens::Tile> platformTiles;
    std::vector<screens::Tile> collectionTiles;
    // Every platform the server listed, by its name as the console shows it
    // (catalog::displayName, "Arcade" for every arcade folder), for anything
    // that names a system outside a tile: Settings' Downloads groups by it.
    // The cover sweep keeps the covers of every id here (App::startBrowsing).
    std::map<int, std::string> platformNames;
    // What a tile's cached cover is validated against: the platform's own
    // `updated_at` and `rom_count`, as the server reported them this boot.
    // Filled for playable platforms only, because nothing else gets a cover.
    std::map<int, covercache::Tile> tileCache;
    // Every game this console has fetched so far, keyed by rom id, so a game
    // that is recent AND a favourite AND in a platform's grid is one card
    // rather than three. THE VALUE IS AN INDEX INTO `cards`, which is why
    // `cards` is append-only — see below.
    std::map<int, int> byRomId;

    // WHAT RomM LAST ANSWERED, kept so a part it does not answer next time
    // stays as it was (putLibrary): its lists, whose they were read as (0 when
    // RomM would not say), and the newest time it gave for any of it.
    std::vector<romm::Platform> platforms;
    std::vector<romm::Collection> collections;
    std::vector<romm::Collection> smartCollections;
    int me = 0;
    std::map<int, Counted> counted;
    std::string newestUpdate;
    // The games left here that RomM's history may not have (App::playedJustNow).
    std::map<int, PlayedHere> playedHere;
    // Which games this person keeps (App::refreshKeeps), for a card added
    // since: a game a grid or Search listed later had no dot until this.
    std::set<int> kept;
};

int appendGame(Library& lib, const romm::Game& g);
// The platforms behind each Library tile: one each, and every arcade folder
// together, the Arcade tile (#611). And a tile's query for its platforms.
std::vector<std::vector<romm::Platform>> tileGroups(const std::vector<romm::Platform>& platforms);
std::string platformsQuery(std::vector<int> ids);
// `--arcade-tile-test`: which platforms share a tile, the Arcade tile's query,
// and what an arcade game's card and line are called. Needs no server. 0 when
// all pass.
int arcadeTileTest();
bool loadTileGames(romm::Client& client, Library& lib, screens::Tile& tile);
// loadTileGames in its two halves, for a caller that reads a tile's first page
// ahead of time (App::readAheadForInvite): the request, which touches nothing
// else and may run on a worker, and putting its answer on the tile, on the
// main thread. False when the request failed.
bool readFirstPage(romm::Client& client, const std::string& query, std::vector<romm::Game>* games,
                   int* total, std::string* err);
void takeFirstPage(Library& lib, screens::Tile& tile, std::vector<romm::Game> games, int total);
std::vector<int> gridOrder(Library& lib, const screens::Tile& tile);

// ---- What Home and the Library are made from, read and built -------------
//
// IN TWO HALVES, READING AND BUILDING, for the two ways a library is made:
// whole, at start, at an account switch and back online (loadLibrary), and
// again while the console is on (libraryrefresh.h). Reading asks RomM and
// touches nothing else, so it may run on a worker; building puts the answers
// into a library, an empty one or the one on the screen, on the thread that
// owns it.

// What to ask RomM for.
struct LibraryAsk {
    storage::User who;             // whose favourites list the order comes from
    // Everything: who is asking, and every favourite's entry. Otherwise only
    // what can have changed since the library in hand was read (askAgain).
    bool whole = true;
    std::string since;             // favourites RomM changed after this
    std::map<int, Counted> counted;
    std::set<int> inHand;          // the games the library holds an entry for
    std::vector<int> playedHere;   // games to ask by themselves if RomM's history has them not
};

// What RomM answered, each part with whether it came.
struct LibraryRead {
    bool whole = true;
    bool reached = false;          // RomM was there to be asked
    int me = 0;
    bool gotPlatforms = false;
    std::string platformsErr;
    std::vector<romm::Platform> platforms;
    std::map<int, Counted> counted;
    bool gotCollections = false;
    std::vector<romm::Collection> collections;
    bool gotSmart = false;
    std::vector<romm::Collection> smartCollections;
    bool gotRecent = false;
    std::vector<romm::Game> recent;
    bool gotFavorites = false;
    std::vector<int> favorites;       // which games, by id
    bool gotFavoriteEntries = false;  // every one, or every changed one
    std::vector<romm::Game> games;    // entries that came beside Recent's
    std::set<int> gone;               // asked by themselves, and not there for this person
    std::string newest;               // the newest time RomM gave in any of it
    std::string failed;               // the parts that did not come, for the log
    // WHAT HAPPENED HERE SINCE IT WAS ASKED, filled in by whoever builds from
    // it: hearts pressed or not yet sent as it was asked, and the games whose
    // time RomM already had by then (App::followLibrary).
    std::map<int, bool> hearts;
    std::map<int, int64_t> settled;   // rom id -> when it was left
};

// What building changed, in words for the log; empty when nothing did.
struct LibraryChanges {
    std::vector<std::string> said;
};

// What to ask again for a library in hand.
LibraryAsk askAgain(const Library& lib, const storage::User& who);
// Asks. `stop`, when given, ends it between requests; the client's own ends
// the one on its way (romm::Client::stopWhen).
LibraryRead readLibrary(romm::Client& client, const LibraryAsk& ask,
                        const std::atomic<bool>* stop = nullptr);
// Builds from what came. `who` is the person it was read for, whose favourites
// list is written (favorites.h).
LibraryChanges putLibrary(Library& lib, LibraryRead read, const storage::User& who);
// Both, whole, into a new one. `platformsErr`, when given, is why the platform
// list did not come: empty when it came, even with nothing in it.
Library loadLibrary(romm::Client& client, std::string* platformsErr = nullptr);
Library offlineLibrary();
std::string gameRecordJson(const romm::Game& g);

// What an arcade game's archive holds, kept in the game's record
// (romm_types.h, Game::members), and read back for the same archive, by its
// name and size; empty when the record has none for it.
void rememberArchive(int romId, const std::string& file, int64_t size,
                     const std::vector<romm::ArchiveMember>& members);
std::vector<romm::ArchiveMember> rememberedArchive(int romId, const std::string& file,
                                                   int64_t size);
// The files a game's save channels can be on (romm_types.h, ChannelFiles),
// kept in the game's record when it has one, where none learned leaves what
// it had (gameRecordJson), and read back; none when the record has none.
// Either may run on a worker.
void rememberChannelFiles(int romId, const romm::ChannelFiles& files);
romm::ChannelFiles rememberedChannelFiles(int romId);
