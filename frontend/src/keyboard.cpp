#include "keyboard.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "design.h"
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

// UTF-8, a character at a time: a byte after the first of a character is
// 10xxxxxx. Every step of the cursor, a delete and a masked field's dots go
// by these, never by a byte.
bool continuationByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
size_t previousChar(const std::string& s, size_t at) {
    if (at == 0 || s.empty()) return 0;
    size_t i = std::min(at, s.size()) - 1;
    while (i > 0 && continuationByte(s[i])) --i;
    return i;
}
size_t nextChar(const std::string& s, size_t at) {
    if (at >= s.size()) return s.size();
    size_t i = at + 1;
    while (i < s.size() && continuationByte(s[i])) ++i;
    return i;
}

// A key whose character changes with Shift: the one that spends a single
// Shift press. A digit, a symbol or a shortcut types the same either way, so
// it leaves the press waiting for the letter it was meant for.
bool isLetter(const std::string& insert) {
    return insert.size() == 1 && std::isalpha(static_cast<unsigned char>(insert[0]));
}

// THE LOCKED SHIFT KEY'S LINE, under its label: the accent, the kept mark's
// cyan, drawn inside the key so the key and the panel keep their size.
constexpr float kLockLineHeight = 4.0f;
constexpr float kLockLineGap = 6.0f;   // below the label's baseline

// The cursor's blink, on then off, while it waits for a press.
constexpr float kCaretBlink = 1.0f;   // seconds

}  // namespace

void Keyboard::open(const Config& config) {
    config_ = config;
    busy_ = false;
    fieldMsg_.clear();
    held_.clear();
    fieldProblem_ = false;
    value_ = config.initial;
    cursor_ = value_.size();   // after what is prefilled, where typing goes on
    revealLast_ = false;
    conceal_ = config.conceal;
    shift_ = Shift::Off;
    row_ = 1;  // the letter row, not the digits: that is where typing starts
    col_ = 0;
    letGoOfHeld();
    open_ = true;
    ghost_ = false;
    buildLayouts();
}

void Keyboard::buildLayouts() {
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

        // THE SHIFT KEY SAYS WHICH OF ITS THREE IT IS IN (#668), in the
        // letters a phone's old keypad used for the same three: lower case,
        // one capital, all capitals. Locked also gets a line under it (draw).
        // It said the case a press would give, "ABC" over lower-case keys,
        // which stopped being one answer once a press can give two.
        const char* shiftLabel = shift_ == Shift::Off    ? "abc"
                                 : shift_ == Shift::Once ? "Abc"
                                                         : "ABC";
        std::vector<Key> c;
        c.push_back(wide(action(shiftLabel, Key::Shift), 2.0f));
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
    return shift_ == Shift::Off ? lower_ : upper_;
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
    row_ = std::clamp(row_, kField, static_cast<int>(rows.size()) - 1);
    // In the field, the digit focus came up from, for Down to go back to.
    const auto& row = rows[static_cast<size_t>(std::max(row_, 0))];
    col_ = std::clamp(col_, 0, static_cast<int>(row.size()) - 1);
}

// THE KEYBOARD SOUNDS LIKE THE REST OF THE CONSOLE (#389): a move is Move, a
// move into the edge is Edge, and a key typed, Shift or a letter taken off is
// the short Type tick. Finishing or leaving it (Done, Cancel, Start, B) is the caller's to
// sound, because what that means depends on what the typing was for. A Done
// that finishes nothing (Config::required) is the Edge bump, here.
void Keyboard::moveFocus(int dx, int dy) {
    if (!open_) return;
    cursorStill_ = std::chrono::steady_clock::now();
    // IN THE FIELD, Left and Right are the cursor and stop at the ends: a
    // cursor that wrapped from the end of an address to its start would lose
    // the place a player is walking it to. Down goes back to the digit focus
    // came up from; Up is the edge, unless the keyboard shares the screen
    // with something above it (Config::upFromField).
    if (row_ == kField) {
        if (dy > 0) {
            row_ = 0;
            clampFocus();
            sound::play(sound::Cue::Move);
        } else if (dy < 0) {
            if (config_.upFromField && config_.upFromField()) letGoOfHeld();
            else sound::play(sound::Cue::Edge);
        } else if (dx != 0) {
            const size_t was = cursor_;
            cursor_ = dx < 0 ? previousChar(value_, cursor_) : nextChar(value_, cursor_);
            sound::play(cursor_ == was ? sound::Cue::Edge : sound::Cue::Move);
        }
        return;
    }
    // UP FROM THE DIGITS IS THE FIELD. An address kept behind a message
    // ("Can't connect", sayInField's `keep`) comes back to be corrected, as
    // the next key would bring it back; not while the panel waits on it,
    // when the message is what is happening.
    if (dy < 0 && row_ == 0) {
        if (!busy_) restoreHeld();
        row_ = kField;
        sound::play(sound::Cue::Move);
        return;
    }
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
    // Vertical movement does not wrap. Five rows is short enough to cross
    // directly, and wrapping from the space bar up to the digits skips the
    // letters — which is where somebody pressing up is almost always going.
    // Down from the space bar is the edge; Up from the digits is the field
    // (above).
    if (dx != 0) {
        const int n = static_cast<int>(rows[row_].size());
        col_ = (col_ + dx % n + n) % n;
    }
    clampFocus();
    sound::play(row_ == wasRow && col_ == wasCol ? sound::Cue::Edge : sound::Cue::Move);
}

// The hold's pace is the console's (design::HeldRepeat), the one every other
// screen's held direction keeps (App::repeatHeld). It is armed before the
// first move, because that move can be Up out of Search's field, which lets
// go of it.
void Keyboard::holdDirection(int dx, int dy) {
    if (!open_) return;
    holdDelete_ = false;
    holdDx_ = dx;
    holdDy_ = dy;
    hold_.start();
    moveFocus(dx, dy);
}

void Keyboard::releaseDirection(int dx, int dy) {
    if (!holdDelete_ && holdDx_ == dx && holdDy_ == dy) letGoOfHeld();
}

// HELD DELETE GOES ON DELETING (#668), as a computer's Backspace and a
// phone's delete do: a wrong address is cleared in a second or two rather
// than a press a character. The slow first repeat is what keeps a tap to one
// character. Only X and Backspace: A on "del" is a key pressed, and keys do
// not repeat.
void Keyboard::holdBackspace() {
    if (!open_) return;
    holdDelete_ = true;
    holdDx_ = holdDy_ = 0;
    hold_.start();
    backspace();
}

void Keyboard::releaseBackspace() {
    if (holdDelete_) letGoOfHeld();
}

void Keyboard::letGoOfHeld() {
    hold_.stop();
    holdDelete_ = false;
    holdDx_ = holdDy_ = 0;
}

void Keyboard::repeatHeld(float dt) {
    if (!open_) letGoOfHeld();
    if (!hold_.due(dt)) return;
    if (holdDelete_) backspace();
    else moveFocus(holdDx_, holdDy_);
}

void Keyboard::restoreHeld() {
    if (held_.empty()) return;
    value_ = held_;
    held_.clear();
    cursor_ = value_.size();
}

// Everything typed goes in here, at the cursor, which then sits after it.
void Keyboard::insert(const std::string& text) {
    restoreHeld();
    cursor_ = std::min(cursor_, value_.size());
    value_.insert(cursor_, text);
    cursor_ += text.size();
    revealAt_ = previousChar(value_, cursor_);
    lastTyped_ = std::chrono::steady_clock::now();
    cursorStill_ = lastTyped_;
    revealLast_ = true;
}

KeyboardResult Keyboard::pressKey() {
    if (!open_) return KeyboardResult::Cancelled;
    cursorStill_ = std::chrono::steady_clock::now();
    if (busy_) return KeyboardResult::Typing;
    if (row_ == kField) {
        moveFocus(0, +1);
        return KeyboardResult::Typing;
    }
    clampFocus();
    const Key& key = layout()[row_][col_];
    switch (key.action) {
        case Key::Backspace: backspace(); return KeyboardResult::Typing;
        case Key::Shift: cycleShift(); return KeyboardResult::Typing;
        case Key::Space:
            sound::play(sound::Cue::Type);
            insert(" ");
            return KeyboardResult::Typing;
        case Key::Conceal:
            sound::play(sound::Cue::Type);
            toggleConceal();
            return KeyboardResult::Typing;
        case Key::Done: return commit();
        case Key::None: break;
    }
    sound::play(sound::Cue::Type);
    // A copy: dropping a one-letter Shift rebuilds the layouts the key is in.
    const std::string typed = key.insert;
    insert(typed);
    // ONE SHIFT PRESS IS ONE CAPITAL, the way a phone keyboard behaves:
    // capitalise a letter and fall back to lower case, because the next
    // character almost never wants the same case. A lock stays (cycleShift).
    if (shift_ == Shift::Once && isLetter(typed)) {
        shift_ = Shift::Off;
        buildLayouts();
        clampFocus();
    }
    return KeyboardResult::Typing;
}

void Keyboard::backspace() {
    if (busy_) return;
    revealLast_ = false;
    restoreHeld();
    if (cursor_ == 0) { sound::play(sound::Cue::Edge); return; }
    sound::play(sound::Cue::Type);
    const size_t from = previousChar(value_, cursor_);
    value_.erase(from, cursor_ - from);
    cursor_ = from;
    cursorStill_ = std::chrono::steady_clock::now();
}

void Keyboard::cycleShift() {
    if (busy_) return;
    cursorStill_ = std::chrono::steady_clock::now();
    sound::play(sound::Cue::Type);
    shift_ = shift_ == Shift::Off    ? Shift::Once
             : shift_ == Shift::Once ? Shift::Locked
                                     : Shift::Off;
    buildLayouts();
    clampFocus();
}

void Keyboard::toggleConceal() {
    restoreHeld();
    conceal_ = !conceal_;
    // The label lives in the built layouts, so rebuild them to say the other
    // thing. Cheap, and it keeps one source of truth for the layout.
    buildLayouts();
}

void Keyboard::typeText(const char* utf8) {
    if (!open_ || !utf8 || !*utf8 || busy_) return;
    insert(utf8);
}

void Keyboard::sayInField(const std::string& msg, bool problem, bool keep) {
    held_ = keep ? (value_.empty() ? held_ : value_) : std::string();
    value_.clear();
    cursor_ = 0;
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

    // The field. FOCUS IN IT IS THE FOCUS RIM, drawn inside its edge, so the
    // field, the keys and the panel keep their size and place whether focus
    // is up there or not (#668).
    Rect field{panelX + pad, y, widest, fieldH, kKeyRadius, Color::black(0.45f)};
    if (row_ == kField && open_) {
        field.border = design::kFocusRimWidth;
        field.borderColor = palette::kFocusRim;
    }
    r.draw(field);
    // What was typed, a character at a time: a masked field draws a dot per
    // character, not per byte, with the character just typed readable for a
    // moment wherever the cursor put it; and the cursor sits between
    // characters, so `caret` counts the ones before it.
    constexpr auto kReveal = std::chrono::milliseconds(1500);
    const bool reveal = conceal_ && revealLast_ &&
                        std::chrono::steady_clock::now() - lastTyped_ < kReveal;
    std::vector<std::string> chars;
    size_t caret = 0;
    for (size_t i = 0; i < value_.size();) {
        const size_t next = nextChar(value_, i);
        if (i < cursor_) caret = chars.size() + 1;
        chars.push_back(!conceal_ || (reveal && i == revealAt_) ? value_.substr(i, next - i)
                                                                : "\xE2\x80\xA2");
        i = next;
    }
    // When it overflows, the end with the cursor in it: the tail, as before
    // the cursor, while the cursor is at the end where somebody typing is;
    // otherwise as much as fits with the cursor in view.
    const float room = widest - 56.0f;
    auto run = [&](size_t from, size_t to) {
        std::string s;
        for (size_t i = from; i < to; ++i) s += chars[i];
        return s;
    };
    size_t first = 0, last = chars.size();
    while (last - first > 1 && text.measure(run(first, last), TextStyle::Title3, scale) > room) {
        if (first < caret) ++first;
        else --last;
    }
    std::string shown = run(first, last);
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
    // WHITE IS WHAT YOU TYPED, GREY IS EVERYTHING ELSE, as in any operating
    // system's text field. A problem ("Wrong password") is as grey as
    // "Joining…" and says it is a problem by its shake; at 0.95 it read as
    // typed text.
    text.draw(r, shown, panelX + pad + 20.0f + shakeX, fieldBaseline, TextStyle::Title3,
              message ? Color::white(0.60f)
                      : (empty ? Color::white(0.35f) : Color::white(1.0f)),
              scale);
    if (!empty) {
        // THE CARET IS THE CURSOR, so the field reads as active rather than
        // as a label that happens to contain text, and says where the next
        // key goes. Centred on the gap between two characters and drawn over
        // the text, never pushing it apart: what follows the cursor stays
        // where it was as the cursor walks through it. IN THE ACCENT, as a
        // phone's is in its own: at this size the gap between two letters is
        // barely wider than the caret, and white touching white letters read
        // as part of them (the A9's 4K frames, #668); it was white at 0.75
        // while it only ever sat after the last letter.
        //
        // AND ON THE LETTERS, not the box: from a little above the capitals,
        // where a tall l ends, to a little below the line, where a g's tail
        // ends. Centred on the field it rose 17 points above the tallest
        // letter and reached 4 below the line, and looked to float up off
        // the text (MMagTech at the TV, 2026-11-08).
        constexpr float kCaretW = 3.0f;
        constexpr float kCaretOver = 5.0f;
        constexpr float kCaretUnder = 8.0f;
        const float capH = text.capHeight(TextStyle::Title3, scale);
        const float caretX = panelX + pad + 20.0f +
                             text.measure(run(first, caret), TextStyle::Title3, scale) -
                             kCaretW * 0.5f;
        // IT BLINKS ONLY WHILE IT WAITS, as a phone's does: steady while
        // anything is being pressed, typing, moving or deleting, and half a
        // second after the last press, wherever focus is, on and off once a
        // second, fading rather than cutting. Blinking only up in the field
        // was tried too: stopping on the keys with nothing blinking looked
        // wrong beside a phone (MMagTech at the TV, 2026-11-08).
        float on = 1.0f;
        if (open_) {
            const float still = std::fmod(
                std::chrono::duration<float>(std::chrono::steady_clock::now() - cursorStill_)
                    .count(),
                kCaretBlink);
            const float half = kCaretBlink * 0.5f, fade = kCaretBlink * 0.1f;
            if (still >= kCaretBlink - fade) on = (still - (kCaretBlink - fade)) / fade;
            else if (still >= half + fade) on = 0.0f;
            else if (still >= half) on = 1.0f - (still - half) / fade;
        }
        Color caretColor = palette::kScreenCyan;
        caretColor.a *= on;
        if (on > 0.0f)
            r.draw(Rect{caretX, fieldBaseline - capH - kCaretOver, kCaretW,
                        capH + kCaretOver + kCaretUnder, 1.5f, caretColor});
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
            const float baseline = dy + dh * 0.5f + text.ascent(st, scale) * 0.5f;
            text.draw(r, key.label, dx + (dw - lw) * 0.5f, baseline, st,
                      focused ? Color::white(1.0f) : Color::white(0.75f), scale);
            // Locked Shift: "ABC" says capitals, the line says they stay.
            if (key.action == Key::Shift && shift_ == Shift::Locked)
                r.draw(Rect{dx + (dw - lw) * 0.5f, baseline + kLockLineGap, lw, kLockLineHeight,
                            kLockLineHeight * 0.5f, palette::kScreenCyan});
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

// ---- --keyboard-test --------------------------------------------------------
//
// Everything below is driven the way a loop drives it, a press and a frame's
// time at a time, and read back only through what was typed: where focus is
// shows as the key A types.
int keyboardTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    // A press and its release, as a d-pad and X send them.
    auto tap = [](Keyboard& kb, int dx, int dy) {
        kb.holdDirection(dx, dy);
        kb.releaseDirection(dx, dy);
    };
    auto del = [](Keyboard& kb) {
        kb.holdBackspace();
        kb.releaseBackspace();
    };
    Keyboard::Config cfg;
    cfg.title = "Test";

    // THE CURSOR. Focus opens on "q"; Up twice is the digits, then the field.
    {
        Keyboard kb;
        cfg.initial = "caf\xC3\xA9";   // "café", the last character two bytes
        kb.open(cfg);
        kb.typeText("s");
        check(kb.value() == "caf\xC3\xA9s", "typing goes after what was prefilled");
        tap(kb, 0, -1);
        check(!kb.inField(), "Up from the letters is the digits");
        tap(kb, 0, -1);
        check(kb.inField(), "Up from the digits is the field");
        tap(kb, -1, 0);
        tap(kb, -1, 0);
        kb.typeText("X");
        check(kb.value() == "cafX\xC3\xA9s",
              "Left steps over a two-byte character whole, and typing goes in at the cursor");
        del(kb);
        check(kb.value() == "caf\xC3\xA9s", "del takes off the character before the cursor");
        tap(kb, +1, 0);
        del(kb);
        check(kb.value() == "cafs", "and takes a two-byte character off whole");
        for (int i = 0; i < 10; ++i) tap(kb, -1, 0);
        del(kb);
        check(kb.value() == "cafs", "the cursor stops at the start, where del has nothing to take");
        for (int i = 0; i < 10; ++i) tap(kb, +1, 0);
        kb.typeText("!");
        check(kb.value() == "cafs!", "and at the end");
        tap(kb, 0, +1);
        check(!kb.inField(), "Down from the field is the keys");
        kb.pressKey();
        check(kb.value() == "cafs!1", "the digit focus came up from, typed at the cursor");
        tap(kb, 0, -1);
        kb.pressKey();
        check(!kb.inField() && kb.value() == "cafs!1", "A in the field goes back to the keys");
        tap(kb, 0, -1);
        tap(kb, 0, -1);
        check(kb.inField(), "Up from the field with nothing above it stays in the field");
    }
    // A KEPT ADDRESS comes back when focus goes into the field to correct it,
    // and not while the panel is waiting on it.
    {
        Keyboard kb;
        cfg.initial = "romm.local";
        kb.open(cfg);
        kb.sayInField("Checking", false, /*keep=*/true);
        kb.setBusy(true);
        tap(kb, 0, -1);
        tap(kb, 0, -1);
        check(kb.value().empty(), "Up into the field while it waits leaves the message");
        kb.setBusy(false);
        kb.sayInField("Can't connect", true, /*keep=*/true);
        tap(kb, 0, +1);
        tap(kb, 0, -1);
        tap(kb, -1, 0);
        kb.typeText("X");
        check(kb.value() == "romm.locaXl", "Up into the field brings a kept address back to fix");
    }
    // SHIFT: off, the next letter only, locked, off.
    {
        Keyboard kb;
        cfg.initial.clear();
        kb.open(cfg);
        kb.cycleShift();
        kb.pressKey();
        kb.pressKey();
        check(kb.value() == "Qq", "one Shift press is one capital");
        kb.cycleShift();
        kb.cycleShift();
        kb.pressKey();
        kb.pressKey();
        kb.pressKey();
        check(kb.value() == "QqQQQ", "two presses lock capitals on");
        kb.toggleConceal();
        kb.pressKey();
        check(kb.value() == "QqQQQQ", "and show or hide leaves the lock on");
        kb.cycleShift();
        kb.pressKey();
        check(kb.value() == "QqQQQQq", "a third press turns them off");
        kb.cycleShift();
        tap(kb, 0, -1);
        kb.pressKey();
        tap(kb, 0, +1);
        kb.pressKey();
        check(kb.value() == "QqQQQQq1Q", "a digit typed after one press leaves it for the letter");
        kb.pressKey();
        check(kb.value() == "QqQQQQq1Qq", "which it capitalises, and no more");
    }
    // THE HOLD, at the console's pace (design.h). Focus opens on "q", the
    // first of a row of twelve that wraps.
    {
        Keyboard kb;
        kb.open(cfg);
        kb.holdDirection(+1, 0);   // "w" at once
        kb.repeatHeld(design::kRepeatDelay - 0.01f);
        kb.pressKey();
        check(kb.value() == "w", "a held direction moves once, then waits");
        kb.repeatHeld(0.02f);   // past kRepeatDelay: "e"
        const float gap = design::repeatGap(design::kRepeatDelay + 0.01f);
        kb.repeatHeld(gap - 0.01f);
        kb.pressKey();
        check(kb.value() == "we", "then moves again after kRepeatDelay, and waits the gap");
        kb.repeatHeld(0.02f);   // "r"
        kb.releaseDirection(0, +1);
        kb.repeatHeld(1.0f);    // "t": letting go of Down did not stop Right
        kb.releaseDirection(+1, 0);
        kb.repeatHeld(1.0f);
        kb.pressKey();
        check(kb.value() == "wet", "only letting go of the held direction stops it");
        check(std::fabs(design::repeatGap(0.0f) - design::kRepeatStart) < 1e-6f &&
                  std::fabs(design::repeatGap(design::kRepeatRamp * 2.0f) -
                            design::kRepeatFast) < 1e-6f,
              "the gap closes from kRepeatStart to kRepeatFast");
        // Three seconds of frames at 60 a second: round the row of twelve
        // several times, left past "q" onto ":" and on.
        kb.holdDirection(-1, 0);
        for (int frame = 0; frame < 180; ++frame) kb.repeatHeld(1.0f / 60.0f);
        kb.releaseDirection(-1, 0);
        kb.pressKey();
        check(kb.value().size() == 4, "a long hold circles the row and still types one key");
    }
    // HELD DELETE, at the same pace, and a tap takes one character.
    {
        Keyboard kb;
        cfg.initial = "romm.local";
        kb.open(cfg);
        kb.holdBackspace();
        kb.repeatHeld(design::kRepeatDelay - 0.01f);
        check(kb.value() == "romm.loca", "held X deletes once, then waits");
        kb.repeatHeld(0.02f);
        check(kb.value() == "romm.loc", "then deletes again after kRepeatDelay");
        kb.releaseDirection(-1, 0);
        for (int frame = 0; frame < 120; ++frame) kb.repeatHeld(1.0f / 60.0f);
        check(kb.value().empty(),
              "and goes on, faster, until the field is empty; letting go of Left does not stop it");
        kb.releaseBackspace();
        kb.typeText("abc");
        del(kb);
        kb.repeatHeld(1.0f);
        check(kb.value() == "ab", "a tap of X takes one character");
        cfg.initial.clear();
    }
    // UP OUT OF SEARCH'S FIELD ends the hold: the results get one press.
    {
        Keyboard kb;
        int left = 0;
        Keyboard::Config search = cfg;
        search.upFromField = [&left] { ++left; return true; };
        kb.open(search);
        kb.holdDirection(0, -1);   // the digits
        kb.repeatHeld(design::kRepeatDelay + 0.01f);   // the field
        for (int i = 0; i < 60; ++i) kb.repeatHeld(0.05f);
        check(left == 1, "a held Up leaves the field for the results once, and stops");
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace ui
