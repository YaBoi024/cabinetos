// Search: the server asked (or the drive, offline) as somebody types, the
// wait after the last key, the results' room, and Search's half of the
// keyboard's answer (#398, restructure batch d).

#include "app.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "cache.h"
#include "catalog.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// The answer, on the frame thread, which owns the store and the screen.
// Shared by the typing loop's worker and by `--screen search --query`, so a
// capture shows what a person's search shows.
void App::showSearchAnswer(const SearchAnswer& a, bool ok, const std::string& err) {
    if (!ok) {
        // NOT "nothing matches". The console could not go and look, and
        // saying the library holds nothing like it would be a lie told
        // confidently. See SearchScreen::State.
        std::fprintf(stderr, "[search] %s: %s\n", a.query.c_str(), err.c_str());
        if (a.query == searchTyped) searchScreen.setFailed(err);
        return;
    }
    std::vector<int> idx;
    idx.reserve(a.found.size());
    for (const auto& g : a.found) {
        // Same rule as Home's shelf and a collection's grid: a result this
        // console cannot start must not be offered. The heading's "12 of
        // 52" is what accounts for the difference.
        if (!catalog::playable(g)) continue;
        idx.push_back(appendGame(lib, g));
    }
    std::fprintf(stderr, "[search] %s: %zu of %d\n", a.query.c_str(), idx.size(), a.total);
    searchScreen.setResults(a.query, std::move(idx), a.total);
}

// Asks the drive at once when offline, and the server otherwise: on a worker
// from the typing loop (pumpSearch), here only for `--screen search
// --query`, whose capture has no frames to wait through.
void App::runSearch(const std::string& q) {
    if (q.empty()) return;
    // OFFLINE (#88, MMagTech 2026-10-05): by name over the games on the
    // drive, the same games the offline Library shows. Results are simply
    // what is here; nothing says what might be on the server. Also while
    // the library is still the drive's, the server back but not yet
    // loaded (the frame loop loads it on Home).
    if (romm::serverAway() || libFromDrive) {
        auto lower = [](std::string x) {
            for (char& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return x;
        };
        const std::string want = lower(q);
        std::vector<int> idx;
        // THE DRIVE'S GAMES ARE READ ONCE, NOT PER SEARCH: a file per game,
        // which on a drive of a thousand would be a stutter per keystroke.
        // Read again at most every half minute (a drive plugged in).
        static std::vector<romm::Game> onDrive;
        static Uint64 readAt = 0;
        if (onDrive.empty() || SDL_GetTicks() - readAt > 30000) {
            readAt = SDL_GetTicks();
            onDrive.clear();
            for (const cache::OnDrive& d : cache::onDrive()) {
                romm::Game g;
                if (d.record.empty() || !romm::gameFromJson(d.record, &g)) continue;
                if (catalog::playable(g)) onDrive.push_back(std::move(g));
            }
        }
        for (const romm::Game& g : onDrive) {
            const std::string name = lower(g.name.empty() ? g.fsName : g.name);
            if (name.find(want) == std::string::npos) continue;
            idx.push_back(appendGame(lib, g));
        }
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            return lower(cards[static_cast<size_t>(a)].title) <
                   lower(cards[static_cast<size_t>(b)].title);
        });
        const int n = static_cast<int>(idx.size());
        std::fprintf(stderr, "[search] %s: %d on the drive (offline)\n", q.c_str(), n);
        searchScreen.setResults(q, std::move(idx), n);
        return;
    }
    SearchAnswer a;
    a.query = q;
    std::string err;
    const bool ok = liveClient.fetchRoms("search_term=" + romm::Client::encodeQueryValue(q),
                                         kSearchLimit, &a.found, &err, &a.total);
    showSearchAnswer(a, ok, err);
}

// Search's keyboard, docked under the results, holding what was typed:
// nothing on arriving (goToDestination clears it first), the last search
// when Down from the results brings it back (apply, FocusKeyboard). That
// is searchTyped itself, so pumpSearch sees nothing new and asks nothing.
void App::openSearchKeyboard() {
    ui::Keyboard::Config cfg;
    cfg.title = "Search";
    // "Game name" spelled out what the box was for (MMagTech,
    // 2026-10-14: "we can do better than that"); a search box says
    // Search, and the dots make it read as a place to type.
    cfg.placeholder = "Search\xE2\x80\xA6";
    cfg.dockedBottom = true;
    cfg.initial = searchTyped;
    // UP OUT OF THE FIELD is how a person gets from what they typed to what
    // it found; anywhere else it is the edge. From the field, not the top row
    // of keys, since the field has a cursor (#668): Up from the digits goes
    // into it on every keyboard, this one too.
    cfg.upFromField = [this] {
        if (here() != Screen::Search || searchScreen.resultCount() == 0) return false;
        searchScreen.setFocused(true);
        sound::play(sound::Cue::Move);
        return true;
    };
    keyboard.open(cfg);
}

// Search's half of keyboardResult (input.cpp), which calls it last.
void App::searchKeyboardResult(ui::KeyboardResult res) {
    if (res == ui::KeyboardResult::Committed) {
        if (searchScreen.resultCount() > 0) {
            searchScreen.setFocused(true);
            // DONE SLIDES THE PANEL OUT, the way leaving Search does, and
            // the covers grow with it (sizeSearchResults). It vanished at
            // once, so the covers jumped to their larger size (MMagTech on
            // the TV, 2026-10-19). With nothing found the keyboard comes
            // straight back below, so there is nothing to slide.
            keyboard.keepForSlide();
            keyboardSlide.from = keyboardSlide.to = 0.0f;
            keyboardSlide.elapsed = 0.0f;
            keyboardSlide.retarget(1.0f, kKeyboardSlide);
        } else if (!keyboard.isOpen()) {
            goToDestination(2);   // nothing found: type again
        }
    } else if (res == ui::KeyboardResult::Cancelled) {
        goToDestination(0);
        sound::play(sound::Cue::Back);
    }
}

void App::tickSearch(float dt, screens::Ctx& ctx) {
    searchScreen.tick(dt, ctx);
}

// THE SEARCH LOOP. It used to be three lines because the whole
// library was in memory and a substring match was free. It asks
// the server now — open question 28 — so it is in two halves:
// notice what was typed, and some time after the typing stops, go
// and ask.
void App::pumpSearch(float dt) {
    if (here() == Screen::Search && keyboard.isOpen() &&
        keyboard.value() != searchTyped) {
        searchTyped = keyboard.value();
        searchScreen.setQuery(searchTyped);
        searchPending = searchTyped;
        searchDebounce = kSearchDebounce;
        // Said immediately, not when the request goes out: the gap
        // between the last keypress and the answer is exactly the
        // stretch where a screen that says nothing looks broken.
        searchScreen.setWaiting();
    }
    {
        SearchAnswer a;
        std::string err;
        bool ok = false;
        if (searchJob.take(&a, &err, &ok) && a.query == searchTyped)
            showSearchAnswer(a, ok, err);
    }
    if (!searchPending.empty()) {
        searchDebounce -= dt;
        // The drive answers at once and needs no worker; the server goes on
        // one, and a query typed while one is out waits for it (job.h's
        // start would join it here).
        const bool drive = romm::serverAway() || libFromDrive;
        if (searchDebounce <= 0.0f && (drive || !searchJob.busy())) {
            const std::string q = searchPending;
            searchPending.clear();
            if (drive) {
                runSearch(q);
            } else {
                searchJob.start([this, q](SearchAnswer& a, std::string& err) {
                    a.query = q;
                    return liveClient.fetchRoms(
                        "search_term=" + romm::Client::encodeQueryValue(q), kSearchLimit,
                        &a.found, &err, &a.total);
                });
            }
        }
    }
}

// The results are sized to the room the keyboard leaves, and the
// keyboard's height depends on its own layout — so it is asked
// rather than assumed. While the panel slides in or out, the room is
// where the panel is this frame, so the covers grow and shrink with
// it (MMagTech on the TV, 2026-10-19).
void App::sizeSearchResults() {
    if (here() == Screen::Search) {
        searchScreen.setKeysUp(keyboard.isOpen());
        searchScreen.setResultsBottom(keyboard.isOpen() || keyboard.sliding()
                                          ? keyboard.panelTop()
                                          : ui::kCanvasHeight);
    }
}
