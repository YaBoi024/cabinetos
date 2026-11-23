#include "pin.h"

#include "panels.h"
#include "sound.h"

#include <algorithm>
#include <cmath>

namespace screens {

namespace {

constexpr int kLength = 4;

// The pad, as a phone draws it. The bottom row's two action keys are known by
// their position (isCancel, isDelete) rather than by their label, so a change
// of wording cannot change what they do.
constexpr const char* kKeys[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"Cancel", "0", "Delete"},
};
bool isCancel(int r, int c) { return r == 3 && c == 0; }
bool isDelete(int r, int c) { return r == 3 && c == 2; }

// Layout, in canvas points. Starting values, judged on the television.
//
// A PANEL IN THE MIDDLE, NOT A SCREEN. The first version covered the whole
// screen in purple; MMagTech on the TV, 2026-09-24: *"do we really need a full
// screen ui for a pin pad"*. A scrim and the pause menu's panel and buttons
// (design::menuPanel), shared with the question panel.
constexpr float kPanelPad = 40.0f;       // the keyboard's
constexpr float kPanelMaxW = 1200.0f;
constexpr float kDotSize = 26.0f;
constexpr float kDotSpacing = 56.0f;
constexpr float kKeyW = 150.0f;
constexpr float kKeyH = 88.0f;
constexpr float kKeyGap = 12.0f;         // the keyboard's
constexpr float kKeyRadius = 16.0f;      // the keyboard's
constexpr float kKeyFocusScale = 1.06f;  // the keyboard's

constexpr float kAppear = 0.280f;        // Settings' own arrival
constexpr float kShake = 0.40f;

}  // namespace

void PinScreen::open(Mode mode, std::string title, std::string detail) {
    open_ = true;
    mode_ = mode;
    title_ = std::move(title);
    detail_ = std::move(detail);
    typed_.clear();
    first_.clear();
    entered_.clear();
    message_.clear();
    row_ = 0;
    col_ = 0;
    shake_ = 0.0f;
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, kAppear);
    focus_.settle(1.0f);
}

void PinScreen::close() {
    open_ = false;
    typed_.clear();
    first_.clear();
}

void PinScreen::reject(const std::string& why) {
    typed_.clear();
    entered_.clear();
    message_ = why;
    shake_ = kShake;
}

void PinScreen::lockFor(float seconds) {
    lockLeft_ = seconds;
    typed_.clear();
}

PinScreen::Outcome PinScreen::add(char digit) {
    if (locked()) { sound::play(sound::Cue::Edge); return Outcome::None; }
    if (static_cast<int>(typed_.size()) >= kLength) return Outcome::None;
    typed_ += digit;
    // The message was about the last attempt; a new digit is a new attempt.
    message_.clear();
    sound::play(sound::Cue::Move);
    if (static_cast<int>(typed_.size()) < kLength) return Outcome::None;

    // FOUR IN, AND IT GOES AT ONCE. No OK key: a PIN of a fixed length is
    // finished when it is long enough, which is how a phone does it.
    if (mode_ == Mode::Check) {
        entered_ = typed_;
        return Outcome::Entered;
    }
    if (first_.empty()) {
        // The title changes to "Enter it again"; see draw.
        first_ = typed_;
        typed_.clear();
        return Outcome::None;
    }
    if (typed_ != first_) {
        first_.clear();
        reject("Those didn't match");
        sound::play(sound::Cue::Edge);
        return Outcome::None;
    }
    entered_ = typed_;
    return Outcome::Entered;
}

PinScreen::Outcome PinScreen::deleteOne() {
    if (typed_.empty()) return Outcome::None;
    typed_.pop_back();
    sound::play(sound::Cue::Back);
    return Outcome::None;
}

void PinScreen::holdDelete() {
    if (!open_) return;
    deleteHold_.start();
    deleteOne();
}

PinScreen::Outcome PinScreen::typeDigit(char c) {
    if (!open_ || c < '0' || c > '9') return Outcome::None;
    return add(c);
}

PinScreen::Outcome PinScreen::key(Nav n) {
    if (!open_) return Outcome::None;
    auto move = [&](int dr, int dc) {
        const int r = row_ + dr, c = col_ + dc;
        // NO WRAPPING. Three columns is short enough to cross, and a pad
        // that wraps puts Delete one press from Cancel.
        if (r < 0 || r > 3 || c < 0 || c > 2) { sound::play(sound::Cue::Edge); return; }
        row_ = r;
        col_ = c;
        focus_.retarget(0.0f, 0.0f);
        focus_.elapsed = 0.0f;
        focus_.retarget(1.0f, design::kFocusDuration);
        sound::play(sound::Cue::Move);
    };
    switch (n) {
        case Nav::Up: move(-1, 0); return Outcome::None;
        case Nav::Down: move(1, 0); return Outcome::None;
        case Nav::Left: move(0, -1); return Outcome::None;
        case Nav::Right: move(0, 1); return Outcome::None;
        case Nav::Activate:
            if (isCancel(row_, col_)) {
                sound::play(sound::Cue::Back);
                return Outcome::Cancelled;
            }
            if (isDelete(row_, col_)) return deleteOne();
            return add(kKeys[row_][col_][0]);
        case Nav::Back:
            // B LEAVES, as it does everywhere else. It used to take back a
            // digit first, which only worked while a legend said so; the
            // legend went (MMagTech, 2026-09-24: people know A and B) and the
            // Delete key on the pad is how a digit comes back.
            sound::play(sound::Cue::Back);
            return Outcome::Cancelled;
    }
    return Outcome::None;
}

void PinScreen::tick(float dt) {
    if (!open_) deleteHold_.stop();
    if (deleteHold_.due(dt)) deleteOne();
    appear_.tick(dt);
    focus_.tick(dt);
    shake_ = std::max(0.0f, shake_ - dt);
    if (lockLeft_ > 0.0f) {
        lockLeft_ = std::max(0.0f, lockLeft_ - dt);
        if (lockLeft_ <= 0.0f) message_.clear();
    }
}

void PinScreen::draw(Ctx& c) {
    if (!open_) return;
    const float a = appear_.value();
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight;
    const float sc = c.sc;
    using ui::TextStyle;

    std::string title = title_;
    if (mode_ == Mode::Choose && !first_.empty()) title = "Enter it again";
    std::string msg = message_;
    if (locked()) {
        const int s = static_cast<int>(std::ceil(lockLeft_));
        msg = "Too many tries. Try again in " + std::to_string(s) +
              (s == 1 ? " second" : " seconds");
    }

    // ---- Measure, so the panel can be centred before anything is drawn ----
    const float padW = kKeyW * 3 + kKeyGap * 2;
    const float padH = kKeyH * 4 + kKeyGap * 3;
    float innerW = padW;
    innerW = std::max(innerW, c.text.measure(title, TextStyle::Title2, sc));
    if (!detail_.empty())
        innerW = std::max(innerW, c.text.measure(detail_, TextStyle::Callout, sc));
    const float panelW = std::min(kPanelMaxW, innerW + kPanelPad * 2);
    const float textMax = panelW - kPanelPad * 2;

    const float titleH = c.text.lineHeight(TextStyle::Title2, sc);
    const float lineH = c.text.lineHeight(TextStyle::Callout, sc);
    const float detailH = detail_.empty() ? 0.0f : 8.0f + lineH;
    const float dotsBlock = 32.0f + kDotSize;
    const float msgBlock = 16.0f + lineH;   // kept even when empty, so nothing jumps
    const float panelH = kPanelPad + titleH + detailH + dotsBlock + msgBlock + 16.0f + padH +
                         kPanelPad;
    const float px = (W - panelW) * 0.5f;
    const float py = (H - panelH) * 0.5f;

    // ---- The scrim and the glass --------------------------------------
    c.r.setContentAlpha(1.0f);
    c.r.draw(ui::Rect{0, 0, W, H, 0, ui::Color::black(0.55f * a)});
    c.r.setContentAlpha(a);
    // THE PAUSE MENU'S PANEL (design::menuPanel), shared with the question
    // panel so a PIN followed by a question reads as one thing.
    c.r.draw(design::menuPanel(px, py, panelW, panelH, 1.0f));

    auto centred = [&](const std::string& s, float base, TextStyle st, float alpha) {
        const std::string t = c.text.truncate(s, st, sc, textMax);
        const float w = c.text.measure(t, st, sc);
        c.text.draw(c.r, t, (W - w) * 0.5f, base, st, ui::Color::white(alpha), sc);
    };

    float y = py + kPanelPad;
    centred(title, y + c.text.ascent(TextStyle::Title2, sc), TextStyle::Title2, 1.0f);
    y += titleH;
    if (!detail_.empty()) {
        centred(detail_, y + 8.0f + c.text.ascent(TextStyle::Callout, sc), TextStyle::Callout,
                0.60f);
        y += detailH;
    }

    // The dots: filled for a digit typed, faint for one still to come. The
    // digits themselves are never drawn, because a PIN is typed with the
    // person it keeps out sitting on the same sofa.
    y += 32.0f;
    float shakeX = 0.0f;
    if (shake_ > 0.0f) {
        const float t = kShake - shake_;
        shakeX = std::sin(t * 50.0f) * 18.0f * (shake_ / kShake);
    }
    const float dotsW = kDotSpacing * (kLength - 1);
    for (int i = 0; i < kLength; ++i) {
        const float cx = (W - dotsW) * 0.5f + i * kDotSpacing + shakeX;
        const bool on = i < static_cast<int>(typed_.size());
        c.r.draw(ui::Rect{cx - kDotSize * 0.5f, y, kDotSize, kDotSize, kDotSize * 0.5f,
                          ui::Color::white(on ? 1.0f : 0.25f)});
    }
    y += kDotSize;

    if (!msg.empty())
        centred(msg, y + 16.0f + c.text.ascent(TextStyle::Callout, sc), TextStyle::Callout, 0.85f);
    y += msgBlock + 16.0f;

    // The pad: the keyboard's keys, brighter and a little larger under focus.
    const float f = focus_.value();
    const float padX = (W - padW) * 0.5f;
    for (int r = 0; r < 4; ++r) {
        for (int col = 0; col < 3; ++col) {
            const float x = padX + col * (kKeyW + kKeyGap);
            const float ky = y + r * (kKeyH + kKeyGap);
            const bool focused = (r == row_ && col == col_);
            const float s = focused ? 1.0f + (kKeyFocusScale - 1.0f) * f : 1.0f;
            const float dw = kKeyW * s, dh = kKeyH * s;
            const float dx = x - (dw - kKeyW) * 0.5f, dy = ky - (dh - kKeyH) * 0.5f;
            const float kf = focused ? f : 0.0f;
            c.r.draw(design::menuButton(dx, dy, dw, dh, kKeyRadius * s, kf, 1.0f));
            const bool action = isCancel(r, col) || isDelete(r, col);
            const TextStyle st = action ? TextStyle::Callout : TextStyle::Title3;
            const std::string label = kKeys[r][col];
            const float lw = c.text.measure(label, st, sc);
            c.text.draw(c.r, label, dx + (dw - lw) * 0.5f,
                        dy + dh * 0.5f + c.text.ascent(st, sc) * 0.5f, st,
                        design::menuLabel(kf, 1.0f), sc);
        }
    }
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
