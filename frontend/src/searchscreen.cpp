// Search's screen: the results over the docked keyboard (SearchScreen,
// searchscreen.h). The asking and the keyboard are the app's (search.cpp).
// Moved out of screens.cpp in the restructure (#398).

#include "searchscreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "screenparts.h"
#include "sound.h"

namespace screens {

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

void SearchScreen::open() {
    // Everything setQuery("") drops, not only the box and the covers: a state
    // and total left from the last visit drew "0 of 200 games" over an empty
    // box (#277).
    query_.clear();
    results_.clear();
    resultsFor_.clear();
    total_ = 0;
    state_ = State::Empty;
    failure_.clear();
    slot_ = 0;
    focused_ = false;
    scroll_.from = scroll_.to = 0.0f;
    scroll_.elapsed = scroll_.duration;
}

void SearchScreen::setQuery(const std::string& q) {
    if (q == query_) return;
    query_ = q;
    slot_ = 0;
    scroll_.retarget(0.0f, design::kFocusDuration);
    if (query_.empty()) {
        // Back to nothing typed, which is a different screen from "nothing
        // matched" and has to drop the previous answer with it — results left
        // standing under an empty box are the last query's, silently.
        results_.clear();
        resultsFor_.clear();
        total_ = 0;
        state_ = State::Empty;
        failure_.clear();
    }
}

void SearchScreen::setWaiting() {
    if (query_.empty()) return;
    state_ = State::Waiting;
    failure_.clear();
}

void SearchScreen::setFailed(const std::string& why) {
    state_ = State::Failed;
    failure_ = why;
    results_.clear();
    resultsFor_.clear();
    total_ = 0;
    slot_ = 0;
}

void SearchScreen::setResults(const std::string& forQuery, std::vector<int> results,
                              int total) {
    // AN ANSWER TO A QUESTION NOBODY IS ASKING ANY MORE IS DROPPED. Requests
    // can land out of order, and showing the results for "mar" under a box
    // that now reads "mario" is the failure this guard exists for.
    if (forQuery != query_) return;
    results_ = std::move(results);
    resultsFor_ = forQuery;
    total_ = total;
    failure_.clear();
    state_ = results_.empty() ? State::NoMatches : State::Results;
    slot_ = 0;
    scroll_.retarget(0.0f, design::kFocusDuration);
}

void SearchScreen::tick(float dt, Ctx& c) {
    scroll_.tick(dt);
    // The results' lift and press: the frame loop ticks every card once
    // (frame.cpp, #484, HOME-06), as GridScreen::tick says.
}

Result SearchScreen::key(Nav n) {
    const int count = static_cast<int>(results_.size());
    if (n == Nav::Back) return {Action::Back, 0};
    if (count == 0) return {};
    if (n == Nav::Activate) return {Action::OpenGame, results_[slot_]};
    // Up out of the results is the bar, the same as every other screen.
    if (n == Nav::Up) return {Action::FocusBar, 0};
    // Down goes back to the keyboard, which is the app's to hand focus to.
    if (n == Nav::Down) return {Action::FocusKeyboard, 0};
    const int d = (n == Nav::Right) ? 1 : -1;
    const int next = std::clamp(slot_ + d, 0, count - 1);
    if (next == slot_) { sound::play(sound::Cue::Edge); return {}; }
    slot_ = next;
    sound::play(sound::Cue::Move);
    return {};
}

void SearchScreen::draw(Ctx& c) {
    if (!c.cards) return;
    const float top = design::kContentTop;
    const float headingH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) + 12.0f;

    // The covers are sized to the room the keyboard leaves, not to a constant.
    // The panel's height depends on its layout and whether it is docked, so a
    // hardcoded cover size would be correct until somebody added a row of keys.
    const float room = resultsBottom_ - (top + headingH) - design::kShelfBreathing * 2.0f;
    const float ch = std::clamp(room, 120.0f, 320.0f);
    const float cw = ch * 0.75f;
    const float coversTop = top + headingH + design::kShelfBreathing;

    // The heading says what happened, because an empty screen that says nothing
    // is indistinguishable from one that is broken.
    // FIVE STATES AND FIVE SENTENCES. "Nothing matches" used to cover all of
    // them, which was honest while the answer was a local substring match and
    // became a lie the moment the answer came from a server: a console that
    // could not ASK would have said the library held nothing like it.
    char line[96];
    switch (state_) {
        // NOTHING YET, NOTHING SAID WHILE THE KEYS ARE UP: the bar above
        // already says Search and the keyboard fills the screen, so a heading
        // of "Search" read the same word twice (MMagTech on the TV,
        // 2026-10-14). Hidden with nothing typed, the page would be empty
        // under the bar, so the heading comes back.
        case State::Empty:
            std::snprintf(line, sizeof line, "%s", keysUp_ ? "" : "Search");
            break;
        case State::Waiting:
            std::snprintf(line, sizeof line, "Searching\xE2\x80\xA6");
            break;
        case State::NoMatches:
            std::snprintf(line, sizeof line, "Nothing matches");
            break;
        case State::Failed:
            // Names the server, not the library. The person typed a word and
            // the console could not go and look.
            std::snprintf(line, sizeof line, "Could not reach the server");
            break;
        case State::Results:
            // `total` is the server's count of what matched, which is larger
            // than what came back whenever the first page was not the whole of
            // it. Saying "20 games" for a search that found four hundred is
            // the kind of quiet wrongness this project keeps paying for.
            if (total_ > static_cast<int>(results_.size()))
                std::snprintf(line, sizeof line, "%zu of %d games",
                              results_.size(), total_);
            else
                std::snprintf(line, sizeof line, "%zu game%s", results_.size(),
                              results_.size() == 1 ? "" : "s");
            break;
    }
    c.text.draw(c.r, line, design::kContentInset,
                top + c.text.ascent(ui::TextStyle::Title3, c.sc),
                ui::TextStyle::Title3, ui::Color::white(1.0f), c.sc);

    // The focused game's name beside it, the way Home's shelf header and the
    // grid's heading both do it: results mix systems, so the system follows
    // the name (#579), and RomM's tags when another copy of this game is
    // among the results (#383). See drawFocusedLine.
    const int fi = focusedCard();
    if (focused_ && fi >= 0 && fi < static_cast<int>(c.cards->size())) {
        const float x = design::kContentInset +
                        c.text.measure(line, ui::TextStyle::Title3, c.sc) + 24.0f;
        const float roomW = ui::kCanvasWidth - design::kContentInset - x;
        const float y = top + c.text.ascent(ui::TextStyle::Title3, c.sc);
        drawFocusedLine(c.r, c.text, c.sc, (*c.cards)[fi],
                        /*tags=*/design::hasTwin(*c.cards, results_, fi),
                        LineNames::System, c.on, x, y, roomW, 0.60f);
    }

    if (results_.empty()) return;

    // One row, scrolling sideways under the focus. A grid would need the height
    // the keyboard is standing on.
    const float pitch = cw + design::kShelfSpacing;
    const float usable = ui::kCanvasWidth - design::kContentInset * 2.0f;
    const float focusX = static_cast<float>(slot_) * pitch;
    float want = scroll_.to;
    if (focusX - want < 0.0f) want = focusX;
    if (focusX + cw - want > usable) want = focusX + cw - usable;
    want = std::max(0.0f, want);
    if (std::fabs(want - scroll_.to) > 0.5f)
        scroll_.retarget(want, design::kFocusDuration);
    const float scroll = scroll_.value();

    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < results_.size(); ++i) {
            const bool isFocused = focused_ && static_cast<int>(i) == slot_;
            if ((pass == 0) == isFocused) continue;
            const int idx = results_[i];
            if (idx < 0 || idx >= static_cast<int>(c.cards->size())) continue;
            const float bx = design::kContentInset +
                             static_cast<float>(i) * pitch - scroll;
            if (bx + cw < -pitch) continue;
            if (bx > ui::kCanvasWidth + pitch) break;

            design::Card& card = (*c.cards)[static_cast<size_t>(idx)];
            card.focus.retarget(isFocused ? 1.0f : 0.0f, design::kFocusDuration);
            const float f = card.focus.value();
            const float s = 1.0f + f * (design::kFocusScale - 1.0f);
            const float w = cw * s, h = ch * s;
            drawCover(c, card, bx - (w - cw) * 0.5f, coversTop - (h - ch) * 0.5f, w, h,
                      design::kGridCoverRadius * s, f, /*rim=*/true);
        }
    }
}

}  // namespace screens
