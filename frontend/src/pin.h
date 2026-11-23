// The PIN pad: four digits, entered with a controller, in a panel over the screen.
//
// docs/SETTINGS.md, Accounts, and issue #57. The owner of the console (the
// first account from setup) can set one PIN. With one set, Wi-Fi changes, Sign
// out, Change server address, Adding or removing an account and File access
// ask for it, and so does switching into the owner's account. The PIN is a
// deterrent against a sibling with a controller; accounts.h says plainly what
// it is not.
//
// IT IS A LAYER, NOT A SCREEN. It is asked for from the account panel and
// from Settings, so it is not on the stack but drawn over whatever is there,
// the way the account panel is. The app opens it, takes its input first while it is open,
// and decides what an entered PIN means.
//
// IT HOLDS NO OPINION ABOUT THE PIN, the division every screen here follows.
// It collects digits and says when four have been entered. Whether they are
// right is `accounts::checkPin`, called by the app, which answers with
// `reject` or `close`. The one thing it does itself is the second entry when
// a new PIN is chosen, because "type it again" is part of entering it.
//
// FOUR DIGITS, fixed. A starting value: long enough that a guess is not a
// guess, short enough to type with a d-pad.
//
// THE PAD IS A TELEPHONE'S, 1 2 3 across the top, because that is the layout
// everyone already knows from a phone's passcode screen. The bottom row is
// Cancel, 0 and Delete, so every way out is a key focus can land on and not
// only a button somebody has to know about.

#pragma once

#include <string>

#include "screens.h"

namespace screens {

class PinScreen {
public:
    enum class Mode {
        Check,    // enter the PIN that is set
        Choose,   // choose a new one, then enter it again
    };
    enum class Outcome {
        None,
        Entered,     // four digits are in; read them with pin()
        Cancelled,   // left without entering one
    };

    // `title` says what is being asked, e.g. "Enter the PIN"; `detail` says
    // what for ("To switch to MMagTech"), or is empty.
    void open(Mode mode, std::string title, std::string detail);
    void close();
    bool isOpen() const { return open_; }
    Mode mode() const { return mode_; }

    // The four digits last entered. In Choose mode, the confirmed PIN.
    const std::string& pin() const { return entered_; }

    // THE APP SAYS IT WAS WRONG. The dots shake, empty, and `why` shows under
    // them until the next digit.
    void reject(const std::string& why);
    // TOO MANY WRONG TRIES: digits are refused for `seconds`, and the pad says
    // how long is left. The app keeps the count; this only keeps the clock.
    void lockFor(float seconds);
    bool locked() const { return lockLeft_ > 0.0f; }

    Outcome key(Nav n);
    // X on a pad, Backspace on a keyboard: a press takes a digit back, as on
    // the on-screen keyboard (and Xbox's, and PlayStation's Square), and
    // held it goes on taking them at the console's pace (design::HeldRepeat,
    // repeated in tick), until releaseDelete (#668).
    void holdDelete();
    void releaseDelete() { deleteHold_.stop(); }
    // A digit from a physical keyboard, into the same field. Anything that is
    // not 0 to 9 is ignored.
    Outcome typeDigit(char c);

    // For a capture: the resting frame, not one part-way through arriving.
    void settle() { appear_.settle(1.0f); focus_.settle(1.0f); }

    void tick(float dt);
    // Draws the whole layer, background included. Call it last, over
    // everything, because it sits over everything.
    void draw(Ctx& c);

private:
    Outcome add(char digit);
    Outcome deleteOne();

    bool open_ = false;
    Mode mode_ = Mode::Check;
    std::string title_, detail_;
    std::string typed_;        // the digits in the dots now
    std::string first_;        // Choose mode: the first entry, while confirming
    std::string entered_;      // what pin() returns
    std::string message_;      // under the dots: an error, or a prompt
    int row_ = 0, col_ = 0;    // focus on the pad, 4 rows of 3
    float lockLeft_ = 0.0f;
    design::Animated appear_;
    design::Animated focus_;
    float shake_ = 0.0f;       // seconds of shake left
    design::HeldRepeat deleteHold_;
};

}  // namespace screens
