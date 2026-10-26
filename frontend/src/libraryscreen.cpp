// The Library: every system and every collection on the server, as tiles
// under the switcher (LibraryScreen, libraryscreen.h), with the pills the
// switcher is drawn from. Moved out of screens.cpp in the restructure (#398).

#include "libraryscreen.h"

#include <algorithm>
#include <cmath>

#include "look.h"
#include "screenparts.h"
#include "sound.h"

namespace screens {

namespace {

// A capsule pill, treatment 2 in the design system: tinted blur, text to full
// white, scale 1.06. `selected` and `focused` are INDEPENDENT and both visible
// at once — a controller UI where focus can move away from the current
// selection has to show both or the person loses their place.
float pillWidth(ui::TextRenderer& text, const std::string& label, float sc) {
    return text.measure(label, ui::TextStyle::Title3, sc) + design::kPillPadX * 2.0f;
}

float pillHeight(ui::TextRenderer& text, float sc) {
    return text.lineHeight(ui::TextStyle::Title3, sc) + design::kPillPadY * 2.0f;
}

// THE SWITCHER IS TEXT, AND THE CAPSULE ONLY APPEARS UNDER FOCUS — 2026-09-21.
//
// It used to be a filled capsule at rest, which made the Library's first row a
// pair of controls where Home's first row is a pair of headings. MMagTech:
// *"the library and collection text dont seem to match home now either."*
// Home's "Recent" and "Favorites" are Title 3 on the backdrop with nothing
// behind them; these are the same words doing the same job and they are Title 3
// on the backdrop now too.
//
// It also settles the margin. A capsule cannot be aligned two ways at once —
// box-aligned puts its label 14 points inside everything else on the screen,
// text-aligned hangs the capsule 14 points outside the tiles below it — and
// text with no box behind it simply starts where the margin is, the way the
// heading on Home does.
//
// SELECTION IS STILL NOT FOCUS AND BOTH ARE STILL VISIBLE AT ONCE, which is the
// rule this has to keep: the SELECTED tab is full white and the other is dimmed
// to 45%, so which tab you are looking at survives focus walking away from it;
// FOCUS is the capsule, which appears only under the thing the cursor is on.
// The two signals are now different in kind rather than different in strength,
// which reads better from a sofa than two tints of the same white did.
void drawPill(Ctx& c, const std::string& label, float x, float y, bool selected,
              float f) {
    const float w0 = pillWidth(c.text, label, c.sc);
    const float h0 = pillHeight(c.text, c.sc);
    const float s = 1.0f + f * (design::kPillFocusScale - 1.0f);
    const float w = w0 * s, h = h0 * s;
    // `x` is the TEXT's left edge; the capsule is drawn a padding-width outside
    // it, so it grows around the label rather than pushing it along.
    const float px = x - design::kPillPadX - (w - w0) * 0.5f;
    const float py = y - (h - h0) * 0.5f;

    if (f > 0.001f)
        c.r.drawGlass(ui::Rect{px, py, w, h, h * 0.5f, ui::Color::white(0)},
                      design::kThinMaterialBlur,
                      ui::Color::white(f * design::kFocusedTint));
    // The label does not move when the capsule grows: a focus scale that shoved
    // the words along would make the pair jitter as focus crossed them.
    c.text.draw(c.r, label, x,
                py + design::kPillPadY * s + c.text.ascent(ui::TextStyle::Title3, c.sc),
                ui::TextStyle::Title3,
                ui::Color::white(selected ? 1.0f : 0.45f), c.sc);
}

}  // namespace

// ---------------------------------------------------------------------------
// The Library
// ---------------------------------------------------------------------------

void LibraryScreen::build(std::vector<Tile> platforms, std::vector<Tile> collections) {
    platforms_ = std::move(platforms);
    collections_ = std::move(collections);
}

void LibraryScreen::learnedTile(const std::string& query, const std::string& detail,
                                const std::string& cover) {
    for (auto* v : {&platforms_, &collections_}) {
        for (Tile& t : *v) {
            if (t.query != query) continue;
            if (!detail.empty()) t.detail = detail;
            // A cover already on the tile is left alone: a collection carries
            // its own from the server and must not be overwritten by whichever
            // game happened to sort first inside it.
            if (t.cover.empty()) t.cover = cover;
            return;
        }
    }
}

void LibraryScreen::enter() {
    // ARRIVING IS ANIMATED EVERY TIME, and it is outside the guard below on
    // purpose: coming back from a platform's grid is an arrival too, and a
    // screen that faded in the first time and cut in afterwards would be worse
    // than one that always cut. 280ms ease-out is the design system's own
    // transition duration.
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);

    if (entered_) return;    // focus lands here ONLY the first time
    entered_ = true;
    row_ = 0;
    slot_ = 0;
    pillFocus_[0].settle(1.0f);
}

std::string LibraryScreen::focusedCover() const {
    const auto& tiles = visible();
    if (tiles.empty()) return {};
    const int i = (row_ == 0) ? 0 : slot_;
    if (i < 0 || i >= static_cast<int>(tiles.size())) return {};
    return tiles[i].cover;
}

void LibraryScreen::focusTile(int index) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    if (tiles.empty()) return;
    if (row_ == 0) pillFocus_[slot_].settle(0.0f);
    row_ = 1;
    slot_ = std::clamp(index, 0, static_cast<int>(tiles.size()) - 1);
    rememberedTileSlot_ = slot_;
    tiles[slot_].focus.settle(1.0f);
    entered_ = true;
}

int LibraryScreen::columns() const {
    // Adaptive: as many columns of at least the minimum width as fit, then
    // stretched to fill. Four on a 1920 canvas at an 80pt inset.
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = static_cast<int>((usable + design::kTileSpacing) /
                                   (design::kTileMinWidth + design::kTileSpacing));
    return std::max(1, n);
}

float LibraryScreen::tileWidth() const {
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = columns();
    return (usable - design::kTileSpacing * static_cast<float>(n - 1)) /
           static_cast<float>(n);
}

LibraryScreen::Layout LibraryScreen::layout(float headingH) const {
    Layout L;
    const auto& tiles = visible();
    const int cols = columns();
    const float pitch = design::kTileHeight + design::kTileSpacing;
    float y = 0.0f;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const bool newGroup = i > 0 && tiles[i].group != tiles[i - 1].group;
        const bool newRow = i == 0 || newGroup || L.rowCount.back() == cols;
        if (newRow) {
            if (i > 0) y += pitch;
            const bool headed = (i == 0 || newGroup) && !tiles[i].group.empty();
            if (headed) {
                if (i > 0) y += design::kTileGroupGapAbove;
                L.headings.push_back({y, tiles[i].group});
                y += headingH + design::kTileGroupGapBelow;
            }
            L.rowFirst.push_back(static_cast<int>(i));
            L.rowCount.push_back(0);
            L.rowY.push_back(y);
            L.rowHeaded.push_back(headed);
        }
        L.rowOf.push_back(static_cast<int>(L.rowFirst.size()) - 1);
        L.colOf.push_back(L.rowCount.back()++);
    }
    L.bottom = tiles.empty() ? 0.0f : y + pitch;
    return L;
}

void LibraryScreen::tick(float dt) {
    appear_.tick(dt);
    for (auto& p : pillFocus_) p.tick(dt);
    scroll_.tick(dt);
    for (auto& t : platforms_) t.focus.tick(dt);
    for (auto& t : collections_) t.focus.tick(dt);
}

void LibraryScreen::moveFocus(int dx, int dy) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    const int count = static_cast<int>(tiles.size());

    auto leave = [&]() {
        if (row_ == 0) pillFocus_[slot_].retarget(0.0f, design::kFocusDuration);
        else if (slot_ < count) tiles[slot_].focus.retarget(0.0f, design::kFocusDuration);
    };
    auto arrive = [&]() {
        if (row_ == 0) pillFocus_[slot_].retarget(1.0f, design::kFocusDuration);
        else if (slot_ < count) tiles[slot_].focus.retarget(1.0f, design::kFocusDuration);
        // THE SCREENS PLAY THEIR OWN MOVE CUE. The app cannot tell a focus move
        // from a press that did nothing — both come back as Action::None — so
        // the only place that knows a move happened is the place that made it.
        sound::play(sound::Cue::Move);
    };

    if (row_ == 0) {
        if (dx != 0) {
            const int next = std::clamp(slot_ + dx, 0, 1);
            if (next == slot_) return;
            leave();
            slot_ = next;
            arrive();
            return;
        }
        if (dy > 0 && count > 0) {
            leave();
            row_ = 1;
            slot_ = std::clamp(rememberedTileSlot_, 0, count - 1);
            arrive();
        }
        return;
    }

    // In the grid, by its rows: a group starts a new one, so a row can be
    // short and the one under it is not `cols` further on.
    const Layout L = layout(0.0f);
    const int r = L.rowOf[static_cast<size_t>(slot_)];
    if (dy < 0 && r == 0) {
        // Off the top row and back to the switcher, which is where the person
        // came in. Remembered, so coming back down lands where they left.
        leave();
        rememberedTileSlot_ = slot_;
        row_ = 0;
        // THE SWITCHER HAS TWO PLACES. The third or fourth tile's place, kept
        // here, indexed pillFocus_ past its two and wrote into the grid screen
        // beside this one, and the next grid opened crashed (A9, 2026-10-20).
        // Past the second tile, focus goes to the nearer word, Collections.
        slot_ = std::min(slot_, 1);
        arrive();
        return;
    }
    int next = slot_;
    if (dx != 0) {
        // Rows do not wrap. Running off the right of one row and appearing at
        // the left of the next is disorienting with a d-pad, and the reference
        // implementation's grids do not do it.
        const int rowStart = L.rowFirst[static_cast<size_t>(r)];
        const int rowEnd = rowStart + L.rowCount[static_cast<size_t>(r)] - 1;
        next = std::clamp(slot_ + dx, rowStart, rowEnd);
    } else if (dy != 0) {
        const int r2 = r + dy;
        if (r2 < 0 || r2 >= static_cast<int>(L.rowFirst.size())) return;
        // The same column, or the last tile of a shorter row.
        next = L.rowFirst[static_cast<size_t>(r2)] +
               std::min(L.colOf[static_cast<size_t>(slot_)],
                        L.rowCount[static_cast<size_t>(r2)] - 1);
    }
    if (next == slot_) return;
    leave();
    slot_ = next;
    rememberedTileSlot_ = slot_;
    arrive();
}

bool LibraryScreen::jumpGroup(int dir) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    std::vector<int> starts;
    for (size_t i = 0; i < tiles.size(); ++i)
        if (i == 0 || tiles[i].group != tiles[i - 1].group) starts.push_back(static_cast<int>(i));
    // On the switcher, focus is before the first group.
    int at = -1;
    if (row_ > 0)
        for (size_t g = 0; g < starts.size(); ++g)
            if (starts[g] <= slot_) at = static_cast<int>(g);
    int want = at + dir;
    if (dir < 0 && at >= 0 && starts[static_cast<size_t>(at)] < slot_) want = at;
    if (starts.size() < 2 || want < 0 || want >= static_cast<int>(starts.size())) {
        sound::play(sound::Cue::Edge);
        return false;
    }
    if (row_ == 0) pillFocus_[slot_].retarget(0.0f, design::kFocusDuration);
    else tiles[static_cast<size_t>(slot_)].focus.retarget(0.0f, design::kFocusDuration);
    row_ = 1;
    slot_ = starts[static_cast<size_t>(want)];
    rememberedTileSlot_ = slot_;
    tiles[static_cast<size_t>(slot_)].focus.retarget(1.0f, design::kFocusDuration);
    sound::play(sound::Cue::Move);
    return true;
}

Result LibraryScreen::key(Nav n) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    switch (n) {
        case Nav::Left:  moveFocus(-1, 0); break;
        case Nav::Right: moveFocus(+1, 0); break;
        // Up out of the switcher is the bar, which is drawn above this screen
        // and belongs to the app rather than to it.
        case Nav::Up:    if (row_ == 0) return {Action::FocusBar, 0};
                         moveFocus(0, -1);
                         break;
        case Nav::Down:  moveFocus(0, +1); break;
        case Nav::Back:  return {Action::Back, 0};
        case Nav::Activate:
            if (row_ == 0) {
                if (slot_ == tab_) { sound::play(sound::Cue::Edge); return {}; }
                sound::play(sound::Cue::Activate);   // #389
                // Changing tab resets the grid position: the remembered slot
                // belonged to a different list and pointing it at this one
                // means nothing.
                tab_ = slot_;
                rememberedTileSlot_ = 0;
                scroll_.retarget(0.0f, design::kFocusDuration);
                return {};
            }
            if (slot_ >= 0 && slot_ < static_cast<int>(tiles.size())) {
                // An unplayable platform is drawn and says why, and does not
                // open. Offering a screen of games that cannot start is the
                // promise this whole catalog exists to avoid breaking.
                if (!tiles[slot_].enterable) { sound::play(sound::Cue::Edge); return {}; }
                return {Action::OpenTile, slot_};
            }
            break;
    }
    return {};
}

// FORK (handheld): a pill is spot 1000 + its place, a tile its own place.
constexpr int kPillSpot = 1000;

void LibraryScreen::focusOn(int row, int slot) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    const int count = static_cast<int>(tiles.size());
    if (row_ == 0) pillFocus_[slot_].retarget(0.0f, design::kFocusDuration);
    else if (slot_ < count) tiles[slot_].focus.retarget(0.0f, design::kFocusDuration);
    row_ = row;
    slot_ = slot;
    if (row_ == 0) {
        pillFocus_[slot_].retarget(1.0f, design::kFocusDuration);
    } else {
        rememberedTileSlot_ = slot_;
        tiles[slot_].focus.retarget(1.0f, design::kFocusDuration);
    }
    sound::play(sound::Cue::Move);
}

bool LibraryScreen::tapAt(float x, float y, Result* out) {
    const int id = taps_.at(x, y);
    if (id < 0) return false;
    const int count = static_cast<int>(visible().size());
    if (id >= kPillSpot) focusOn(0, id - kPillSpot);
    else if (id < count) focusOn(1, id);
    else return false;
    *out = key(Nav::Activate);
    return true;
}

void LibraryScreen::draw(Ctx& c) {
    taps_.clear();
    // Everything this screen draws is multiplied by the arrival. The backdrop
    // underneath is NOT — it belongs to the app and stays put, which is what
    // the content is fading in against.
    c.r.setContentAlpha(appear_.value());
    // Everything below scrolls, so it is clipped to the window under the bar.
    // See design::kScrollClipTop.
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
    const auto& tiles = visible();
    const float tw = tileWidth();
    const float pillH = pillHeight(c.text, c.sc);
    const float gridTop = design::kSwitcherTop + pillH + design::kSwitcherGapBelow;
    const float rowPitch = design::kTileHeight + design::kTileSpacing;
    const float headingH = c.text.lineHeight(ui::TextStyle::Title3, c.sc);
    const Layout L = layout(headingH);

    // Scroll to keep the focused row on screen, and never past the end of the
    // content: pinning the last row to the top leaves half a screen of nothing
    // under it, which reads as the layout having broken.
    const float contentH = gridTop + L.bottom + design::kScreenBottomPad;
    const float maxScroll = std::max(0.0f, contentH - ui::kCanvasHeight);
    float want = 0.0f;
    if (row_ > 0 && slot_ < static_cast<int>(L.rowOf.size())) {
        const int r = L.rowOf[static_cast<size_t>(slot_)];
        // A row under a heading brings its heading on screen with it.
        const float rowTop = gridTop + L.rowY[static_cast<size_t>(r)] -
                             (L.rowHeaded[static_cast<size_t>(r)]
                                  ? headingH + design::kTileGroupGapBelow
                                  : 0.0f);
        const float rowBottom = gridTop + L.rowY[static_cast<size_t>(r)] + design::kTileHeight;
        const float current = scroll_.to;
        if (rowBottom - current > ui::kCanvasHeight - design::kScreenBottomPad)
            want = rowBottom - (ui::kCanvasHeight - design::kScreenBottomPad);
        else if (rowTop - current < gridTop)
            want = std::max(0.0f, rowTop - gridTop);
        else
            want = current;
        want = std::clamp(want, 0.0f, maxScroll);
    }
    if (std::fabs(want - scroll_.to) > 0.5f)
        scroll_.retarget(want, design::kFocusDuration);
    const float scroll = scroll_.value();

    if (tiles.empty()) {
        // An honest empty state rather than a blank grid. A library with no
        // collections is ordinary, not broken.
        const char* line = tab_ == 0 ? "No systems on this server"
                                     : "No collections on this server";
        const float w = c.text.measure(line, ui::TextStyle::Title3, c.sc);
        c.text.draw(c.r, line, (ui::kCanvasWidth - w) * 0.5f, ui::kCanvasHeight * 0.5f,
                    ui::TextStyle::Title3, ui::Color::white(0.60f), c.sc);
        return;
    }

    // The groups' headings: Title 3 on the margin, as Home's shelves.
    for (const auto& [hy, text] : L.headings) {
        const float top = gridTop + hy - scroll;
        if (top + headingH < 0.0f || top > ui::kCanvasHeight) continue;
        c.text.draw(c.r, c.text.truncate(text, ui::TextStyle::Title3, c.sc,
                                         ui::kCanvasWidth - design::kLibraryInset * 2.0f),
                    design::kLibraryInset, top + c.text.ascent(ui::TextStyle::Title3, c.sc),
                    ui::TextStyle::Title3, ui::Color::white(1.0f), c.sc);
    }

    // Unfocused first, so a focused tile's shadow lands on top of its
    // neighbours rather than under them.
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < tiles.size(); ++i) {
            const bool isFocused = (row_ > 0 && static_cast<int>(i) == slot_);
            if ((pass == 0) == isFocused) continue;

            const int col = L.colOf[i];
            const float bx = design::kLibraryInset +
                             static_cast<float>(col) * (tw + design::kTileSpacing);
            const float by = gridTop + L.rowY[static_cast<size_t>(L.rowOf[i])] - scroll;
            // Cull to what is on screen. A grid is a window onto a list, not a
            // drawing of the list — and the cover cache must not be asked for
            // art that is nowhere near the screen.
            if (by + design::kTileHeight < -rowPitch) continue;
            if (by > ui::kCanvasHeight + rowPitch) break;

            taps_.add(static_cast<int>(i), bx, by, tw, design::kTileHeight,
                      design::kScrollClipTop);

            const Tile& t = tiles[i];
            const float f = t.focus.value();
            const float s = 1.0f + f * (design::kFocusScale - 1.0f);
            const float w = tw * s, h = design::kTileHeight * s;
            const float x = bx - (w - tw) * 0.5f;
            const float y = by - (h - design::kTileHeight) * 0.5f;

            // The tile's ground: the one solid panel colour in the design
            // system. Dimmed when the platform cannot be played, which is the
            // difference a person has to be able to see from a sofa.
            ui::Rect plate{x, y, w, h, design::kTileRadius,
                           t.enterable ? look::surface()
                                       : ui::Color::white(0.06f)};
            // NO RIM: a platform tile is a composite element, art mixed with
            // its own text, and a rectangle drawn round it crosses the text.
            applyArtworkFocus(plate, f, /*rim=*/false);
            c.r.draw(plate);

            // The cover, right-hand side, inset by the tile's padding. A cover
            // thumbnail inside another element takes the 8pt radius.
            const float pad = design::kTilePadding * s;
            // SMALLER THAN THE TILE IS TALL, and the width it gives back goes to
            // the label. A thumbnail filling the full height left 227pt for a
            // name, which is less than "Sega Master" needs at Title 3 — so the
            // artwork was winning an argument against the thing the tile is
            // actually for. A tile with no artwork at all gives the label the
            // whole width, rather than reserving a column for a picture that
            // does not exist: that is the unplayable ones, which have the most
            // to say and had the least room to say it in.
            const float artH = t.cover.empty() ? 0.0f : design::kTileArtHeight * s;
            const float artW = artH * 0.75f;      // 3:4
            const float artX = x + w - pad - artW;
            if (!t.cover.empty()) {
                const ui::Image& img = c.images.get(t.cover);
                // The blurred echo behind everything, so the tile is the art's
                // own colours rather than a flat panel. Clipped to the tile.
                if (img.ready) {
                    c.r.drawTextured(x, y, w, h, img.texture, 0, 0, 1, 1,
                                     ui::Color{1, 1, 1, 0.35f * img.fade}, false,
                                     design::kBackdropBlur, x, y, w, h,
                                     design::kTileRadius);
                }
                // Centred against the tile rather than pinned to the padding,
                // now that it no longer spans the full height.
                ui::drawImage(c.r, img, artX, y + (h - artH) * 0.5f, artW, artH,
                              ui::Fit::Fill, t.enterable ? 1.0f : 0.45f, 8.0f);
            }

            // The label, left. Title 3 for the name, Footnote for the count —
            // the ramp's own roles for a tile title and a game count.
            //
            // TWO LINES FOR THE TITLE, for the same reason grid captions get
            // two: one line truncated almost every real name at this width, and
            // here it did worse than look untidy. "Nintendo 64" and "Nintendo
            // DS" both came out as "Nintendo ..." on adjacent tiles, and
            // "Arcade (FinalBurn Neo)" and "Arcade (MAME 2003-Plus)" — the two
            // this console goes to some trouble to tell apart — both came out as
            // "Arcade (...". A tile nobody can identify is not a tile.
            const float textW = artW > 0 ? (artX - x - pad - design::kTileArtGap)
                                         : (w - pad * 2.0f);
            const std::vector<std::string> titleLines =
                wrapTwoLines(c.text, t.title, ui::TextStyle::Title3, c.sc, textW);
            const float titleH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) *
                                 static_cast<float>(titleLines.size());
            const float labelH = titleH + c.text.lineHeight(ui::TextStyle::Footnote, c.sc);
            // Centred against the artwork rather than pinned to the top, so a
            // one-line tile and a two-line tile both sit square in the row.
            float baseline = y + (h - labelH) * 0.5f +
                             c.text.ascent(ui::TextStyle::Title3, c.sc);
            for (const std::string& line : titleLines) {
                c.text.draw(c.r, line, x + pad, baseline, ui::TextStyle::Title3,
                            ui::Color::white(t.enterable ? 1.0f : 0.60f), c.sc);
                baseline += c.text.lineHeight(ui::TextStyle::Title3, c.sc);
            }
            c.text.draw(c.r, c.text.truncate(t.detail, ui::TextStyle::Footnote, c.sc, textW),
                        x + pad, baseline, ui::TextStyle::Footnote,
                        ui::Color::white(0.60f), c.sc);
        }
    }
}

void LibraryScreen::drawGlass(Ctx& c) {
    c.r.setContentAlpha(appear_.value());
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
    // The switcher: Platforms and Collections as capsule pills. It is the only
    // heading this screen gets — the navigation bar already says "Library", and
    // a screen title that repeats the tab is chrome the reference
    // implementation deliberately removed.
    //
    // IT SCROLLS WITH THE TILES, which is what "a pushed page carries its title
    // as ordinary content at the top of its own SCROLL VIEW, never as system
    // chrome" means. Pinned, it sat over the grid as the grid moved under it,
    // and a tile's name ran behind a pill that was not part of the same surface.
    static const char* kLabels[2] = {"Platforms", "Collections"};
    const float y = design::kSwitcherTop - scroll_.value();
    if (y + pillHeight(c.text, c.sc) < 0.0f) return;
    // OPTICALLY ALIGNED, not box-aligned: the capsule starts a padding-width
    // LEFT of the margin so that "Platforms" begins exactly where the bar's
    // "Library" and the tiles below do. See kContentInset.
    // The TEXT starts on the margin, the same as Home's shelf headings and the
    // bar above. drawPill puts its focus capsule outside that.
    float x = design::kLibraryInset;
    for (int i = 0; i < 2; ++i) {
        const float f = (row_ == 0 && slot_ == i) ? pillFocus_[i].value() : 0.0f;
        taps_.add(kPillSpot + i, x - design::kPillPadX, y, pillWidth(c.text, kLabels[i], c.sc),
                  pillHeight(c.text, c.sc), design::kScrollClipTop);
        drawPill(c, kLabels[i], x, y, tab_ == i, f);
        x += pillWidth(c.text, kLabels[i], c.sc) + design::kPillGap;
    }
}

}  // namespace screens
