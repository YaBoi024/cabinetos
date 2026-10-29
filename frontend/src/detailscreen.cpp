// A game's page, the launch screen: the cover, the rows, the facts, the
// states and the marks (DetailScreen, detailscreen.h). What fills it is the
// app's (detail.cpp). Moved out of screens.cpp in the restructure (#398).

#include "detailscreen.h"

#include <algorithm>
#include <cstdio>
#include <tuple>

#include "screenparts.h"
#include "sound.h"

namespace screens {

// ---------------------------------------------------------------------------
// The launch screen
// ---------------------------------------------------------------------------

void DetailScreen::open(GameDetail d) {
    game_ = std::move(d);
    notice_.clear();
    slot_ = 0;
    states_.clear();
    saveWhen_.clear();
    facts_ = {};
    details_.settle(0.0f);
    waited_ = 0.0f;
    detailsIn_ = false;
    inStates_ = false;
    onFav_ = false;
    onTrophy_ = false;
    ach_ = {};
    stateSlot_ = 0;
    rebuildRows();
    focus_.settle(1.0f);
    // NOT FADED IN YET: see draw(), which starts it once everything is here.
    appear_.smooth = true;
    appear_.settle(0.0f);
    started_ = false;
}

void DetailScreen::rebuildRows() {
    rows_.clear();
    if (game_.playable) {
        // The primary action first, and reachable without travelling through
        // the secondary ones.
        rows_.push_back({Action::Play, "Play", saveWhen_});
        // DOWNLOAD IS THE DELIBERATE ONE. The cache is invisible — pressing
        // Play fetches the game if it is not here and says nothing about it —
        // so this row is not "is it cached", it is "put this game on the
        // machine and do not take it away again". That is what makes it a keep,
        // and what makes it the one action the console may refuse.
        //
        // "Download", not "Download and keep", MMagTech 2026-09-26: "and keep"
        // only told it apart from Play's fetch into the cache, which nobody
        // sees. One family of words: Download, Remove download, Downloads.
        if (game_.kept)
            rows_.push_back({Action::RemoveDownload, "Remove download", ""});
        else
            rows_.push_back({Action::Download, "Download", ""});
    }
    // A different core and an export belong here too. They are not built yet
    // and a row that does nothing is worse than no row.
}

void DetailScreen::setFacts(Facts f) { facts_ = std::move(f); }

void DetailScreen::setAchievements(Achievements a) {
    ach_ = a;
    if (onTrophy_ && !trophyFocusable()) { onTrophy_ = false; onFav_ = true; }
}

void DetailScreen::detailsArrived() { detailsIn_ = true; }

std::string DetailScreen::factsLine(ui::TextRenderer& text, float sc, float width) const {
    auto join = [&](const std::string& maker) {
        std::string m;
        for (const std::string* p :
             {&facts_.year, &maker, &facts_.players, &facts_.played, &facts_.disc})
            if (!p->empty()) m += (m.empty() ? "" : "  \xC2\xB7  ") + *p;
        return m;
    };
    const std::string whole = join(facts_.maker);
    if (facts_.maker.empty() || text.measure(whole, ui::TextStyle::Callout, sc) <= width)
        return whole;
    // TOO LONG: THE MAKER GIVES WAY, not the end of the line. Cut at the end,
    // a long maker took the time played with it ("12 ho..."), and that is the
    // one part a person came to read. Every part stays on every game; only a
    // long company name is shortened. MMagTech, 2026-09-27.
    const float rest = text.measure(join(""), ui::TextStyle::Callout, sc) +
                       text.measure("  \xC2\xB7  ", ui::TextStyle::Callout, sc);
    if (width - rest <= 0.0f) return whole;
    return join(text.truncate(facts_.maker, ui::TextStyle::Callout, sc, width - rest));
}

void DetailScreen::setSaveWhen(std::string when) {
    saveWhen_ = std::move(when);
    rebuildRows();
}

void DetailScreen::setStates(std::vector<StateChoice> states) {
    states_ = std::move(states);
    if (states_.empty()) inStates_ = false;
    stateSlot_ = std::clamp(stateSlot_, 0, std::max(0, static_cast<int>(states_.size()) - 1));
}

void DetailScreen::tick(float dt) {
    focus_.tick(dt);
    appear_.tick(dt);
    details_.tick(dt);
    waited_ += dt;
}

// THE PAGE SOUNDS LIKE EVERY OTHER SCREEN (#389). It moved in silence: the
// focus here is five things (the rows, the heart, the cup, the states), so
// the cue is decided from where focus is before and after a press rather than
// in each branch. A direction that moved it is Move; one that did not is
// Edge; A on something that does nothing is Edge. Back, and A on something
// that does something, sound where the app acts on them.
Result DetailScreen::key(Nav n) {
    const auto where = [&] { return std::make_tuple(onFav_, onTrophy_, inStates_, slot_, stateSlot_); };
    const auto before = where();
    const Result r = step(n);
    if (n == Nav::Up || n == Nav::Down || n == Nav::Left || n == Nav::Right)
        sound::play(where() == before ? sound::Cue::Edge : sound::Cue::Move);
    else if (n == Nav::Activate && r.action == Action::None)
        sound::play(sound::Cue::Edge);
    return r;
}

Result DetailScreen::step(Nav n) {
    if (n == Nav::Back) return {Action::Back, 0};
    auto refocus = [&]() {
        notice_.clear();
        focus_.retarget(0.0f, 0.0f);
        focus_.elapsed = 0.0f;
        focus_.retarget(1.0f, design::kFocusDuration);
    };
    // THE FAVOURITE MARK sits above the rows, beside the title: Up from the
    // top row reaches it, Down comes back to that row. A game this console
    // cannot play has no rows, and its mark is the only thing to focus.
    // THE CUP beside the heart: Left goes back to the heart, Down to the
    // rows, A opens the list.
    if (onTrophy_) {
        if (n == Nav::Activate) return {Action::ShowAchievements, game_.romId};
        if (n == Nav::Left) {
            onTrophy_ = false;
            onFav_ = true;
            refocus();
        } else if (n == Nav::Down && !rows_.empty()) {
            onTrophy_ = false;
            slot_ = 0;
            refocus();
        }
        return {};
    }
    if (onFav_ && n == Nav::Right && trophyFocusable()) {
        onFav_ = false;
        onTrophy_ = true;
        refocus();
        return {};
    }
    if (onFav_ || rows_.empty()) {
        if (n == Nav::Activate) return {Action::ToggleFavorite, game_.romId};
        if (n == Nav::Down && !rows_.empty()) {
            onFav_ = false;
            slot_ = 0;
            refocus();
        } else if (rows_.empty() && !onFav_) {
            onFav_ = true;
            refocus();
        }
        return {};
    }
    if (n == Nav::Up && slot_ == 0 && !inStates_) {
        onFav_ = true;
        refocus();
        return {};
    }
    // CONTINUE FROM is one line of up to three, under the rows: Down from the
    // last row goes in, on the newest; Up comes back out to the row it left.
    if (inStates_) {
        const int count = static_cast<int>(states_.size());
        if (n == Nav::Left || n == Nav::Right) {
            const int next = std::clamp(stateSlot_ + (n == Nav::Right ? 1 : -1), 0, count - 1);
            if (next != stateSlot_) { stateSlot_ = next; refocus(); }
        } else if (n == Nav::Up) {
            inStates_ = false;
            refocus();
        } else if (n == Nav::Activate && stateSlot_ < count) {
            return {Action::PlayState, states_[stateSlot_].id};
        }
        return {};
    }
    if (n == Nav::Up || n == Nav::Down) {
        const int d = (n == Nav::Down) ? 1 : -1;
        const int last = static_cast<int>(rows_.size()) - 1;
        if (d > 0 && slot_ == last && !states_.empty() && game_.playable) {
            inStates_ = true;
            stateSlot_ = 0;
            refocus();
            return {};
        }
        const int next = std::clamp(slot_ + d, 0, last);
        if (next != slot_) { slot_ = next; refocus(); }
        return {};
    }
    // A row that is showing a download's progress does nothing (#185): the
    // game counts as downloaded from the first byte, so the row under the
    // progress is Remove download, and what removing a half-written game
    // should do was never designed. It is answered when the download lands.
    if (n == Nav::Activate && !rowBusy(slot_))
        return {rows_[slot_].action, game_.romId};
    return {};
}

bool DetailScreen::rowBusy(int i) const {
    if (!progress_.active || i < 0 || i >= static_cast<int>(rows_.size())) return false;
    if (progress_.action == rows_[i].action) return true;
    // COMING BACK TO A DOWNLOAD (#185). A Download records the keep before the
    // first byte moves, so a page opened mid-download is built as kept and
    // has Remove download where Download was. The progress goes on that row.
    return progress_.action == Action::Download && rows_[i].action == Action::RemoveDownload;
}

void DetailScreen::draw(Ctx& c) {
    // THE WHOLE PAGE FADES IN AT ONCE, when everything behind it has loaded:
    // RomM's answer (facts, save, states), the cover, and every state's
    // picture. Parts arriving one by one read as a page assembling itself
    // (MMagTech on the TV, 2026-09-27: "everything faded in at once once
    // stuff in the background was loaded"). RomM slow or away, it stops
    // waiting at kDetailsWait and fades in with what it has.
    if (!started_) {
        bool ready = detailsIn_ && (!ach_.show || ach_.known);
        if (ready && !game_.coverLarge.empty()) ready = c.images.get(game_.coverLarge).ready;
        for (const StateChoice& st : states_)
            if (ready && !st.picture.empty()) ready = c.images.get(st.picture).ready;
        // And never before the old screen has mostly dissolved away (the
        // copy of it drawn over this one, as the top-bar switch does).
        if ((ready && waited_ >= design::kDetailMinWait) || waited_ >= design::kDetailsWait) {
            started_ = true;
            details_.settle(1.0f);
            appear_.retarget(1.0f, design::kDetailsFade);
        }
    }
    const float a = appear_.value();

    // THE ARTWORK IS ITS OWN BACKDROP. A full-screen cover rather than a push,
    // with the game's cover filled and blurred behind everything and a scrim
    // over it — so the leftovers are the art's own colours rather than a flat
    // panel, and the text still has something to be read against.
    // THE BACKDROP IS THE THUMBNAIL, AT THE ORIGINAL BLUR, and that is a
    // decision rather than an oversight — 2026-09-21.
    //
    // It was changed to the 810x1080 original, which made it a legible picture
    // of the game rather than a wash of its colours. Two attempts at it, sharp
    // and softened, and MMagTech on both: *"no that is not what was there for
    // the launch screen backdrop before you started."*
    //
    // So it keeps the small cover and `kBackdropBlur`'s fixed mip bias. Five
    // texels across a 162-wide source is exactly the muddy average the rest of
    // this session has been removing elsewhere — and here it is what the screen
    // wants, because everything in FRONT of it is the same artwork sharp: the
    // cover, at the original resolution, and the title. The backdrop's job here
    // is to be a colour, not a second copy of the picture.
    const ui::Image* art = nullptr;
    if (!game_.cover.empty()) {
        const ui::Image& img = c.images.get(game_.cover);
        if (img.ready) art = &img;
    }
    if (art) {
        // Filled means CROPPED, not stretched: mapping a 3:4 cover across a
        // 16:9 screen by UV 0..1 smears it into a band that is no longer the
        // art's colours at all, which is the whole point of the backdrop.
        const float boxAspect = ui::kCanvasWidth / ui::kCanvasHeight;
        const float imgAspect = art->aspect();
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (imgAspect < boxAspect) {
            const float span = imgAspect / boxAspect;
            v0 = (1.0f - span) * 0.5f;
            v1 = v0 + span;
        } else {
            const float span = boxAspect / imgAspect;
            u0 = (1.0f - span) * 0.5f;
            u1 = u0 + span;
        }
        c.r.drawTextured(0, 0, ui::kCanvasWidth, ui::kCanvasHeight, art->texture,
                         u0, v0, u1, v1, ui::Color{1, 1, 1, art->fade}, false,
                         design::kBackdropBlur);
    }
    c.r.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                      ui::Color::black(design::kScrimOverlay)});
    // THE BACKDROP IS THERE AT ONCE, under the old screen's dissolving copy,
    // as a top-bar switch's new background is. Everything on it waits, then
    // fades in RISING a little, the same arrival as a top-bar switch's.
    const float keepY = c.r.contentOffsetY();
    c.r.setContentOffsetY(keepY + (1.0f - a) * design::kDetailRise);
    c.r.setContentOffsetX(slideX_);

    // ONE COLUMN ON THE LEFT, THE COVER ON THE RIGHT, the pair centred across
    // the screen. Everything read or chosen (title, Play, Download, Continue
    // from) lines up on one left edge, and the cover balances it.
    //
    // Two layouts came first, both judged by MMagTech on the TV, 2026-09-27.
    // Cover left with the states under the rows: the right side ran above and
    // below the cover and nothing lined up ("off balance"). Cover left with
    // the states as a shelf under it: the shelf filled two-thirds of the width
    // and the page sat heavy on the left ("the bottom feels out of weight,
    // maybe the image somewhere on the right").
    //
    // The column's top is level with the cover's while the column is the
    // shorter; once Continue from makes it taller, the cover is centred
    // against it instead.
    const float calloutLH = c.text.lineHeight(ui::TextStyle::Callout, c.sc);
    const float colW = design::kRowColumnMaxWidth;
    cardW_ = (colW - 2.0f * design::kStateCardGap) / 3.0f;
    cardH_ = cardW_ * (9.0f / 16.0f);
    const float headToRows = c.text.ascent(ui::TextStyle::LargeTitle, c.sc) +
                             c.text.lineHeight(ui::TextStyle::LargeTitle, c.sc) * 0.55f +
                             c.text.ascent(ui::TextStyle::Callout, c.sc) + calloutLH * 2.6f;
    const float rowH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) + design::kRowPadY * 2.0f;
    // EVERY PAGE IS LAID OUT THE SAME, whatever the game has: two rows and
    // a shelf, so the cover is one size in one place and the title, the rows
    // and Continue from never move between games. A game with no states has
    // open space where the shelf would be. (The cover grew only when there
    // was a shelf, first, and MMagTech: a game without saves and states
    // should look like one with them.)
    const float rowsH = 2.0f * rowH + design::kDetailRowGap;
    const float shelfH = design::kStateShelfGap + calloutLH + design::kDetailRowGap + cardH_ +
              design::kDetailRowGap * 0.75f + calloutLH;
    const float colH = headToRows + rowsH + shelfH;
    // THE COVER GROWS TO THE COLUMN'S HEIGHT once Continue from makes the
    // column taller than it: top level with the title, foot level with the
    // shelf's labels, so the right side is filled by the art itself. (Facts
    // listed under a cover of the old size were tried first and read as
    // filler; MMagTech: "a horrible afterthought". They went into the line
    // under the title.) The pair is centred across the screen either way.
    //
    // LINED UP WITH WHAT IS SEEN, not with the text's line boxes (MMagTech,
    // the same day: the cover's top sat above the title's letters and its foot
    // below the pictures, at the labels'). Its top is the top of the title's
    // capitals; its foot is the foot of the state pictures.
    const float capInset = c.text.ascent(ui::TextStyle::LargeTitle, c.sc) -
                           c.text.capHeight(ui::TextStyle::LargeTitle, c.sc);
    const float picturesFoot = headToRows + rowsH + design::kStateShelfGap + calloutLH +
                               design::kDetailRowGap + cardH_;
    const float coverH = std::clamp(picturesFoot - capInset, design::kDetailCoverHeight,
                                    design::kDetailCoverMaxHeight);
    const float coverW = coverH * (design::kDetailCoverWidth / design::kDetailCoverHeight);
    const float pairW = colW + design::kDetailCoverGap + coverW;
    const float colX = (ui::kCanvasWidth - pairW) * 0.5f;
    const float coverX = colX + colW + design::kDetailCoverGap;
    const float pairH = std::max(colH, capInset + coverH);
    const float top = std::max(design::kStateBlockMinTop, (ui::kCanvasHeight - pairH) * 0.5f);
    const float coverY = top + capInset;
    const float blockTop = top;
    statesX_ = colX;
    statesY_ = blockTop + headToRows + rowsH + design::kStateShelfGap;
    if (c.cards && game_.cardIndex >= 0 &&
        game_.cardIndex < static_cast<int>(c.cards->size())) {
        drawCover(c, (*c.cards)[game_.cardIndex], coverX, coverY, coverW, coverH,
                  design::kDetailRadius, /*f=*/1.0f, /*rim=*/false, /*large=*/true);
    } else {
        c.r.draw(ui::Rect{coverX, coverY, coverW, coverH, design::kDetailRadius, game_.art});
    }
    // Where the small book goes, for the glass pass (#364).
    bookX_ = coverX + coverW - design::kManualMarkInset - design::kManualMarkDisc;
    bookY_ = coverY + coverH - design::kManualMarkInset - design::kManualMarkDisc;

    // The title, Large Title — the one place in the product that size is used
    // for a game rather than a settings page.
    const float textX = colX;
    const float textW = colW;
    float y = blockTop + c.text.ascent(ui::TextStyle::LargeTitle, c.sc);
    // THE FAVOURITE MARK FOLLOWS THE TITLE (#267), its height the capitals',
    // centred on them; a long title is shortened to leave it room.
    const float cap = c.text.capHeight(ui::TextStyle::LargeTitle, c.sc);
    favS_ = cap * design::kFavMarkScale;
    // Clear of the title even with its focus disc round it (MMagTech on the
    // TV: the circle sat too close to the words).
    const float favGap = cap * 1.05f;
    // THE CUP (#74) follows the heart, the heart's size, so
    // a long title loses no more than a heart's width to it (MMagTech on the
    // TV, 2026-10-06: the pill with the count cut titles short).
    trophyH_ = trophyW_ = trophyShown() ? favS_ : 0.0f;
    const float trophyRoom = trophyShown() ? favGap + trophyW_ : 0.0f;
    const std::string title = c.text.truncate(game_.title, ui::TextStyle::LargeTitle, c.sc,
                                              textW - favGap - favS_ - trophyRoom);
    c.text.draw(c.r, title, textX, y, ui::TextStyle::LargeTitle, ui::Color::white(a), c.sc);
    favX_ = textX + c.text.measure(title, ui::TextStyle::LargeTitle, c.sc) + favGap;
    favY_ = y - cap * 0.5f - favS_ * 0.5f;
    trophyX_ = favX_ + favS_ + favGap;
    trophyY_ = favY_;
    y += c.text.lineHeight(ui::TextStyle::LargeTitle, c.sc) * 0.55f +
         c.text.ascent(ui::TextStyle::Callout, c.sc);

    // WHAT THE GAME IS, in two lines, the same two on every game: platform and
    // size, then RomM's year, maker and players. They were one line first, and
    // a long platform name pushed it into the cover; dropping parts to make it
    // fit showed different facts on different games, and MMagTech: "has to be
    // consistent across everything". THE SECOND LINE'S SPACE IS ALWAYS KEPT,
    // blank for a game RomM never matched, so the rows and the shelf sit in
    // the same place on every game (MMagTech, the same day: the pages did not
    // line up when the line came and went). A part RomM lacks is left out.
    std::string meta = game_.platform;
    if (game_.sizeBytes > 0) {
        // A unit that suits the number. A library holds a 19 KB Game Boy ROM
        // and a 1.78 GB arcade set, and megabytes flatter neither: the first
        // reads as "0 MB", which looks like the server failed to say.
        char buf[64];
        const double b = static_cast<double>(game_.sizeBytes);
        if (b >= 1e9) std::snprintf(buf, sizeof buf, "  \xC2\xB7  %.1f GB", b / 1e9);
        else if (b >= 1e6) std::snprintf(buf, sizeof buf, "  \xC2\xB7  %.0f MB", b / 1e6);
        else std::snprintf(buf, sizeof buf, "  \xC2\xB7  %.0f KB", b / 1e3);
        meta += buf;
    }
    c.text.draw(c.r, c.text.truncate(meta, ui::TextStyle::Callout, c.sc, textW), textX, y,
                ui::TextStyle::Callout, ui::Color::white(0.60f * a), c.sc);
    y += c.text.lineHeight(ui::TextStyle::Callout, c.sc);
    const std::string facts = factsLine(c.text, c.sc, textW);
    if (!facts.empty())
        c.text.draw(c.r, c.text.truncate(facts, ui::TextStyle::Callout, c.sc, textW), textX, y,
                    ui::TextStyle::Callout,
                    ui::Color::white(0.60f * a * details_.value()), c.sc);

    rowsX_ = textX;
    rowsW_ = textW;
    rowsY_ = y + c.text.lineHeight(ui::TextStyle::Callout, c.sc) * 1.6f;

    // A game this console cannot play gets the screen and the reason, and no
    // Play button. Saying it here is the whole point of knowing it.
    if (!game_.playable) {
        c.text.draw(c.r, c.text.truncate(game_.reason, ui::TextStyle::Title3, c.sc, textW),
                    textX, rowsY_ + c.text.ascent(ui::TextStyle::Title3, c.sc),
                    ui::TextStyle::Title3, ui::Color::white(0.60f * a), c.sc);
    }
    c.r.setContentOffsetY(keepY);
    c.r.setContentOffsetX(0.0f);
}

void DetailScreen::drawGlass(Ctx& c) {
    const float a = appear_.value();
    const float keepY = c.r.contentOffsetY();
    c.r.setContentOffsetY(keepY + (1.0f - a) * design::kDetailRise);
    c.r.setContentOffsetX(slideX_);
    // Treatment 3, the row one: a surface that is always there. Blur untinted
    // at rest, white 22% focused, scale 1.03 — a full-width row growing a
    // cover's tenth would collide with its neighbours.
    const float rowH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) +
                       design::kRowPadY * 2.0f;
    float y = rowsY_;
    for (size_t i = 0; i < rows_.size(); ++i) {
        // NOT while focus is up on the heart or the cup: the row stayed lit
        // there and flashed each time focus moved between them, because
        // every move restarts the shared focus animation (MMagTech on the
        // TV, 2026-10-06; since the heart, #267).
        const bool on = !inStates_ && !onFav_ && !onTrophy_ && static_cast<int>(i) == slot_;
        const float f = on ? focus_.value() : 0.0f;
        const float s = 1.0f + f * (design::kRowFocusScale - 1.0f);
        const float w = rowsW_ * s, h = rowH * s;
        const float x = rowsX_ - (w - rowsW_) * 0.5f;
        const float ry = y - (h - rowH) * 0.5f;

        c.r.drawGlass(ui::Rect{x, ry, w, h, design::kRowRadius, ui::Color::white(0)},
                      design::kRegularMaterialBlur,
                      ui::Color::white((0.08f + 0.14f * f) * a));

        // THE ROW IS THE PROGRESS BAR. Not a bar drawn inside the row — the
        // row's own surface fills from the left, so the thing you pressed is
        // the thing that is loading rather than a widget that appeared on it.
        const bool busy = rowBusy(i);
        std::string label = rows_[i].label;
        if (busy) {
            if (progress_.total > 0) {
                const float frac = std::clamp(
                    static_cast<float>(progress_.got) /
                        static_cast<float>(progress_.total), 0.0f, 1.0f);
                // Clipped to the row's own rounded rectangle, so the fill has
                // the row's corners rather than square ones poking out of them.
                ui::Rect fill{x, ry, w * frac, h, design::kRowRadius,
                              ui::Color::white(0.22f * a)};
                c.r.draw(fill);
            }
            char buf[96];
            const double g = static_cast<double>(progress_.got);
            const double t = static_cast<double>(progress_.total);
            if (progress_.installing)
                std::snprintf(buf, sizeof buf, "Installing\xE2\x80\xA6");
            else if (progress_.unpacking)
                std::snprintf(buf, sizeof buf, "Unpacking\xE2\x80\xA6");
            else if (progress_.total > 0)
                std::snprintf(buf, sizeof buf, "%.0f of %.0f MB", g / 1e6, t / 1e6);
            else
                std::snprintf(buf, sizeof buf, "%.0f MB", g / 1e6);
            label = buf;
        }

        const float base = ry + design::kRowPadY * s + c.text.ascent(ui::TextStyle::Title3, c.sc);
        c.text.draw(c.r, label, x + design::kRowPadX, base, ui::TextStyle::Title3,
                    ui::Color::white((on || busy ? 1.0f : 0.60f) * a), c.sc);
        if (!busy && !rows_[i].value.empty()) {
            const float vw = c.text.measure(rows_[i].value, ui::TextStyle::Callout, c.sc);
            c.text.draw(c.r, rows_[i].value, x + w - design::kRowPadX - vw, base,
                        ui::TextStyle::Callout,
                        ui::Color::white(0.60f * a * details_.value()), c.sc);
        }
        y += rowH + design::kDetailRowGap;
    }

    // A SMALL BOOK ON THE COVER WHEN THERE IS A MANUAL (#364): a quiet sign,
    // not another button. In the cover's foot corner, on a disc of the rows'
    // own frosted glass, darkened, with a hairline rim, so it reads as a
    // badge on light art and dark art alike; the book white.
    if (game_.hasManual) {
        const float disc = design::kManualMarkDisc;
        ui::Rect glass{bookX_, bookY_, disc, disc, disc * 0.5f, ui::Color::white(0)};
        c.r.drawGlass(glass, design::kRegularMaterialBlur,
                      ui::Color::black(design::kManualMarkDiscAlpha * a));
        ui::Rect rim = glass;
        rim.border = 1.5f;
        rim.borderColor = ui::Color::white(design::kManualMarkRimAlpha * a);
        c.r.draw(rim);
        const float m = disc * design::kManualMarkScale;
        ui::Rect book{bookX_ + (disc - m) * 0.5f, bookY_ + (disc - m) * 0.5f + m * 0.03f, m, m,
                      m * 0.04f, ui::Color::white(design::kManualMarkAlpha * a)};
        book.shape = ui::Rect::Shape::Book;
        c.r.draw(book);
    }

    // THE FAVOURITE MARK (#267). Outlined until it is a favourite, then
    // filled; focused, it sits on the rows' own glass, as a disc, and grows
    // as they do.
    {
        const float f = onFav_ ? focus_.value() : 0.0f;
        const float pad = favS_ * 0.45f;
        const float disc = favS_ + 2.0f * pad;
        const float s = 1.0f + f * (design::kFavFocusScale - 1.0f);
        const float cx = favX_ + favS_ * 0.5f, cy = favY_ + favS_ * 0.5f;
        if (f > 0.0f)
            c.r.drawGlass(ui::Rect{cx - disc * s * 0.5f, cy - disc * s * 0.5f, disc * s,
                                   disc * s, disc * s * 0.5f, ui::Color::white(0)},
                          design::kRegularMaterialBlur,
                          ui::Color::white(0.22f * f * a));
        const float m = favS_ * s;
        ui::Rect mark{cx - m * 0.5f, cy - m * 0.5f, m, m, m * 0.06f, ui::Color::white(0)};
        mark.shape = design::kFavMarkShape;
        const float strength = (onFav_ ? 1.0f : 0.60f) * a;
        if (game_.favorite) {
            mark.fill = ui::Color::white(strength);
        } else {
            mark.border = std::max(2.0f, m * 0.09f);
            mark.borderColor = ui::Color::white(strength);
        }
        c.r.draw(mark);
    }

    // THE CUP (#74), drawn like the heart beside it and
    // saying where the person is without a number (MMagTech, 2026-10-06):
    // outlined with none unlocked, filled with some, gold with every one. The
    // count is in the list it opens. Focused, it sits on the same glass disc.
    if (trophyShown()) {
        const float f = onTrophy_ ? focus_.value() : 0.0f;
        const float pad = trophyW_ * 0.45f;
        const float disc = trophyW_ + 2.0f * pad;
        const float s = 1.0f + f * (design::kFavFocusScale - 1.0f);
        const float cx = trophyX_ + trophyW_ * 0.5f, cy = trophyY_ + trophyW_ * 0.5f;
        const float da = a * details_.value();
        if (f > 0.0f)
            c.r.drawGlass(ui::Rect{cx - disc * s * 0.5f, cy - disc * s * 0.5f, disc * s,
                                   disc * s, disc * s * 0.5f, ui::Color::white(0)},
                          design::kRegularMaterialBlur, ui::Color::white(0.22f * f * da));
        const float m = trophyW_ * s;
        ui::Rect cup{cx - m * 0.5f, cy - m * 0.5f, m, m, 0.0f, ui::Color::white(0)};
        cup.shape = ui::Rect::Shape::Trophy;
        const bool all = ach_.total > 0 && ach_.unlocked >= ach_.total;
        const float strength = (onTrophy_ ? 1.0f : 0.60f) * da;
        if (all) {
            ui::Color gold = design::kTrophyGold;
            gold.a = onTrophy_ ? da : 0.85f * da;
            cup.fill = gold;
        } else if (ach_.unlocked > 0) {
            cup.fill = ui::Color::white(strength);
        } else {
            cup.border = std::max(2.0f, m * 0.09f);
            cup.borderColor = ui::Color::white(strength);
        }
        c.r.draw(cup);
    }

    // CONTINUE FROM, as Cabinet's tvOS launch screen called it: the game's
    // states, newest first, each with the picture it was saved with and when.
    // Picking one plays from there; Play itself still starts the game from
    // its own save. Cards rather than rows because the picture is how a
    // person tells three moments in one game apart.
    if (!states_.empty() && game_.playable) {
        const float rowsEnd = y;
        // The shelf in RomM's fade; `a` below is only ever used as an alpha.
        const float a = appear_.value() * details_.value();
        y = statesY_;
        c.text.draw(c.r, "Continue from", statesX_,
                    y + c.text.ascent(ui::TextStyle::Callout, c.sc), ui::TextStyle::Callout,
                    ui::Color::white(0.60f * a), c.sc);
        y += c.text.lineHeight(ui::TextStyle::Callout, c.sc) + design::kDetailRowGap;
        const float cardH = cardH_;
        const float cardW = cardW_;
        float x = statesX_;
        for (size_t i = 0; i < states_.size(); ++i) {
            const bool on = inStates_ && static_cast<int>(i) == stateSlot_;
            const float f = on ? focus_.value() : 0.0f;
            const float s = 1.0f + f * (design::kFocusScale - 1.0f);
            const float w = cardW * s, h = cardH * s;
            const float cx = x - (w - cardW) * 0.5f;
            const float cy = y - (h - cardH) * 0.5f;
            c.r.drawGlass(ui::Rect{cx, cy, w, h, design::kRowRadius, ui::Color::white(0)},
                          design::kRegularMaterialBlur, ui::Color::white(0.08f * a));
            // The whole picture, on a blurred echo of itself, as a cover of
            // an odd shape is drawn: a tall arcade screen keeps its shape and
            // the sides are its own colours rather than bars. The ones not
            // focused sit a little darker, so the focused one reads as chosen.
            if (!states_[i].picture.empty()) {
                const ui::Image& img = c.images.get(states_[i].picture);
                if (img.ready) {
                    const float ew = w * 1.3f, eh = h * 1.3f;
                    c.r.drawTextured(cx - (ew - w) * 0.5f, cy - (eh - h) * 0.5f, ew, eh,
                                     img.texture, 0, 0, 1, 1,
                                     ui::Color{1, 1, 1, a * img.fade}, false, 4.0f, cx, cy,
                                     w, h, design::kRowRadius);
                    c.r.draw(ui::Rect{cx, cy, w, h, design::kRowRadius,
                                      ui::Color::black(0.18f * a)});
                }
                ui::drawImage(c.r, img, cx, cy, w, h, ui::Fit::Contain, a,
                              design::kRowRadius);
            }
            c.r.draw(ui::Rect{cx, cy, w, h, design::kRowRadius,
                              ui::Color::black(0.30f * (1.0f - f) * a)});
            const float labelY = y + cardH + design::kDetailRowGap * 0.75f +
                                 c.text.ascent(ui::TextStyle::Callout, c.sc);
            c.text.draw(c.r,
                        c.text.truncate(states_[i].when, ui::TextStyle::Callout, c.sc, cardW),
                        x, labelY, ui::TextStyle::Callout,
                        ui::Color::white((on ? 1.0f : 0.60f) * a), c.sc);
            x += cardW + design::kStateCardGap;
        }
        // A notice goes under the rows, where it always went, not under the
        // shelf.
        y = rowsEnd;
    }

    // A refusal, and the number that makes it actionable. docs/PROJECT.md:
    // "the one failure the person ever sees is 'the disk is full of things you
    // asked me to keep'" — which is only useful with an amount beside it.
    if (!notice_.empty()) {
        // WRAPPED, NOT TRUNCATED. On one line this refusal came out as "...the
        // disk is full of things you asked me to keep. Remo..." — cutting off
        // the amount, which is the only part of it a person can act on. A
        // message that loses its own point is worse than no message.
        float baseline = y + c.text.ascent(ui::TextStyle::Callout, c.sc);
        for (const std::string& line :
             wrapTwoLines(c.text, notice_, ui::TextStyle::Callout, c.sc, rowsW_)) {
            c.text.draw(c.r, line, rowsX_, baseline, ui::TextStyle::Callout,
                        ui::Color::white(0.75f * a), c.sc);
            baseline += c.text.lineHeight(ui::TextStyle::Callout, c.sc);
        }
    }
    c.r.setContentOffsetY(keepY);
    c.r.setContentOffsetX(0.0f);
}

}  // namespace screens
