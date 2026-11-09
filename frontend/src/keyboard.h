// The on-screen keyboard.
//
// The baseline for all text entry, per the input model in docs/PROJECT.md: the
// controller is the only device the design may assume exists, so every field
// has to be fillable with a d-pad and two buttons. A physical keyboard types
// into the same field at the same time, and neither implementation may be the
// only one.
//
// There is nothing to copy here. Cabinet never wrote one — tvOS supplied it —
// so this is designed from the input model rather than ported.
//
// The decisions worth stating, because they are the ones that make it
// bearable rather than merely possible:
//
// A PASSWORD IS MASKED, BUT THE CHARACTER JUST TYPED SHOWS. Reversed
// 2026-09-24 by MMagTech, from "show what was typed, including passwords"
// (open question 17). The objection to masking was that a typo nobody can
// see only surfaces as a failed join; showing each character for a moment as
// it is typed, the way phones do, answers that, and the "show" key reveals
// the lot before done. Since the field has a cursor (below) that character
// can sit anywhere, and it is the one shown, wherever it is. Callers ask for
// it with `conceal`; other fields are shown as typed.
//
// REDUCE TYPING RATHER THAN SPEEDING IT UP. A key that inserts ".com", a
// prefilled scheme, and a remembered previous value each save more than any
// amount of layout tuning.
//
// LEFT AND RIGHT WRAP AROUND A ROW; UP AND DOWN DO NOT. Getting from "1" to
// "del" is one press rather than eleven (moveFocus in keyboard.cpp says why
// this reversed the first design). A HELD DIRECTION REPEATS, as on every
// other screen, at the same pace (#668; design.h's kRepeatDelay and the rest):
// the slow first repeat is what lets a player stop on the key they want in a
// row that wraps. The hold is the keyboard's own (holdDirection), so the three
// loops that drive a keyboard, the console's, first run's and the startup
// screen's, repeat it the same way.
//
// THE FIELD HAS A CURSOR (#668). Up from the digits moves focus into the
// field; there Left and Right move the cursor a character at a time, and Down
// (or A) goes back to the key focus left. Every key types, and del deletes, at
// the cursor. A typo early in a long address or password is fixed in place
// rather than by deleting back to it. On every keyboard alike, Search's
// included: there Up from the field goes on to the results
// (Config::upFromField), one press more than before the field had a cursor,
// because one rule for every field beats a Search that skips its own field.
//
// SHIFT IS A CYCLE OF THREE PRESSES, WITH NO TIMING (#668). Off; one press
// capitalises the next letter only, as a phone does; a second press before a
// letter locks capitals on until Shift is pressed again. A phone tells a lock
// from a second tap by how quickly it came; a controller's Y, or A on the
// Shift key, has no such convention, and a press that means something else if
// it is a little slow is one a player cannot learn by looking. The Shift key
// says which of the three it is in.

#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "design.h"

namespace ui {

class Renderer;
class TextRenderer;

enum class KeyboardResult { Typing, Committed, Cancelled };

class Keyboard {
public:
    struct Config {
        std::string title = "Enter text";
        std::string hint;          // a line under the title, or empty
        std::string initial;       // prefilled, e.g. "https://"
        std::string placeholder;   // shown greyed when the field is empty
        bool conceal = false;      // start with the value masked
        // DONE ON AN EMPTY FIELD DOES NOTHING but bump, the sound backspace
        // makes on one, and the panel stays as it was. For a field where
        // empty is never an answer: a password keyboard only opens for a
        // secured network, and an empty one waited 12 seconds to say
        // "Wrong password" again (MMagTech on the TV, 2026-11-01).
        bool required = false;
        // Extra keys worth having for this particular field. A URL wants
        // ".com" and "/"; a Wi-Fi passphrase wants neither.
        std::vector<std::string> shortcuts;

        // A PANEL THAT SHARES THE SCREEN RATHER THAN COVERING IT — new
        // 2026-09-21, for Search.
        //
        // Everything that opened this before was a question with one answer:
        // an address, a passphrase. Nothing else needed to be visible, so the
        // panel sat in the middle of a dimmed screen and owned it.
        //
        // Search is the opposite. The results are the point and they change on
        // every key, so the keyboard has to live at the bottom of the screen
        // with the results above it, and nothing may be dimmed. Its title and
        // hint go too: the field IS the title when you can see what it is
        // filtering.
        bool dockedBottom = false;

        // UP FROM THE FIELD, for a keyboard that shares the screen: Search
        // goes to its results. Returns whether focus left the keyboard, which
        // also ends a held Up, so a held Up does in the results only what a
        // press does. Unset, or false, and Up from the field is the edge.
        std::function<bool()> upFromField;
    };

    // Whether focus is in the field rather than on a key.
    bool inField() const { return row_ == kField; }

    // Where the panel's top edge is, in canvas points, from the last time it
    // was drawn, a slide included. A docked keyboard shares the screen, so
    // whatever is above it has to know where "above it" ends — and the panel's
    // height depends on the layout, the shift state and whether it is docked,
    // so nothing outside can work it out for itself.
    float panelTop() const { return panelTop_; }

    void open(const Config& config);
    bool isOpen() const { return open_; }

    // DIRECTIONS AND DELETE, from a pad or a keyboard: the layer above does
    // not say which, because nothing here may depend on the answer.
    //
    // A direction, or delete (X, Backspace), that goes down acts once and is
    // held (holdDirection, holdBackspace); the same one coming up lets go
    // (releaseDirection, releaseBackspace), and letting go of another does
    // not stop it; repeatHeld, every frame the keyboard has the input, acts
    // again at the console's pace (design::HeldRepeat). One thing is held at
    // a time, the last pressed, as a d-pad has one direction. A system's own
    // key repeat is not a press: the loops drop it for the arrows and
    // Backspace, so a keyboard holds exactly as a pad does. letGoOfHeld ends
    // the hold whatever is down, for a loop whose presses stop reaching the
    // keyboard (focus gone to Search's results, a menu over it, a game).
    void holdDirection(int dx, int dy);
    void releaseDirection(int dx, int dy);
    void holdBackspace();
    void releaseBackspace();
    void repeatHeld(float dt);
    void letGoOfHeld();
    // Presses the focused key. RETURNS A RESULT, because two of the keys on
    // this layout end the whole session — "done" and "cancel" — and until
    // 2026-09-20 this returned void and handled them silently. So somebody
    // driving the keyboard with a CONTROLLER, pressing A on "done", closed the
    // panel and had what they had typed thrown away: the caller was watching
    // for its own Start button and never learned. The physical keyboard's
    // Return worked, which is exactly why nobody noticed.
    //
    // A in the field places the cursor where it is and goes back to the key
    // focus left, as Down does: focus never sits on something that ignores
    // the button.
    KeyboardResult pressKey();
    // Off, the next letter only, locked, off again: the Shift key and Y.
    void cycleShift();
    void toggleConceal();
    KeyboardResult commit();   // "done"
    KeyboardResult cancel();

    // WAITING ON WHAT WAS TYPED, e.g. a Wi-Fi join. The panel stays up and
    // takes no typing; only cancel (B) works. It used to close on done and a
    // new one open on a wrong password, and the two cuts of a full-screen
    // scrim read as a strobe (MMagTech on the TV, 2026-09-24).
    void setBusy(bool on) { busy_ = on; }
    bool busy() const { return busy_; }

    // A MESSAGE IN THE FIELD ITSELF: it empties and says `msg` where the
    // placeholder goes, until the first character is typed, in grey: only
    // typed text is white. `problem` shakes it once
    // ("Wrong password"). MMagTech's idea,
    // 2026-09-24: a line under the title made the panel grow and jump, and
    // the pill at the foot of the screen was out of the line of sight.
    //
    // `keep` holds what was typed behind the message instead of emptying it,
    // for a field worth correcting rather than retyping (a server address).
    // The next key brings it back and acts on it; Done sends it again as it
    // was.
    void sayInField(const std::string& msg, bool problem, bool keep = false);

    // A physical keyboard types into the same field. Not a separate path: the
    // same string, the same cursor, the same commit, the same screen.
    void typeText(const char* utf8);

    const std::string& value() const { return value_; }

    void draw(Renderer& r, TextRenderer& text, float scale);

    // SLIDING A DOCKED KEYBOARD, 0 in place and 1 fully below the screen —
    // 2026-09-24. Search's keyboard slides down as Search is left and up as it
    // arrives, which is how an on-screen keyboard goes away; dissolving it
    // with the rest of the screen was the big solid thing in a switch that
    // otherwise only faded.
    //
    // keepForSlide() lets it go on drawing after cancel() so it can leave;
    // it takes no input while it does, because it is not open.
    void setSlide(float t) { slide_ = t; }
    // Also right after commit() or cancel(), which close it: Search's Done
    // slides the panel out rather than dropping it.
    void keepForSlide() { ghost_ = true; }
    void endSlide() { ghost_ = false; slide_ = 0.0f; }
    bool sliding() const { return ghost_; }

    // --- A pointer, which is a THIRD way in and takes nothing away ----------
    //
    // docs/PROJECT.md open question 16 gives the mouse focus and click and
    // nothing else, and MMagTech's condition on extending it here, 2026-09-20,
    // was that *"typing with keyboard still works as well or the on screen
    // keyboard can still be driven by a wired controller."* Both do: this moves
    // the same `row_`/`col_` the d-pad moves and presses the same key `A`
    // presses. There is no pointer-only affordance and no key that can only be
    // reached with a mouse.
    //
    // WHY THE KEYBOARD NEEDS IT AT ALL. A mouse that drives the list behind
    // this panel and then goes dead the moment a password field opens is worse
    // than no mouse: it teaches somebody it works and abandons them at the
    // hardest typing in the whole flow. And people arrive here straight out of
    // a pointer-driven installer — MMagTech's argument, and a better one than
    // the document had.
    //
    // Coordinates are CANVAS POINTS, because that is what the layout is in and
    // the letterbox is the caller's problem to undo.

    // Moves focus to the key under the point. Returns false when there is no
    // key there, which includes the panel's own margins — nothing is focused
    // by pointing at the gap between keys.
    bool focusAt(float canvasX, float canvasY);

    // Focuses the key under the point and presses it, returning what that
    // press meant. `hit` says whether there was a key there at all — a click on
    // the scrim does nothing rather than pressing whatever was focused last.
    KeyboardResult pressAt(float canvasX, float canvasY, bool* hit = nullptr);

    // Public only so the layout tables in the .cpp can be built by free
    // helpers. Nothing outside this class has a reason to touch it.
    struct Key {
        std::string label;   // what is drawn
        std::string insert;  // what is typed; empty for an action key
        enum Action { None, Backspace, Shift, Space, Done, Conceal } action = None;
        float width = 1.0f;  // in key units
    };

private:
    // The field is a row above the digits, so the d-pad, the pointer and the
    // clamp all move one `row_`. While focus is there `col_` keeps the digit
    // it came up from, which Down returns to.
    static constexpr int kField = -1;
    enum class Shift { Off, Once, Locked };

    const std::vector<std::vector<Key>>& layout() const;
    // Both cases' layouts, with the Shift and show keys saying what they
    // are now: built at open and again when either changes.
    void buildLayouts();
    void clampFocus();
    void moveFocus(int dx, int dy);
    void insert(const std::string& text);
    // Deletes the character before the cursor.
    void backspace();

    // Where `draw` last put each key, in canvas points. The draw pass fills it
    // and the event pass reads it on the frame after — the same order the rest
    // of this flow already relies on. Empty until the first frame is drawn, so
    // a click that somehow arrives first simply finds nothing.
    struct KeyRect { float x = 0, y = 0, w = 0, h = 0; int row = 0, col = 0; };
    std::vector<KeyRect> keyRects_;

    bool open_ = false;
    bool ghost_ = false;     // closed, still drawing while it slides away
    float slide_ = 0.0f;
    Config config_;
    std::string value_;
    // Where the next character goes, in bytes, always at the start of a
    // UTF-8 character: half of one is a string that will not render.
    size_t cursor_ = 0;
    Shift shift_ = Shift::Off;
    bool conceal_ = false;
    bool busy_ = false;
    std::string fieldMsg_;          // replaces the placeholder while set
    std::string held_;              // typed text behind the message (keep)
    void restoreHeld();
    bool fieldProblem_ = false;
    std::chrono::steady_clock::time_point shakeAt_{};
    // The character just typed stays readable this long in a masked field;
    // `revealAt_` is where it starts, in bytes.
    std::chrono::steady_clock::time_point lastTyped_{};
    bool revealLast_ = false;
    size_t revealAt_ = 0;
    // The last press of any kind: the cursor blinks only once it is still.
    std::chrono::steady_clock::time_point cursorStill_{};
    int row_ = 0, col_ = 0;
    // What is held: delete, or a direction (holdDx_, holdDy_), and its pace.
    design::HeldRepeat hold_;
    bool holdDelete_ = false;
    int holdDx_ = 0, holdDy_ = 0;
    float panelTop_ = 0.0f;
    std::vector<std::vector<Key>> lower_, upper_;
};

// --keyboard-test: the cursor, Shift's three presses and the held repeat's
// pace, directions and delete, with no screen, no pad and no sound. 0 when all pass.
int keyboardTest();

}  // namespace ui
