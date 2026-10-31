// The Library's platform covers, fetched behind Home, and the frame's
// share of what arrived (#398, restructure batch d). Their start at boot
// is in startup.cpp's startBrowsing, with the sweep of the cover cache.

#include "app.h"

#include <cstdio>
#include <utility>

#include "covercache.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// STARTED AGAIN BY AN ACCOUNT SWITCH, which builds new tiles with no
// covers. It used to run at boot only, so after a switch the Library's
// platforms stayed bare colour until each was opened. MMagTech, on the A9,
// 2026-09-24. Any fill still running for the last account is stopped and
// its results dropped first, so no cover lands on the wrong person's tile.
void App::startCoverFill() {
    coverFill.quit.store(true);
    for (std::thread& w : coverFill.workers)
        if (w.joinable()) w.join();
    coverFill.workers.clear();
    coverFill.quit.store(false);
    coverFill.next.store(0);
    coverFill.want.clear();
    {
        std::lock_guard<std::mutex> lk(coverFill.m);
        coverFill.done.clear();
    }
    // THE TILE MAP FIRST, WHICH IS THE WHOLE POINT. Without it the console
    // asks thirty-six times at every start which cover a tile should use
    // and gets the same thirty-six answers — 1.07 s and a third of a
    // megabyte to learn nothing new. A row is used when the platform's
    // `updated_at` AND `rom_count` still match what they were when it was
    // written; both ride on the platform list boot already fetched, so
    // checking costs no request. See covercache.h for why this is not the
    // library snapshot open question 22 rules out.
    const std::map<int, covercache::Tile> saved = covercache::loadTiles();
    int reused = 0;
    for (screens::Tile& t : platformTiles) {
        if (!t.enterable || !t.cover.empty()) continue;
        auto mine = lib.tileCache.find(t.id);
        auto was = saved.find(t.id);
        if (mine != lib.tileCache.end() && was != saved.end() &&
            !was->second.cover.empty() &&
            was->second.updatedAt == mine->second.updatedAt &&
            was->second.romCount == mine->second.romCount) {
            t.cover = was->second.cover;
            mine->second.cover = was->second.cover;
            // AND THE SCREEN HAS TO BE TOLD, because it was built from
            // these tiles before this ran and holds its own copies. The
            // cold path gets this for free — every cover that arrives goes
            // through learnedTile — so a remembered one that skipped it
            // drew the colour and never the picture. Found by a warm boot
            // requesting exactly one image, the avatar.
            libraryScreen.learnedTile(t.query, "", was->second.cover);
            ++reused;
            continue;
        }
        coverFill.want.emplace_back(t.id, t.query);
    }
    if (reused || !coverFill.want.empty())
        std::fprintf(stderr, "[covers] %d tile(s) remembered, %zu to ask about\n",
                     reused, coverFill.want.size());
    // FOUR, THE SAME AS THE IMAGE CACHE, and for the same reason: these
    // are round trips rather than work, so the number that helps is the
    // number in flight. Thirty-six platforms one at a time is 1.55 s;
    // four at a time is about a quarter of that, which is the difference
    // between tiles that fill in and tiles you watch filling in.
    const unsigned kWorkers = 4;
    for (unsigned w = 0; w < kWorkers; ++w) {
        coverFill.workers.emplace_back([this]() {
            for (;;) {
                if (coverFill.quit.load()) return;
                const size_t i = coverFill.next.fetch_add(1);
                if (i >= coverFill.want.size()) return;
                std::vector<romm::Game> one;
                std::string err;
                // The tile's own query, so the Arcade tile's cover is the
                // first among every arcade folder's games (#611).
                if (!liveClient.fetchRoms(coverFill.want[i].second, 1, &one, &err) ||
                    one.empty())
                    continue;
                if (one[0].coverPath.empty()) continue;
                std::lock_guard<std::mutex> lk(coverFill.m);
                coverFill.done.emplace_back(coverFill.want[i].first, one[0].coverPath);
            }
        });
    }
}

// Covers that arrived behind Home. Applied here because this is
// the thread that owns the tiles and the screen drawing them.
void App::drainCovers() {
    {
        std::vector<std::pair<int, std::string>> arrived;
        {
            std::lock_guard<std::mutex> lk(coverFill.m);
            arrived.swap(coverFill.done);
        }
        for (const auto& [id, cover] : arrived) {
            for (screens::Tile& t : platformTiles) {
                if (t.id != id) continue;
                if (t.cover.empty()) t.cover = cover;
                libraryScreen.learnedTile(t.query, "", cover);
                break;
            }
            auto it = lib.tileCache.find(id);
            if (it != lib.tileCache.end()) it->second.cover = cover;
        }
        // Written when the last one lands rather than per cover: this
        // is thirty-six short lines and the next boot is what reads it.
        if (!arrived.empty() && coverFill.next.load() >= coverFill.want.size())
            covercache::saveTiles(lib.tileCache);
    }
}
