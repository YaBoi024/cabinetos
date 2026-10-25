// A grid of games: one system's or one collection's games, with the letter
// index the triggers bring up (GridScreen, grid.h). Moved out of screens.cpp
// in the restructure (#398).

#include "grid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "panels.h"
#include "screenparts.h"
#include "sound.h"

namespace screens {

using design::Card;

// ---------------------------------------------------------------------------
// A grid of games
// ---------------------------------------------------------------------------

void GridScreen::open(std::string title, bool namesSystem, std::vector<int> cards,
                      const std::vector<design::Card>& all) {
    title_ = std::move(title);
    namesSystem_ = namesSystem;
    cards_ = std::move(cards);
    slot_ = 0;

    // The letter index, built once. The list arrives sorted by name — that is
    // the Library's doing, not this screen's — so the distinct initials come
    // out in order by walking it.
    //
    // ANYTHING THAT IS NOT A LETTER IS '#', which is one bucket rather than
    // several: a library holds "1080 Snowboarding", "3D Lemmings" and
    // "@Home", and three buckets of one game each at the top of the index
    // would be three targets nobody wants to land on separately.
    letters_.clear();
    letterFirst_.clear();
    for (size_t i = 0; i < cards_.size(); ++i) {
        const int idx = cards_[i];
        if (idx < 0 || idx >= static_cast<int>(all.size())) continue;
        // The index covers the playable games only. Greyed ones follow them
        // in an alphabet of their own, and a second A to Z down the side
        // would send a jump to the wrong one.
        if (!all[static_cast<size_t>(idx)].unavailable.empty()) continue;
        // THE SORT KEY'S LETTER, NOT THE NAME'S. The list is in RomM's order,
        // which drops a leading "The" or "A" and can file a game under another
        // name; read off the title, "The Combatribes" put a T among the C's
        // and a jump landed there (A9, 2026-10-10).
        const design::Card& card = all[static_cast<size_t>(idx)];
        const std::string& n = card.sortKey.empty() ? card.title : card.sortKey;
        char c = n.empty() ? '#' : static_cast<char>(std::toupper(
            static_cast<unsigned char>(n[0])));
        if (c < 'A' || c > 'Z') c = '#';
        if (letters_.empty() || letters_.back() != c) {
            letters_.push_back(c);
            letterFirst_.push_back(static_cast<int>(i));
        }
    }
    index_.from = index_.to = 0.0f;
    index_.elapsed = index_.duration;
    wave_.settle(0.0f);
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);
    scroll_.retarget(0.0f, 0.0f);
    scroll_.elapsed = scroll_.duration;
}

void GridScreen::append(const std::vector<int>& more,
                        const std::vector<design::Card>& all) {
    if (more.empty()) return;
    // The letter index is EXTENDED rather than rebuilt, which is the whole
    // reason appending is safe: `letterFirst_` holds positions into `cards_`,
    // and every existing one still points at the same card after a push_back.
    // Rebuilding would be correct too and is not worth the risk of a jump
    // under somebody's thumb.
    for (int idx : more) {
        if (idx < 0 || idx >= static_cast<int>(all.size())) continue;
        // A GREYED GAME GOES ON THE END; a playable one goes before the first
        // greyed one, so playable games stay first as pages arrive, as
        // gridOrder (library.cpp) put them when the grid opened. Everything
        // after that point moves down one, so focus there moves with its
        // card; the letter index only ever points before it.
        if (!all[static_cast<size_t>(idx)].unavailable.empty()) {
            cards_.push_back(idx);
            continue;
        }
        size_t at = 0;
        while (at < cards_.size() && all[static_cast<size_t>(cards_[at])].unavailable.empty()) ++at;
        cards_.insert(cards_.begin() + static_cast<long>(at), idx);
        if (at < cards_.size() - 1 && slot_ >= static_cast<int>(at)) ++slot_;
        // THE SORT KEY'S LETTER, NOT THE NAME'S. The list is in RomM's order,
        // which drops a leading "The" or "A" and can file a game under another
        // name; read off the title, "The Combatribes" put a T among the C's
        // and a jump landed there (A9, 2026-10-10).
        const design::Card& card = all[static_cast<size_t>(idx)];
        const std::string& n = card.sortKey.empty() ? card.title : card.sortKey;
        char c = n.empty() ? '#' : static_cast<char>(std::toupper(
            static_cast<unsigned char>(n[0])));
        if (c < 'A' || c > 'Z') c = '#';
        if (letters_.empty() || letters_.back() != c) {
            letters_.push_back(c);
            letterFirst_.push_back(static_cast<int>(at));
        }
    }
    // Focus and scroll are otherwise untouched. Somebody is looking at row
    // three; growing the list below them must not move them.
}

int GridScreen::columns() const {
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = static_cast<int>((usable + design::kGridColumnSpacing) /
                                   (design::kGridCoverMin + design::kGridColumnSpacing));
    return std::max(1, n);
}

float GridScreen::coverWidth() const {
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = columns();
    return (usable - design::kGridColumnSpacing * static_cast<float>(n - 1)) /
           static_cast<float>(n);
}

int GridScreen::letterOf(int slot) const {
    int at = -1;
    for (size_t i = 0; i < letterFirst_.size(); ++i)
        if (letterFirst_[i] <= slot) at = static_cast<int>(i);
    return at;
}

bool GridScreen::jumpLetter(int dir) {
    if (letters_.empty()) { sound::play(sound::Cue::Edge); return false; }
    const int at = letterOf(slot_);
    // An index coming up from nothing starts its swell where focus already
    // is, rather than gliding in from wherever it was last time.
    if (index_.value() <= 0.01f) wave_.settle(static_cast<float>(std::max(at, 0)));
    // GOING BACK FROM THE MIDDLE OF A LETTER GOES TO ITS OWN START FIRST, the
    // way a track-skip button does: pressing back inside the M's should reach
    // the first M, not jump past every one of them into the L's.
    int want = at;
    if (dir > 0) {
        want = at + 1;
    } else {
        if (at >= 0 && letterFirst_[static_cast<size_t>(at)] < slot_) want = at;
        else want = at - 1;
    }
    if (want < 0 || want >= static_cast<int>(letters_.size())) {
        sound::play(sound::Cue::Edge);
        // Still show the index: hitting the end of the alphabet is an answer,
        // and the index is what makes it a legible one.
        index_.retarget(1.0f, 0.150f);
        indexHold_ = 1.600f;
        return false;
    }
    slot_ = letterFirst_[static_cast<size_t>(want)];
    sound::play(sound::Cue::Move);
    index_.retarget(1.0f, 0.150f);
    indexHold_ = 1.600f;
    wave_.retarget(static_cast<float>(want), design::kFocusDuration);
    return true;
}

void GridScreen::tick(float dt, Ctx& c) {
    appear_.tick(dt);
    scroll_.tick(dt);
    index_.tick(dt);
    wave_.tick(dt);
    // It stays up while it is being used and goes when it is not.
    if (indexHold_ > 0.0f) {
        indexHold_ -= dt;
        if (indexHold_ <= 0.0f) index_.retarget(0.0f, 0.600f);
    }
    // The covers' own lift and press are not ticked here: the frame loop
    // ticks every card once (frame.cpp). Ticked here too, a grid's lift
    // took about 90 ms where design.h says 180 (#484, HOME-06).
}

Result GridScreen::key(Nav n) {
    const int count = static_cast<int>(cards_.size());
    if (n == Nav::Back) return {Action::Back, 0};
    if (count == 0) return {};
    if (n == Nav::Activate) return {Action::OpenGame, cards_[slot_]};

    const int cols = columns();
    // Up out of the top row of covers is the bar, the same as it is on every
    // other screen the bar is drawn over.
    if (n == Nav::Up && slot_ < cols) return {Action::FocusBar, 0};
    int next = slot_;
    if (n == Nav::Left || n == Nav::Right) {
        const int d = (n == Nav::Right) ? 1 : -1;
        const int rowStart = (slot_ / cols) * cols;
        const int rowEnd = std::min(rowStart + cols - 1, count - 1);
        next = std::clamp(slot_ + d, rowStart, rowEnd);
    } else {
        const int d = (n == Nav::Down) ? 1 : -1;
        next = slot_ + d * cols;
        // The last row is ragged. Stepping down from a column that row does not
        // have should land on its last entry rather than refusing to move.
        if (d > 0 && next >= count && slot_ / cols < (count - 1) / cols)
            next = count - 1;
        if (next < 0 || next >= count) { sound::play(sound::Cue::Edge); return {}; }
    }
    if (next == slot_) sound::play(sound::Cue::Edge);
    else sound::play(sound::Cue::Move);
    slot_ = next;
    return {};
}

void GridScreen::draw(Ctx& c) {
    c.r.setContentAlpha(appear_.value());
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
    if (!c.cards) return;
    auto& cards = *c.cards;
    const int cols = columns();
    const float cw = coverWidth();
    const float ch = cw * 4.0f / 3.0f;
    const float rowPitch = ch + design::kGridCaptionGap + design::kGridRowSpacing;

    // THE HEADING STAYS (2026-10-13). It carries the focused game's name (no
    // captions under the covers, design.h's grid notes), and when it
    // scrolled away with the covers the name went with it: two rows down,
    // nothing said which game was selected. Reported to MMagTech, who chose
    // keeping it in place. So the covers scroll under it instead: clipped at
    // its foot, and each one fading out as it slides beneath, rather than cut.
    const float chipH = c.text.lineHeight(ui::TextStyle::ScreenTitle, c.sc) +
                        design::kScreenChipPadY * 2.0f;
    const float gridTop = design::kSwitcherTop + chipH + design::kSwitcherGapBelow;
    const float clipTop = design::kSwitcherTop + chipH;
    c.r.setScissor(0, clipTop, ui::kCanvasWidth, ui::kCanvasHeight - clipTop);
    const float appear = appear_.value();

    const int count = static_cast<int>(cards_.size());
    const int rows = count == 0 ? 0 : (count + cols - 1) / cols;
    const float contentH = gridTop + static_cast<float>(rows) * rowPitch;
    // WHOLE ROWS ONLY (MMagTech, 2026-10-13): every resting scroll is a
    // multiple of the row pitch, so the top row sits exactly under the heading
    // and the row above is out of sight (54 points of gap and spacing against
    // the 28 under the heading). A scroll that stopped wherever the focused
    // row first fitted left half a row of covers ending at the heading, which
    // read as a hard line however it was faded.
    const float maxScroll =
        std::ceil(std::max(0.0f, contentH - ui::kCanvasHeight) / rowPitch) * rowPitch;

    const int focusedRow = count == 0 ? 0 : slot_ / cols;
    const float rowTop = gridTop + static_cast<float>(focusedRow) * rowPitch;
    // HEADROOM FOR THE FOCUSED CARD, which grows by a tenth: without it the
    // lifted card is clipped against the top of the screen on the first row it
    // scrolls to. Half of (scale - 1) times the height, which is exactly how
    // far a scale about the centre advances an edge.
    const float lift = ch * (design::kFocusScale - 1.0f) * 0.5f + 8.0f;
    float want = scroll_.to;
    if (rowTop - want < gridTop) want = std::max(0.0f, rowTop - gridTop);
    const float rowBottom = rowTop + ch + design::kGridCaptionGap + lift;
    if (rowBottom - want > ui::kCanvasHeight - design::kScreenBottomPad)
        want = std::ceil((rowBottom - (ui::kCanvasHeight - design::kScreenBottomPad)) /
                         rowPitch) * rowPitch;
    want = std::clamp(want, 0.0f, maxScroll);
    if (std::fabs(want - scroll_.to) > 0.5f)
        scroll_.retarget(want, design::kFocusDuration);
    const float scroll = scroll_.value();

    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < count; ++i) {
            const bool isFocused = (i == slot_);
            if ((pass == 0) == isFocused) continue;
            const int idx = cards_[i];
            if (idx < 0 || idx >= static_cast<int>(cards.size())) continue;

            const int col = i % cols;
            const int r = i / cols;
            const float bx = design::kLibraryInset +
                             static_cast<float>(col) * (cw + design::kGridColumnSpacing);
            const float by = gridTop + static_cast<float>(r) * rowPitch - scroll;
            if (by + ch < -rowPitch) continue;
            if (by > ui::kCanvasHeight + rowPitch) break;

            Card& card = cards[idx];
            card.focus.retarget(isFocused ? 1.0f : 0.0f, design::kFocusDuration);
            const float f = card.focus.value();
            const float p = card.press.value();
            // Pressed reads as a push INTO the screen against the focused lift,
            // so a click still registers on a card that is already raised.
            const float s = 1.0f + f * (design::kFocusScale - 1.0f) -
                            p * (design::kFocusScale - design::kPressScale);
            const float w = cw * s, h = ch * s;
            const float x = bx - (w - cw) * 0.5f;
            const float y = by - (h - ch) * 0.5f;

            // WHILE THE LIST MOVES, a row sliding up under the heading fades by
            // how much of it is still below the gap, so it dissolves rather than
            // being cut. At rest no row is there (whole-row scrolling, above).
            // The focused card is always whole.
            float row = 1.0f;
            if (y < gridTop && !isFocused) {
                const float shown = std::clamp((y + h - gridTop) / h, 0.0f, 1.0f);
                const float t = std::clamp((shown - 0.15f) / 0.7f, 0.0f, 1.0f);
                row = t * t * (3.0f - 2.0f * t);
            }
            c.r.setContentAlpha(appear * row);
            drawCover(c, card, x, y, w, h, design::kGridCoverRadius * s, f, /*rim=*/true);
            c.r.setContentAlpha(appear);
            // NO CAPTION HERE. The focused card's title is drawn once, beside
            // the screen's heading: see drawGlass, and design.h's grid notes
            // for what that bought and what it cost.
        }
    }
}

void GridScreen::drawGlass(Ctx& c) {
    c.r.setContentAlpha(appear_.value());
    // FIRST, AND ON ITS OWN. It was drawn at the end of this function, after
    // the heading's `return` for a heading scrolled off the top, so it showed
    // only near the top of a list: MMagTech saw it once in Arcade and never
    // again, while the jumps kept working (2026-10-10).
    drawIndex(c);
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
    // THE TITLE IS TEXT, NOT A CHIP — changed 2026-09-21, and this is a
    // DEPARTURE FROM docs/PROJECT.md, which describes it as "a static glass
    // capsule, not a button". That description was right when it was written:
    // on tvOS this title painted straight over scrolling artwork and the glass
    // was what kept it readable.
    //
    // Two things changed underneath it. The screen now has a scrimmed backdrop
    // of its own, so a title on it is legible without a surface; and the
    // Library's switcher lost its capsule for consistency with Home's shelf
    // headings, which left this the only capsule in the product hanging off the
    // left margin to keep its own label aligned. A heading is a heading on all
    // three screens now. PROJECT.md's design system section needs this.
    //
    // 40 bold stays. It is the one hardcoded size in the whole reference UI and
    // it sits between Title 2 and Large Title deliberately: a grid title should
    // not shout as loudly as a game's own name does.
    // STAYS PUT (2026-10-13; it used to scroll away with the covers, as the
    // Library's switcher does). It holds the focused game's name, which has to
    // be readable whichever row is focused. See draw().
    const float y = design::kSwitcherTop;
    const float baseline = y + design::kScreenChipPadY +
                           c.text.ascent(ui::TextStyle::ScreenTitle, c.sc);
    c.text.draw(c.r, title_, design::kLibraryInset, baseline,
                ui::TextStyle::ScreenTitle, ui::Color::white(1.0f), c.sc);

    // THE FOCUSED GAME'S NAME, BESIDE THE HEADING, which is where Home puts the
    // focused card's title and for the same reason: it costs no vertical space
    // and it is the only one of forty names anybody is reading. With it, the
    // count — the Library's tile knew how many games a platform holds and this
    // screen used to forget it on the way in.
    if (!c.cards || cards_.empty()) return;
    float x = design::kLibraryInset +
              c.text.measure(title_, ui::TextStyle::ScreenTitle, c.sc) + 28.0f;
    char count[64];
    // A COUNT THAT IS STILL CLIMBING SAYS SO. A big platform arrives a page at
    // a time, and a number that silently ticks upward while somebody reads it
    // is worse than one that admits it is not finished.
    if (loadingMore_)
        std::snprintf(count, sizeof count, "%zu game%s\xE2\x80\xA6", cards_.size(),
                      cards_.size() == 1 ? "" : "s");
    else
        std::snprintf(count, sizeof count, "%zu game%s", cards_.size(),
                      cards_.size() == 1 ? "" : "s");
    c.text.draw(c.r, count, x, baseline, ui::TextStyle::Callout,
                ui::Color::white(0.45f), c.sc);
    x += c.text.measure(count, ui::TextStyle::Callout, c.sc) + 24.0f;

    const int idx = focusedCard();
    if (idx >= 0 && idx < static_cast<int>(c.cards->size())) {
        // After the name: RomM's tags when another copy of this game is in
        // the grid (#383), the system in a collection (#579), and a greyed
        // game's reason. See drawFocusedLine.
        const float room = ui::kCanvasWidth - design::kLibraryInset - x - design::kLetterIndexWidth;
        drawFocusedLine(c.r, c.text, c.sc, (*c.cards)[idx],
                        /*tags=*/design::hasTwin(*c.cards, cards_, idx),
                        /*system=*/namesSystem_, /*reason=*/true, x, baseline, room, 0.95f);
    }
}

// --- The letter index, down the right ---------------------------------------
//
// OUTSIDE THE SCROLL WINDOW, because it is not part of the list: it is a
// readout of where you are in it, and it may sit beside the bar.
//
// A SHORT COLUMN OF SEVEN LETTERS, MADE OF THE CONSOLE'S OWN PARTS (MMagTech,
// 2026-10-10): the one focus is in, in the middle, and three either side. "If
// I scrolled past M, I still don't need to see A." And: "we put so much effort
// into the UI of this and consistency ... it just doesn't even look like it
// fits in it." So nothing here is new:
//   * the panel is the browsing screens' regular glass (the game page's rows,
//     kRegularMaterialBlur) with the panels' lift shadow and hairline edge.
//     Glass takes its colour from the backdrop behind it, which is the
//     signed-in person's colour, so it matches every colour without a branch;
//   * the current letter is marked the way the top bar marks focus: the thin
//     glass capsule behind it and full white, the others at the bar's 45%;
//   * the type is Title 3, the bar's and the Settings rows', one size for all;
//   * a jump slides the column one slot over the focus duration, the way focus
//     moves between covers.
//
// TRIED AND REJECTED THE SAME EVENING, so they are not tried again: the whole
// alphabet small on a black pill ("really bad"); the whole alphabet with a
// Dock-style swell ("super tiny", then "crammed ... overflowing"); a window of
// swelling letters on a panel that resized ("this heart beating ... hideous").
void GridScreen::drawIndex(Ctx& c) {
    const float fade = index_.value();
    if (fade <= 0.01f || letters_.empty()) return;
    c.r.clearScissor();
    // ONE FADE FOR THE WHOLE COLUMN, through the renderer's content alpha.
    // Faded colour by colour, the tint, letters and capsule went smoothly but
    // the glass's blur does not read a colour's alpha, only this, so it stayed
    // fully frosted to the last frame and then vanished: "not fading out as a
    // smooth element ... like it's in two phases" (MMagTech, 2026-10-10).
    const float before = c.r.contentAlpha();
    c.r.setContentAlpha(before * fade);
    constexpr float a = 1.0f;

    constexpr ui::TextStyle kStyle = ui::TextStyle::Title3;
    constexpr int kSide = 3;                 // letters either side of the current
    constexpr float kRestText = 0.45f;       // the bar's unselected tab
    constexpr float kFarther = 0.12f;        // and a little less per step away

    const float lineH = c.text.lineHeight(kStyle, c.sc);
    const float capH = lineH + design::kPillPadY * 2.0f;   // the bar's capsule
    const float pitch = capH + 6.0f;
    const float panelW = design::kLetterIndexWidth;
    const float panelH = pitch * static_cast<float>(2 * kSide) + capH + 2.0f * 18.0f;
    const float cx = ui::kCanvasWidth - design::kLibraryInset - panelW * 0.5f;
    const float midY = ui::kCanvasHeight * 0.5f;
    const float px = cx - panelW * 0.5f, py = midY - panelH * 0.5f;

    // The lift under the glass, as the game page's panel has.
    {
        ui::Rect lift{px, py, panelW, panelH, panelW * 0.5f, ui::Color::white(0)};
        lift.shadowBlur = design::kOverlayPanelShadowBlur;
        lift.shadowOffsetY = design::kOverlayPanelShadowY;
        lift.shadowColor = ui::Color::black(design::kOverlayPanelShadowAlpha * 0.6f * a);
        c.r.draw(lift);
    }
    c.r.drawGlass(ui::Rect{px, py, panelW, panelH, panelW * 0.5f, ui::Color::white(0)},
                  design::kRegularMaterialBlur, ui::Color::white(0.14f * a));
    {
        ui::Rect edge{px, py, panelW, panelH, panelW * 0.5f, ui::Color::white(0)};
        edge.border = design::kOverlayPanelBorder;
        edge.borderColor = ui::Color::white(design::kOverlayPanelBorderAlpha * a);
        edge.edgeLight = ui::Color::white(design::kOverlayPanelEdgeLight * 0.6f * a);
        c.r.draw(edge);
    }

    // The focus capsule stays in the middle; the letters move under it.
    const float capW = panelW - 2.0f * 14.0f;
    c.r.drawGlass(ui::Rect{cx - capW * 0.5f, midY - capH * 0.5f, capW, capH, capH * 0.5f,
                           ui::Color::white(0)},
                  design::kThinMaterialBlur, ui::Color::white(design::kFocusedTint * a));

    const float wave = wave_.value();
    const int n = static_cast<int>(letters_.size());
    const float limit = static_cast<float>(kSide) + 0.5f;
    for (int i = 0; i < n; ++i) {
        const float d = static_cast<float>(i) - wave;
        if (std::fabs(d) > limit + 0.5f) continue;
        const float ad = std::fabs(d);
        // Full white in the capsule, the bar's 45% one slot out, a little
        // less each slot after; and the last half slot fades to nothing so a
        // letter turning in or out of the column never pops.
        const float near = std::clamp(1.0f - ad, 0.0f, 1.0f);
        float bright = kRestText - kFarther * std::max(0.0f, ad - 1.0f);
        bright = bright + (1.0f - bright) * near;
        bright *= std::clamp(limit + 0.5f - ad, 0.0f, 1.0f);
        const std::string ch(1, letters_[static_cast<size_t>(i)]);
        const float w = c.text.measure(ch, kStyle, c.sc);
        const float y = midY + d * pitch;
        const float baseline = y - lineH * 0.5f + c.text.ascent(kStyle, c.sc);
        c.text.draw(c.r, ch, cx - w * 0.5f, baseline, kStyle,
                    ui::Color::white(bright * a), c.sc);
    }
    c.r.setContentAlpha(before);
}

}  // namespace screens
