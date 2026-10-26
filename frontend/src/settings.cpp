#include "settings.h"
#include "qr.h"
#include "wikiqr.h"
#include "wikiqrview.h"

#include "sound.h"

#include <algorithm>
#include <cmath>

namespace screens {

using Kind = SettingsRow::Kind;

namespace {

// The right side cross-fading to a different category. Quicker than the
// design system's 220 ms segmented choice, because this happens once per step
// down the list. A starting value.
constexpr float kPaneChange = 0.150f;

// The one arrow the product already uses for "this opens something": the
// shelf headings on Home carry it.
constexpr const char* kChevron = "\xE2\x80\xBA";
// Its mirror, for a choice row's "left moves it this way".
constexpr const char* kChevronBack = "\xE2\x80\xB9";

// A choice row's arrows: bright where left or right still goes somewhere,
// faint at the end it cannot pass. Starting values.
constexpr float kChoiceArrowOn = 0.70f;
constexpr float kChoiceArrowOff = 0.18f;
constexpr float kChoiceArrowGap = 14.0f;

float rowHeight(Ctx& c, const SettingsRow& row) {
    float h = design::kRowPadY * 2.0f + c.text.lineHeight(ui::TextStyle::Title3, c.sc);
    if (!row.detail.empty())
        h += design::kSettingsDetailGap + c.text.lineHeight(ui::TextStyle::Callout, c.sc);
    return h;
}

// Where the rows start, left edge and width. The list's capsules are pulled
// back by their own padding so their LABELS sit on the product's one margin,
// the same rule the bar and the switcher follow; the rows start a gap to the
// right of the list's capsules.
float listX() { return design::kContentInset - design::kRowPadX * 0.5f; }
float paneX() { return listX() + design::kSettingsListWidth + design::kSettingsPaneGap; }
float paneW() {
    return std::min(design::kRowColumnMaxWidth,
                    ui::kCanvasWidth - design::kContentInset - paneX());
}

}  // namespace

void SettingsScreen::setCategories(std::vector<SettingsCategory> cats) {
    cats_ = std::move(cats);
    if (cats_.empty()) { cat_ = 0; row_ = -1; return; }
    cat_ = std::clamp(cat_, 0, static_cast<int>(cats_.size()) - 1);
    if (row_ >= 0 && !focusable(cat_, row_)) row_ = firstFocusable(cat_);
}

void SettingsScreen::enter() {
    // Animated every time, for the reason the Library gives: coming back is an
    // arrival too, and a screen that faded in once and cut in after is worse
    // than one that always cut.
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);
    row_ = -1;
    scroll_.settle(0.0f);
    // Ease-in-out, so the two sets of rows cross at the middle.
    paneChange_.smooth = true;
    paneChange_.settle(1.0f);
    focus_.settle(1.0f);
}

void SettingsScreen::focusCategory(int index, bool intoRows) {
    if (cats_.empty()) return;
    cat_ = std::clamp(index, 0, static_cast<int>(cats_.size()) - 1);
    row_ = intoRows ? firstFocusable(cat_) : -1;
    // Settled, not arriving: a capture should show the resting screen, not a
    // frame part-way through the fade in.
    appear_.settle(1.0f);
    focus_.settle(1.0f);
    paneChange_.settle(1.0f);
}

bool SettingsScreen::focusable(int cat, int row) const {
    if (cat < 0 || cat >= static_cast<int>(cats_.size())) return false;
    const auto& rows = cats_[cat].rows;
    if (row < 0 || row >= static_cast<int>(rows.size())) return false;
    return rows[row].kind == Kind::Action || rows[row].kind == Kind::Toggle ||
           rows[row].kind == Kind::Choice || (rows[row].kind == Kind::Info && rows[row].pressable);
}

int SettingsScreen::choiceOf(int id) const {
    for (const auto& cat : cats_)
        for (const auto& row : cat.rows)
            if (row.kind == Kind::Choice && row.id == id) return row.choice;
    return -1;
}

int SettingsScreen::firstFocusable(int cat) const {
    if (cat < 0 || cat >= static_cast<int>(cats_.size())) return -1;
    for (int i = 0; i < static_cast<int>(cats_[cat].rows.size()); ++i)
        if (focusable(cat, i)) return i;
    return -1;
}

void SettingsScreen::retargetFocus() {
    focus_.retarget(0.0f, 0.0f);
    focus_.elapsed = 0.0f;
    focus_.retarget(1.0f, design::kFocusDuration);
}

void SettingsScreen::setHasFocus(bool on) {
    if (on == hasFocus_) return;
    hasFocus_ = on;
    if (on) retargetFocus();
}

void SettingsScreen::tick(float dt) {
    appear_.tick(dt);
    focus_.tick(dt);
    scroll_.tick(dt);
    paneChange_.tick(dt);
}

Result SettingsScreen::key(Nav n) {
    if (cats_.empty()) return n == Nav::Back ? Result{Action::Back, 0} : Result{};
    const int cats = static_cast<int>(cats_.size());

    // ---- In the category list ----
    if (row_ < 0) {
        switch (n) {
            case Nav::Up:
                // Up off the first category is the bar, drawn above this
                // screen and owned by the app.
                if (cat_ == 0) return {Action::FocusBar, 0};
                [[fallthrough]];
            case Nav::Down: {
                const int next = cat_ + (n == Nav::Down ? 1 : -1);
                if (next < 0 || next >= cats) { sound::play(sound::Cue::Edge); return {}; }
                prevCat_ = cat_;
                cat_ = next;
                // THE RIGHT SIDE FOLLOWS AT ONCE. That is the whole reason for
                // this shape: no open, no back, just look.
                paneChange_.retarget(0.0f, 0.0f);
                paneChange_.elapsed = 0.0f;
                paneChange_.retarget(1.0f, kPaneChange);
                scroll_.settle(0.0f);
                retargetFocus();
                sound::play(sound::Cue::Move);
                return {};
            }
            case Nav::Right:
            case Nav::Activate: {
                // Into the rows, on the first one that does something. A
                // category with nothing built yet has nowhere to go, and says
                // so with the edge sound rather than moving focus onto a row
                // that would do nothing.
                const int r = firstFocusable(cat_);
                if (r < 0) { sound::play(sound::Cue::Edge); return {}; }
                row_ = r;
                retargetFocus();
                sound::play(sound::Cue::Move);
                return {};
            }
            case Nav::Left:
                sound::play(sound::Cue::Edge);
                return {};
            case Nav::Back:
                return {Action::Back, 0};
        }
        return {};
    }

    // ---- In the rows ----
    auto& rows = cats_[cat_].rows;

    // A CHOICE ROW TAKES LEFT AND RIGHT FOR ITSELF, which is what makes it
    // one row rather than a page: the level changes where you are standing.
    // Back is the way out to the list from it. Nothing here sounds: the app
    // applies the change first, so a sounds row is heard at its new level.
    if (row_ >= 0 && rows[row_].kind == Kind::Choice &&
        (n == Nav::Left || n == Nav::Right || n == Nav::Activate)) {
        SettingsRow& row = rows[row_];
        const int count = static_cast<int>(row.choices.size());
        int next = row.choice + (n == Nav::Left ? -1 : 1);
        // A walks forward and wraps, so the row also works from A alone.
        if (n == Nav::Activate && next >= count) next = 0;
        if (count == 0 || next < 0 || next >= count) {
            sound::play(sound::Cue::Edge);
            return {};
        }
        row.choice = next;
        return {Action::SettingChoice, row.id};
    }

    switch (n) {
        case Nav::Up:
        case Nav::Down: {
            const int d = (n == Nav::Down) ? 1 : -1;
            int next = row_ + d;
            while (next >= 0 && next < static_cast<int>(rows.size()) &&
                   !focusable(cat_, next))
                next += d;
            if (next < 0 || next >= static_cast<int>(rows.size())) {
                sound::play(sound::Cue::Edge);
                return {};
            }
            row_ = next;
            retargetFocus();
            sound::play(sound::Cue::Move);
            return {};
        }
        // LEFT AND BACK BOTH RETURN TO THE LIST, and Back does not leave
        // Settings from here: somebody who walked into a category and pressed
        // Back wants the category list, not Home.
        case Nav::Left:
        case Nav::Back:
            row_ = -1;
            retargetFocus();
            sound::play(n == Nav::Back ? sound::Cue::Back : sound::Cue::Move);
            return {};
        case Nav::Right:
            sound::play(sound::Cue::Edge);
            return {};
        case Nav::Activate:
            if (focusable(cat_, row_)) return {Action::Setting, rows[row_].id};
            return {};
    }
    return {};
}

// FORK (handheld): a category is spot 1000 + its place, a row of the open
// category its own place.
constexpr int kCategorySpot = 1000;

bool SettingsScreen::tapAt(float x, float y, Result* out) {
    const int id = taps_.at(x, y);
    if (id < 0) return false;
    if (id >= kCategorySpot) {
        const int next = id - kCategorySpot;
        if (next >= static_cast<int>(cats_.size())) return false;
        if (next != cat_ || row_ >= 0) {
            // As walking onto it with the d-pad does: the rows on the right follow at once.
            if (next != cat_) {
                prevCat_ = cat_;
                cat_ = next;
                paneChange_.retarget(0.0f, 0.0f);
                paneChange_.elapsed = 0.0f;
                paneChange_.retarget(1.0f, kPaneChange);
                scroll_.settle(0.0f);
            }
            row_ = -1;
            retargetFocus();
            sound::play(sound::Cue::Move);
        }
        *out = {};
        return true;
    }
    if (id >= static_cast<int>(cats_[cat_].rows.size()) || !focusable(cat_, id)) {
        *out = {};
        return true;   // on a row, but one that does nothing: taken, not acted on
    }
    row_ = id;
    retargetFocus();
    *out = key(Nav::Activate);
    return true;
}

void SettingsScreen::draw(Ctx&) {
    // Nothing: every surface on this screen is glass, which reads the scene
    // through itself and so has to draw after presentScene. See drawGlass.
}

void SettingsScreen::drawGlass(Ctx& c) {
    taps_.clear();   // FORK (handheld)
    if (cats_.empty()) return;
    const float a = appear_.value();
    c.r.setContentAlpha(a);
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);

    const float top = design::kContentTop;
    const float f = focus_.value();

    // ---- The category list ----
    //
    // SELECTION IS NOT FOCUS, and both show at once: the category whose rows
    // are on the right is full white with a quiet ground under it; the one
    // the cursor is on gets the focused tint and grows a little. Walking into
    // the rows leaves the ground, so which category you are in survives focus
    // leaving the list.
    const float itemH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) +
                        design::kSettingsListPadY * 2.0f;
    float y = top;
    for (int i = 0; i < static_cast<int>(cats_.size()); ++i) {
        const bool selected = (i == cat_);
        const bool focused = selected && row_ < 0 && hasFocus_;
        const float ff = focused ? f : 0.0f;
        const float s = 1.0f + ff * (design::kRowFocusScale - 1.0f);
        const float w0 = design::kSettingsListWidth;
        const float w = w0 * s, h = itemH * s;
        const float x = listX() - (w - w0) * 0.5f;
        const float iy = y - (h - itemH) * 0.5f;
        if (focused)
            c.r.drawGlass(ui::Rect{x, iy, w, h, design::kRowRadius, ui::Color::white(0)},
                          design::kRegularMaterialBlur,
                          ui::Color::white(design::kFocusedTint * ff));
        else if (selected && hasFocus_)
            c.r.draw(ui::Rect{x, iy, w, h, design::kRowRadius,
                              ui::Color::white(design::kSelectedTint * 0.6f)});
        c.text.draw(c.r, cats_[i].name, design::kContentInset,
                    iy + design::kSettingsListPadY * s +
                        c.text.ascent(ui::TextStyle::Title3, c.sc),
                    ui::TextStyle::Title3,
                    ui::Color::white(selected ? 1.0f : 0.60f), c.sc);
        taps_.add(kCategorySpot + i, listX(), y, design::kSettingsListWidth, itemH,
                  design::kScrollClipTop);
        y += itemH + design::kSettingsListGap;
    }

    // ---- The rows ----
    const auto& rows = cats_[cat_].rows;
    const float px = paneX(), pw = paneW();
    const float gap = design::kDetailRowGap;

    // Scroll to keep the focused row on screen, never past the end.
    float contentH = 0.0f;
    float focusTop = 0.0f, focusBottom = 0.0f;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const float h = rowHeight(c, rows[i]);
        if (i == row_) { focusTop = contentH; focusBottom = contentH + h; }
        contentH += h + gap;
    }
    const float room = ui::kCanvasHeight - design::kScreenBottomPad - top;
    const float maxScroll = std::max(0.0f, contentH - gap - room);
    float want = scroll_.to;
    if (row_ >= 0) {
        if (focusBottom - want > room) want = focusBottom - room;
        else if (focusTop - want < 0.0f) want = focusTop;
    }
    want = std::clamp(want, 0.0f, maxScroll);
    if (std::fabs(want - scroll_.to) > 0.5f) scroll_.retarget(want, design::kFocusDuration);

    // A CATEGORY CHANGE CROSS-FADES: the rows that were showing fade out as
    // the new ones fade in, with no movement, because a person runs down this
    // list quickly and anything that slid would be busy. MMagTech asked,
    // 2026-09-24; the old rows used to vanish in one frame.
    auto drawRows = [&](const std::vector<SettingsRow>& list, float alpha, int focusRow,
                        float scrollY) {
    c.r.setContentAlpha(a * alpha);
    float y = top - scrollY;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const SettingsRow& row = list[i];
        const float rh = rowHeight(c, row);
        if (&list == &rows) taps_.add(i, px, y, pw, rh, design::kScrollClipTop);
        const bool on = (i == focusRow) && hasFocus_;
        const float rf = on ? f : 0.0f;
        const bool dimmed = row.kind == Kind::Disabled;
        const float s = 1.0f + rf * (design::kRowFocusScale - 1.0f);
        const float w = pw * s, h = rh * s;
        const float x = px - (w - pw) * 0.5f;
        const float ry = y - (h - rh) * 0.5f;

        // Treatment 3, the row: a surface that is always there, brighter under
        // focus. A dimmed row keeps a fainter surface so the list still
        // reads as one list.
        c.r.drawGlass(ui::Rect{x, ry, w, h, design::kRowRadius, ui::Color::white(0)},
                      design::kRegularMaterialBlur,
                      ui::Color::white(dimmed ? 0.04f : 0.08f + 0.14f * rf));

        const float textA = dimmed ? design::kSettingsUnbuiltAlpha : 1.0f;
        const bool chevron = row.kind == Kind::Action;
        const float chevW = chevron
            ? c.text.measure(kChevron, ui::TextStyle::Title3, c.sc) : 0.0f;
        const bool choice = row.kind == Kind::Choice && !row.choices.empty();
        const int pick = choice ? std::clamp(row.choice, 0,
                                             static_cast<int>(row.choices.size()) - 1)
                                : 0;
        const std::string value = choice ? row.choices[pick] : row.value;
        const float valueW = value.empty()
            ? 0.0f : c.text.measure(value, ui::TextStyle::Callout, c.sc);
        float right = x + w - design::kRowPadX;

        // The value and the chevron sit on the TITLE's line, so a row with a
        // detail line under it still reads left to right as "name ... state".
        const float titleBase = ry + design::kRowPadY * s +
                                c.text.ascent(ui::TextStyle::Title3, c.sc);
        if (chevron) {
            right -= chevW;
            c.text.draw(c.r, kChevron, right, titleBase, ui::TextStyle::Title3,
                        ui::Color::white(0.30f), c.sc);
            right -= 16.0f;
        }
        // A CHOICE ROW UNDER FOCUS shows which way it can still go, an arrow
        // either side of the value. Unfocused it reads like any other value,
        // because the arrows only mean something when left and right do.
        if (choice && rf > 0.0f) {
            const float aw = c.text.measure(kChevron, ui::TextStyle::Callout, c.sc);
            const bool canRight = pick + 1 < static_cast<int>(row.choices.size());
            right -= aw;
            c.text.draw(c.r, kChevron, right, titleBase, ui::TextStyle::Callout,
                        ui::Color::white((canRight ? kChoiceArrowOn : kChoiceArrowOff) * rf),
                        c.sc);
            right -= kChoiceArrowGap;
        }
        if (!value.empty()) {
            right -= valueW;
            c.text.draw(c.r, value, right, titleBase, ui::TextStyle::Callout,
                        ui::Color::white(0.60f), c.sc);
            right -= 24.0f;
        }
        if (choice && rf > 0.0f) {
            const float aw = c.text.measure(kChevronBack, ui::TextStyle::Callout, c.sc);
            right += 24.0f - kChoiceArrowGap;
            right -= aw;
            c.text.draw(c.r, kChevronBack, right, titleBase, ui::TextStyle::Callout,
                        ui::Color::white((pick > 0 ? kChoiceArrowOn : kChoiceArrowOff) * rf),
                        c.sc);
            right -= 24.0f;
        }

        const float textX = x + design::kRowPadX;
        const float titleMax = std::max(0.0f, right - textX);
        c.text.draw(c.r, c.text.truncate(row.title, ui::TextStyle::Title3, c.sc, titleMax),
                    textX, titleBase, ui::TextStyle::Title3,
                    ui::Color::white((on ? 1.0f : 0.92f) * textA), c.sc);
        if (!row.detail.empty()) {
            const float detailBase = ry + design::kRowPadY * s +
                                     c.text.lineHeight(ui::TextStyle::Title3, c.sc) +
                                     design::kSettingsDetailGap +
                                     c.text.ascent(ui::TextStyle::Callout, c.sc);
            const float detailMax = x + w - design::kRowPadX - textX;
            c.text.draw(c.r,
                        c.text.truncate(row.detail, ui::TextStyle::Callout, c.sc, detailMax),
                        textX, detailBase, ui::TextStyle::Callout,
                        ui::Color::white(0.60f * textA), c.sc);
        }
        y += rh + gap;
    }
    };
    // A CATEGORY WITH A QR CODE (How to's Wiki): its rows as any other
    // category's (the address is a row's value), then the code centred under
    // them, a clear gap below the last row, as large as reaches the bottom of
    // the category's own item in the list. The card above anchors it and the
    // list's item ends level with it. Alone, at the pane's edge or centred,
    // it floated (MMagTech on the TV, 2026-10-10). Sized from the drawn
    // category's own item, not the focused one's, so a code fading out keeps
    // its size (SETTINGS-16).
    auto drawQr = [&](int ci, float alpha) {
        const SettingsCategory& cat = cats_[ci];
        // THE WIKI'S CODE IS MADE ONCE, with the cabinet in it
        // (tools/make-wiki-qr.py, level H): its address never changes.
        const bool wiki = cat.qr == wikiqr::kUrl;
        if (qrFor_ != cat.qr) {
            std::string err;
            if (wiki) {
                qr_.set(wikiqr::qrCode(wikiqr::Which::Wiki));
            } else {
                qr_.set(qr::encode(cat.qr, &err));
            }
            qrFor_ = cat.qr;
        }
        float below = 0.0f;
        for (const SettingsRow& r : cat.rows) below += rowHeight(c, r) + gap;
        c.r.setContentAlpha(a * alpha);
        const float qy = top + below + 28.0f;
        const float itemBottom = top + ci * (itemH + design::kSettingsListGap) + itemH;
        const float side = std::clamp(itemBottom - qy, 360.0f, std::min(pw, 640.0f));
        const float qx = px + (pw - side) * 0.5f;
        if (qr_.valid()) qr_.draw(c.r, qx, qy, side);
        if (wiki && qr_.valid())
            wikiqr::drawPatch(wikiqr::Which::Wiki, c.r, qx, qy, side, alpha);
    };
    auto drawPane = [&](int ci, float alpha, int focusRow, float scrollY) {
        drawRows(cats_[ci].rows, alpha, focusRow, scrollY);
        if (!cats_[ci].qr.empty()) drawQr(ci, alpha);
    };
    const float pa = paneChange_.value();
    if (pa < 1.0f && prevCat_ >= 0 && prevCat_ < static_cast<int>(cats_.size()) &&
        prevCat_ != cat_)
        drawPane(prevCat_, 1.0f - pa, -1, 0.0f);
    drawPane(cat_, pa, row_, scroll_.value());
}

}  // namespace screens
