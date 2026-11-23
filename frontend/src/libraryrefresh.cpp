// When Home and the Library ask RomM again, and the App's side of it: the read
// out, its answer put into the live library when nothing is in front of it,
// and Home's highlight kept on what it was on (#730, libraryrefresh.h).
// --library-refresh-test is at the end.

#include "libraryrefresh.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "app.h"
#include "cache.h"
#include "catalog.h"
#include "favorites.h"
#include "fileio.h"
#include "storage.h"
#include "timewords.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace libraryrefresh {

bool When::asks(const Moment& m) {
    if (!started_) {
        started_ = true;
        view_ = m.view;
        away_ = m.away;
        pressedAt_ = m.at;
        return false;
    }
    const bool arrived = m.view != view_;
    const bool pickedUp = m.pressed && m.at - pressedAt_ >= kPickUpSeconds;
    if (m.pressed) pressedAt_ = m.at;
    // Back with nobody there is for whoever picks up the controller next.
    const bool back = away_ && !m.away && m.at - pressedAt_ < kPickUpSeconds;
    view_ = m.view;
    away_ = m.away;
    return m.view != View::Neither && !m.away && (arrived || pickedUp || back);
}

Place homePlace(Place at, const std::vector<int>& recentWas, const std::vector<int>& favoritesWas,
                const std::vector<int>& recentNow, const std::vector<int>& favoritesNow,
                bool* newResume) {
    auto card = [](const std::vector<int>& recent, const std::vector<int>& favorites, Place p) {
        const std::vector<int>& row = p.row == App::RowRecent ? recent : favorites;
        return p.slot >= 0 && p.slot < static_cast<int>(row.size()) ? row[static_cast<size_t>(p.slot)]
                                                                    : -1;
    };
    const int was = card(recentWas, favoritesWas, at);
    const bool onResume = at.row == App::RowRecent && at.slot == 0 && was >= 0;
    Place to = at;
    if (was < 0) {
        to.row = recentNow.empty() && !favoritesNow.empty() ? App::RowFavorites : App::RowRecent;
        to.slot = 0;
    } else if (recentNow.empty() && favoritesNow.empty()) {
        to = Place{};
    } else {
        if ((to.row == App::RowRecent ? recentNow : favoritesNow).empty())
            to.row = to.row == App::RowRecent ? App::RowFavorites : App::RowRecent;
        const std::vector<int>& row = to.row == App::RowRecent ? recentNow : favoritesNow;
        const auto it = std::find(row.begin(), row.end(), was);
        if (onResume && to.row == App::RowRecent) to.slot = 0;
        else if (to.row == at.row && it != row.end()) to.slot = static_cast<int>(it - row.begin());
        else to.slot = std::clamp(at.slot, 0, std::max(0, static_cast<int>(row.size()) - 1));
    }
    const int now = card(recentNow, favoritesNow, to);
    *newResume = to.row == App::RowRecent && to.slot == 0 && now >= 0 && !(onResume && now == was);
    return to;
}

}  // namespace libraryrefresh

// ---- The App's side ------------------------------------------------------
// Members of the App (app.h).

// Once a frame: what happened that asks for a read, the one out, and its
// answer when there is one.
void App::followLibrary() {
    LibraryRefresh& r = libraryRefresh;
    const double now = power::upSeconds();
    const bool inGame = playing || standaloneRun.active();
    libraryrefresh::Moment m;
    m.at = now;
    if (!inGame && !linkUp.picking)
        m.view = here() == Screen::Home      ? libraryrefresh::View::Home
                 : here() == Screen::Library ? libraryrefresh::View::Library
                                             : libraryrefresh::View::Neither;
    m.pressed = idleWatch.lastInput() != r.inputSeen;
    r.inputSeen = idleWatch.lastInput();
    m.away = romm::serverAway() || libFromDrive;
    bool ask = r.when.asks(m);

    if (inGame || switchPendingId || leaving != Leave::None || poweringDown) dropLibraryRead();

    LibraryRead got;
    if (r.out && r.job.take(&got, nullptr, nullptr)) {
        r.out = false;
        if (!r.dropped && got.reached) {
            r.when.ended(now);
            if (got.failed != r.failing) {
                r.failing = got.failed;
                if (!r.failing.empty())
                    std::fprintf(stderr, "[library] not read again: %s\n", r.failing.c_str());
            }
            r.held = std::move(got);
            r.heldAt = now;
        } else if (!r.dropped) {
            // It never reached RomM, and RomM may have answered since.
            ask = ask || (r.askedMeanwhile && !m.away && m.view != libraryrefresh::View::Neither);
        }
        r.dropped = false;
    }
    // An answer nothing could be done with for a minute is an old one, and
    // the next arrival reads again.
    if (r.held && (r.forUser != storage::currentUser().id || libFromDrive ||
                   now - r.heldAt >= libraryrefresh::kBetweenSeconds))
        r.held.reset();
    if (r.held && libraryMayChange()) {
        takeLibraryRead(std::move(*r.held));
        r.held.reset();
    }

    if (!ask) return;
    if (r.out) r.askedMeanwhile = true;
    else if (!r.held && r.when.rested(now)) startLibraryRead();
}

void App::startLibraryRead() {
    LibraryRefresh& r = libraryRefresh;
    const storage::User who = storage::currentUser();
    if (!rommAddress || !liveClient.haveToken() || !who.valid() || leaving != Leave::None ||
        switchPendingId || poweringDown || linkupSessionOpen() || linkupGallery)
        return;
    LibraryAsk ask = askAgain(lib, who);
    r.forUser = who.id;
    r.hearts = favOwed(who);
    // A GAME LEFT HERE WHOSE TIME RomM HAS is RomM's to list again or not;
    // one still owed, or never sent, is this console's (PlayedHere).
    r.settled.clear();
    for (const auto& [id, p] : lib.playedHere)
        if (p.sent && playtime::owedMs(who, id) == 0) {
            r.settled[id] = p.at;
            std::erase(ask.playedHere, id);
        }
    r.out = true;
    r.dropped = r.askedMeanwhile = false;
    r.stop = false;
    // ITS OWN CLIENT, the live one's sign-in as it is now: stopped alone, and
    // never answering as somebody the console has since become.
    auto client = std::make_shared<romm::Client>();
    client->copySignIn(liveClient);
    client->stopWhen(&r.stop);
    r.job.start([client, ask = std::move(ask), stop = &r.stop](LibraryRead& out, std::string&) {
        out = readLibrary(*client, ask, stop);
        return true;
    });
}

// The read out, and an answer waiting, are for nobody now. Never waits: the
// worker ends by itself, within a second, and its answer is thrown away.
void App::dropLibraryRead() {
    LibraryRefresh& r = libraryRefresh;
    r.held.reset();
    if (!r.out || r.dropped) return;
    r.dropped = true;
    r.stop = true;
    r.when.ended(power::upSeconds());
}

// The whole library has just been read, some other way: the minute starts over.
void App::libraryLoaded() {
    dropLibraryRead();
    libraryRefresh.when.ended(power::upSeconds());
}

// Whether the library may change on the screen now: nothing of the console's
// is in front of it, no game is starting or running, and LinkUp is not
// choosing one from it or in a session.
bool App::libraryMayChange() {
    return !playing && !standaloneRun.active() && !exitPending && !arriving && !switchPendingId &&
           leaving == Leave::None && !poweringDown && curtain.value() < 0.01f && !overlayOpen &&
           !accountsOpen && !pinScreen.isOpen() && !choiceScreen.isOpen() && !raList.isOpen() &&
           !downloadsPanel.isOpen() && !padWindowOpen &&
           !(keyboard.isOpen() && here() != Screen::Search) &&
           !(launchJob.playWhenReady && (launchJob.busy() || launchJob.waiting())) &&
           pendingDetail < 0 && pendingDest < 0 && !linkUp.picking && !linkupNextOpen &&
           !linkupSessionOpen();
}

void App::takeLibraryRead(LibraryRead read) {
    read.hearts = libraryRefresh.hearts;
    read.settled = libraryRefresh.settled;
    const std::vector<int> recentWas = shelf, favoritesWas = favorites;
    const LibraryChanges changes = putLibrary(lib, std::move(read), storage::currentUser());
    keepHomePlace(recentWas, favoritesWas);
    libraryScreen.refresh(platformTiles, collectionTiles);
    fillNewCovers();
    std::string said;
    for (const std::string& s : changes.said) said += (said.empty() ? "" : "; ") + s;
    if (!said.empty()) std::fprintf(stderr, "[library] RomM changed: %s\n", said.c_str());
}

// Home's highlight, its lift and its rows' places, after the rows changed
// under them (homePlace).
void App::keepHomePlace(const std::vector<int>& recentWas, const std::vector<int>& favoritesWas) {
    bool newResume = false;
    const libraryrefresh::Place was{focusRow, focusSlot};
    const libraryrefresh::Place to = libraryrefresh::homePlace(was, recentWas, favoritesWas, shelf,
                                                               favorites, &newResume);
    bool settle = !(to == was);
    for (int row = RowRecent; row <= RowFavorites; ++row) {
        const std::vector<int>& now = row == RowRecent ? shelf : favorites;
        if (now == (row == RowRecent ? recentWas : favoritesWas)) continue;
        // The lifts belong to places on the row, and the games moved.
        slotFocus[row].clear();
        slotPress[row].clear();
        settle = settle || row == to.row;
        // A shorter row is not left scrolled past its end.
        const float pitch = kShelfCoverWidth + kShelfSpacing;
        const float end = std::max(0.0f, 2.0f * kContentInset + static_cast<float>(now.size()) * pitch -
                                             kShelfSpacing - ui::kCanvasWidth);
        if (shelfScroll[row].to > end) shelfScroll[row].retarget(end, kFocusDuration * 1.6f);
    }
    focusRow = to.row;
    focusSlot = to.slot;
    if (settle && !barFocused && !accountsOpen) settleFocus();
    if (newResume) resumeHeldUntil = clockSeconds() + libraryrefresh::kResumeHoldSeconds;
}

// ---- --library-refresh-test ----------------------------------------------

namespace libraryrefresh {
namespace {

romm::Platform platform(int id, const char* slug, const char* name, int games,
                        const char* updated = "2026-11-01T00:00:00+00:00") {
    romm::Platform p;
    p.id = id;
    p.slug = slug;
    p.fsSlug = std::string(slug) + "-folder" + std::to_string(id);
    p.name = name;
    p.romCount = games;
    p.updatedAt = updated;
    return p;
}

romm::Game game(int id, const char* name, const char* lastPlayed = "", int platformId = 1,
                const char* slug = "nes", const char* system = "NES") {
    romm::Game g;
    g.id = id;
    g.name = name;
    g.fsName = std::string(name) + ".zip";
    g.platformId = platformId;
    g.platformSlug = slug;
    g.platformFsSlug = std::string(slug) + "-folder" + std::to_string(platformId);
    g.platformName = system;
    g.lastPlayed = lastPlayed;
    g.coverPath = "/art/" + std::to_string(id) + "?ts=1";
    g.coverLargePath = g.coverPath + "&big";
    return g;
}

romm::Collection collection(int id, const char* name, int owner, int games, bool smart = false) {
    romm::Collection c;
    c.id = id;
    c.name = name;
    c.userId = owner;
    c.ownerName = owner == 7 ? "me" : "alex";
    c.romCount = games;
    c.smart = smart;
    c.coverPath = "/collection/" + std::to_string(id) + "?ts=1";
    return c;
}

// An answer to a whole read, every part of it there.
LibraryRead everything() {
    LibraryRead r;
    r.whole = true;
    r.reached = true;
    r.me = 7;
    r.gotPlatforms = r.gotCollections = r.gotSmart = r.gotRecent = r.gotFavorites =
        r.gotFavoriteEntries = true;
    return r;
}

// The same, as a read again answers.
LibraryRead again(const LibraryRead& was) {
    LibraryRead r = was;
    r.whole = false;
    r.games.clear();
    return r;
}

std::string titles(const Library& lib, const std::vector<int>& row) {
    std::string out;
    for (const int i : row)
        out += (out.empty() ? "" : ",") + lib.cards[static_cast<size_t>(i)].title;
    return out;
}

std::string tiles(const std::vector<screens::Tile>& list) {
    std::string out;
    for (const screens::Tile& t : list) out += (out.empty() ? "" : ",") + t.title + "=" + t.detail;
    return out;
}

const screens::Tile* tileOf(const std::vector<screens::Tile>& list, const char* title) {
    for (const screens::Tile& t : list)
        if (t.title == title) return &t;
    return nullptr;
}

int cardOf(const Library& lib, int id) {
    const auto it = lib.byRomId.find(id);
    return it == lib.byRomId.end() ? -1 : it->second;
}

}  // namespace

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // ---- When a read may start -------------------------------------------
    {
        using enum View;
        auto at = [](double s, View v, bool pressed = false, bool away = false) {
            Moment m;
            m.at = s;
            m.view = v;
            m.pressed = pressed;
            m.away = away;
            return m;
        };
        When w;
        check(!w.asks(at(0, Home)), "Home up as the console starts asks for nothing: it was just read");
        check(w.asks(at(1, Library, true)), "the Library coming into view asks");
        check(!w.asks(at(2, Library, true)), "a press on it does not");
        check(!w.asks(at(3, Neither, true)) && w.asks(at(4, Library, true)),
              "and back to it from a system's list does");
        check(w.asks(at(5, Home, true)), "Home from the Library does");
        check(!w.asks(at(6, Neither)) && w.asks(at(7, Home)), "Home out of a game does, pressed or not");
        check(!w.asks(at(64, Home, true)) && !w.asks(at(123.9, Home, true)),
              "presses less than a minute apart do not");
        check(!w.asks(at(300, Home)), "nor does sitting on it, however long");
        check(w.asks(at(480, Home, true)), "a press after a minute with none does");
        check(!w.asks(at(481, Home, true)), "and the next one does not");
        check(!w.asks(at(482, Neither, true)) && !w.asks(at(600, Neither, true)),
              "a press after a minute on another screen does not");
        check(!w.asks(at(601, Home, true, true)), "nothing asks while RomM is away");
        check(!w.asks(at(602, Home, false, true)) && w.asks(at(620, Home)),
              "RomM answering again does, with somebody there");
        check(!w.asks(at(621, Home, false, true)) && !w.asks(at(700, Home)),
              "and not with nobody there for a minute");
        check(w.asks(at(701, Home, true)), "whoever picks up the controller next does instead");

        When r;
        check(r.rested(0), "nothing read yet, nothing to wait for");
        r.ended(100);
        check(!r.rested(159.9) && r.rested(160), "a minute from the end of the last, not its start");
    }

    // ---- Where Home's highlight goes -------------------------------------
    {
        using P = Place;
        bool resume = false;
        auto to = [&](P from, std::vector<int> rw, std::vector<int> fw, std::vector<int> rn,
                      std::vector<int> fn) { return homePlace(from, rw, fw, rn, fn, &resume); };
        check(to({0, 0}, {1, 2, 3}, {}, {9, 1, 2, 3}, {}) == P{0, 0} && resume,
              "on Resume it stays first, and A is another game's now");
        check(to({0, 0}, {1, 2, 3}, {}, {1, 3, 2}, {}) == P{0, 0} && !resume,
              "on Resume with the same game first, nothing is different");
        check(to({0, 2}, {1, 2, 3}, {}, {9, 1, 2, 3}, {}) == P{0, 3} && !resume,
              "on another cover it follows the game along the row");
        check(to({0, 2}, {1, 2, 3}, {}, {3, 1, 2}, {}) == P{0, 0} && resume,
              "and to the front, where A starts it and is held");
        check(to({0, 1}, {1, 2, 3}, {}, {1, 3}, {}) == P{0, 1} && !resume,
              "a game that is gone gives it to the one in its place");
        check(to({0, 2}, {1, 2, 3}, {}, {1, 2}, {}) == P{0, 1}, "or to the last, when it was the last");
        check(to({1, 1}, {1}, {5, 6}, {1}, {6, 5}) == P{1, 0} && !resume,
              "on Favorites it follows its game too");
        check(to({1, 0}, {1, 5}, {5, 6}, {5, 1}, {6, 5}) == P{1, 1},
              "and stays on Favorites though the game is on Recent as well");
        check(to({1, 1}, {1, 2, 3}, {5, 6}, {1, 2, 3}, {}) == P{0, 1} && !resume,
              "Favorites gone, Recent has it");
        check(to({0, 0}, {1}, {5}, {}, {5}) == P{1, 0} && !resume, "Recent gone, Favorites has it");
        check(to({0, 0}, {}, {}, {4}, {5}) == P{0, 0} && resume,
              "a Home that was empty has it on Resume, held");
        check(to({0, 0}, {}, {}, {}, {5, 6}) == P{1, 0} && !resume,
              "or on Favorites when that is all there is");
        check(to({0, 0}, {1}, {}, {}, {}) == P{0, 0} && !resume, "and an emptied Home holds nothing");
    }

    // ---- What building changes -------------------------------------------
    char root[] = "/tmp/cabinetos-refresh-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[library] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[library] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    // Cores for three systems, so a game on them plays: no core's bytes.
    const std::string coreDir = std::string(root) + "/cores";
    storage::makeDirs(coreDir);
    catalog::setCoreDirectory(coreDir.c_str());
    for (const char* slug : {"nes", "snes", "genesis", "arcade", "3ds"})
        if (const catalog::Coverage c = catalog::coverageFor(platform(1, slug, slug, 1)); c.core)
            cab::writeBytes(coreDir + "/" + catalog::coreFileName(c.core), std::string("core"));
    storage::User me;
    me.id = 7;
    me.name = "refresh-test";
    storage::makeDirs(storage::userDir(me));

    const char* kMon = "2026-11-02T10:00:00+00:00";
    const char* kTue = "2026-11-03T10:00:00+00:00";
    const char* kWed = "2026-11-04T10:00:00+00:00";
    const char* kThu = "2026-11-05T10:00:00+00:00";
    auto secs = [](const char* iso) { return isoMicros(iso) / 1000000; };
    LibraryRead first = everything();
    first.platforms = {platform(1, "nes", "NES", 5), platform(2, "snes", "SNES", 9)};
    first.recent = {game(11, "Wed", kWed), game(12, "Tue", kTue), game(13, "Mon", kMon)};
    first.games = {game(21, "Fav A"), game(22, "Fav B")};
    first.favorites = {21, 22};
    romm::Collection favs = collection(1, "Favorites", 7, 2);
    favs.isFavorite = true;
    first.collections = {favs, collection(2, "Beat em ups", 7, 4), collection(3, "Later", 7, 0)};
    first.smartCollections = {collection(2, "Every RPG", 7, 30, true)};
    auto fresh = [&] {
        Library lib;
        putLibrary(lib, first, storage::User{});
        return lib;
    };
    {
        Library lib = fresh();
        check(titles(lib, lib.shelf) == "Wed,Tue,Mon" && titles(lib, lib.favorites) == "Fav A,Fav B" &&
                  lib.heroIndex == cardOf(lib, 11) && lib.heroPlatform == "NES",
              "a start: Recent as RomM lists it, its first the one to resume, and Favorites");
        check(tiles(lib.platformTiles) == "NES=5 games,SNES=9 games" &&
                  tiles(lib.collectionTiles) ==
                      "Favorites=2 games,Beat em ups=4 games,Every RPG=30 games,Later=0 games",
              "its systems and collections, with their counts");
        check(!tileOf(lib.collectionTiles, "Later")->enterable &&
                  tileOf(lib.collectionTiles, "Beat em ups")->enterable &&
                  tileOf(lib.collectionTiles, "Beat em ups")->group.empty(),
              "an empty collection not to be opened, and the person's own under no heading");
        check(putLibrary(lib, again(first), {}).said.empty(), "the same answer again changes nothing");
    }
    {
        Library lib = fresh();
        LibraryRead r = again(first);
        r.recent = {game(14, "Thu", kThu), game(12, "Tue", kTue), game(13, "Mon", kMon)};
        const int kept = cardOf(lib, 11);
        putLibrary(lib, r, {});
        check(titles(lib, lib.shelf) == "Thu,Tue,Mon" && lib.heroIndex == cardOf(lib, 14),
              "played elsewhere: first on Recent; taken off Continue Playing or deleted: gone from it");
        check(lib.cards[static_cast<size_t>(kept)].title == "Wed" && cardOf(lib, 11) == kept,
              "and its card stays where every list that holds it looks");
    }
    {
        Library lib = fresh();
        LibraryRead r = again(first);
        r.recent = {};
        putLibrary(lib, r, {});
        check(lib.shelf.empty() && lib.heroIndex == -1, "nothing played any more: no Recent row, nothing to resume");
    }
    // A game left on this console.
    {
        Library lib = fresh();
        appendGame(lib, game(15, "Short"));
        lib.playedHere[15] = {secs(kThu), false};
        putLibrary(lib, again(first), {});
        check(titles(lib, lib.shelf) == "Short,Wed,Tue,Mon",
              "a game left within a minute, which RomM is never told, stays first");
        LibraryRead r = again(first);
        r.recent.insert(r.recent.begin(), game(16, "Later still", "2026-11-05T11:00:00+00:00"));
        putLibrary(lib, r, {});
        check(titles(lib, lib.shelf) == "Later still,Short,Wed,Tue,Mon",
              "and behind one played elsewhere after it");
        r = again(first);
        r.gone = {15};
        putLibrary(lib, r, {});
        check(titles(lib, lib.shelf) == "Wed,Tue,Mon", "until RomM no longer has it");
    }
    {
        Library lib = fresh();
        lib.playedHere[13] = {secs(kThu), true};
        putLibrary(lib, again(first), {});
        check(titles(lib, lib.shelf) == "Mon,Wed,Tue",
              "a game whose time is on its way to RomM is first, though RomM's is older");
        LibraryRead r = again(first);
        r.recent = {game(13, "Mon", kThu), game(11, "Wed", kWed), game(12, "Tue", kTue)};
        putLibrary(lib, r, {});
        check(titles(lib, lib.shelf) == "Mon,Wed,Tue" && lib.playedHere.empty(),
              "and once RomM has the time it is RomM's to say");
        r.recent = {game(11, "Wed", kWed), game(12, "Tue", kTue)};
        putLibrary(lib, r, {});
        check(titles(lib, lib.shelf) == "Wed,Tue", "so taken off Continue Playing, it goes");
    }
    {
        Library lib = fresh();
        appendGame(lib, game(17, "Sent"));
        lib.playedHere[17] = {secs(kThu), true};
        LibraryRead r = again(first);
        r.settled[17] = secs(kThu);
        putLibrary(lib, r, {});
        check(titles(lib, lib.shelf) == "Wed,Tue,Mon" && lib.playedHere.empty(),
              "RomM had its time before it was asked and does not list it: taken off there, it goes here");
        lib.playedHere[17] = {secs(kThu) + 60, true};
        putLibrary(lib, r, {});
        check(titles(lib, lib.shelf) == "Sent,Wed,Tue,Mon",
              "played again since it was asked, it stays");
    }
    {
        Library lib = fresh();
        LibraryRead r = again(first);
        for (int i = 0; i < 20; ++i)
            r.recent.push_back(game(100 + i, "Old", "2026-10-01T00:00:00+00:00"));
        putLibrary(lib, r, {});
        check(lib.shelf.size() == kRecentGames, "Recent holds no more than it did");
    }
    // Entries: a name, a cover, a manual, a system's name.
    {
        Library lib = fresh();
        LibraryRead r = again(first);
        r.recent[0].name = "Wed, renamed";
        r.recent[0].coverPath = "/art/11?ts=2";
        r.recent[0].manualPath = "manual.pdf";
        romm::Game a = game(21, "Fav A, renamed");
        a.coverPath = "/art/21?ts=2";
        r.games = {a};
        const LibraryChanges c = putLibrary(lib, r, {});
        const Card& wed = lib.cards[static_cast<size_t>(cardOf(lib, 11))];
        const Card& fav = lib.cards[static_cast<size_t>(cardOf(lib, 21))];
        check(wed.title == "Wed, renamed" && wed.cover == "/art/11?ts=2" &&
                  lib.games[static_cast<size_t>(cardOf(lib, 11))].manualPath == "manual.pdf" &&
                  fav.title == "Fav A, renamed",
              "a game on Home takes its new name, cover and manual, a favorite too");
        check(wed.coverWas == "/art/11?ts=1" && fav.coverWas == "/art/21?ts=1" &&
                  wed.coverLargeWas.empty(),
              "and keeps the picture it had until the new one is in");
        check(c.said.size() == 1 && c.said[0] == "2 game(s) on Home changed", "said once, as two games");
        r = again(first);
        r.platforms[0].name = "Famicom";
        putLibrary(lib, r, {});
        check(lib.cards[static_cast<size_t>(cardOf(lib, 22))].platform == "Famicom" &&
                  lib.platformNames[1] == "Famicom",
              "a system renamed is renamed on its games, read again or not");
    }
    // Favorites: which, in whose order, and what a press during a read does.
    {
        Library lib = fresh();
        LibraryRead r = again(first);
        r.favorites = {22, 23};
        r.games = {game(23, "Fav C")};
        putLibrary(lib, r, {});
        check(titles(lib, lib.favorites) == "Fav B,Fav C",
              "a favorite made in RomM is on Favorites, one taken off is not");
        r.favorites = {22, 23, 24};
        r.games = {};
        putLibrary(lib, r, {});
        check(titles(lib, lib.favorites) == "Fav B,Fav C", "one whose entry did not come waits for it");
    }
    {
        Library lib;
        putLibrary(lib, first, me);
        check(favoriteList(me) == std::vector<int>({21, 22}), "the person's list is kept for offline");
        LibraryRead r = again(first);
        r.favorites = {22};
        r.hearts = {{21, true}, {22, false}};
        putLibrary(lib, r, me);
        check(titles(lib, lib.favorites) == "Fav A" && favoriteList(me) == std::vector<int>({21}),
              "a heart pressed while RomM was asked wins over its answer, on and off");
        r.hearts.clear();
        r.favorites = {22, 21};
        putLibrary(lib, r, me);
        check(titles(lib, lib.favorites) == "Fav B,Fav A", "and the order is the console's, newest first");
    }
    // Systems and collections.
    {
        Library lib = fresh();
        LibraryRead r = again(first);
        r.platforms = {platform(2, "snes", "Super Famicom", 10), platform(3, "genesis", "Genesis", 1)};
        const LibraryChanges c = putLibrary(lib, r, {});
        check(tiles(lib.platformTiles) == "Genesis=1 game,Super Famicom=10 games" &&
                  lib.platformNames.size() == 2 && lib.tileCache.size() == 2,
              "systems: a new one, one gone, one renamed, and its count");
        check(c.said.size() == 1 && c.said[0] == "systems: new Genesis; gone NES; 1 changed",
              "and the log says so");
    }
    {
        Library lib = fresh();
        LibraryRead r = again(first);
        r.collections = {favs, collection(2, "Brawlers", 7, 4), collection(3, "Later", 7, 1),
                         collection(5, "Shared", 8, 6)};
        r.smartCollections = {};
        putLibrary(lib, r, {});
        check(tiles(lib.collectionTiles) ==
                  "Favorites=2 games,Brawlers=4 games,Later=1 game,Shared=6 games",
              "collections: one shared, one renamed, one gone, and an empty one with a game now");
        check(tileOf(lib.collectionTiles, "Later")->enterable &&
                  tileOf(lib.collectionTiles, "Shared")->group == "From alex",
              "which can be opened, and the shared one under whose it is");
    }
    // What a tile learned.
    {
        Library lib = fresh();
        screens::Tile& t = lib.collectionTiles[1];   // Beat em ups
        t.detail = "3 of 4";
        t.cards = {cardOf(lib, 11)};
        t.readId = 5;
        t.fetched = t.total = 4;
        lib.platformTiles[0].cover = "/art/first?ts=1";
        lib.tileCache[1].cover = "/art/first?ts=1";
        LibraryRead r = again(first);
        putLibrary(lib, r, {});
        const screens::Tile* now = tileOf(lib.collectionTiles, "Beat em ups");
        check(now->detail == "3 of 4" && now->cards.size() == 1 && now->readId == 5 && now->total == 4,
              "the line a collection learned stays while its count has not moved, its grid too");
        check(tileOf(lib.platformTiles, "NES")->cover == "/art/first?ts=1" &&
                  lib.tileCache[1].cover == "/art/first?ts=1",
              "a system keeps its cover");
        r.collections[1].romCount = 5;
        r.collections[1].coverPath = "/collection/2?ts=2";
        r.platforms[0] = platform(1, "nes", "NES", 6, "2026-11-09T00:00:00+00:00");
        putLibrary(lib, r, {});
        now = tileOf(lib.collectionTiles, "Beat em ups");
        check(now->detail == "5 games" && now->cards.size() == 1,
              "and once its count moves the line is the count again, its grid still read");
        check(now->cover == "/collection/2?ts=2" && now->coverWas == "/collection/2?ts=1",
              "its new cover, with the old one until it is in");
        r.collections[2].coverPath = "/game/9?ts=3";
        r.collections[2].covers = {"/game/9?ts=3", "/game/4?ts=3"};
        putLibrary(lib, r, {});
        check(tileOf(lib.collectionTiles, "Later")->cover == "/game/9?ts=3", "(a collection with no art of its own)");
        r.collections[2].coverPath = "/game/4?ts=4";
        r.collections[2].covers = {"/game/4?ts=4", "/game/9?ts=4"};
        putLibrary(lib, r, {});
        check(tileOf(lib.collectionTiles, "Later")->cover == "/game/9?ts=3",
              "one with no art of its own keeps its picture while RomM lists it, wherever");
        r.collections[2].coverPath = "/game/4?ts=5";
        r.collections[2].covers = {"/game/4?ts=5"};
        putLibrary(lib, r, {});
        check(tileOf(lib.collectionTiles, "Later")->cover == "/game/4?ts=5", "and takes another once it does not");
        check(tileOf(lib.platformTiles, "NES")->cover == "/art/first?ts=1" &&
                  lib.tileCache[1].cover.empty() && lib.tileCache[1].romCount == 6,
              "a system that changed keeps its cover on the screen, to be asked about at the next start");
    }
    {
        Library lib = fresh();
        for (screens::Tile& t : lib.platformTiles) t.coverAsked = true;
        LibraryRead r = again(first);
        r.platforms[1] = platform(2, "snes", "SNES", 9, "2026-11-09T00:00:00+00:00");
        putLibrary(lib, r, {});
        check(tileOf(lib.platformTiles, "NES")->coverAsked && !tileOf(lib.platformTiles, "SNES")->coverAsked,
              "a tile with no cover is asked about again only when its system changed");
    }
    // The Arcade tile.
    {
        Library lib;
        LibraryRead r = first;
        r.platforms.push_back(platform(45, "arcade", "Arcade", 3));
        putLibrary(lib, r, {});
        screens::Tile& t = lib.platformTiles[0];
        t.cards = {cardOf(lib, 11)};
        t.readId = 9;
        t.cover = "/art/arcade";
        LibraryRead more = again(r);
        more.platforms.push_back(platform(40, "cps2", "CPS2", 2));
        screens::LibraryScreen screen;
        screen.build(lib.platformTiles, lib.collectionTiles);
        screen.focusTile(0);
        putLibrary(lib, more, {});
        check(tiles(lib.platformTiles) == "Arcade=5 games,NES=5 games,SNES=9 games" &&
                  lib.platformTiles[0].query == "platform_ids=40&platform_ids=45" &&
                  lib.platformTiles[0].cover == "/art/arcade",
              "a new arcade folder: the same Arcade tile, with its cover");
        check(lib.platformTiles[0].cards.empty() && lib.platformTiles[0].readId == 0,
              "its list, of the folders it named, is read again when it is opened");
        more.platforms.push_back(platform(50, "genesis", "Aardvark", 1));
        putLibrary(lib, more, {});
        screen.refresh(lib.platformTiles, lib.collectionTiles);
        check(screen.focusedTile() == 1 && screen.visible()[1].title == "Arcade",
              "and the highlight stays on it, whatever its folders");
    }
    // A platform that hides some of its games.
    {
        Library lib;
        LibraryRead r = first;
        r.platforms.push_back(platform(9, "3ds", "3DS", 8));
        r.counted[9] = {8, 5};
        putLibrary(lib, r, {});
        const screens::Tile* t = tileOf(lib.platformTiles, "3DS");
        check(t && t->detail == "5 games", "a system that hides some shows how many it shows");
        const LibraryAsk ask = askAgain(lib, me);
        check(!ask.whole && ask.counted.count(9) && ask.counted.at(9).listed == 8 &&
                  ask.inHand.count(21) && ask.inHand.count(11),
              "asked again, RomM is not asked for its list, nor for the games in hand");
        LibraryRead same = again(r);
        same.counted.clear();
        putLibrary(lib, same, {});
        check(tileOf(lib.platformTiles, "3DS")->detail == "5 games", "and it still says so");
        same.platforms.back().romCount = 9;
        same.counted[9] = {9, 6};
        putLibrary(lib, same, {});
        check(tileOf(lib.platformTiles, "3DS")->detail == "6 games", "until its count moved");
    }
    // A part that did not come.
    {
        Library lib = fresh();
        LibraryRead r;
        r.whole = false;
        r.me = 7;
        check(putLibrary(lib, r, me).said.empty() && titles(lib, lib.shelf) == "Wed,Tue,Mon" &&
                  titles(lib, lib.favorites) == "Fav A,Fav B" && lib.platformTiles.size() == 2 &&
                  lib.collectionTiles.size() == 4,
              "nothing came: nothing changes");
        r = again(first);
        r.gotPlatforms = r.gotCollections = r.gotFavorites = false;
        r.platforms.clear();
        r.collections.clear();
        r.favorites.clear();
        r.smartCollections.push_back(collection(6, "Every shmup", 7, 3, true));
        r.recent.erase(r.recent.begin());
        putLibrary(lib, r, me);
        check(titles(lib, lib.shelf) == "Tue,Mon" && titles(lib, lib.favorites) == "Fav A,Fav B" &&
                  lib.platformTiles.size() == 2,
              "one part failed: it keeps what was there, and the others are new");
        check(tiles(lib.collectionTiles) == "Favorites=2 games,Beat em ups=4 games,Every RPG=30 games,"
                                            "Every shmup=3 games,Later=0 games",
              "and a list of collections that failed keeps its own beside the one that came");
        r = again(first);
        r.gotRecent = false;
        r.recent.clear();
        putLibrary(lib, r, me);
        check(titles(lib, lib.shelf) == "Tue,Mon", "Recent too");
    }
    // The clock a favorite's change is asked for by is RomM's.
    {
        Library lib;
        LibraryRead r = first;
        r.newest = "2026-11-06T00:00:00+00:00";
        putLibrary(lib, r, {});
        check(askAgain(lib, me).since == "2026-11-06T00:00:00+00:00",
              "a favorite is asked for when RomM changed it after the newest time RomM gave");
        LibraryRead late = again(first);
        late.newest = "2026-11-07T00:00:00+00:00";
        late.gotFavoriteEntries = false;
        putLibrary(lib, late, {});
        check(askAgain(lib, me).since == "2026-11-06T00:00:00+00:00",
              "which waits for an answer that failed");
    }
    // A console that started to nothing.
    {
        Library lib;
        putLibrary(lib, again(first), {});
        check(titles(lib, lib.shelf) == "Wed,Tue,Mon" && lib.platformTiles.size() == 2,
              "a console that started to an empty library fills in");
    }
    // The download dot.
    {
        Library lib = fresh();
        lib.kept = {31};
        appendGame(lib, game(31, "Kept"));
        appendGame(lib, game(32, "Not kept"));
        check(lib.cards[static_cast<size_t>(cardOf(lib, 31))].kept &&
                  !lib.cards[static_cast<size_t>(cardOf(lib, 32))].kept,
              "a downloaded game listed later has its dot");
    }
    // The Library's highlight.
    {
        Library lib = fresh();
        screens::LibraryScreen s;
        s.build(lib.platformTiles, lib.collectionTiles);
        s.focusTile(1);
        LibraryRead r = again(first);
        r.platforms.push_back(platform(3, "genesis", "Genesis", 1));
        putLibrary(lib, r, {});
        s.refresh(lib.platformTiles, lib.collectionTiles);
        check(s.focusedTile() == 2 && s.visible()[2].title == "SNES" && s.visible()[2].focus.value() > 0.99f,
              "on a system it stays on that system, lifted as it was");
        r.platforms = {platform(1, "nes", "NES", 5), platform(3, "genesis", "Genesis", 1)};
        putLibrary(lib, r, {});
        s.refresh(lib.platformTiles, lib.collectionTiles);
        check(s.focusedTile() == 1 && s.visible()[1].title == "NES", "one that is gone gives it to the nearest");
        // To the Collections, and down onto Beat em ups.
        s.key(screens::Nav::Up);
        s.key(screens::Nav::Right);
        s.key(screens::Nav::Activate);
        s.key(screens::Nav::Down);
        s.key(screens::Nav::Right);
        check(s.tab() == 1 && s.visible()[static_cast<size_t>(s.focusedTile())].title == "Beat em ups",
              "(the test is on a collection)");
        r.collections.insert(r.collections.begin(), collection(8, "Aces", 7, 1));
        putLibrary(lib, r, {});
        s.refresh(lib.platformTiles, lib.collectionTiles);
        check(s.focusedTile() == 2 && s.visible()[2].title == "Beat em ups", "a collection too");
        r.collections = {};
        r.smartCollections = {};
        putLibrary(lib, r, {});
        s.refresh(lib.platformTiles, lib.collectionTiles);
        check(s.focusedTile() == -1 && s.visible().empty() && s.tab() == 1,
              "and no collections at all gives it to the switcher");
    }

    storage::removeEntry(root);
    std::printf("%s\n", failures ? "library refresh test: FAILED" : "library refresh test: passed");
    return failures ? 1 : 0;
}

}  // namespace libraryrefresh
