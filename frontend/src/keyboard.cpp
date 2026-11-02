#include "keyboard.h"

#include <algorithm>
#include <cmath>

#include "sound.h"
#include "text.h"
#include "ui.h"

namespace ui {
namespace {

// Design-system values, from docs/PROJECT.md.
constexpr float kKeyUnit = 96.0f;     // one key's width and height
constexpr float kKeyGap = 12.0f;
constexpr float kKeyRadius = 16.0f;   // the settings-row radius; a key is that size
constexpr float kPanelRadius = 32.0f; // the pause-panel radius; this is a panel
constexpr float kPanelPad = 40.0f;

// The focus scale for a small element. The design system's rule is that the
// scale shrinks as the element grows — 1.10 for a cover, 1.06 for a pill, 1.03
// for a full-width row — and a key is pill-sized.
constexpr float kFocusScale = 1.06f;

Keyboard::Key k(const char* label, const char* insert) {
    Keyboard::Key key;
    key.label = label;
    key.insert = insert;
    return key;
}
Keyboard::Key action(const char* label, Keyboard::Key::Action a, float width = 1.0f) {
    Keyboard::Key key;
    key.label = label;
    key.action = a;
    key.width = width;
    return key;
}

}  // namespace

void Keyboard::open(const Config& config) {
    config_ = config;
    busy_ = false;
    fieldMsg_.clear();
    held_.clear();
    fieldProblem_ = false;
    value_ = config.initial;
    conceal_ = config.conceal;
    shifted_ = false;
    row_ = 1;  // the letter row, not the digits: that is where typing starts
    col_ = 0;
    open_ = true;
    ghost_ = false;

    // A FIXED 12-COLUMN GRID, every row totalling exactly 12 units.
    //
    // The first version sized the panel to its widest row — a long function
    // row — and left the letter rows short, so a third of the panel was empty
    // to the right of the letters. It looked unfinished, and it wasted the
    // one thing a ten-foot keyboard is short of: reach.
    //
    // The layout follows what PlayStation, Xbox and Steam all converge on,
    // which is a REAL KEYBOARD's own geography rather than an invented one:
    //
    //   * digits across the top, with BACKSPACE at their right end, where the
    //     backspace key has always been;
    //   * SHIFT at the bottom-left of the letter block, where it has always
    //     been;
    //   * SPACE spanning the bottom, with the commit key at its right.
    //
    // None of that is decoration. Somebody looking for backspace looks
    // top-right before they read anything, and a layout that rewards the
    // guess is faster than one that has to be read.
    //
    // Digits get their own row rather than living behind a shift layer,
    // because a Wi-Fi passphrase is usually being read off the underside of a
    // router and is mostly digits and symbols. Burying them costs more than
    // the row does.
    constexpr float kCols = 12.0f;

    auto build = [&](bool upper) {
        std::vector<std::vector<Key>> rows;
        auto wide = [](Key key, float w) {
            key.width = w;
            return key;
        };

        std::vector<Key> digits;
        for (const char* p = "1234567890"; *p; ++p)
            digits.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        digits.push_back(wide(action("del", Key::Backspace), 2.0f));   // 10 + 2 = 12
        rows.push_back(digits);

        const char* r1 = upper ? "QWERTYUIOP" : "qwertyuiop";
        const char* r2 = upper ? "ASDFGHJKL" : "asdfghjkl";
        const char* r3 = upper ? "ZXCVBNM" : "zxcvbnm";

        std::vector<Key> a;
        for (const char* p = r1; *p; ++p)
            a.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        a.push_back(k("/", "/"));
        a.push_back(k(":", ":"));                                       // 10 + 2 = 12
        rows.push_back(a);

        std::vector<Key> b;
        for (const char* p = r2; *p; ++p)
            b.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        b.push_back(k(".", "."));
        b.push_back(k("-", "-"));
        b.push_back(k("_", "_"));                                       // 9 + 3 = 12
        rows.push_back(b);

        std::vector<Key> c;
        c.push_back(wide(action(upper ? "abc" : "ABC", Key::Shift), 2.0f));
        for (const char* p = r3; *p; ++p)
            c.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        c.push_back(k("@", "@"));
        c.push_back(wide(action(conceal_ ? "show" : "hide", Key::Conceal), 2.0f));
        rows.push_back(c);                                              // 2 + 7 + 1 + 2 = 12

        // The bottom row absorbs whatever is left, so it fits the grid
        // whatever the field asked for. A password field passes no shortcuts
        // and simply gets a longer space bar.
        std::vector<Key> fn;
        float used = 0;
        for (const auto& sc : config_.shortcuts) {
            fn.push_back(wide(k(sc.c_str(), sc.c_str()), 2.5f));
            used += 2.5f;
        }
        const float doneW = 3.0f;
        const float spaceW = std::max(2.0f, kCols - used - doneW);
        fn.push_back(wide(action("space", Key::Space), spaceW));
        fn.push_back(wide(action("done", Key::Done), doneW));
        rows.push_back(fn);
        return rows;
    };
    lower_ = build(false);
    upper_ = build(true);
}

const std::vector<std::vector<Keyboard::Key>>& Keyboard::layout() const {
    return shifted_ ? upper_ : lower_;
}

bool Keyboard::focusAt(float canvasX, float canvasY) {
    if (!open_) return false;
    for (const KeyRect& k : keyRects_) {
        if (canvasX < k.x || canvasX > k.x + k.w) continue;
        if (canvasY < k.y || canvasY > k.y + k.h) continue;
        row_ = k.row;
        col_ = k.col;
        clampFocus();
        return true;
    }
    return false;
}

KeyboardResult Keyboard::pressAt(float canvasX, float canvasY, bool* hit) {
    if (!focusAt(canvasX, canvasY)) {
        if (hit) *hit = false;
        return KeyboardResult::Typing;
    }
    if (hit) *hit = true;
    // The same call the controller's A button makes, on the same focused key.
    // A click is "point at it and press A" and never a second path in.
    return pressKey();
}

void Keyboard::clampFocus() {
    const auto& rows = layout();
    row_ = std::clamp(row_, 0, static_cast<int>(rows.size()) - 1);
    col_ = std::clamp(col_, 0, static_cast<int>(rows[row_].size()) - 1);
}

// THE KEYBOARD SOUNDS LIKE THE REST OF THE CONSOLE (#389): a move is Move, a
// move into the edge is Edge, and a key typed, Shift or a letter taken off is
// the short Type tick. Finishing or leaving it (Done, Cancel, Start, B) is the caller's to
// sound, because what that means depends on what the typing was for. A Done
// that finishes nothing (Config::required) is the Edge bump, here.
void Keyboard::moveFocus(int dx, int dy) {
    if (!open_) return;
    const int wasRow = row_, wasCol = col_;
    const auto& rows = layout();
    if (dy != 0) {
        // Keep the horizontal POSITION across a row change, not the index.
        // Rows have different key counts and different widths, so moving down
        // from "p" should land near "l", not on whatever happens to be ninth.
        float centre = 0;
        for (int i = 0; i < col_; ++i) centre += rows[row_][i].width;
        centre += rows[row_][col_].width * 0.5f;

        row_ = std::clamp(row_ + dy, 0, static_cast<int>(rows.size()) - 1);

        float best = 1e9f;
        int bestCol = 0, i = 0;
        float x = 0;
        for (const auto& key : rows[row_]) {
            const float mid = x + key.width * 0.5f;
            if (std::fabs(mid - centre) < best) {
                best = std::fabs(mid - centre);
                bestCol = i;
            }
            x += key.width;
            ++i;
        }
        col_ = bestCol;
    }
    // HORIZONTAL MOVEMENT WRAPS. This reverses an earlier decision here, and
    // the earlier one was wrong.
    //
    // The argument against wrapping was that it reads as a glitch when you are
    // holding a direction. That holds for a shelf, where the next item is a
    // different game and landing somewhere unexpected loses your place. It does
    // not hold for a keyboard, where the grid is twelve columns wide and every
    // key is equally "where you meant to be": without wrapping, getting from
    // "1" to "del" is eleven presses when it should be one. PlayStation and
    // Xbox both wrap their keyboards for exactly this reason.
    //
    // Vertical movement still clamps. Five rows is short enough to cross
    // directly, and wrapping from the space bar up to the digits skips the
    // letters — which is where somebody pressing up is almost always going.
    if (dx != 0) {
        const int n = static_cast<int>(rows[row_].size());
        col_ = (col_ + dx % n + n) % n;
    }
    clampFocus();
    sound::play(row_ == wasRow && col_ == wasCol ? sound::Cue::Edge : sound::Cue::Move);
}

void Keyboard::restoreHeld() {
    if (held_.empty()) return;
    value_ = held_;
    held_.clear();
}

KeyboardResult Keyboard::pressKey() {
    if (!open_) return KeyboardResult::Cancelled;
    if (busy_) return KeyboardResult::Typing;
    clampFocus();
    const Key& key = layout()[row_][col_];
    switch (key.action) {
        case Key::Backspace: backspace(); return KeyboardResult::Typing;
        case Key::Shift: toggleShift(); return KeyboardResult::Typing;
        case Key::Space:
            sound::play(sound::Cue::Type);
            restoreHeld();
            value_ += ' ';
            lastTyped_ = std::chrono::steady_clock::now();
            revealLast_ = true;
            return KeyboardResult::Typing;
        case Key::Conceal:
            sound::play(sound::Cue::Type);
            toggleConceal();
            return KeyboardResult::Typing;
        case Key::Done: return commit();
        case Key::None: break;
    }
    sound::play(sound::Cue::Type);
    restoreHeld();
    value_ += key.insert;
    lastTyped_ = std::chrono::steady_clock::now();
    revealLast_ = true;
    // Shift is one-shot, the way a phone keyboard behaves: capitalise a letter
    // and fall back to lowercase, because the next character almost never wants
    // the same case.
    if (shifted_) {
        shifted_ = false;
        clampFocus();
    }
    return KeyboardResult::Typing;
}

void Keyboard::backspace() {
    if (busy_) return;
    revealLast_ = false;
    restoreHeld();
    if (value_.empty()) { sound::play(sound::Cue::Edge); return; }
    sound::play(sound::Cue::Type);
    // Step back over a whole UTF-8 code point, not a byte. Deleting half of a
    // multi-byte character leaves an invalid string that will not render.
    size_t n = value_.size() - 1;
    while (n > 0 && (static_cast<unsigned char>(value_[n]) & 0xC0) == 0x80) --n;
    value_.erase(n);
}

void Keyboard::toggleShift() {
    if (busy_) return;
    sound::play(sound::Cue::Type);
    shifted_ = !shifted_;
    clampFocus();
}

void Keyboard::toggleConceal() {
    restoreHeld();
    conceal_ = !conceal_;
    // The labels live in the built layouts, so rebuild them to say the other
    // thing. Cheap, and it keeps one source of truth for the layout.
    const std::string keep = value_;
    const int r = row_, c = col_;
    Config cfg = config_;
    cfg.initial = keep;
    cfg.conceal = conceal_;
    open(cfg);
    row_ = r;
    col_ = c;
    clampFocus();
}

void Keyboard::typeText(const char* utf8) {
    if (!open_ || !utf8 || busy_) return;
    restoreHeld();
    value_ += utf8;
    lastTyped_ = std::chrono::steady_clock::now();
    revealLast_ = true;
}

void Keyboard::sayInField(const std::string& msg, bool problem, bool keep) {
    held_ = keep ? (value_.empty() ? held_ : value_) : std::string();
    value_.clear();
    revealLast_ = false;
    fieldMsg_ = msg;
    fieldProblem_ = problem;
    if (problem) shakeAt_ = std::chrono::steady_clock::now();
}

KeyboardResult Keyboard::commit() {
    if (busy_) return KeyboardResult::Typing;
    if (config_.required && value_.empty() && held_.empty()) {
        sound::play(sound::Cue::Edge);
        return KeyboardResult::Typing;
    }
    restoreHeld();
    open_ = false;
    return KeyboardResult::Committed;
}

KeyboardResult Keyboard::cancel() {
    busy_ = false;
    open_ = false;
    return KeyboardResult::Cancelled;
}

void Keyboard::draw(Renderer& r, TextRenderer& text, float scale) {
    if (!open_ && !ghost_) return;
    const auto& rows = layout();

    // A DOCKED KEYBOARD IS SMALLER, and it has to be: at full size this panel
    // is 788 points of a 1080 canvas, which leaves 94 under the top bar for the
    // results it exists to filter. Two thirds of the key size gives it 550 and
    // leaves room for a row of covers, which is the whole point of docking it.
    //
    // The keys stay well above a comfortable target size on a television — 64
    // design points is 128 real pixels at 4K — and every position below is
    // computed from these three, including the rectangles the pointer path
    // hit-tests against. A second set of numbers that only the drawing knew
    // about is how a mouse ends up pressing the key next to the one it is over.
    const bool dock = config_.dockedBottom;
    const float unit = dock ? 64.0f : kKeyUnit;
    const float gap = dock ? 9.0f : kKeyGap;
    const float pad = dock ? 28.0f : kPanelPad;

    // Widest row decides the panel, so the panel does not jump when the layout
    // changes between shift states.
    float widest = 0;
    for (const auto& row : rows) {
        float w = -gap;
        for (const auto& key : row) w += key.width * unit + gap;
        widest = std::max(widest, w);
    }

    const float fieldH = dock ? 76.0f : 96.0f;
    // A DOCKED PANEL DROPS ITS TITLE AND HINT. See Config::dockedBottom: the
    // field is the title when the thing it filters is on the screen above it,
    // and those two lines are ninety points the results want.
    const float titleH = config_.dockedBottom ? 0.0f : 70.0f;
    const float hintH = (config_.dockedBottom || config_.hint.empty()) ? 0.0f : 34.0f;
    const float gridH = rows.size() * unit + (rows.size() - 1) * gap;
    const float panelW = widest + pad * 2;
    const float panelH = titleH + hintH + fieldH + (dock ? 20.0f : 28.0f) + gridH +
                         pad * 2 + (dock ? 44.0f : 56.0f);
    const float panelX = (kCanvasWidth - panelW) * 0.5f;
    // Bottom-anchored when docked, with the safe inset under it — this is text
    // and controls a person has to reach, so it obeys the same rule the top bar
    // does about the edges of a television.
    const float restY = config_.dockedBottom
                            ? kCanvasHeight - panelH - kSafeInset * 0.5f
                            : (kCanvasHeight - panelH) * 0.5f;
    // Where the results stop is where the panel's top edge IS this frame,
    // slide included: Search's covers grow and shrink with the slide instead
    // of jumping to their resting size while the panel is still moving
    // (MMagTech on the TV, 2026-10-19). Until then it was where the panel
    // rests, so the results would not follow a slide; they should.
    const float panelY = restY + slide_ * (kCanvasHeight - restY + 24.0f);
    panelTop_ = panelY;

    // Scrim, then ONE piece of frosted glass.
    //
    // Glass does not nest. Every glass surface samples the same captured
    // scene, so a key drawn as its own glass re-samples the bright cover art
    // and ignores the darkening of the panel it is sitting on — which looked
    // like stained glass and was unreadable. The panel is the glass; the keys
    // are ordinary surfaces on top of it, which is also how the reference
    // implementation layers its own materials.
    //
    // And the tint is DARK. A white tint over a blur lightens, and this is a
    // dark interface: what makes a material read as a material here is that it
    // dims what is behind it as well as softening it.
    // NO SCRIM WHEN DOCKED. The screen behind it is not something to be got out
    // of the way, it is the answer to what is being typed.
    if (!config_.dockedBottom)
        r.draw(Rect{0, 0, kCanvasWidth, kCanvasHeight, 0, Color::black(0.45f)});
    // THE DOCKED PANEL IS PLAIN BLACK AND SEE-THROUGH — settled 2026-09-21
    // after three wrong answers, each one recorded because the next person will
    // otherwise try them again in the same order:
    //
    //   BLACK AT 68%, BLURRED. A slab borrowed from nowhere — *"the search
    //   background just seems off against the black keyboard."*
    //   THE CONSOLE'S SURFACE PURPLE, on the pause menu's precedent. Worse —
    //   *"no i dont think purple is working out here for the keyboard."*
    //   NO PANEL, just a gradient and a lit outline. *"no thats looks horrible
    //   like it needs a whole background."*
    //
    // What it wanted all along: *"maybe just black but transparent."* Not
    // glass — glass blurs what is behind it and then tints it, which is why
    // 68% read as opaque. This is flat black at 62% with the screen showing
    // straight through it, so the artwork behind stays legible AS artwork and
    // the keys have something solid to sit on.
    //
    // No border and no edge light. A panel you can see through does not need an
    // outline to say it is there, and the one that was tried is the thing that
    // looked horrible.
    if (dock) {
        r.draw(Rect{panelX, panelY, panelW, panelH, kPanelRadius, Color::black(0.62f)});
    } else {
        r.drawGlass(Rect{panelX, panelY, panelW, panelH, kPanelRadius, Color::white(0)},
                    6.0f, Color::black(0.68f));
    }

    float y = panelY + pad;
    if (!config_.dockedBottom)
        text.draw(r, config_.title, panelX + pad,
                  y + text.ascent(TextStyle::Title2, scale), TextStyle::Title2,
                  Color::white(1.0f), scale);
    if (!config_.hint.empty() && !config_.dockedBottom) {
        // Set by every caller and drawn by none until now. A field whose title
        // is "Connect to RomM" genuinely needs the line that says WHICH
        // address, and it was being silently dropped.
        text.draw(r, config_.hint, panelX + pad,
                  y + titleH + text.ascent(TextStyle::Callout, scale) - 10.0f,
                  TextStyle::Callout, Color::white(0.55f), scale);
        y += 34.0f;
    }
    y += titleH;

    // The field.
    r.draw(Rect{panelX + pad, y, widest, fieldH, kKeyRadius, Color::black(0.45f)});
    std::string shown = value_;
    if (conceal_) {
        // A dot per character, not per byte, and the last one readable for
        // a moment after it was typed.
        constexpr auto kReveal = std::chrono::milliseconds(1500);
        const bool reveal = revealLast_ &&
                            std::chrono::steady_clock::now() - lastTyped_ < kReveal;
        std::vector<std::string> chars;
        for (size_t i = 0; i < value_.size();) {
            size_t n = 1;
            while (i + n < value_.size() &&
                   (static_cast<unsigned char>(value_[i + n]) & 0xC0) == 0x80)
                ++n;
            chars.push_back(value_.substr(i, n));
            i += n;
        }
        shown.clear();
        for (size_t i = 0; i < chars.size(); ++i)
            shown += (reveal && i + 1 == chars.size()) ? chars[i] : "\xE2\x80\xA2";
    }
    const bool empty = shown.empty();
    // A message in the field (sayInField) stands in for the placeholder,
    // and so goes the moment something is typed.
    const bool message = empty && !fieldMsg_.empty();
    if (empty) shown = message ? fieldMsg_ : config_.placeholder;
    // ONE SHORT SHAKE for a problem, the PIN dots' own: a decaying wobble.
    float shakeX = 0.0f;
    if (message && fieldProblem_) {
        const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                                     shakeAt_).count();
        constexpr float kShake = 0.40f;
        if (t < kShake) shakeX = std::sin(t * 50.0f) * 18.0f * (1.0f - t / kShake);
    }
    const float fieldBaseline = y + fieldH * 0.5f + text.ascent(TextStyle::Title3, scale) * 0.5f;
    // Show the tail when it overflows: what someone is typing is at the end,
    // and a field that scrolls off the right hides exactly the character they
    // just pressed.
    const float room = widest - 56.0f;
    while (text.measure(shown, TextStyle::Title3, scale) > room && shown.size() > 1) {
        size_t n = 1;
        while (n < shown.size() && (static_cast<unsigned char>(shown[n]) & 0xC0) == 0x80) ++n;
        shown.erase(0, n);
    }
    // WHITE IS WHAT YOU TYPED, GREY IS EVERYTHING ELSE, as in any operating
    // system's text field. A problem ("Wrong password") is as grey as
    // "Joining…" and says it is a problem by its shake; at 0.95 it read as
    // typed text.
    text.draw(r, shown, panelX + pad + 20.0f + shakeX, fieldBaseline, TextStyle::Title3,
              message ? Color::white(0.60f)
                      : (empty ? Color::white(0.35f) : Color::white(1.0f)),
              scale);
    if (!empty) {
        // A caret at the end, so the field reads as active rather than as a
        // label that happens to contain text.
        const float caretX =
            panelX + pad + 20.0f + text.measure(shown, TextStyle::Title3, scale) + 4.0f;
        r.draw(Rect{caretX, y + 22.0f, 3.0f, fieldH - 44.0f, 1.5f, Color::white(0.75f)});
    }
    y += fieldH + (dock ? 20.0f : 28.0f);

    // The keys.
    //
    // Their resting rectangles are recorded as they are drawn, so a pointer can
    // be asked which key it is over. The RESTING one, not the focused one: a
    // focused key is drawn slightly larger, and hit-testing the grown shape
    // would make the key under the pointer subtly harder to leave than to
    // enter.
    keyRects_.clear();
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        float x = panelX + pad;
        for (size_t ci = 0; ci < rows[ri].size(); ++ci) {
            const Key& key = rows[ri][ci];
            const float kw = key.width * unit;
            keyRects_.push_back(KeyRect{x, y, kw, unit, static_cast<int>(ri),
                                        static_cast<int>(ci)});
            const bool focused = (static_cast<int>(ri) == row_ && static_cast<int>(ci) == col_);
            const float s = focused ? kFocusScale : 1.0f;
            const float dw = kw * s, dh = unit * s;
            const float dx = x - (dw - kw) * 0.5f;
            const float dy = y - (dh - unit) * 0.5f;

            Rect cap{dx, dy, dw, dh, kKeyRadius * s,
                     focused ? Color::white(0.30f) : Color::white(0.10f)};
            if (focused) {
                cap.shadowBlur = 18.0f;
                cap.shadowOffsetY = 8.0f;
                cap.shadowColor = Color::black(0.45f);
            }
            r.draw(cap);

            const TextStyle st = key.action == Key::None ? TextStyle::Title3 : TextStyle::Callout;
            const float lw = text.measure(key.label, st, scale);
            text.draw(r, key.label, dx + (dw - lw) * 0.5f,
                      dy + dh * 0.5f + text.ascent(st, scale) * 0.5f, st,
                      focused ? Color::white(1.0f) : Color::white(0.75f), scale);
            x += kw + gap;
        }
        y += unit + gap;
    }

    // What the buttons do. A controller-only UI has to say, because there is
    // no convention to fall back on and no pointer to explore with.
    // Callout, not Caption. 25pt is inside Apple's ten-foot ramp but it is the
    // size for something glanceable, and this is a line someone has to actually
    // READ to know what the buttons do. Anything a person must read sits at
    // Callout or above.
    const char* legend = "A select     X delete     Y shift     Start done     B back";
    const float lw = text.measure(legend, TextStyle::Callout, scale);
    text.draw(r, legend, panelX + (panelW - lw) * 0.5f,
              panelY + panelH - pad + 12.0f, TextStyle::Callout, Color::white(0.55f),
              scale);
}

}  // namespace ui
