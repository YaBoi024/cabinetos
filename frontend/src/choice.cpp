#include "choice.h"

#include <cmath>

#include "panels.h"
#include "sound.h"

#include <algorithm>

namespace screens {

namespace {

// The PIN pad's panel sizes (pin.cpp); the look is design::menuPanel.
constexpr float kPanelPad = 40.0f;
constexpr float kPanelMaxW = 1200.0f;
constexpr float kButtonW = 600.0f;
constexpr float kButtonH = 88.0f;
constexpr float kButtonGap = 12.0f;
constexpr float kButtonRadius = 16.0f;
constexpr float kFocusScale = 1.04f;
constexpr float kAppear = 0.280f;
// Back from a level of the pause menu: its rows out, then the menu's in.
constexpr float kBackFade = 0.400f;
// MORE THAN THIS SCROLLS, for a Wi-Fi list in a crowded building.
constexpr int kMaxVisible = 6;
// How long a growing list takes to make room for a new row.
constexpr float kGrow = 0.25f;

}  // namespace

void ChoiceScreen::open(std::string title, std::string detail,
                        std::vector<std::string> options, int focus) {
    open_ = true;
    staysOpen_ = false;
    busy_ = false;
    fixedW_ = 0.0f;
    panelW_ = panelH_ = 0.0f;
    overMenu_ = false;
    withMenu_ = false;
    fit_ = 0;
    grows_ = false;
    placeholder_.clear();
    listHSet_ = false;
    title_ = std::move(title);
    detail_ = std::move(detail);
    options_ = std::move(options);
    values_.clear();
    disabled_.clear();
    promptBefore_.clear();
    promptButton_.clear();
    promptAfter_.clear();
    stepper_ = -1;
    stepDir_ = 0;
    slot_ = options_.empty() ? 0 : std::clamp(focus, 0, static_cast<int>(options_.size()) - 1);
    top_ = std::max(0, slot_ - (kMaxVisible - 1));
    appear_.smooth = false;
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, kAppear);
    focus_.settle(1.0f);
}

int ChoiceScreen::fit() const { return fit_ > 0 ? fit_ : kMaxVisible; }

// GOING IN, THE MENU'S ROWS GO AT ONCE AS THE LEVEL'S FADE IN. COMING
// BACK, ONE AFTER THE OTHER: the level's rows fade out over the first half
// of `appear_` running back down, then the menu's fade in over the second,
// eased in and out so each half takes as long. The panel under them never
// goes. (A fade across, both at once, did not look right: MMagTech at the
// TV, 2026-10-25.)
float ChoiceScreen::menuRows() const {
    if (!overMenu_) return 1.0f;
    return open_ || withMenu_ ? 0.0f : std::clamp(1.0f - 2.0f * appear_.value(), 0.0f, 1.0f);
}

float ChoiceScreen::levelShown() const {
    return open_ || withMenu_ ? appear_.value()
                              : std::clamp(2.0f * appear_.value() - 1.0f, 0.0f, 1.0f);
}

void ChoiceScreen::close() {
    open_ = false;
    withMenu_ = false;
    if (overMenu_) {
        appear_.smooth = true;
        appear_.retarget(0.0f, kBackFade);
    }
}

void ChoiceScreen::forget() {
    open_ = false;
    overMenu_ = false;
    withMenu_ = false;
    appear_.settle(0.0f);
}

void ChoiceScreen::replace(std::vector<std::string> options, std::vector<std::string> values,
                           std::string detail) {
    const std::string was = (slot_ >= 0 && slot_ < static_cast<int>(options_.size()))
                                ? options_[slot_] : std::string();
    options_ = std::move(options);
    values_ = std::move(values);
    detail_ = std::move(detail);
    slot_ = 0;
    for (int i = 0; i < static_cast<int>(options_.size()); ++i)
        if (options_[i] == was) slot_ = i;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(options_.size()) - fit()));
    if (slot_ < top_) top_ = slot_;
    if (slot_ >= top_ + fit()) top_ = slot_ - fit() + 1;
}

void ChoiceScreen::setDisabled(std::vector<bool> disabled) {
    disabled_ = std::move(disabled);
    const int n = static_cast<int>(options_.size());
    if (!isDisabled(slot_)) return;
    for (int i = 0; i < n; ++i) {
        const int j = (slot_ + i) % n;
        if (!isDisabled(j)) {
            slot_ = j;
            break;
        }
    }
    top_ = std::max(0, slot_ - (kMaxVisible - 1));
}

ChoiceScreen::Outcome ChoiceScreen::key(Nav n) {
    if (!open_) return Outcome::None;
    switch (n) {
        case Nav::Up:
        case Nav::Down: {
            // Past any greyed answer: focus never lands on one.
            int next = slot_ + (n == Nav::Down ? 1 : -1);
            while (next >= 0 && next < static_cast<int>(options_.size()) && isDisabled(next))
                next += (n == Nav::Down ? 1 : -1);
            if (next < 0 || next >= static_cast<int>(options_.size())) {
                sound::play(sound::Cue::Edge);
                return Outcome::None;
            }
            slot_ = next;
            if (slot_ < top_) top_ = slot_;
            if (slot_ >= top_ + fit()) top_ = slot_ - fit() + 1;
            focus_.retarget(0.0f, 0.0f);
            focus_.elapsed = 0.0f;
            focus_.retarget(1.0f, design::kFocusDuration);
            sound::play(sound::Cue::Move);
            return Outcome::None;
        }
        case Nav::Left:
        case Nav::Right:
            if (slot_ == stepper_ && (n == Nav::Left ? canLeft_ : canRight_)) {
                stepDir_ = n == Nav::Left ? -1 : 1;
                sound::play(sound::Cue::Move);
                return Outcome::Stepped;
            }
            sound::play(sound::Cue::Edge);
            return Outcome::None;
        case Nav::Activate:
            // A PROMPT THAT NAMES (A) IS ANSWERED BY A: a question with one
            // thing to do says it rather than focusing a lone button nobody
            // moved onto ("Press (A) to insert and restart", #162).
            if (options_.empty() && promptButton_ == "A") {
                slot_ = 0;
                sound::play(sound::Cue::Activate);
                return Outcome::Chosen;
            }
            if (options_.empty() || isDisabled(slot_)) {
                sound::play(sound::Cue::Edge);
                return Outcome::None;
            }
            sound::play(sound::Cue::Activate);
            return Outcome::Chosen;
        case Nav::Back:
            if (busy_) {
                sound::play(sound::Cue::Edge);
                return Outcome::None;
            }
            sound::play(sound::Cue::Back);
            return Outcome::Cancelled;
    }
    return Outcome::None;
}

void ChoiceScreen::tick(float dt) {
    appear_.tick(dt);
    listH_.tick(dt);
    focus_.tick(dt);
    if (busy_) spin_ += dt;
}

// A LEVEL OF THE PAUSE MENU IS DRAWN AS THE MENU DRAWS ITS OWN ROWS
// (pausemenu.cpp, drawPauseMenu): the same buttons, sizes, spacing and
// place in the panel, centred, a row with a value written "Name: value",
// a stepper's arrows at the button's ends. So opening it changes the rows'
// words and nothing else. As a list of its own (names left, values right,
// a title), the whole panel rearranged at the switch, and MMagTech at the
// TV found "something off" (2026-10-25).
void ChoiceScreen::drawMenuRows(Ctx& c, float a) {
    using ui::TextStyle;
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight, sc = c.sc;
    const float px = (W - panelW_) * 0.5f, py = (H - panelH_) * 0.5f;
    const float rowH = design::kOverlayButtonHeight, gap = design::kOverlayButtonGap;
    const int n = static_cast<int>(options_.size());
    // As many rows as the menu's panel holds (its 32 above and below), focus
    // kept in view among them.
    fit_ = std::max(1, static_cast<int>((panelH_ - 64.0f + gap) / (rowH + gap)));
    top_ = std::clamp(top_, 0, std::max(0, n - fit_));
    if (slot_ < top_) top_ = slot_;
    if (slot_ >= top_ + fit_) top_ = slot_ - fit_ + 1;
    const TextStyle st = TextStyle::Title3;
    const float f = focus_.value();
    c.r.setContentAlpha(a);
    for (int i = top_; i < std::min(n, top_ + fit_); ++i) {
        const bool on = i == slot_;
        const float fi = on ? f : 0.0f;
        const float s = 1.0f + fi * (design::kOverlayFocusScale - 1.0f);
        const float bw = (panelW_ - 64.0f) * s, bh = rowH * s;
        const float bx = px + (panelW_ - bw) * 0.5f;
        const float by = py + 32.0f + static_cast<float>(i - top_) * (rowH + gap) - (bh - rowH) * 0.5f;
        c.r.draw(design::menuButton(bx, by, bw, bh, design::kOverlayButtonRadius, fi, 1.0f));
        const std::string value =
            i < static_cast<int>(values_.size()) ? values_[i] : std::string();
        const std::string line = value.empty() ? options_[i] : options_[i] + ": " + value;
        const float base = by + (bh - c.text.lineHeight(st, sc)) * 0.5f + c.text.ascent(st, sc);
        const std::string t = c.text.truncate(line, st, sc, bw - 112.0f);
        const float lw = c.text.measure(t, st, sc);
        c.text.draw(c.r, t, bx + (bw - lw) * 0.5f, base, st,
                    design::menuLabel(fi, isDisabled(i) ? 0.35f : 1.0f), sc);
        if (i == stepper_ && on) {
            const char* back = "\xE2\x80\xB9";
            const char* fwd = "\xE2\x80\xBA";
            const float aw = c.text.measure(fwd, st, sc);
            c.text.draw(c.r, back, bx + 28.0f, base, st,
                        ui::Color::white((canLeft_ ? 0.70f : 0.18f) * fi), sc);
            c.text.draw(c.r, fwd, bx + bw - 28.0f - aw, base, st,
                        ui::Color::white((canRight_ ? 0.70f : 0.18f) * fi), sc);
        }
    }
    c.r.setContentAlpha(1.0f);
}

void ChoiceScreen::draw(Ctx& c) {
    if (!visible()) return;
    using ui::TextStyle;
    if (overMenu_) {
        // It fades in over the menu's panel, and fades with the menu itself.
        drawMenuRows(c, levelShown() * menuFade_);
        return;
    }
    const float a = appear_.value();
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight, sc = c.sc;

    // The detail may be more than one line, split at '\n' (Sign out's two).
    std::vector<std::string> lines;
    for (size_t at = 0; !detail_.empty() && at <= detail_.size();) {
        size_t nl = detail_.find('\n', at);
        if (nl == std::string::npos) nl = detail_.size();
        lines.push_back(detail_.substr(at, nl - at));
        at = nl + 1;
    }
    // A LIST GROWS TO ITS LONGEST LINE, name and value, up to the panel's
    // limit, so "Universal Blue ... Image tooling" is not cut to "Unive...".
    // A question keeps its fixed buttons. Found on Credits and licences,
    // 2026-09-25; long Wi-Fi names get the same room.
    const bool list = !values_.empty();
    float rowW = kButtonW;
    if (list) {
        for (size_t i = 0; i < options_.size(); ++i) {
            const float vw = i < values_.size() && !values_[i].empty()
                ? c.text.measure(values_[i], TextStyle::Callout, sc) + 24.0f : 0.0f;
            rowW = std::max(rowW, c.text.measure(options_[i], TextStyle::Title3, sc) + vw + 48.0f);
        }
        rowW = std::min(rowW, kPanelMaxW - kPanelPad * 2);
    }
    float innerW = rowW;
    innerW = std::max(innerW, c.text.measure(title_, TextStyle::Title2, sc));
    for (const std::string& l : lines)
        innerW = std::max(innerW, c.text.measure(l, TextStyle::Callout, sc));
    if (fixedW_ > 0.0f) rowW = innerW = std::min(fixedW_, kPanelMaxW - kPanelPad * 2);
    const float panelW = std::min(kPanelMaxW, innerW + kPanelPad * 2);
    const float textMax = panelW - kPanelPad * 2;
    const float titleH = c.text.lineHeight(TextStyle::Title2, sc);
    const float lineH = c.text.lineHeight(TextStyle::Callout, sc);
    const float detailH = lines.empty() ? 0.0f : 8.0f + lineH * lines.size();
    const int n = static_cast<int>(options_.size());
    const int shown = grows_ ? std::max(1, std::min(n, kMaxVisible)) : std::min(n, kMaxVisible);
    const bool prompt = n == 0 && !promptButton_.empty();
    float listH = prompt || busy_ ? kButtonH : shown * kButtonH + std::max(0, shown - 1) * kButtonGap;
    if (grows_) {
        if (!listHSet_) {
            listH_.settle(listH);
            listHSet_ = true;
        } else {
            listH_.retarget(listH, kGrow);
        }
        listH = listH_.value();
    }
    const float panelH = kPanelPad + titleH + detailH + 36.0f + listH + kPanelPad;
    const float px = (W - panelW) * 0.5f, py = (H - panelH) * 0.5f;

    c.r.setContentAlpha(1.0f);
    c.r.draw(ui::Rect{0, 0, W, H, 0, ui::Color::black(0.55f * a)});
    c.r.setContentAlpha(a);
    // THE PAUSE MENU'S PANEL (design::menuPanel). The keyboard's black glass
    // was tried first (*"forgotten about"*), then the account panel's frosted
    // glass (*"too frosted or washed out"*). MMagTech, 2026-09-24.
    c.r.draw(design::menuPanel(px, py, panelW, panelH, 1.0f));

    auto centred = [&](const std::string& s, float base, TextStyle st, float alpha) {
        const std::string t = c.text.truncate(s, st, sc, textMax);
        const float w = c.text.measure(t, st, sc);
        c.text.draw(c.r, t, (W - w) * 0.5f, base, st, ui::Color::white(alpha), sc);
    };
    float y = py + kPanelPad;
    centred(title_, y + c.text.ascent(TextStyle::Title2, sc), TextStyle::Title2, 1.0f);
    y += titleH;
    for (size_t i = 0; i < lines.size(); ++i)
        centred(lines[i], y + 8.0f + lineH * i + c.text.ascent(TextStyle::Callout, sc),
                TextStyle::Callout, 0.60f);
    y += detailH;
    y += 36.0f;

    if (prompt) {
        // "Press (B) to cancel": the words, and the button as a white disc
        // with its dark letter on the text's x-height, as Home's empty state
        // draws "Press (A) to open the Library" (screenparts.cpp, drawPressPrompt).
        const TextStyle st = TextStyle::Title3;
        const float gap = 16.0f;
        const float badge = c.text.lineHeight(st, sc) * 0.95f;
        const float w1 = c.text.measure(promptBefore_, st, sc);
        const float w2 = c.text.measure(promptAfter_, st, sc);
        const float base = y + kButtonH * 0.5f + c.text.ascent(st, sc) * 0.40f;
        float x = (W - (w1 + gap + badge + gap + w2)) * 0.5f;
        c.text.draw(c.r, promptBefore_, x, base, st, ui::Color::white(0.92f), sc);
        x += w1 + gap;
        const float capMid = base - c.text.ascent(st, sc) * 0.36f;
        c.r.draw(ui::Rect{x, capMid - badge * 0.5f, badge, badge, badge * 0.5f,
                          ui::Color::white(0.95f)});
        const float bw = c.text.measure(promptButton_, st, sc);
        c.text.draw(c.r, promptButton_, x + (badge - bw) * 0.5f,
                    capMid + c.text.ascent(st, sc) * 0.36f, st,
                    ui::Color{0.07f, 0.05f, 0.12f, 1.0f}, sc);
        x += badge + gap;
        c.text.draw(c.r, promptAfter_, x, base, st, ui::Color::white(0.92f), sc);
    }
    if (busy_) {
        // EIGHT DOTS IN A RING, the bright one going round once a second and
        // the others fading behind it: moving, so the console is plainly not
        // stuck, with no percentage it could not honestly give.
        constexpr int kDots = 8;
        const float cx = W * 0.5f, cy = y + kButtonH * 0.5f;
        const float ring = kButtonH * 0.32f, dot = kButtonH * 0.11f;
        const float lead = spin_ * kDots;
        for (int i = 0; i < kDots; ++i) {
            const float ang = 6.2831853f * i / kDots - 1.5707963f;
            float behind = std::fmod(lead - i, static_cast<float>(kDots));
            if (behind < 0.0f) behind += kDots;
            const float alpha = 0.18f + 0.77f * (1.0f - behind / kDots);
            const float dx = cx + ring * std::cos(ang) - dot * 0.5f;
            const float dy = cy + ring * std::sin(ang) - dot * 0.5f;
            c.r.draw(ui::Rect{dx, dy, dot, dot, dot * 0.5f, ui::Color::white(alpha)});
        }
    }
    if (n == 0 && !placeholder_.empty())
        centred(placeholder_, y + listH * 0.5f + c.text.ascent(TextStyle::Callout, sc) * 0.4f,
                TextStyle::Callout, 0.45f);
    const float f = focus_.value();
    const float bx = (W - rowW) * 0.5f;
    // A row still arriving shows only as far as the panel has grown.
    if (grows_) c.r.setScissor(0, y - 8.0f, W, listH + 16.0f);
    for (int i = top_; i < std::min(n, top_ + fit()); ++i) {
        const bool on = (i == slot_);
        const float s = on ? 1.0f + (kFocusScale - 1.0f) * f : 1.0f;
        const float dw = rowW * s, dh = kButtonH * s;
        const float dx = bx - (dw - rowW) * 0.5f, dy = y - (dh - kButtonH) * 0.5f;
        const float bf = on ? f : 0.0f;
        c.r.draw(design::menuButton(dx, dy, dw, dh, kButtonRadius * s, bf, 1.0f));
        const float base = dy + dh * 0.5f + c.text.ascent(TextStyle::Title3, sc) * 0.40f;
        // A LIST (answers with values) reads left to right, "name ... state",
        // like a Settings row; a plain question keeps its answers centred.
        const std::string value =
            (list && i < static_cast<int>(values_.size())) ? values_[i] : std::string();
        const float vw = value.empty() ? 0.0f : c.text.measure(value, TextStyle::Callout, sc);
        const float room = rowW - 48.0f - (value.empty() ? 0.0f : vw + 24.0f);
        const std::string label = c.text.truncate(options_[i], TextStyle::Title3, sc, room);
        const float lw = c.text.measure(label, TextStyle::Title3, sc);
        const float lx = list ? dx + 24.0f : dx + (dw - lw) * 0.5f;
        const float la = isDisabled(i) ? 0.35f : 1.0f;
        c.text.draw(c.r, label, lx, base, TextStyle::Title3, design::menuLabel(bf, la), sc);
        if (!value.empty()) {
            float vx = dx + dw - 24.0f - vw;
            // THE STEPPER'S ARROWS, as a Settings choice row draws them: only
            // under focus, dim at an end (settings.cpp).
            if (i == stepper_ && on) {
                const char* back = "\xE2\x80\xB9";
                const char* fwd = "\xE2\x80\xBA";
                const float aw = c.text.measure(fwd, TextStyle::Callout, sc);
                vx -= aw + 14.0f;
                c.text.draw(c.r, fwd, dx + dw - 24.0f - aw, base, TextStyle::Callout,
                            ui::Color::white((canRight_ ? 0.70f : 0.18f) * f), sc);
                const float bw = c.text.measure(back, TextStyle::Callout, sc);
                c.text.draw(c.r, back, vx - 14.0f - bw, base, TextStyle::Callout,
                            ui::Color::white((canLeft_ ? 0.70f : 0.18f) * f), sc);
            }
            c.text.draw(c.r, value, vx, base, TextStyle::Callout,
                        ui::Color::white(0.60f * la), sc);
        }
        y += kButtonH + kButtonGap;
    }
    if (grows_) c.r.clearScissor();
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
