// A platform's or collection's grid: opened on its first page, and the
// rest fetched behind it and added as it lands (#398, restructure batch e).

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

#include "catalog.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// Adds a filler's pages to its tile, and with `toScreen` to the grid on
// screen too. Applied here because this thread owns the store and the
// screen. Answers whether the filler has ended with nothing left to give.
bool App::takeGridPages(GridFill& f, bool toScreen) {
    std::vector<romm::Game> got;
    bool ended = false;
    {
        std::lock_guard<std::mutex> lk(f.m);
        got.swap(f.arrived);
        ended = !f.running;
    }
    screens::Tile* t = nullptr;
    for (auto& v : {&platformTiles, &collectionTiles})
        for (screens::Tile& x : *v)
            if (x.query == f.tileQuery && x.readId == f.readId) { t = &x; break; }
    if (!t) {
        // An older reading of the tile (#387), or a library replaced since
        // (another person, offline and back): its pages belong to nothing.
        std::lock_guard<std::mutex> lk(f.m);
        f.quit = true;
        return ended;
    }
    std::vector<int> added;
    added.reserve(got.size());
    for (const auto& g : got) {
        // COUNTED BEFORE IT IS KEPT OR LEFT OUT: the next page starts after
        // every game the server sent (#471, HOME-04).
        ++t->fetched;
        if (!catalog::playable(g) &&
            !catalog::shownGreyed(catalog::coverageFor(g).support))
            continue;
        const int i = appendGame(lib, g);
        // In the server's order, as the first page (gridOrder).
        t->cards.push_back(i);
        if (toScreen) added.push_back(i);
    }
    if (toScreen) gridScreen.append(added, cards);
    return ended;
}

// Every filler's pages, once a frame, on every screen: a tile left before
// its pages all arrived keeps them (#471, HOME-02). The open grid's wait
// while a game's page is in front of it, and go on the screen when it is
// back, as they always did.
void App::drainGridPages() {
    const bool shown = here() == Screen::Grid;
    const bool under = !shown && std::find(stack.begin(), stack.end(), Screen::Grid) != stack.end();
    bool loading = false;
    for (auto it = gridFills.begin(); it != gridFills.end();) {
        GridFill& f = **it;
        const bool open = f.tileQuery == gridTileQuery && f.readId == gridReadId;
        if (open && under) {
            loading = true;
            ++it;
            continue;
        }
        const bool ended = takeGridPages(f, open && shown);
        if (!ended) {
            loading = loading || open;
            ++it;
            continue;
        }
        if (f.th.joinable()) f.th.join();   // it has returned, so at once
        for (const auto* v : {&platformTiles, &collectionTiles})
            for (const screens::Tile& x : *v)
                if (x.query == f.tileQuery && x.readId == f.readId)
                    std::fprintf(stderr, "[library] %s: %zu of %zu from the server, %zu game(s)\n",
                                 x.title.c_str(), x.fetched, x.total, x.cards.size());
        it = gridFills.erase(it);
    }
    if (shown) gridScreen.setLoadingMore(loading);
}

// The rest of a tile's games, from where the server's list got to.
//
// ONE FILLER AT A TIME. Walking into a second tile stops the first after
// the page it is on: nobody is looking at it any more. Opening it again
// carries on from there, or keeps the same filler going if it has not
// ended yet, so no page is fetched twice.
void App::fillGrid(screens::Tile& t) {
    bool going = false;
    for (auto& f : gridFills) {
        std::lock_guard<std::mutex> lk(f->m);
        if (f->tileQuery == t.query && f->readId == t.readId && f->running) {
            f->quit = false;
            going = true;
        } else {
            f->quit = true;
        }
    }
    if (going || t.fetched >= t.total) return;
    auto f = std::make_unique<GridFill>();
    f->tileQuery = t.query;
    f->readId = t.readId;
    GridFill* fp = f.get();
    const std::string filter = t.query;
    const size_t from = t.fetched;
    fp->th = std::thread([this, fp, filter, from]() {
        constexpr int kPage = 500;
        size_t offset = from;
        for (;;) {
            std::vector<romm::Game> page;
            std::string err;
            int total = 0;
            const bool ok = liveClient.fetchRoms(
                filter + "&offset=" + std::to_string(offset), kPage, &page, &err, &total);
            if (!ok)
                std::fprintf(stderr, "[library] %s: page at %zu: %s\n", filter.c_str(),
                             offset, err.c_str());
            std::lock_guard<std::mutex> lk(fp->m);
            offset += page.size();
            for (auto& g : page) fp->arrived.push_back(std::move(g));
            if (!ok || page.empty() || fp->quit ||
                (total > 0 && offset >= static_cast<size_t>(total))) {
                fp->running = false;
                return;
            }
        }
    });
    gridFills.push_back(std::move(f));
}

void App::openTile(const screens::Result& res) {
    const auto& tiles = libraryScreen.visible();
    if (res.value < 0 || res.value >= static_cast<int>(tiles.size())) return;
    // THE GAMES ARE FETCHED HERE AND NOWHERE EARLIER — open
    // question 28. Boot draws this screen having fetched no games
    // at all; a grid costs one request at the moment somebody
    // walks into it, and nothing at all for the systems they never
    // open. The second visit costs nothing: loadTileGames sees a
    // membership it already filled and returns. Whether each game is
    // greyed is asked again on every visit (gridOrder): pairing a Wii
    // Remote or starting a stream changes it.
    // Pages that came while the Library was up go to their tiles first, so
    // this tile is as far along as the server's list has come.
    drainGridPages();
    const bool isCollection = libraryScreen.tab() != 0;
    const std::string tileQuery = tiles[res.value].query;
    auto& own = isCollection ? collectionTiles : platformTiles;
    screens::Tile* t = nullptr;
    for (auto& x : own) if (x.query == tileQuery) { t = &x; break; }
    if (!t) return;
    // READ AGAIN WHEN REOPENED A MINUTE OR MORE LATER (#387). A cover fixed on
    // RomM showed only after a restart, because a grid was read once a run.
    // RomM moves a game's `updated_at` when it is matched but not its
    // platform's (checked on the reference server, 2026-10-18), so the
    // platform list cannot say a grid changed: time is the only signal. The
    // cost is the first open's, one page, and nothing while the server is
    // away. A read that fails keeps the list there was, and its filler.
    constexpr unsigned long long kRereadAfterMs = 60 * 1000;
    if (!t->cards.empty() && !romm::serverAway() &&
        SDL_GetTicks() - t->loadedAtMs >= kRereadAfterMs) {
        std::vector<int> before;
        before.swap(t->cards);
        if (loadTileGames(liveClient, lib, *t)) {
            libraryScreen.learnedTile(tileQuery, t->detail, t->cover);
            std::fprintf(stderr, "[library] %s: read again, %zu game(s)\n",
                         t->title.c_str(), t->cards.size());
        } else {
            t->cards.swap(before);
        }
    }
    if (t->cards.empty()) {
        // ONE PAGE, AND IT BLOCKS FOR THAT. The person has chosen
        // a system and is waiting for one request rather than for
        // the whole platform — 0.19 s for 141 games here, and the
        // same 0.19 s for a platform of thirty thousand.
        loadTileGames(liveClient, lib, *t);
        libraryScreen.learnedTile(tileQuery, t->detail, t->cover);
    }
    if (t->cards.empty()) {
        // Nothing came back, or nothing in it can be played here.
        // Refusing is better than opening onto an empty grid with
        // no reason on it.
        sound::play(sound::Cue::Edge);
        return;
    }
    gridScreen.open(t->title, /*namesSystem=*/isCollection, gridOrder(lib, *t), cards);
    gridTileQuery = t->query;
    gridReadId = t->readId;
    fillGrid(*t);
    gridScreen.setLoadingMore(t->fetched < t->total);
    stack.push_back(Screen::Grid);
    sound::play(sound::Cue::Activate);
}
