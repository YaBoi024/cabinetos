// Home: the Recent and Favorites shelves, where focus is on them, Home's
// keys, and Home's draw (#398, restructure batch d).

#include "app.h"

#include <algorithm>
#include <cmath>

#include "catalog.h"
#include "screenparts.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// ASKED, NOT REMEMBERED. These were constants worked out once at startup,
// and switching accounts replaces the library underneath them: a console
// that started as someone with an empty Home and switched to someone with
// sixteen cards could focus none of them (the d-pad clicked, nothing
// moved), and the other way round it reached for a card that no longer
// existed and crashed. Found on the A9, 2026-09-24.
//
// THE SHELF AND NOTHING ELSE. An empty shelf used to mean every card in the
// store, which grows with every grid page and search result, so a person
// who had played nothing saw a whole platform as Recent once they opened
// one, and their favourites twice (#398, HOME-01).
auto App::shelfSlots() -> size_t { return shelf.size(); }

// Whether there is a game to resume. It is shelf slot 0 when there is.
bool App::haveResume() { return heroIndex >= 0 && !shelf.empty(); }

bool App::haveFavorites() { return !favorites.empty(); }

// A GAME JUST PLAYED GOES TO THE FRONT OF RECENT (#196), as Resume. Recent
// is RomM's play history, fetched only at start and on an account switch,
// so Forza stayed behind the night before's game until a restart (A9,
// 2026-09-30) although RomM already had it first. Moved here instead of
// refetched: no network, so it is right offline too, and it is what the
// next fetch will say. Every system, built-in and separate alike.
void App::playedJustNow(int romId) {
    auto it = lib.byRomId.find(romId);
    if (it == lib.byRomId.end() || !catalog::playable(lib.games[it->second])) return;
    const int idx = it->second;
    shelf.erase(std::remove(shelf.begin(), shelf.end(), idx), shelf.end());
    shelf.insert(shelf.begin(), idx);
    if (shelf.size() > 16) shelf.resize(16);   // as many as fetchRecent asks for
    heroIndex = idx;
    heroPlatform = lib.games[idx].platformName;
}

// A HOME WITH NOTHING ON IT: a new account that has played nothing and
// starred nothing. MMagTech, switching to one on the A9, 2026-09-24: the
// screen was "just purple", focus was nowhere, and finding the Library
// took pressing Up by accident. See the frame loop and drawShelf's caller.
bool App::homeEmpty() { return shelfSlots() == 0 && !haveFavorites(); }

auto App::rowSlots(int row) -> size_t {
    if (row == RowRecent) return shelfSlots();
    return favorites.size();
}

bool App::rowExists(int row) { return rowSlots(row) > 0; }

// The card a (row, slot) points at, or nullptr for the top bar, whose
// items are destinations rather than cards.
auto App::cardAt(int row, int slot) -> Card* {
    if (row == RowRecent) {
        if (slot < 0 || static_cast<size_t>(slot) >= shelfSlots()) return nullptr;
        return &cards[static_cast<size_t>(shelf[slot])];
    }
    if (slot < 0 || static_cast<size_t>(slot) >= favorites.size()) return nullptr;
    return &cards[favorites[slot]];
}

auto App::slotAnim(std::vector<Animated>* v, int row, int slot) -> Animated& {
    std::vector<Animated>& r = v[std::clamp(row, 0, 1)];
    const size_t s = static_cast<size_t>(std::max(slot, 0));
    if (s >= r.size()) r.resize(s + 1);
    return r[s];
}

// The row Home starts on: Recent, or Favorites when nothing has been played.
int App::homeTopRow() {
    return (!rowExists(RowRecent) && rowExists(RowFavorites)) ? RowFavorites : RowRecent;
}

// HOME FROM THE START: the first card of the top row, every row scrolled
// back. For a new person, and for arriving by the bar or a shoulder, which
// enters every destination fresh. Back to Home keeps your place.
void App::homeFromTheStart() {
    focusRow = homeTopRow();
    focusSlot = 0;
    shelfScroll[0].settle(0.0f);
    shelfScroll[1].settle(0.0f);
    scrollY.settle(0.0f);
    for (int r = 0; r < 2; ++r) { slotFocus[r].clear(); slotPress[r].clear(); }
    if (cardAt(focusRow, focusSlot)) slotAnim(slotFocus, focusRow, focusSlot).settle(1.0f);
}

// Settled, not animating: a screenshot should show the resting focused
// state, not a frame part-way through the transition into it.
void App::settleFocus() {
    if (cardAt(focusRow, focusSlot)) {
        slotAnim(slotFocus, focusRow, focusSlot).settle(1.0f);
    } else {
        // The top bar's items are not Cards and have no animation of their
        // own: focus there is a tinted pill drawn from focusRow/focusSlot.
    }
}

// HOME'S TWO ACTIONS, AND THEY MUST NOT COLLAPSE INTO ONE.
//
// The first card on Recent is Resume and goes straight into the game, with
// the previous choices already made. Every other card opens its launch
// screen, which is where a different state is chosen. Cabinet's own comment
// on this: stopping at a screen with a Play button on it is two actions, not
// one, and Home promises one.
void App::activateHome() {
    // THE FIRST CARD ON RECENT IS RESUME, and it goes straight into the
    // game. This used to be a pill on the hero, and the objection recorded
    // against a shelf card launching directly was that it would be "a
    // second Resume that nothing on the screen says is one". That was
    // right, and it is answered rather than ignored: the shelf header says
    // Resume over this card and the card carries a play mark. It is the
    // only card on Home that behaves this way.
    //
    // Home promises one action from cold to playing, and this is it.
    if (haveResume() && focusRow == RowRecent && focusSlot == 0) {
        if (refuseRemoteStart(cards[heroIndex].id)) return;
        sound::play(sound::Cue::Activate);   // #389
        // Set only when the press counts (#484): refused because another
        // game is downloading, it cleared that game's Resume.
        if (launchById(cards[heroIndex].id)) {
            forgetLaunchAsks();
            pendingResume = true;
        }
        return;
    }
    // Every other cover on Home opens the launch screen, the same as a
    // cover anywhere else.
    if (const Card* c = cardAt(focusRow, focusSlot)) {
        for (size_t i = 0; i < cards.size(); ++i) {
            if (cards[i].id == c->id) {
                sound::play(sound::Cue::Activate);   // #389
                openDetailSoon(static_cast<int>(i));
                return;
            }
        }
    }
}

void App::leaveFocus() {
    if (cardAt(focusRow, focusSlot))
        slotAnim(slotFocus, focusRow, focusSlot).retarget(0.0f, kFocusDuration);
}

void App::enterFocus() {
    if (cardAt(focusRow, focusSlot))
        slotAnim(slotFocus, focusRow, focusSlot).retarget(1.0f, kFocusDuration);
}

void App::moveFocus(int delta) {
    const int slots = static_cast<int>(rowSlots(focusRow));
    if (slots <= 0) return;
    const int next = std::clamp(focusSlot + delta, 0, slots - 1);
    // The end of a row is a sound too. Silence here reads as the console
    // having stopped listening rather than as there being nothing there.
    if (next == focusSlot) { sound::play(sound::Cue::Edge); return; }
    leaveFocus();
    sound::play(sound::Cue::Move);
    focusSlot = next;
    enterFocus();
}

void App::moveRow(int delta) {
    // Two rows: past either end, or onto one that is not there (no
    // Favorites), nothing happens, as it never did.
    const int row = focusRow + delta;
    if (row < RowRecent || row > RowFavorites || !rowExists(row)) return;
    // LAND ON THE COVER ABOVE OR BELOW — #126, MMagTech 2026-09-27. Each
    // row used to remember its own slot and go back to it, so Down from
    // the third cover could land on the ninth. Now it is the cover nearest
    // the same place on screen, whatever each row has scrolled to, and the
    // last one when the other row is shorter. Going up to the bar and back
    // still returns to the same card: that is not a row move.
    const float pitch = kShelfCoverWidth + kShelfSpacing;
    const float x = static_cast<float>(focusSlot) * pitch - shelfScroll[focusRow].to;
    const float slot = std::round((x + shelfScroll[row].to) / pitch);
    leaveFocus();
    sound::play(sound::Cue::Move);
    focusRow = row;
    focusSlot = std::clamp(static_cast<int>(slot), 0, static_cast<int>(rowSlots(row)) - 1);
    enterFocus();
}

// Home's keys, routed through the same door as every other screen's so the
// bar can be offered them first. See `navigate`.
auto App::homeKey(screens::Nav n) -> bool {
    // AN EMPTY HOME SAYS "Press A to open the Library", and A does.
    // Up is still the bar.
    if (homeEmpty()) {
        switch (n) {
            case screens::Nav::Up:
                barSlot = BarHome;
                barFocused = true;
                sound::play(sound::Cue::Move);
                return true;
            case screens::Nav::Activate:
                transitionTo(1);
                sound::play(sound::Cue::Activate);
                return true;
            default:
                sound::play(sound::Cue::Edge);
                return true;
        }
    }
    switch (n) {
        case screens::Nav::Left:  moveFocus(-1); return true;
        case screens::Nav::Right: moveFocus(+1); return true;
        case screens::Nav::Down:  moveRow(+1); return true;
        case screens::Nav::Up:
            // Up out of the top shelf is the bar, the same as it is on the
            // Library and in a grid. This is the one that used to be a row
            // move into RowBar. Favorites is the top shelf when there is
            // no Recent.
            if (focusRow == RowRecent || !rowExists(RowRecent)) {
                leaveFocus();
                barSlot = BarHome;
                barFocused = true;
                sound::play(sound::Cue::Move);
            } else {
                moveRow(-1);
            }
            return true;
        case screens::Nav::Activate: activateHome(); return true;
        // Home is the root. Back has nowhere to go and says so.
        case screens::Nav::Back: sound::play(sound::Cue::Edge); return true;
    }
    return false;
}

// THE PRESS IS THE COVER'S ONLY WHEN THE COVER HAS THE PRESS. With the
// PIN pad, a question, Downloads or a menu over Home, A belongs to
// them, and the cover under them swelled with every digit of the PIN
// (MMagTech on the TV, 2026-10-04, entering it for Switch to Steam).
void App::retargetHomePress() {
    const bool pressIsHomes = pressing && !pinScreen.isOpen() && !choiceScreen.isOpen() &&
                              !downloadsPanel.isOpen() && !overlayOpen;
    if (cardAt(focusRow, focusSlot))
        slotAnim(slotPress, focusRow, focusSlot)
            .retarget(pressIsHomes ? 1.0f : 0.0f, kPressDuration);
}

void App::drawHome(float dt, float sc) {
    // The height a shelf will occupy, known before it is drawn so the
    // scroll target can be computed this frame rather than one frame late.
    // No caption row. The focused card's title rides in the shelf header
    // instead, which is what makes Home fit in one screen — see the
    // budget in design.h.
    const float shelfBlockHeight =
        text.lineHeight(ui::TextStyle::Title3, sc) + kShelfHeaderGap + kShelfHeadroom +
        kShelfCoverHeight + kShelfHeadroom;

    const float recentTop = barTop + barHeight + barGapBelow;
    // Favorites takes Recent's place when nothing has been played.
    const float favoritesTop = recentTop + (rowExists(RowRecent) ? shelfBlockHeight : 0.0f);

    // Scroll only as far as the focused row needs. On the bar or Recent
    // that is not at all — Home should not drift under the cursor. With
    // the hero gone it does not scroll at all at 1080, and the machinery
    // stays because a shelf's cover height is a number people change.
    //
    // And never past the end of the content. Pinning the last row to the
    // top of the screen leaves half a screen of nothing under it, which is
    // not what a scroll view does and reads as the layout having broken.
    const float contentHeight =
        (haveFavorites() ? favoritesTop + shelfBlockHeight : recentTop + shelfBlockHeight) +
        kHomeBottomPad;
    const float maxScroll = std::max(0.0f, contentHeight - ui::kCanvasHeight);
    float wantScroll = 0.0f;
    if (focusRow == RowFavorites)
        wantScroll = std::min(favoritesTop - recentTop, maxScroll);
    if (std::fabs(wantScroll - scrollY.to) > 0.5f)
        scrollY.retarget(wantScroll, kFocusDuration);
    const float scroll = scrollY.value();

    // ---- The shelves ----------------------------------------------------
    float shelfHeaderY = recentTop - scroll;
    screens::Ctx coverCtx{renderer, text, images, sc, &cards};

    // One shelf, drawn twice: Recent and Favorites are the same component in
    // different arrangements, which is what the design system says every
    // row on this screen is. Returns the height it used, so the caller can
    // stack the next one under it without either knowing the other's size.
    auto drawShelf = [&](const char* label, const std::vector<int>& indices,
                         int rowId, float top) -> float {
        // FOCUS IS IN ONE PLACE AT A TIME. When it is up in the bar — or
        // in the panel the chip opened — Home must stop drawing a focused
        // card, or two things look selected at once and pressing A appears
        // to do something to the wrong one.
        const bool rowFocused = (focusRow == rowId) && !barFocused && !accountsOpen;
        const size_t count = indices.size();
        if (count == 0) return 0.0f;
        auto at = [&](size_t slot) -> size_t { return static_cast<size_t>(indices[slot]); };

        // The shelf header: Title 3, with the chevron that says the row
        // continues into a screen of its own.
        //
        // TITLE 3 AND NOT TITLE 2, changed on the panel 2026-09-19. At
        // Title 2 the headings read too large beside 210-point covers —
        // the heading was shouting over the artwork it labels. The room
        // it frees goes to the hero.
        const float headerBaseline = top + text.ascent(ui::TextStyle::Title3, sc);
        text.draw(renderer, label, kContentInset, headerBaseline,
                  ui::TextStyle::Title3, ui::Color::white(1.0f), sc);
        const float headerWidth = text.measure(label, ui::TextStyle::Title3, sc);
        // A step below the heading, not level with it: the chevron says
        // "this row continues", it is not part of the heading, and at
        // heading size it competes with it.
        text.draw(renderer, "\xE2\x80\xBA", kContentInset + headerWidth + 10.0f,
                  headerBaseline, ui::TextStyle::Callout, ui::Color::white(0.30f), sc);

        // THE FOCUSED CARD'S TITLE, HERE RATHER THAN UNDER THE COVER.
        //
        // A caption under every cover reserved about 45 points per shelf
        // and Home could not afford two shelves with it. Putting the one
        // title you are actually reading into the header costs nothing
        // vertically, and it is only ever one line because only one card
        // is focused.
        //
        // Only on the focused row: a title under an unfocused shelf would
        // be a label for something nobody is pointing at, and two of them
        // at once reads as two headings.
        if (rowFocused && !cards.empty()) {
            const size_t slot = static_cast<size_t>(
                std::clamp(focusSlot, 0, static_cast<int>(count) - 1));
            const Card& fc = cards[at(slot)];
            float titleX = kContentInset + headerWidth + 10.0f +
                           text.measure("\xE2\x80\xBA", ui::TextStyle::Callout, sc) +
                           24.0f;
            // RESUME IS SAID, NOT IMPLIED. This one card launches straight
            // into the game where every other cover on Home opens a launch
            // screen, and the objection recorded against that was exactly
            // that nothing on the screen would say so. Now something does.
            if (rowId == RowRecent && slot == 0 && haveResume()) {
                const char* kResume = "\xE2\x96\xB6  Resume";
                text.draw(renderer, kResume, titleX, headerBaseline,
                          ui::TextStyle::Callout, ui::Color::white(0.95f), sc);
                titleX += text.measure(kResume, ui::TextStyle::Callout, sc) + 20.0f;
            }
            const float room = ui::kCanvasWidth - kContentInset - titleX;
            // The platform after the name, which the hero's band used to
            // carry and nothing has carried since. A library with a Game
            // Boy and a Genesis "Altered Beast" in it needs this said.
            const std::string platform =
                fc.platform.empty() ? "" : "   \xC2\xB7   " + fc.platform;
            // RomM's tags between the two, dimmer, when another copy of
            // this game is on the same shelf (#383). Both survive; the
            // name is what gets shortened.
            if (design::hasTwin(cards, indices, static_cast<int>(at(slot)))) {
                const float tagW = text.measure(fc.tagLabel, ui::TextStyle::Callout, sc);
                const float platW = text.measure(platform, ui::TextStyle::Callout, sc);
                const std::string name =
                    text.truncate(fc.title, ui::TextStyle::Callout, sc,
                                  std::max(0.0f, room - tagW - platW - 24.0f));
                float lx = titleX;
                text.draw(renderer, name, lx, headerBaseline, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc);
                lx += text.measure(name, ui::TextStyle::Callout, sc) + 24.0f;
                text.draw(renderer, fc.tagLabel, lx, headerBaseline, ui::TextStyle::Callout,
                          ui::Color::white(0.45f), sc);
                lx += tagW;
                text.draw(renderer, platform, lx, headerBaseline, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc);
            } else {
                text.draw(renderer,
                          text.truncate(fc.title + platform, ui::TextStyle::Callout, sc, room),
                          titleX, headerBaseline, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc);
            }
        }

        const float shelfTop =
            top + text.lineHeight(ui::TextStyle::Title3, sc) + kShelfHeaderGap + kShelfHeadroom;

        // CULL TO WHAT IS ON SCREEN, and do it before touching the cover
        // cache. This loop used to run over every card: invisible with a
        // six-entry stand-in library, and against a real 1232 it queued a
        // download for every cover in the collection and overran the
        // texture budget. A shelf is a window onto a list, not a drawing of
        // the list. The margin keeps a card's art loading just before it
        // slides in, so the fade has somewhere to start.
        const float kCullMargin = (kShelfCoverWidth + kShelfSpacing) * 2.0f;
        // THE ROW FOLLOWS FOCUS, and only as far as it has to. Moving
        // right, the row slides once the focused card would cross the
        // right edge, and it stops where the NEXT card still peeks in, so
        // there is visibly more to come. Moving back left, it slides once
        // focus would cross the left inset. The last card rests against
        // the right edge; the first against the left inset.
        const float pitch = kShelfCoverWidth + kShelfSpacing;
        if (rowId == RowRecent || rowId == RowFavorites) {
            Animated& rs = shelfScroll[rowId];
            if (rowFocused) {
                const float left = kContentInset + static_cast<float>(focusSlot) * pitch;
                const bool last = focusSlot >= static_cast<int>(count) - 1;
                const float peek = last ? 0.0f : kShelfSpacing + kShelfCoverWidth * 0.35f;
                const float rightEdge = ui::kCanvasWidth - kContentInset - peek;
                float target = rs.to;
                if (left + kShelfCoverWidth - target > rightEdge)
                    target = left + kShelfCoverWidth - rightEdge;
                if (left - target < kContentInset) target = left - kContentInset;
                target = std::max(0.0f, target);
                rs.retarget(target, kFocusDuration * 1.6f);
            }
            rs.tick(dt);
        }
        const float scroll =
            (rowId == RowRecent || rowId == RowFavorites) ? shelfScroll[rowId].value() : 0.0f;
        auto cardBaseX = [&](size_t slot) {
            return kContentInset + static_cast<float>(slot) * pitch - scroll;
        };

    // Unfocused cards first, so a focused card's shadow and rim land on top
    // of its neighbours rather than under them.
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t slot = 0; slot < count; ++slot) {
            const size_t i = at(slot);
            const bool isFocused = rowFocused && (static_cast<int>(slot) == focusSlot);
            if ((pass == 0) == isFocused) continue;

            const float cullX = cardBaseX(slot);
            if (cullX + kShelfCoverWidth < -kCullMargin) continue;
            if (cullX > ui::kCanvasWidth + kCullMargin) break;

            const Card& card = cards[i];
            // THE LIFT FOLLOWS FOCUS EVERY FRAME, as the grid's does, so
            // focus going up to the bar, into the account panel, or
            // arriving fresh from the bar all look right without each
            // path remembering to say so.
            Animated& lift = slotAnim(slotFocus, rowId, static_cast<int>(slot));
            lift.retarget(isFocused ? 1.0f : 0.0f, kFocusDuration);
            const float f = lift.value();
            const float p = slotAnim(slotPress, rowId, static_cast<int>(slot)).value();

            // Pressed reads as a push INTO the screen, against the focused
            // lift, so a click still registers on a card that is already
            // raised.
            const float scale = 1.0f + f * (kFocusScale - 1.0f) -
                                p * (kFocusScale - kPressScale);

            const float baseX = cullX;
            const float w = kShelfCoverWidth * scale;
            const float h = kShelfCoverHeight * scale;
            const float x = baseX - (w - kShelfCoverWidth) * 0.5f;
            const float y = shelfTop - (h - kShelfCoverHeight) * 0.5f;

            // THE GRID'S COVER, NOT A COPY OF IT. Home had its own, which
            // drifted: no grey for a game that cannot be played here, and a
            // kept dot that grew with focus (#398, HOME-07).
            screens::drawCover(coverCtx, card, x, y, w, h, kCoverRadius * scale, f,
                               /*rim=*/true);

            // No caption under the card: the focused card's title is in
            // the shelf header.
        }
    }
        // Header, headroom, cover, headroom. No caption row: the focused
        // title is in the header, so it costs nothing here.
        return (shelfTop - top) + kShelfCoverHeight + kShelfHeadroom;
    };

    float rowY = shelfHeaderY;
    rowY += drawShelf("Recent", shelf, RowRecent, rowY);
    // Only when there are any. An empty Favorites row is worse than none.
    if (haveFavorites()) drawShelf("Favorites", favorites, RowFavorites, rowY);
    // SAID, NOT LEFT BLANK, with the way out under it. The first version
    // lit Library in the bar instead, which read as already being on the
    // Library: MMagTech pressed A on what he took for a loaded Library and
    // it only then opened. Home stays Home; the button is the suggestion.
    if (homeEmpty()) {
        const char* title = "Nothing played yet";
        // One line. A second, pointing at the Library, was dropped on
        // MMagTech's word: Library is already lit in the bar.
        const char* detail = "Games you play or favorite show up here.";
        const float tw = text.measure(title, ui::TextStyle::Title2, sc);
        const float dw = text.measure(detail, ui::TextStyle::Callout, sc);
        const float ty = ui::kCanvasHeight * 0.45f;
        text.draw(renderer, title, (ui::kCanvasWidth - tw) * 0.5f, ty,
                  ui::TextStyle::Title2, ui::Color::white(0.95f), sc);
        text.draw(renderer, detail, (ui::kCanvasWidth - dw) * 0.5f,
                  ty + text.lineHeight(ui::TextStyle::Title2, sc) * 0.9f,
                  ui::TextStyle::Callout, ui::Color::white(0.60f), sc);
        // AN INSTRUCTION, NOT A BUTTON. A focused Open Library button was
        // tried twice, quiet and then white, and neither read as selected,
        // because nobody had moved focus onto it. MMagTech, 2026-09-24:
        // *"since one didn't navigate to end up there it doesn't read as
        // already being selected... it should Press A to Open Library."*
        // The A is drawn as a button badge, the way consoles prompt. It
        // dims while focus is up in the bar, where A means something else.
        const bool on = !barFocused && !accountsOpen;
        drawPressPrompt(renderer, text, sc, "Press", "to open the Library",
                        ty + text.lineHeight(ui::TextStyle::Title2, sc) * 0.9f + 96.0f,
                        on ? 1.0f : 0.45f);
    }
}
