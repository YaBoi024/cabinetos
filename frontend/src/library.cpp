#include "library.h"

#include <SDL3/SDL.h>
#include <json-c/json.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <utility>

#include "app.h"
#include "arcadematch.h"
#include "cache.h"
#include "catalog.h"
#include "favorites.h"
#include "platform.h"
#include "storage.h"
#include "timewords.h"
#include "wii.h"

using namespace design;   // NOLINT: Card and colorForTitle (card.h)

// As many as Home's Favorites row holds.
constexpr size_t kFavoriteGames = 40;

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

// What RomM says of a game, onto its card: everything but the colour stand-in,
// which is the first name's and stays.
static void dress(Card& c, const romm::Game& g) {
    c.title = g.name.empty() ? g.fsName : g.name;
    c.sortKey = g.sortKey;
    c.platform = catalog::systemName(g);
    c.folder = catalog::folderName(g);
    c.platformId = g.platformId;
    c.wii = g.platformSlug == "wii";
    c.needsRemote = c.wii && wii::padDevice(wii::codeFromTitleId(g.titleId)) == 0;
    c.tagLabel = g.tagLabel;
    c.unavailable = greyedReason(g);
    if (c.cover != g.coverPath) c.coverWas = std::exchange(c.cover, g.coverPath);
    if (c.coverLarge != g.coverLargePath)
        c.coverLargeWas = std::exchange(c.coverLarge, g.coverLargePath);
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
        // its grid is read (#387) or Home is (#730), not after a restart. The
        // old cover stays until the new one is in (Card::coverWas).
        const size_t i = static_cast<size_t>(it->second);
        dress(lib.cards[i], g);
        if (i < lib.games.size()) lib.games[i] = g;
        return it->second;
    }
    Card c;
    c.id = g.id;
    dress(c, g);
    c.art = colorForTitle(c.title);
    c.kept = lib.kept.count(g.id) > 0;
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
    std::vector<romm::Game> games;
    std::string err;
    int total = 0;
    if (!readFirstPage(client, tile.query, &games, &total, &err)) {
        std::fprintf(stderr, "[library] %s: %s\n", tile.title.c_str(), err.c_str());
        return false;
    }
    takeFirstPage(lib, tile, std::move(games), total);
    return true;
}

bool readFirstPage(romm::Client& client, const std::string& filter, std::vector<romm::Game>* games,
                   int* total, std::string* err) {
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
    // ONLY THE FIRST PAGE, AND THE CALLER DRAWS IT. The rest is somebody
    // else's job — see GridFill. A grid used to fetch the whole platform
    // before anything appeared, which is fine at 141 games and is ten seconds
    // for a full MAME set in one platform.
    // fetchRoms writes the limit itself, so only the offset rides on the
    // filter.
    return client.fetchRoms(filter, kPage, games, err, total);
}

void takeFirstPage(Library& lib, screens::Tile& tile, std::vector<romm::Game> games, int total) {
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

// A tile's query for one or more platforms: "platform_ids=45&platform_ids=55",
// each once, in id order so the same platforms always make the same tile.
std::string platformsQuery(std::vector<int> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::string q;
    for (const int id : ids) q += (q.empty() ? "" : "&") + ("platform_ids=" + std::to_string(id));
    return q;
}

// The platforms behind each Library tile: one each, except EVERY ARCADE
// FOLDER, which is one tile (#611, arcadematch::isArcadeSlug). At the TV a
// player picks a game, not a board, and RomM keeps the folders admins need.
// The arcade platforms are in id order, so the tile's id is the first.
//
// RomM LISTS SEVERAL PLATFORMS IN ONE REQUEST, in its title order across all
// of them and paged as one list, so the Arcade tile's grid, its pages and its
// letter index are any platform's (RomM 5.4.0, 2026-10-29: 853 games from 14
// folders, the first page as quick as FinalBurn Neo's 432 alone). Each game
// keeps its own platform for everything else: its emulator, BIOS, saves and
// states.
std::vector<std::vector<romm::Platform>> tileGroups(const std::vector<romm::Platform>& platforms) {
    std::vector<std::vector<romm::Platform>> out;
    std::vector<romm::Platform> arcade;
    for (const romm::Platform& p : platforms) {
        if (arcadematch::isArcadeSlug(p.slug)) arcade.push_back(p);
        else out.push_back({p});
    }
    if (!arcade.empty()) {
        std::sort(arcade.begin(), arcade.end(),
                  [](const romm::Platform& a, const romm::Platform& b) { return a.id < b.id; });
        out.push_back(std::move(arcade));
    }
    return out;
}

// What a tile learned since it was made, onto the one RomM's newer answer
// makes: its games, read when somebody walked in, and where the server's list
// had got to; a cover RomM gave none of; and a line the grid corrected, while
// the count it was written from has not moved.
static void carryTile(screens::Tile& now, const screens::Tile& was) {
    if (now.query == was.query) {
        // The Arcade tile's list is of the folders its query named: with
        // another folder it is read again, whole, as it is next opened.
        now.cards = was.cards;
        now.loadedAtMs = was.loadedAtMs;
        now.fetched = was.fetched;
        now.total = was.total;
        now.readId = was.readId;
    }
    now.coverAsked = was.coverAsked;
    now.coverWas = was.coverWas;
    if (now.cover.empty()) now.cover = was.cover;
    else if (now.cover != was.cover) now.coverWas = was.cover;
    if (now.enterable && was.enterable && now.counted == was.counted) now.detail = was.detail;
}

static const screens::Tile* tileLike(const std::vector<screens::Tile>& in, const screens::Tile& t) {
    for (const screens::Tile& x : in)
        if (screens::sameTile(x, t)) return &x;
    return nullptr;
}

// The Library's platform tiles, from RomM's list in hand (lib.platforms).
//
// THE PLATFORMS THIS CONSOLE CANNOT PLAY ARE LEFT OUT, and that is a decision
// rather than an oversight — see catalog.h. A console must not offer a game it
// cannot run; the moment to discover that is not after someone has chosen it.
// What it must not do is drop them silently, so every exclusion is reported on
// stderr with its reason, once, as the library loads (`say`).
static void buildPlatformTiles(Library& lib, bool say) {
    const std::vector<screens::Tile> was = std::move(lib.platformTiles);
    const std::map<int, covercache::Tile> cacheWas = std::move(lib.tileCache);
    lib.platformTiles.clear();
    lib.tileCache.clear();
    lib.platformNames.clear();
    for (const romm::Platform& p : lib.platforms) lib.platformNames[p.id] = catalog::displayName(p);
    // A tile per platform, or for every arcade folder together (tileGroups),
    // built whether or not this console can play it. The
    // unplayable ones are why `coverageFor` gives four different answers rather
    // than one boolean: "no core exists", "Cabinet did not ship it", "this
    // console has not built it yet" and "it is built and cannot be driven" lead
    // to different work and to different words on the screen. Every arcade
    // folder has the same answer (catalog::coverageFor).
    for (const std::vector<romm::Platform>& group : tileGroups(lib.platforms)) {
        const romm::Platform& p = group.front();
        const catalog::Coverage cov = catalog::coverageFor(p);
        screens::Tile tile;
        tile.id = p.id;
        std::vector<int> ids;
        int romCount = 0;
        std::string updatedAt;
        for (const romm::Platform& m : group) {
            ids.push_back(m.id);
            romCount += m.romCount;
            // The newest, so a change to any one folder asks for the
            // tile's cover again (covercache.h).
            updatedAt = std::max(updatedAt, m.updatedAt);
        }
        // FORK (handheld): NO TILE FOR A SYSTEM WITH NO GAMES, playable or not.
        // A folder on the server with nothing in it is not a place to go; it
        // shows again the moment a game is added and the library reloads. The
        // Arcade tile counts every arcade folder, so it goes only when all are empty.
        if (romCount == 0) continue;
        tile.query = platformsQuery(ids);
        tile.slug = group.front().slug;
        tile.title = catalog::displayName(p);
        tile.namesFolder = arcadematch::isArcadeSlug(p.slug);
                // FORK (handheld): and a system with no core yet opens on its greyed games.
        tile.enterable = (cov.support == catalog::Support::Playable) ||
                         (cov.support == catalog::Support::NotInstalled &&
                          platform::showsGamesWithoutACore());
        const screens::Tile* before = tileLike(was, tile);

        if (!tile.enterable) {
            // The tile gets the short form, which is all it has room for. The
            // log keeps the full sentence, because a log is read by somebody
            // trying to find out why.
            tile.detail = catalog::shortReason(cov);
            if (say)
                std::fprintf(stderr, "[library] %s (%d games) — %s\n", tile.title.c_str(),
                             romCount, cov.reason ? cov.reason : tile.detail.c_str());
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
            // FORK (phones): nor one whose emulator this program will never have.
            if (cov.support == catalog::Support::NotInstalled && platform::hidesSystemsWithoutACore())
                continue;
            if (before) carryTile(tile, *before);
        }

        // THE COUNT COMES OFF THE PLATFORM, which is the finding that made all
        // of this cheap: it was there the whole time, and boot was fetching
        // sixteen hundred games to draw a number the server had already sent.
        //
        // EXCEPT WHERE SOME OF A PLATFORM'S GAMES ARE HIDDEN (the 3DS's CIAs,
        // catalog::hidesSomeGames), which RomM's count includes: that
        // platform's list is read and what shows is counted (readLibrary), so
        // a server whose 3DS games are all CIAs gets "0 games", as a platform
        // with none does.
        int shown = romCount;
        if (catalog::hidesSomeGames(p) && shown > 0) {
            std::vector<romm::Game> games;
            std::string gerr;
            if (client.fetchGames(p.id, &games, &gerr)) {
                shown = 0;
                for (const auto& g : games)
                    if (catalog::coverageFor(g).support != catalog::Support::Hidden) ++shown;
            }
        }
        // FORK (handheld): all of its games hidden (the 3DS's CIAs): no tile.
        if (shown == 0) continue;
        if (catalog::hidesSomeGames(p) && shown > 0)
            if (const auto c = lib.counted.find(p.id); c != lib.counted.end())
                shown = c->second.shown;
        char count[48];
        std::snprintf(count, sizeof count, "%d game%s", shown, shown == 1 ? "" : "s");
        tile.detail = count;
        tile.counted = shown;
        // `tile.cover` is deliberately left empty. It used to be the first
        // cover among the platform's games, which cost a fetch per platform —
        // 1.55 s of a 1.8 s boot, measured against the reference server. The
        // tiles come up in their colour and the covers are filled in behind
        // Home; see App::startCoverFill (coverfill.cpp). MMagTech chose that
        // over both paying for them at boot and using RomM's platform logo,
        // 2026-09-22.
        if (before) carryTile(tile, *before);
        // A PLATFORM THAT CHANGED is asked for its cover again at the next
        // start (covercache.h), so its row forgets the one it has; now, only
        // when its tile has none.
        covercache::Tile row{"", updatedAt, shown};
        if (const auto c = cacheWas.find(p.id); c != cacheWas.end()) {
            if (c->second.updatedAt == updatedAt && c->second.romCount == shown) row = c->second;
            else tile.coverAsked = false;
        }
        lib.tileCache[p.id] = row;
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
}

// The collections' tiles, from RomM's two lists in hand. The membership used
// to be resolved against a catalogue that was already in memory — "no second
// request", said the comment, and that was true only because boot had paid for
// all of them. It is one request now, made when the collection is opened,
// exactly like a platform.
//
// SMART COLLECTIONS BESIDE THE REGULAR ONES (#352), read with this person's
// token, so an account switch shows the new person's own and everyone's
// public ones, as RomM decides (romm_types.h, Collection).
//
// A FAVORITES IS PERSONAL, AND OTHER PEOPLE'S ARE GROUPED BY WHOSE
// (MMagTech, 2026-10-12, #352). RomM lists other accounts' public
// collections, their Favorites included, so a parent's public
// Favorites showed on a child's account beside the child's own: two
// tiles reading "Favorites". Only this person's own is shown. Then:
// their own first, Favorites at the front, the rest A to Z, regular
// and smart together (how a collection was made is not what it is
// for); then each other person's shared ones under "From <name>",
// people A to Z and each A to Z. If the server would not say who this
// person is (lib.me), it is one A to Z list.
static void buildCollectionTiles(Library& lib) {
    const std::vector<screens::Tile> was = std::move(lib.collectionTiles);
    lib.collectionTiles.clear();
    const int me = lib.me;
    std::vector<const romm::Collection*> collections;
    for (const auto* list : {&lib.collections, &lib.smartCollections})
        for (const romm::Collection& c : *list) collections.push_back(&c);
    auto lower = [](std::string t) {
        for (char& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return t;
    };
    auto mine = [me](const romm::Collection& c) { return me == 0 || c.userId == me; };
    std::erase_if(collections,
                  [&](const romm::Collection* c) { return c->isFavorite && !mine(*c); });
    std::stable_sort(collections.begin(), collections.end(),
                     [&](const romm::Collection* x, const romm::Collection* y) {
                         const romm::Collection &a = *x, &b = *y;
                         if (mine(a) != mine(b)) return mine(a);
                         if (!mine(a) && a.userId != b.userId) {
                             const std::string oa = lower(a.ownerName), ob = lower(b.ownerName);
                             return oa != ob ? oa < ob : a.userId < b.userId;
                         }
                         if (a.isFavorite != b.isFavorite) return a.isFavorite;
                         return lower(a.name) < lower(b.name);
                     });
    for (const romm::Collection* shown : collections) {
        const romm::Collection& col = *shown;
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
        tile.counted = col.romCount;
        // The server's count, not how many are playable here — that is not
        // known until it is opened, and loadTileGames rewrites the line to
        // "4 of 12" at that point if the two differ.
        tile.enterable = col.romCount > 0;
        if (const screens::Tile* before = tileLike(was, tile)) {
            // A PICTURE STAYS WHILE RomM STILL LISTS IT: a collection with no
            // art of its own has its games' covers, in no order (romm_types.h),
            // and one picked again each time would change by itself.
            auto path = [](std::string_view c) { return c.substr(0, c.find('?')); };
            const std::string_view had = path(before->cover);
            if (std::any_of(col.covers.begin(), col.covers.end(),
                            [&](const std::string& c) { return path(c) == had; }))
                tile.cover = before->cover;
            carryTile(tile, *before);
        }
        lib.collectionTiles.push_back(std::move(tile));
    }
}

// Recent and the hero, from the server's play history rather than from
// anything this console remembers. A game played on another device is recent
// here the moment this console is paired, which is what lets a machine that
// has never launched anything still open on the right game.
//
// THE GAMES COME STRAIGHT OFF RomM's ANSWER. They used to be looked up in
// the catalogue and DROPPED IF NOT FOUND — which, once boot stopped
// fetching the catalogue, would have quietly emptied Home while every one
// of these calls still succeeded. That is the trap in this change and it
// is why the store is filled from here rather than searched.
//
// AND A GAME PLAYED HERE goes by whichever is later, RomM's time for it or
// when it was left here (#730): one left within a minute is never sent to RomM
// (playtime::kShortestMs), and one just left may still be on its way. Once
// RomM has the time, its history is the word on that game again, so one taken
// off RomM's Continue Playing goes (LibraryRead::settled), and so does one
// RomM no longer has for this person (LibraryRead::gone).
static void buildRecent(Library& lib, const LibraryRead& read) {
    for (const auto& [id, at] : read.settled)
        if (const auto p = lib.playedHere.find(id); p != lib.playedHere.end() && p->second.at == at)
            lib.playedHere.erase(p);
    for (const int id : read.gone) lib.playedHere.erase(id);

    std::vector<std::pair<int64_t, int>> recent;   // when, and which card
    for (const romm::Game& g : read.recent) {
        const int64_t at = isoMicros(g.lastPlayed) / 1000000;
        if (const auto p = lib.playedHere.find(g.id); p != lib.playedHere.end() && at >= p->second.at)
            lib.playedHere.erase(p);
        // A recent game on a platform this console cannot play is skipped,
        // not shown greyed: Home must not offer a Resume that cannot run.
        // The reference library exercises this — the most recent "Altered
        // Beast" is the Game & Watch one, which this console leaves out.
        if (!catalog::playable(g)) continue;
        recent.emplace_back(at, appendGame(lib, g));
    }
    std::vector<std::pair<int64_t, int>> here;
    for (const auto& [id, p] : lib.playedHere)
        if (const auto it = lib.byRomId.find(id);
            it != lib.byRomId.end() && catalog::playable(lib.games[static_cast<size_t>(it->second)]))
            here.emplace_back(p.at, it->second);
    std::sort(here.begin(), here.end());
    for (const auto& h : here) {
        std::erase_if(recent, [&](const auto& r) { return r.second == h.second; });
        // RomM's order is kept: in front of the first game played before it.
        auto to = recent.begin();
        while (to != recent.end() && to->first > h.first) ++to;
        recent.insert(to, h);
    }
    lib.shelf.clear();
    for (const auto& r : recent)
        if (lib.shelf.size() < kRecentGames) lib.shelf.push_back(r.second);
    // THE MOST RECENT ONE STAYS ON THE SHELF — changed 2026-09-21.
    //
    // `heroIndex` is what Home focuses on when it opens, which is the
    // whole of "resume-first". It is an index into the shelf's first
    // slot rather than a separate object on the screen.
    lib.heroIndex = lib.shelf.empty() ? -1 : lib.shelf.front();
    lib.heroPlatform =
        lib.heroIndex < 0 ? "" : lib.games[static_cast<size_t>(lib.heroIndex)].platformName;
}

// Favorites: which games RomM says, in the console's order (favorites.cpp,
// keepFavorites): newest first as far as it knows, with what the server has
// not heard yet winning (#267): a favourite taken off while it was away stays
// off, one added stays on, until sent. Kept for the offline Home (#88). A
// heart pressed while RomM was being asked wins too, sent or not.
//
// Home's shelf holds the first 40 this console can play.
static void buildFavorites(Library& lib, const LibraryRead& read, const storage::User& who) {
    std::vector<int> order = read.favorites;
    for (const auto& [id, on] : read.hearts) {
        std::erase(order, id);
        if (on) order.insert(order.begin(), id);
    }
    if (who.valid()) order = keepFavorites(who, order);
    lib.favorites.clear();
    for (const int id : order) {
        if (lib.favorites.size() >= kFavoriteGames) break;
        const auto it = lib.byRomId.find(id);
        if (it != lib.byRomId.end()) {
            if (catalog::playable(lib.games[static_cast<size_t>(it->second)]))
                lib.favorites.push_back(it->second);
            continue;
        }
        for (const romm::Game& g : read.games)
            if (g.id == id && catalog::playable(g)) {
                lib.favorites.push_back(appendGame(lib, g));
                break;
            }
    }
}

LibraryAsk askAgain(const Library& lib, const storage::User& who) {
    LibraryAsk ask;
    ask.who = who;
    ask.whole = false;
    ask.since = lib.newestUpdate;
    ask.counted = lib.counted;
    for (const auto& [id, idx] : lib.byRomId) ask.inHand.insert(id);
    for (const auto& [id, p] : lib.playedHere) ask.playedHere.push_back(id);
    return ask;
}

// What the console knows the moment it finishes booting, and what it asks for
// again while it is on.
//
// SMALL CALLS, AND NOT ONE OF THEM IS THE CATALOGUE — open question 28. This
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
//
// AGAIN (#730) IT IS FIVE CALLS: the platforms, the two lists of collections,
// the last sixteen played, and the favourites RomM changed since the last time
// it was asked, which is usually none. Which games are favourites is on the
// person's own Favorites collection, so their entries (23 KB each) are not
// asked for again. What more it asks for is what it has none of: a new
// favourite's entry, a count of a platform that hides some of its games when
// RomM's moved, and a game played here that RomM's history does not have.
LibraryRead readLibrary(romm::Client& client, const LibraryAsk& ask, const std::atomic<bool>* stop) {
    LibraryRead r;
    r.whole = ask.whole;
    r.me = ask.whole ? 0 : ask.who.id;
    std::string err;
    auto newer = [&](const std::string& at) { if (at > r.newest) r.newest = at; };
    auto failed = [&](const char* part) {
        r.failed += (r.failed.empty() ? "" : "; ") + std::string(part) + ": " + err;
        std::string().swap(err);
    };
    // Nothing more is asked of a server that has gone, nor by a read nobody
    // wants any more.
    auto over = [&] { return (stop && stop->load()) || (!ask.whole && romm::serverAway()); };

    r.gotPlatforms = client.fetchPlatforms(&r.platforms, &err);
    if (!r.gotPlatforms) {
        r.platformsErr = err;
        failed("platforms");
        if (ask.whole || over()) return r;
    }
    r.reached = true;
    for (const romm::Platform& p : r.platforms) {
        newer(p.updatedAt);
        if (!catalog::hidesSomeGames(p) || p.romCount <= 0 || !catalog::playable(p)) continue;
        // One request, only for such a platform, and again only when RomM's
        // count of it moved.
        if (const auto c = ask.counted.find(p.id);
            c != ask.counted.end() && c->second.listed == p.romCount)
            continue;
        if (over()) return r;
        std::vector<romm::Game> games;
        if (!client.fetchGames(p.id, &games, &err)) { failed("a system's games"); continue; }
        Counted c{p.romCount, 0};
        for (const romm::Game& g : games)
            if (catalog::coverageFor(g).support != catalog::Support::Hidden) ++c.shown;
        r.counted[p.id] = c;
    }

    if (over()) return r;
    r.gotSmart = client.fetchSmartCollections(&r.smartCollections, &err);
    if (!r.gotSmart) failed("smart collections");
    if (over()) return r;
    r.gotCollections = client.fetchCollections(&r.collections, &err);
    // Not fatal. A server that will not list collections still has a
    // library, and the switcher's Collections tab says it is empty.
    if (!r.gotCollections) failed("collections");
    // Who "this person" is costs one request, at start only: a library in hand
    // is whose it is.
    if (ask.whole && !(r.collections.empty() && r.smartCollections.empty())) {
        romm::User u;
        if (client.fetchCurrentUser(&u, &err)) r.me = u.id;
        else failed("who is this");
    }
    for (const auto* list : {&r.collections, &r.smartCollections})
        for (const romm::Collection& c : *list) newer(c.updatedAt);

    // TIMED, because once on the A9 these two took eleven seconds during an
    // account switch when they take a fifth of one at boot, and nothing else
    // in the log said which.
    if (over()) return r;
    const Uint64 tRecent = SDL_GetTicks();
    r.gotRecent = client.fetchRecent(static_cast<int>(kRecentGames), &r.recent, &err);
    if (ask.whole)
        std::fprintf(stderr, "[library] play history in %llu ms\n",
                     static_cast<unsigned long long>(SDL_GetTicks() - tRecent));
    if (!r.gotRecent) failed("play history");
    for (const romm::Game& g : r.recent) newer(g.updatedAt);

    // ALL OF THE FAVOURITES ARE ASKED FOR, up to 200, not the shelf's 40
    // (#476): RomM lists favourites by title, so asking for 40 left a person
    // with more never seeing the rest, and a new one past the fortieth title
    // never reached Home. About 23 KB each on RomM 5.3.1. Again, only the ones
    // RomM changed since: a name, a cover or a manual.
    if (over()) return r;
    const Uint64 tFavs = SDL_GetTicks();
    r.gotFavoriteEntries = client.fetchFavorites(200, &r.games, &err, ask.whole ? "" : ask.since);
    if (ask.whole)
        std::fprintf(stderr, "[library] favourites in %llu ms\n",
                     static_cast<unsigned long long>(SDL_GetTicks() - tFavs));
    if (!r.gotFavoriteEntries) failed("favourites");
    for (const romm::Game& g : r.games) newer(g.updatedAt);

    // WHICH GAMES ARE FAVOURITES: at start, the ones that came, in RomM's
    // title order, then any past the 200; again, the person's own Favorites
    // collection, and none for somebody who has none.
    if (ask.whole)
        for (const romm::Game& g : r.games) r.favorites.push_back(g.id);
    r.gotFavorites = ask.whole ? r.gotFavoriteEntries : r.gotCollections;
    if (r.gotFavorites && r.me > 0)
        for (const romm::Collection& c : r.collections)
            if (c.isFavorite && c.userId == r.me)
                for (const int id : c.romIds)
                    if (std::find(r.favorites.begin(), r.favorites.end(), id) == r.favorites.end())
                        r.favorites.push_back(id);

    // THE ENTRIES STILL MISSING FROM HOME'S SHELF: a game hearted here and not
    // yet on RomM, or one hearted elsewhere since. Its entry is Recent's, the
    // one kept on the drive, or asked for (#476).
    auto inHand = [&](int id) -> const romm::Game* {
        for (const auto* list : {&r.games, &r.recent})
            for (const romm::Game& g : *list)
                if (g.id == id) return &g;
        return nullptr;
    };
    size_t shown = 0;
    if (r.gotFavorites)
        for (const int id : ask.who.valid() ? favoriteOrder(ask.who, r.favorites) : r.favorites) {
            if (shown >= kFavoriteGames || over()) break;
            if (ask.inHand.count(id)) { ++shown; continue; }
            if (const romm::Game* g = inHand(id)) { shown += catalog::playable(*g); continue; }
            romm::Game g;
            if (romm::gameFromJson(cache::gameRecord(id), &g) || client.fetchGame(id, &g, nullptr)) {
                shown += catalog::playable(g);
                r.games.push_back(std::move(g));
            }
        }

    // A GAME PLAYED HERE THAT RomM's HISTORY DOES NOT HAVE is asked for by
    // itself, so its name and cover keep up too, and one RomM no longer has
    // for this person goes.
    if (r.gotRecent)
        for (const int id : ask.playedHere) {
            if (over()) break;
            if (std::any_of(r.recent.begin(), r.recent.end(),
                            [id](const romm::Game& g) { return g.id == id; }))
                continue;
            romm::Game g;
            bool notFound = false;
            if (client.fetchGame(id, &g, nullptr, &notFound)) r.games.push_back(std::move(g));
            else if (notFound) r.gone.insert(id);
        }
    return r;
}

// What is on a tile's face, for saying what changed.
static std::string tileFace(const screens::Tile& t) {
    return t.title + '\n' + t.detail + '\n' + t.group + '\n' + (t.enterable ? "1" : "0") + t.cover;
}

// "systems: new Saturn; gone Lynx; 2 changed", or nothing.
static void sayTiles(LibraryChanges& out, const char* what, const std::vector<screens::Tile>& was,
                     const std::vector<screens::Tile>& now) {
    std::string words;
    auto add = [&](const std::string& w) { words += (words.empty() ? "" : "; ") + w; };
    int changed = 0;
    for (const screens::Tile& t : now) {
        const screens::Tile* before = tileLike(was, t);
        if (!before) add("new " + t.title);
        else if (tileFace(*before) != tileFace(t)) ++changed;
    }
    for (const screens::Tile& t : was)
        if (!tileLike(now, t)) add("gone " + t.title);
    if (changed) add(std::to_string(changed) + " changed");
    if (!words.empty()) out.said.push_back(std::string(what) + ": " + words);
}

// BUILT IN PLACE, part by part (#730): the cards are only ever added to, so an
// index a grid, Search or a game's page holds stays the game it was, and a
// part RomM did not answer for keeps what was there. An empty library is the
// same: everything is new.
LibraryChanges putLibrary(Library& lib, LibraryRead read, const storage::User& who) {
    LibraryChanges out;
    const std::vector<int> recentWas = lib.shelf, favoritesWas = lib.favorites;
    const std::vector<screens::Tile> platformsWas = lib.platformTiles;
    const std::vector<screens::Tile> collectionsWas = lib.collectionTiles;
    std::vector<std::string> facesWas;
    auto face = [&](int i) {
        const Card& c = lib.cards[static_cast<size_t>(i)];
        return c.title + '\n' + c.cover + '\n' + c.coverLarge + '\n' + c.platform + '\n' +
               lib.games[static_cast<size_t>(i)].manualPath;
    };
    for (const auto* list : {&recentWas, &favoritesWas})
        for (const int i : *list) facesWas.push_back(face(i));

    if (read.gotPlatforms) {
        std::map<int, const romm::Platform*> had;
        for (const romm::Platform& p : lib.platforms) had[p.id] = &p;
        bool sameList = !read.whole && had.size() == read.platforms.size(), sameNames = true;
        for (const romm::Platform& p : read.platforms) {
            const auto h = had.find(p.id);
            if (h == had.end() || h->second->slug != p.slug || h->second->fsSlug != p.fsSlug)
                sameList = false;
            else if (h->second->name != p.name)
                sameList = sameNames = false;
        }
        lib.platforms = std::move(read.platforms);
        if (!sameList) savePlatforms(lib.platforms);
        for (const auto& [id, c] : read.counted) lib.counted[id] = c;
        buildPlatformTiles(lib, /*say=*/read.whole);
        // A SYSTEM RENAMED IN RomM is renamed on every card of its games: a
        // favourite's entry is asked for only when the game itself changed.
        if (!sameNames) {
            std::map<int, std::string> names;
            for (const romm::Platform& p : lib.platforms) names[p.id] = p.name;
            for (size_t i = 0; i < lib.games.size(); ++i) {
                const auto n = names.find(lib.games[i].platformId);
                if (n == names.end() || n->second == lib.games[i].platformName) continue;
                lib.games[i].platformName = n->second;
                dress(lib.cards[i], lib.games[i]);
            }
        }
    }

    if (read.gotCollections || read.gotSmart) {
        if (read.gotCollections) lib.collections = std::move(read.collections);
        if (read.gotSmart) lib.smartCollections = std::move(read.smartCollections);
        lib.me = read.me;
        buildCollectionTiles(lib);
    }

    // Entries of games already in hand; the rest are added only where a shelf
    // has a place for them.
    for (const romm::Game& g : read.games)
        if (lib.byRomId.count(g.id)) appendGame(lib, g);
    if (read.gotRecent) buildRecent(lib, read);
    if (read.gotFavorites) buildFavorites(lib, read, who);
    if (read.gotFavoriteEntries && read.newest > lib.newestUpdate) lib.newestUpdate = read.newest;

    if (lib.shelf != recentWas)
        out.said.push_back(lib.shelf.empty() ? "Recent is empty"
                           : recentWas.empty() || lib.shelf.front() != recentWas.front()
                               ? "Recent starts with " +
                                     lib.cards[static_cast<size_t>(lib.shelf.front())].title
                               : "Recent changed");
    if (lib.favorites != favoritesWas)
        out.said.push_back("Favorites: " + std::to_string(lib.favorites.size()));
    int changed = 0;
    size_t k = 0;
    std::set<int> seen;
    for (const auto* list : {&recentWas, &favoritesWas})
        for (const int i : *list)
            if (face(i) != facesWas[k++] && seen.insert(i).second) ++changed;
    if (changed) out.said.push_back(std::to_string(changed) + " game(s) on Home changed");
    sayTiles(out, "systems", platformsWas, lib.platformTiles);
    sayTiles(out, "collections", collectionsWas, lib.collectionTiles);
    return out;
}

Library loadLibrary(romm::Client& client, std::string* platformsErr) {
    Library lib;
    LibraryAsk ask;
    ask.who = storage::currentUser();
    LibraryRead read = readLibrary(client, ask);
    if (!read.failed.empty())
        std::fprintf(stderr, "[library] not read: %s\n", read.failed.c_str());
    if (!read.gotPlatforms) {
        if (platformsErr) *platformsErr = read.platformsErr;
        return lib;
    }
    putLibrary(lib, std::move(read), ask.who);

    int withArt = 0, skippedGames = 0;
    for (const auto& c : lib.cards) if (!c.cover.empty()) ++withArt;
    for (const romm::Platform& p : lib.platforms)
        if (!catalog::playable(p)) skippedGames += p.romCount;
    std::fprintf(stderr, "[library] %zu collection(s), %zu of them smart; %zu favourites\n",
                 lib.collectionTiles.size(), lib.smartCollections.size(), lib.favorites.size());
    // NOT "playable games" any more, and the wording matters: this is what the
    // console is holding, which is Home's worth of it. The library's size is
    // the server's business now.
    std::fprintf(stderr, "[library] %zu game(s) in hand, %d with art; "
                 "%zu platform tile(s), %d games on systems this console cannot play\n",
                 lib.cards.size(), withArt, lib.platformTiles.size(), skippedGames);

    if (lib.heroIndex >= 0)
        std::fprintf(stderr, "[library] resume: %s (%s), %zu on the Recent shelf\n",
                     lib.cards[static_cast<size_t>(lib.heroIndex)].title.c_str(),
                     lib.heroPlatform.c_str(), lib.shelf.size());
    else
        std::fprintf(stderr, "[library] nothing recent is playable here\n");
    return lib;
}

// THE LIBRARY WITH NO SERVER (#88): the games on the drive, downloaded or
// cached, whoever got them there. MMagTech, 2026-10-05: offline is the normal
// console with fewer games. So Recent is the games on the drive played most
// recently by anyone (the entry's own time, a launch touches it), capped as
// the server's is; the Library has a tile per system with games on the drive,
// each holding only those, and one Arcade tile for every arcade folder's, as
// online (tileGroups); there are no favourites or collections, which are
// the server's. A game with no record cannot be shown (its folder carries no
// platform slug to launch it with); while online the console fetches the
// missing ones (backfillDrive), so that is only a game never seen online.
Library offlineLibrary() {
    Library lib;
    const Uint64 t0 = SDL_GetTicksNS();
    const std::vector<cache::OnDrive> drive = cache::onDrive();
    // Platform id -> cards, with every arcade folder's under 0 (no platform's
    // id), the Arcade tile.
    std::map<int, std::vector<int>> byPlatform;
    std::map<int, std::string> platformTitle;
    const std::map<int, romm::Platform> known = savedPlatforms();
    // The Arcade tile names every arcade folder the server had, as its
    // online query does, so reading it again once the server is back
    // (App::openTile) lists them all; and any a game on the drive names.
    std::vector<int> arcadeIds;
    for (const auto& [id, p] : known)
        if (arcadematch::isArcadeSlug(p.slug)) arcadeIds.push_back(id);
    int noRecord = 0, notHere = 0;
    for (const cache::OnDrive& d : drive) {
        romm::Game g;
        if (d.record.empty() || !romm::gameFromJson(d.record, &g)) { ++noRecord; continue; }
        if (!catalog::playable(g)) { ++notHere; continue; }
        const int idx = appendGame(lib, g);
        if (lib.shelf.size() < kRecentGames) lib.shelf.push_back(idx);
        if (lib.heroIndex < 0) {
            lib.heroIndex = idx;
            lib.heroPlatform = g.platformName;
        }
        const bool arcade = arcadematch::isArcadeSlug(g.platformSlug);
        if (arcade) arcadeIds.push_back(g.platformId);
        const int key = arcade ? 0 : g.platformId;
        byPlatform[key].push_back(idx);
        platformTitle[key] = catalog::systemName(g);
        lib.platformNames[g.platformId] = catalog::systemName(g);
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
        if (pid == 0) {
            tile.query = platformsQuery(arcadeIds);
            tile.id = *std::min_element(arcadeIds.begin(), arcadeIds.end());
            tile.namesFolder = true;
        } else if (auto k = known.find(pid); k != known.end()) {
            // The same name the online tile has (loadLibrary).
            tile.title = catalog::displayName(k->second);
            lib.platformNames[pid] = tile.title;
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
        tile.detail = count;
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
    // What an arcade game's archive holds (romm_types.h), in RomM's own
    // field names, for its emulator to be picked offline. A game from
    // RomM's lists carries none, so the record keeps what a launch learned
    // (rememberArchive) rather than lose it at every press.
    romm::Game kept;
    const bool haveKept = romm::gameFromJson(cache::gameRecord(g.id), &kept);
    const romm::Game& from = !g.members.empty() || !haveKept ? g : kept;
    if (!from.members.empty()) {
        json_object* a = json_object_new_object();
        json_object_object_add(a, "file", json_object_new_string(from.membersFile.c_str()));
        json_object_object_add(a, "size", json_object_new_int64(from.membersSize));
        json_object* list = json_object_new_array();
        for (const romm::ArchiveMember& m : from.members) {
            json_object* e = json_object_new_object();
            char crc[9];
            std::snprintf(crc, sizeof crc, "%08x", m.crc);
            json_object_object_add(e, "name", json_object_new_string(m.name.c_str()));
            json_object_object_add(e, "size", json_object_new_int64(m.size));
            json_object_object_add(e, "crc_hash", json_object_new_string(crc));
            json_object_array_add(list, e);
        }
        json_object_object_add(a, "members", list);
        json_object_object_add(o, "archive_members", a);
    }
    // The files the game's save channels can be on (romm_types.h,
    // ChannelFiles), under RomM's own names: `channel_file_id`, and `files`
    // holding each game file's id. Only the game's own entry carries them, so
    // the record keeps what a launch or the game's page learned
    // (rememberChannelFiles).
    const romm::ChannelFiles& files =
        !g.channelFiles.files.empty() || !haveKept ? g.channelFiles : kept.channelFiles;
    if (const int file = g.channelFiles.channelFile != 0 ? g.channelFiles.channelFile
                                                         : kept.channelFiles.channelFile)
        json_object_object_add(o, "channel_file_id", json_object_new_int(file));
    if (!files.files.empty()) {
        json_object* list = json_object_new_array();
        for (const int id : files.files) {
            json_object* f = json_object_new_object();
            json_object_object_add(f, "id", json_object_new_int(id));
            json_object_array_add(list, f);
        }
        json_object_object_add(o, "files", list);
    }
    const std::string out = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    return out;
}

void rememberArchive(int romId, const std::string& file, int64_t size,
                     const std::vector<romm::ArchiveMember>& members) {
    romm::Game g;
    if (!romm::gameFromJson(cache::gameRecord(romId), &g)) return;
    if (g.membersFile == file && g.membersSize == size && g.members.size() == members.size())
        return;
    g.membersFile = file;
    g.membersSize = size;
    g.members = members;
    cache::rememberGame(romId, gameRecordJson(g));
}

std::vector<romm::ArchiveMember> rememberedArchive(int romId, const std::string& file,
                                                   int64_t size) {
    romm::Game g;
    if (!romm::gameFromJson(cache::gameRecord(romId), &g) || g.membersFile != file ||
        g.membersSize != size)
        return {};
    return g.members;
}

void rememberChannelFiles(int romId, const romm::ChannelFiles& files) {
    romm::Game g;
    if (!romm::gameFromJson(cache::gameRecord(romId), &g) ||
        (g.channelFiles.channelFile == files.channelFile && g.channelFiles.files == files.files))
        return;
    g.channelFiles = files;
    cache::rememberGame(romId, gameRecordJson(g));
}

romm::ChannelFiles rememberedChannelFiles(int romId) {
    romm::Game g;
    return romm::gameFromJson(cache::gameRecord(romId), &g) ? g.channelFiles
                                                            : romm::ChannelFiles{};
}

// ---- The kept badges on the store's cards (#398) --------------------------
// A member of the App (app.h), moved out of main.cpp in batch (d).

// WHICH GAMES ARE KEPT, ASKED ONCE AND NOT PER FRAME. `cache::keptRoms`
// reads the keep directory; `cache::isKeptBy` is a stat() per game, and
// twelve covers at sixty frames a second would be seven hundred stats a
// second to draw a dot. So the answer is pushed onto the cards whenever it
// can have changed: at startup, and after a download or a removal. And kept
// beside them for the cards added after it (appendGame): a game listed by a
// grid or Search had no dot until this ran again.
void App::refreshKeeps() {
    const storage::User me = storage::currentUser();
    if (!me.valid()) return;
    const std::vector<int> kept = cache::keptRoms(me);
    lib.kept = std::set<int>(kept.begin(), kept.end());
    for (Card& c : cards) c.kept = lib.kept.count(c.id) > 0;
}

// ---- --arcade-tile-test ----------------------------------------------------

int arcadeTileTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto platform = [](int id, const char* slug, const char* fs, const char* name) {
        romm::Platform p;
        p.id = id;
        p.slug = slug;
        p.fsSlug = fs;
        p.name = name;
        return p;
    };
    const std::vector<romm::Platform> server = {
        platform(8, "snes", "Super Nintendo", "Super Nintendo Entertainment System"),
        platform(55, "arcade", "FBNEO", "Arcade"),
        platform(58, "cps2", "CPS2", "Capcom Play System 2"),
        platform(14, "dc", "Sega Dreamcast", "Dreamcast"),
        platform(45, "arcade", "MAME2003", "Arcade"),
        platform(66, "neogeomvs", "NeoGeoMVS", "Neo Geo MVS"),
        platform(62, "model3", "Model3", "Sega Model 3"),
    };
    const auto groups = tileGroups(server);
    check(groups.size() == 2 + 1, "every arcade folder is one tile, every other platform its own");
    std::vector<int> arcade;
    for (const auto& g : groups)
        if (g.size() > 1)
            for (const romm::Platform& p : g) arcade.push_back(p.id);
    check(arcade == std::vector<int>({45, 55, 58, 62, 66}),
          "the Arcade tile holds every arcade label, RomM's and the boards', in id order");
    bool alone = true;
    for (const auto& g : groups)
        if (g.size() == 1 && arcadematch::isArcadeSlug(g[0].slug)) alone = false;
    check(alone, "and no arcade folder keeps a tile of its own");
    check(tileGroups({platform(55, "arcade", "FBNEO", "Arcade")}).size() == 1,
          "one arcade folder alone is the Arcade tile too");
    check(platformsQuery({58, 45, 55, 45}) ==
              "platform_ids=45&platform_ids=55&platform_ids=58",
          "the Arcade tile's query names each folder once, in id order");
    check(platformsQuery({5}) == "platform_ids=5", "and any other tile's is its one platform");
    check(catalog::displayName(server[2]) == "Arcade" &&
              catalog::displayName(server[1]) == "Arcade",
          "every arcade folder is called Arcade, whatever RomM names it");
    check(catalog::displayName(server[0]) == "Super Nintendo Entertainment System",
          "any other platform keeps RomM's name");

    romm::Game mvs;
    mvs.id = 1;
    mvs.name = "Metal Slug";
    mvs.fsName = "mslug.zip";
    mvs.platformId = 66;
    mvs.platformSlug = "neogeomvs";
    mvs.platformFsSlug = "NeoGeoMVS";
    mvs.platformName = "Neo Geo MVS";
    check(catalog::systemName(mvs) == "Arcade" && catalog::folderName(mvs) == "NeoGeoMVS",
          "an arcade game's system is Arcade and its folder is its RomM folder");
    romm::Game dcZip = mvs;
    dcZip.platformSlug = "dc";
    dcZip.platformFsSlug = "Sega Dreamcast";
    dcZip.platformName = "Dreamcast";
    dcZip.fsName = "ikaruga.zip";
    check(catalog::systemName(dcZip) == "Dreamcast",
          "a Naomi zip in Dreamcast's folder is a Dreamcast game");
    romm::Game bare = mvs;
    bare.platformFsSlug.clear();
    check(catalog::folderName(bare) == "Neo Geo MVS", "a game with no folder names RomM's name");
    Library lib;
    const Card& card = lib.cards[static_cast<size_t>(appendGame(lib, mvs))];
    check(card.platform == "Arcade" && card.folder == "NeoGeoMVS",
          "its card says Arcade where a list names the system, and keeps the folder for the "
          "Arcade tile's line");

    std::printf("%s\n", failures ? "arcade tile test: FAILED" : "arcade tile test: passed");
    return failures ? 1 : 0;
}
