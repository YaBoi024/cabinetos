#include "library.h"

#include <SDL3/SDL.h>
#include <json-c/json.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"
#include "cache.h"
#include "catalog.h"
#include "favorites.h"
#include "storage.h"
#include "wii.h"

using namespace design;   // NOLINT: Card and colorForTitle (card.h)

// What a game's card says while its grid shows it greyed, or nothing.
//
// A LIVE ANSWER, ASKED AGAIN EACH TIME (#398, HOME-09). A Wii game that needs
// a Wii Remote greys while none is paired or a stream plays
// (catalog::coverageFor). Worked out once, when the game was first fetched,
// pairing the first Remote left Kirby greyed at the end of the Wii grid while
// its page offered Play.
static std::string greyedReason(const romm::Game& g) {
    const catalog::Coverage cov = catalog::coverageFor(g);
    return catalog::shownGreyed(cov.support) ? cov.reason : std::string();
}

// Adds a game to the store if it is not already there, and answers where it is.
// A game already there is asked again whether it is greyed.
//
// `cards` IS APPEND-ONLY, AND THIS IS THE REASON. Every shelf, every tile and
// the hero hold plain indices into it, so sorting or erasing would silently
// re-point all of them at the wrong game. Ordering belongs to each list rather
// than to the store: the Recent shelf is in recency order, a platform grid is
// alphabetical, and neither needs the store itself to be either.
//
// The library used to be sorted wholesale and the membership lists rebuilt
// afterwards, with a comment explaining that the sort had to come first. That
// worked because everything arrived in one pass. Nothing arrives in one pass
// any more.
int appendGame(Library& lib, const romm::Game& g) {
    auto it = lib.byRomId.find(g.id);
    if (it != lib.byRomId.end()) {
        // A GAME ALREADY KNOWN TAKES WHAT THE SERVER SAYS NOW: a cover or a
        // name fixed on RomM (a Match, an uploaded cover) shows the next time
        // its grid is read, not after a restart (#387). The colour stand-in
        // stays, so nothing flashes while the new cover loads.
        const size_t i = static_cast<size_t>(it->second);
        Card& c = lib.cards[i];
        c.title = g.name.empty() ? g.fsName : g.name;
        c.sortKey = g.sortKey;
        c.cover = g.coverPath;
        c.coverLarge = g.coverLargePath;
        c.unavailable = greyedReason(g);
        if (i < lib.games.size()) lib.games[i] = g;
        return it->second;
    }
    Card c;
    c.id = g.id;
    c.title = g.name.empty() ? g.fsName : g.name;
    c.sortKey = g.sortKey;
    c.cover = g.coverPath;
    c.coverLarge = g.coverLargePath;
    c.platform = g.platformName;
    c.platformId = g.platformId;
    c.wii = g.platformSlug == "wii";
    c.needsRemote = c.wii && wii::padDevice(wii::codeFromTitleId(g.titleId)) == 0;
    c.tagLabel = g.tagLabel;
    c.art = colorForTitle(c.title);
    c.unavailable = greyedReason(g);
    const int idx = static_cast<int>(lib.cards.size());
    lib.cards.push_back(std::move(c));
    lib.games.push_back(g);
    lib.byRomId.emplace(g.id, idx);
    return idx;
}

// The games behind one tile, fetched when somebody opens it and not before.
//
// THIS IS THE WHOLE POINT OF OPEN QUESTION 28. A tile knows its own count from
// the server without holding a single game — `romCount` on the platform, and
// on the collection — so boot draws the Library screen having fetched nothing,
// and a grid costs one request at the moment it is walked into.
//
// Returns false only when the request failed. A tile that legitimately holds
// nothing this console can play returns true with an empty membership, which
// is a different thing and reads differently on the screen.
bool loadTileGames(romm::Client& client, Library& lib, screens::Tile& tile) {
    if (!tile.cards.empty()) return true;   // already walked into once
    const std::string& filter = tile.query;
    std::vector<romm::Game> games;
    std::string err;
    // PAGED, AND CHECKED AGAINST `total`. This asked for ten thousand in one
    // request and called that enough, with a comment claiming it matched
    // fetchGames. It did not: one request is not paging, and a platform past
    // the limit would have come back SHORT AND SUCCESSFUL — the exact failure
    // fetchGames' own comment calls the worst possible one, because the grid
    // would simply show fewer games and say nothing.
    //
    // A full MAME set is tens of thousands of roms in a single platform, so
    // this is not a hypothetical library.
    constexpr int kPage = 500;
    int total = 0;
    // ONLY THE FIRST PAGE, AND THE CALLER DRAWS IT. The rest is somebody
    // else's job — see GridFill. A grid used to fetch the whole platform
    // before anything appeared, which is fine at 141 games and is ten seconds
    // for a full MAME set in one platform.
    {
        std::vector<romm::Game> page;
        // fetchRoms writes the limit itself, so only the offset rides on the
        // filter.
        if (!client.fetchRoms(filter, kPage, &page, &err, &total)) {
            std::fprintf(stderr, "[library] %s: %s\n", tile.title.c_str(), err.c_str());
            return false;
        }
        games = std::move(page);
    }
    tile.loadedAtMs = SDL_GetTicks();
    static unsigned reading = 0;
    tile.readId = ++reading;
    tile.fetched = games.size();
    tile.total = std::max(games.size(), static_cast<size_t>(std::max(total, 0)));
    int unplayable = 0;
    for (const auto& g : games) {
        // A collection can hold games from systems this console cannot run.
        // Platform grids cannot, since the tile would not be enterable, but
        // the check is cheap and the two paths share it.
        // EXCEPT A GAME THAT NEEDS A CONTROLLER NOBODY HAS PAIRED, which its
        // own platform's grid shows, greyed and saying why: the system plays
        // here, so a shorter list would read as games gone missing. MMagTech,
        // 2026-09-30, for the Wii games that need a Wii Remote. And an arcade
        // set no emulator here runs (#310), for the same reason.
        if (!catalog::playable(g) && !catalog::shownGreyed(catalog::coverageFor(g).support)) {
            ++unplayable;
            continue;
        }
        tile.cards.push_back(appendGame(lib, g));
        if (tile.cover.empty()) {
            const int i = tile.cards.back();
            if (!lib.cards[i].cover.empty()) tile.cover = lib.cards[i].cover;
        }
    }
    // NOT SORTED HERE. RomM returns roms in title order already, which is what
    // the grid's letter-jump needs, and a later page has to concatenate onto
    // this one — a sort would fight the append and move cards out from under
    // whoever is looking at them. The tile keeps the server's order; the
    // greyed games go last as the grid opens (gridOrder).
    if (unplayable > 0) {
        // Say what is missing rather than quietly showing a shorter list: a
        // collection of twelve that opens onto four looks like a bug unless
        // the tile already said why. Short, because a tile's second line holds
        // about sixteen characters beside a cover.
        char count[96];
        std::snprintf(count, sizeof count, "%zu of %zu",
                      tile.cards.size(), tile.cards.size() + unplayable);
        tile.detail = count;
    }
    std::fprintf(stderr, "[library] %s: %zu game(s)%s\n", tile.title.c_str(),
                 tile.cards.size(),
                 unplayable ? " (some not playable here)" : "");
    return true;
}

// A tile's games in the order its grid shows them, each asked again whether
// it is greyed (greyedReason), every time the grid opens.
//
// GREYED GAMES GO LAST, each half still in title order. MMagTech, 2026-09-30,
// on the Wii grid: mixed in, the playable games were hard to find. A later
// page puts its playable games before the greyed ones (GridScreen::append),
// so the order holds as pages arrive. Split from the tile's own list, which
// stays in the server's order, so a game that stops being greyed goes back
// to its place among the letters rather than to the end of the playable ones.
std::vector<int> gridOrder(Library& lib, const screens::Tile& tile) {
    std::vector<int> order = tile.cards;
    for (int i : order)
        lib.cards[static_cast<size_t>(i)].unavailable =
            greyedReason(lib.games[static_cast<size_t>(i)]);
    std::stable_partition(order.begin(), order.end(), [&](int i) {
        return lib.cards[static_cast<size_t>(i)].unavailable.empty();
    });
    return order;
}

// THE SERVER'S SYSTEMS, KEPT FOR OFFLINE (#88), so an offline Library tile
// says what the online one says ("Arcade" over "FinalBurn Neo") rather than
// whatever a game's own record calls its system. `games/platforms.json`,
// rewritten at every online start.
static void savePlatforms(const std::vector<romm::Platform>& platforms) {
    json_object* arr = json_object_new_array();
    for (const romm::Platform& p : platforms) {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "id", json_object_new_int(p.id));
        json_object_object_add(o, "name", json_object_new_string(p.name.c_str()));
        json_object_object_add(o, "slug", json_object_new_string(p.slug.c_str()));
        json_object_object_add(o, "fs_slug", json_object_new_string(p.fsSlug.c_str()));
        json_object_array_add(arr, o);
    }
    storage::makeDirs(storage::gamesDir());
    const std::string path = storage::gamesDir() + "/platforms.json";
    const std::string tmp = path + ".part";
    if (json_object_to_file_ext(tmp.c_str(), arr, JSON_C_TO_STRING_PRETTY) == 0)
        ::rename(tmp.c_str(), path.c_str());
    json_object_put(arr);
}

static std::map<int, romm::Platform> savedPlatforms() {
    std::map<int, romm::Platform> out;
    json_object* arr = json_object_from_file((storage::gamesDir() + "/platforms.json").c_str());
    if (!arr) return out;
    if (json_object_is_type(arr, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(arr); ++i) {
            json_object* o = json_object_array_get_idx(arr, i);
            json_object* v = nullptr;
            romm::Platform p;
            if (json_object_object_get_ex(o, "id", &v)) p.id = json_object_get_int(v);
            if (json_object_object_get_ex(o, "name", &v)) p.name = json_object_get_string(v);
            if (json_object_object_get_ex(o, "slug", &v)) p.slug = json_object_get_string(v);
            if (json_object_object_get_ex(o, "fs_slug", &v)) p.fsSlug = json_object_get_string(v);
            if (p.id > 0) out[p.id] = p;
        }
    }
    json_object_put(arr);
    return out;
}

// Builds the shelf from a real library.
//
// THE PLATFORMS THIS CONSOLE CANNOT PLAY ARE LEFT OUT, and that is a decision
// rather than an oversight — see catalog.h. A console must not offer a game it
// cannot run; the moment to discover that is not after someone has chosen it.
// What it must not do is drop them silently, so every exclusion is reported on
// stderr with its reason. The screen that tells the person the same thing is
// still to build; this is the part that knows.
//
// What the console knows the moment it finishes booting.
//
// FOUR CALLS, AND NOT ONE OF THEM IS THE CATALOGUE — open question 28. This
// used to walk every platform and page through all of them, sixteen hundred
// games on the reference server, before anything was drawn. It was the
// dominant cost of a boot and it was PROPORTIONAL TO THE LIBRARY, so somebody
// with twenty thousand games waited proportionally longer every single time
// they turned the console on, and nothing in the design had noticed.
//
// Nothing needed it. A platform tile's count was already on the platform
// object; a collection's was already on the collection. Home shows about
// fourteen covers and every one of them comes from the recents and favourites
// calls, which are bounded. A grid does not exist until somebody walks into
// it, and search asks the server.
//
// So boot is CONSTANT rather than proportional, which is the whole prize: the
// person with twenty thousand games boots as fast as the person with two
// hundred.
Library loadLibrary(romm::Client& client, std::string* platformsErr) {
    Library lib;
    std::string err;

    std::vector<romm::Platform> platforms;
    if (!client.fetchPlatforms(&platforms, &err)) {
        std::fprintf(stderr, "[romm] platforms: %s\n", err.c_str());
        if (platformsErr) *platformsErr = err;
        return lib;
    }
    savePlatforms(platforms);

    int skippedGames = 0;
    // A tile per platform, built whether or not this console can play it. The
    // unplayable ones are why `coverageFor` gives four different answers rather
    // than one boolean: "no core exists", "Cabinet did not ship it", "this
    // console has not built it yet" and "it is built and cannot be driven" lead
    // to different work and to different words on the screen.
    for (const auto& p : platforms) {
        const catalog::Coverage cov = catalog::coverageFor(p);
        lib.platformNames[p.id] = catalog::displayName(p);
        screens::Tile tile;
        tile.id = p.id;
        tile.query = "platform_ids=" + std::to_string(p.id);
        // THE NAME AND THE QUALIFIER ARE TWO FACTS ON A TILE, not one string.
        // Joined, "Arcade (FinalBurn Neo)" and "Arcade (MAME 2003-Plus)" both
        // came out as "Arcade" over "(FinalBurn ..." and "(MAME 200..." at this
        // width — so the qualifier that exists SOLELY to tell the two tiles
        // apart was the part being cut off. See catalog::displayQualifier.
        tile.title = p.name.empty() ? p.slug : p.name;
        const std::string qualifier = catalog::displayQualifier(p);
        tile.enterable = (cov.support == catalog::Support::Playable);

        if (!tile.enterable) {
            skippedGames += p.romCount;
            // The tile gets the short form, which is all it has room for. The
            // log keeps the full sentence, because a log is read by somebody
            // trying to find out why.
            tile.detail = catalog::shortReason(cov);
            std::fprintf(stderr, "[library] %s (%d games) — %s\n", tile.title.c_str(),
                         p.romCount, cov.reason ? cov.reason : tile.detail.c_str());
            // ONLY WHAT THIS CONSOLE CAN PLAY (#117, MMagTech 2026-10-04), as
            // Cabinet's tvOS Library did: a system with no emulator here
            // (Jaguar, ColecoVision, Vita) or left out by decision (Game &
            // Watch) gets no tile. Search and collections still find its
            // games, whose page says why. A core that SHOULD be here and is
            // missing (NotInstalled) keeps its greyed tile, because that is a
            // fault worth seeing, not a choice.
            if (cov.support == catalog::Support::NoCore ||
                cov.support == catalog::Support::Excluded)
                continue;
            lib.platformTiles.push_back(std::move(tile));
            continue;
        }

        // THE COUNT COMES OFF THE PLATFORM, which is the finding that made all
        // of this cheap: it was there the whole time, and boot was fetching
        // sixteen hundred games to draw a number the server had already sent.
        //
        // EXCEPT WHERE SOME OF A PLATFORM'S GAMES ARE HIDDEN (the 3DS's CIAs,
        // catalog::hidesSomeGames), which RomM's count includes: that
        // platform's list is fetched and what shows is counted, so a server
        // whose 3DS games are all CIAs gets "0 games", as a platform with
        // none does. One request, only for such a platform.
        int shown = p.romCount;
        if (catalog::hidesSomeGames(p) && shown > 0) {
            std::vector<romm::Game> games;
            std::string gerr;
            if (client.fetchGames(p.id, &games, &gerr)) {
                shown = 0;
                for (const auto& g : games)
                    if (catalog::coverageFor(g).support != catalog::Support::Hidden) ++shown;
            }
        }
        char count[48];
        std::snprintf(count, sizeof count, "%d game%s", shown, shown == 1 ? "" : "s");
        tile.detail = qualifier.empty() ? std::string(count)
                                        : qualifier + "  \xC2\xB7  " + count;
        lib.tileCache[p.id] = covercache::Tile{"", p.updatedAt, shown};
        // `tile.cover` is deliberately left empty. It used to be the first
        // cover among the platform's games, which cost a fetch per platform —
        // 1.55 s of a 1.8 s boot, measured against the reference server. The
        // tiles come up in their colour and the covers are filled in behind
        // Home; see fillTileCovers. MMagTech chose that over both paying for
        // them at boot and using RomM's platform logo, 2026-09-22.
        lib.platformTiles.push_back(std::move(tile));
    }

    // Playable systems first, then alphabetically inside each group. The
    // alternative — one flat alphabet — puts a system this console cannot play
    // in the first tile on the screen, which is the wrong thing to lead with on
    // a console whose whole job is the games it CAN run. The unplayable ones
    // are still all there, below, saying why.
    std::stable_sort(lib.platformTiles.begin(), lib.platformTiles.end(),
                     [](const screens::Tile& a, const screens::Tile& b) {
                         if (a.enterable != b.enterable) return a.enterable;
                         // Ignoring case: a server owner's "Playstation" sorts
                         // with "PlayStation 2", not after every capital P.
                         return std::lexicographical_compare(
                             a.title.begin(), a.title.end(), b.title.begin(), b.title.end(),
                             [](unsigned char x, unsigned char y) {
                                 return std::tolower(x) < std::tolower(y);
                             });
                     });

    // Collections. The membership used to be resolved against a catalogue that
    // was already in memory — "no second request", said the comment, and that
    // was true only because boot had paid for all of them. It is one request
    // now, made when the collection is opened, exactly like a platform.
    //
    // SMART COLLECTIONS BESIDE THE REGULAR ONES (#352), fetched in this same
    // load with this person's token, so an account switch shows the new
    // person's own and everyone's public ones, as RomM decides
    // (romm_types.h, Collection). The order is below.
    std::vector<romm::Collection> collections;
    std::vector<romm::Collection> smart;
    std::string smartErr;
    const bool smartOk = client.fetchSmartCollections(&smart, &smartErr);
    if (!smartOk) std::fprintf(stderr, "[library] no smart collections: %s\n", smartErr.c_str());
    if (client.fetchCollections(&collections, &err)) {
        // A FAVORITES IS PERSONAL, AND OTHER PEOPLE'S ARE GROUPED BY WHOSE
        // (MMagTech, 2026-10-12, #352). RomM lists other accounts' public
        // collections, their Favorites included, so a parent's public
        // Favorites showed on a child's account beside the child's own: two
        // tiles reading "Favorites". Only this person's own is shown. Then:
        // their own first, Favorites at the front, the rest A to Z, regular
        // and smart together (how a collection was made is not what it is
        // for); then each other person's shared ones under "From <name>",
        // people A to Z and each A to Z. Who "this person" is costs one
        // request; if the server will not say, it is one A to Z list.
        int me = 0;
        collections.insert(collections.end(), smart.begin(), smart.end());
        if (!collections.empty()) {
            romm::User u;
            std::string uerr;
            if (client.fetchCurrentUser(&u, &uerr)) me = u.id;
            else std::fprintf(stderr, "[library] who is this: %s\n", uerr.c_str());
        }
        auto lower = [](std::string t) {
            for (char& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return t;
        };
        auto mine = [me](const romm::Collection& c) { return me == 0 || c.userId == me; };
        collections.erase(std::remove_if(collections.begin(), collections.end(),
                                         [&](const romm::Collection& c) {
                                             return c.isFavorite && !mine(c);
                                         }),
                          collections.end());
        std::stable_sort(collections.begin(), collections.end(),
                         [&](const romm::Collection& a, const romm::Collection& b) {
                             if (mine(a) != mine(b)) return mine(a);
                             if (!mine(a) && a.userId != b.userId) {
                                 const std::string oa = lower(a.ownerName), ob = lower(b.ownerName);
                                 return oa != ob ? oa < ob : a.userId < b.userId;
                             }
                             if (a.isFavorite != b.isFavorite) return a.isFavorite;
                             return lower(a.name) < lower(b.name);
                         });
        for (const auto& col : collections) {
            screens::Tile tile;
            tile.id = col.id;
            tile.query = (col.smart ? "smart_collection_id=" : "collection_id=") +
                         std::to_string(col.id);
            tile.title = col.name;
            if (!mine(col))
                tile.group = "From " + (col.ownerName.empty() ? std::string("another account")
                                                              : col.ownerName);
            tile.cover = col.coverPath;
            char count[48];
            std::snprintf(count, sizeof count, "%d game%s", col.romCount,
                          col.romCount == 1 ? "" : "s");
            tile.detail = count;
            // The server's count, not how many are playable here — that is not
            // known until it is opened, and loadTileGames rewrites the line to
            // "4 of 12" at that point if the two differ.
            tile.enterable = col.romCount > 0;
            lib.collectionTiles.push_back(std::move(tile));
        }
        std::fprintf(stderr, "[library] %zu collection(s), %zu of them smart\n",
                     lib.collectionTiles.size(), smart.size());
    } else {
        // Not fatal. A server that will not list collections still has a
        // library, and the switcher's Collections tab says it is empty.
        std::fprintf(stderr, "[library] no collections: %s\n", err.c_str());
    }

    // The hero, from the server's play history rather than from anything this
    // console remembers. A game played on another device is recent here the moment
    // this console is paired, which is what lets a machine that has never
    // launched anything still open on the right game.
    //
    // THE GAMES COME STRAIGHT OFF THIS CALL NOW. They used to be looked up in
    // the catalogue and DROPPED IF NOT FOUND — which, once boot stopped
    // fetching the catalogue, would have quietly emptied Home while every one
    // of these calls still succeeded. That is the trap in this change and it
    // is why the store is filled from here rather than searched.
    // TIMED, because once on the A9 these two took eleven seconds during an
    // account switch when they take a fifth of one at boot, and nothing else
    // in the log said which.
    const Uint64 tRecent = SDL_GetTicks();
    std::vector<romm::Game> recent;
    const bool gotRecent = client.fetchRecent(16, &recent, &err);
    std::fprintf(stderr, "[library] play history in %llu ms\n",
                 static_cast<unsigned long long>(SDL_GetTicks() - tRecent));
    if (gotRecent) {
        for (const auto& g : recent) {
            // A recent game on a platform this console cannot play is skipped,
            // not shown greyed: Home must not offer a Resume that cannot run.
            // The reference library exercises this — the most recent "Altered
            // Beast" is the Game & Watch one, which this console leaves out.
            if (!catalog::playable(g)) continue;
            const int idx = appendGame(lib, g);
            // THE MOST RECENT ONE STAYS ON THE SHELF — changed 2026-09-21.
            //
            // `heroIndex` is what Home focuses on when it opens, which is the
            // whole of "resume-first". It is an index into the shelf's first
            // slot rather than a separate object on the screen.
            if (lib.heroIndex < 0) {
                lib.heroIndex = idx;
                lib.heroPlatform = g.platformName;
            }
            lib.shelf.push_back(idx);
        }
    } else {
        std::fprintf(stderr, "[library] no play history: %s\n", err.c_str());
    }

    // ALL OF THEM ARE ASKED FOR, up to 200, not the shelf's 40 (#476):
    // RomM lists favourites by title, so asking for 40 left a person with
    // more never seeing the rest, and a new one past the fortieth title never
    // reached Home. About 23 KB each on RomM 5.3.1.
    const Uint64 tFavs = SDL_GetTicks();
    std::vector<romm::Game> favs;
    const bool gotFavs = client.fetchFavorites(200, &favs, &err);
    std::fprintf(stderr, "[library] favourites in %llu ms\n",
                 static_cast<unsigned long long>(SDL_GetTicks() - tFavs));
    if (gotFavs) {
        // THE ORDER IS THE CONSOLE'S (favorites.cpp, keepFavorites): newest
        // first as far as it knows, with what the server has not heard yet
        // winning (#267): a favourite taken off while it was away stays off,
        // one added stays on, until sent. Kept for the offline Home (#88).
        std::vector<int> order;
        for (const auto& g : favs) order.push_back(g.id);
        if (const storage::User& who = storage::currentUser(); who.valid())
            order = keepFavorites(who, order);
        // Home's shelf holds the first 40 this console can play. A game
        // hearted here and not yet on RomM is not in RomM's answer; its
        // record is the Recent shelf's, the one kept on the drive, or asked
        // for (#476).
        for (const int id : order) {
            if (lib.favorites.size() >= 40) break;
            romm::Game asked;
            const romm::Game* g = nullptr;
            for (const auto& f : favs) if (f.id == id) { g = &f; break; }
            if (!g)
                for (const auto& r : lib.games) if (r.id == id) { g = &r; break; }
            if (!g && (romm::gameFromJson(cache::gameRecord(id), &asked) ||
                       client.fetchGame(id, &asked, nullptr)))
                g = &asked;
            if (!g || !catalog::playable(*g)) continue;
            const romm::Game game = *g;   // appendGame grows lib.games
            lib.favorites.push_back(appendGame(lib, game));
        }
        std::fprintf(stderr, "[library] %zu favourites\n", lib.favorites.size());
    }

    int withArt = 0;
    for (const auto& c : lib.cards) if (!c.cover.empty()) ++withArt;
    // NOT "playable games" any more, and the wording matters: this is what the
    // console is holding, which is Home's worth of it. The library's size is
    // the server's business now.
    std::fprintf(stderr, "[library] %zu game(s) in hand, %d with art; "
                 "%zu platform tile(s), %d games on systems this console cannot play\n",
                 lib.cards.size(), withArt, lib.platformTiles.size(), skippedGames);

    if (lib.heroIndex >= 0)
        std::fprintf(stderr, "[library] resume: %s (%s), %zu on the Recent shelf\n",
                     lib.cards[lib.heroIndex].title.c_str(), lib.heroPlatform.c_str(),
                     lib.shelf.size());
    else
        std::fprintf(stderr, "[library] nothing recent is playable here\n");
    return lib;
}

// THE LIBRARY WITH NO SERVER (#88): the games on the drive, downloaded or
// cached, whoever got them there. MMagTech, 2026-10-05: offline is the normal
// console with fewer games. So Recent is the games on the drive played most
// recently by anyone (the entry's own time, a launch touches it), capped as
// the server's is; the Library has a tile per system with games on the drive,
// each holding only those; there are no favourites or collections, which are
// the server's. A game with no record cannot be shown (its folder carries no
// platform slug to launch it with); while online the console fetches the
// missing ones (backfillDrive), so that is only a game never seen online.
Library offlineLibrary() {
    Library lib;
    const Uint64 t0 = SDL_GetTicksNS();
    const std::vector<cache::OnDrive> drive = cache::onDrive();
    std::map<int, std::vector<int>> byPlatform;   // platform id -> cards
    std::map<int, std::string> platformTitle;
    const std::map<int, romm::Platform> known = savedPlatforms();
    int noRecord = 0, notHere = 0;
    for (const cache::OnDrive& d : drive) {
        romm::Game g;
        if (d.record.empty() || !romm::gameFromJson(d.record, &g)) { ++noRecord; continue; }
        if (!catalog::playable(g)) { ++notHere; continue; }
        const int idx = appendGame(lib, g);
        if (lib.shelf.size() < 16) lib.shelf.push_back(idx);   // as many as fetchRecent asks for
        if (lib.heroIndex < 0) {
            lib.heroIndex = idx;
            lib.heroPlatform = g.platformName;
        }
        byPlatform[g.platformId].push_back(idx);
        platformTitle[g.platformId] = g.platformName;
        lib.platformNames[g.platformId] = g.platformName;
    }
    // FAVORITES: this person's, as last seen online, that are on the drive,
    // in their order (MMagTech, 2026-10-05). None on the drive: no shelf, as
    // online with none.
    if (const storage::User& who = storage::currentUser(); who.valid())
        for (const int id : favoriteList(who))
            if (auto it = lib.byRomId.find(id); it != lib.byRomId.end())
                lib.favorites.push_back(it->second);
    for (auto& [pid, idxs] : byPlatform) {
        screens::Tile tile;
        tile.id = pid;
        tile.query = "platform_ids=" + std::to_string(pid);
        tile.title = platformTitle[pid];
        std::string qualifier;
        if (auto k = known.find(pid); k != known.end()) {
            // The same name and qualifier the online tile has (loadLibrary).
            if (!k->second.name.empty()) tile.title = k->second.name;
            qualifier = catalog::displayQualifier(k->second);
            lib.platformNames[pid] = catalog::displayName(k->second);
        }
        tile.enterable = true;
        // RomM's order, by its sort key where the record has one, so the
        // offline grid matches the online one and the letter index agrees.
        auto orderOf = [&](int i) -> const std::string& {
            const Card& c = lib.cards[static_cast<size_t>(i)];
            return c.sortKey.empty() ? c.title : c.sortKey;
        };
        std::sort(idxs.begin(), idxs.end(), [&](int a, int b) {
            const std::string& x = orderOf(a);
            const std::string& y = orderOf(b);
            return std::lexicographical_compare(
                x.begin(), x.end(), y.begin(), y.end(), [](unsigned char p, unsigned char q) {
                    return std::tolower(p) < std::tolower(q);
                });
        });
        tile.cards = idxs;
        for (int i : idxs)
            if (!lib.cards[static_cast<size_t>(i)].cover.empty()) {
                tile.cover = lib.cards[static_cast<size_t>(i)].cover;
                break;
            }
        char count[48];
        std::snprintf(count, sizeof count, "%zu game%s", idxs.size(), idxs.size() == 1 ? "" : "s");
        tile.detail = qualifier.empty() ? std::string(count)
                                        : qualifier + "  \xC2\xB7  " + count;
        lib.platformTiles.push_back(std::move(tile));
    }
    std::stable_sort(lib.platformTiles.begin(), lib.platformTiles.end(),
                     [](const screens::Tile& a, const screens::Tile& b) {
                         return std::lexicographical_compare(
                             a.title.begin(), a.title.end(), b.title.begin(), b.title.end(),
                             [](unsigned char x, unsigned char y) {
                                 return std::tolower(x) < std::tolower(y);
                             });
                     });
    std::fprintf(stderr,
                 "[offline] %zu game(s) on the drive: %zu shown, %d with no record, %d not "
                 "playable here; %zu system(s), %zu favourite(s); %.1f ms\n",
                 drive.size(), lib.cards.size(), noRecord, notHere, lib.platformTiles.size(),
                 lib.favorites.size(), (SDL_GetTicksNS() - t0) / 1e6);
    return lib;
}

// A kept game's record: the whole library entry, not a subset.
//
// Cabinet's KeptGame embedded the entire Rom captured at keep time so that a kept
// game can be browsed and launched with NO NETWORK AT ALL — cover path and
// platform identifiers included. A subset is how that promise gets broken later
// by a field nobody thought of, so this writes every field this client knows a
// Rom to have, and the day the struct grows, so does the record.
std::string gameRecordJson(const romm::Game& g) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "id", json_object_new_int(g.id));
    json_object_object_add(o, "platform_id", json_object_new_int(g.platformId));
    json_object_object_add(o, "platform_slug", json_object_new_string(g.platformSlug.c_str()));
    json_object_object_add(o, "platform_fs_slug", json_object_new_string(g.platformFsSlug.c_str()));
    json_object_object_add(o, "platform_name", json_object_new_string(g.platformName.c_str()));
    json_object_object_add(o, "name", json_object_new_string(g.name.c_str()));
    json_object_object_add(o, "fs_name", json_object_new_string(g.fsName.c_str()));
    json_object_object_add(o, "name_sort_key", json_object_new_string(g.sortKey.c_str()));
    json_object_object_add(o, "path_cover_small", json_object_new_string(g.coverPath.c_str()));
    json_object_object_add(o, "path_cover_large", json_object_new_string(g.coverLargePath.c_str()));
    json_object_object_add(o, "title_id", json_object_new_string(g.titleId.c_str()));
    // Not RomM's: read off a Wii U game's file online, and needed offline to
    // tell which controllers it takes (romm::gameFromJson).
    json_object_object_add(o, "product_code", json_object_new_string(g.productCode.c_str()));
    json_object_object_add(o, "ra_hash", json_object_new_string(g.raHash.c_str()));
    json_object_object_add(o, "md5_hash", json_object_new_string(g.md5.c_str()));
    json_object_object_add(o, "path_manual", json_object_new_string(g.manualPath.c_str()));
    json_object_object_add(o, "fs_size_bytes", json_object_new_int64(g.sizeBytes));
    json_object_object_add(o, "has_multiple_files", json_object_new_boolean(g.multiFile));
    // RomM's tags (#383), so an offline heading can still tell two copies
    // apart. A record kept before this has none until the game is next played.
    auto strings = [&](const char* key, const std::vector<std::string>& v) {
        json_object* a = json_object_new_array();
        for (const std::string& x : v) json_object_array_add(a, json_object_new_string(x.c_str()));
        json_object_object_add(o, key, a);
    };
    strings("regions", g.regions);
    strings("languages", g.languages);
    json_object_object_add(o, "revision", json_object_new_string(g.revision.c_str()));
    strings("tags", g.tags);
    const std::string out = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    return out;
}

// ---- The kept badges on the store's cards (#398) --------------------------
// A member of the App (app.h), moved out of main.cpp in batch (d).

// WHICH GAMES ARE KEPT, ASKED ONCE AND NOT PER FRAME. `cache::keptRoms`
// reads the keep directory; `cache::isKeptBy` is a stat() per game, and
// twelve covers at sixty frames a second would be seven hundred stats a
// second to draw a dot. So the answer is pushed onto the cards whenever it
// can have changed: at startup, and after a download or a removal.
void App::refreshKeeps() {
    const storage::User me = storage::currentUser();
    if (!me.valid()) return;
    const std::vector<int> kept = cache::keptRoms(me);
    for (Card& c : cards) c.kept = false;
    for (int romId : kept)
        for (Card& c : cards)
            if (c.id == romId) { c.kept = true; break; }
}
