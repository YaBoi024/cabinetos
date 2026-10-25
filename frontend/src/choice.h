// A question with a few answers, in a panel in the middle of the screen.
//
// "Who do you want to remove?", "Remove vivian from this console?" and, when
// they are built, sign out and forgetting a network. A scrim, the pause
// menu's panel, and a column of its buttons (design::menuPanel).
//
// A LAYER, LIKE THE PIN PAD. It is opened over whatever is showing, takes
// input first while it is open, and says which answer was chosen. What the
// answer means is the app's.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

class ChoiceScreen {
public:
    // Stepped: Left or Right on the stepper row (setStepper); stepped() says
    // which way. The app changes the value and calls setValues again.
    enum class Outcome { None, Chosen, Cancelled, Stepped };

    // `focus` is the answer focus starts on. For a question whose first
    // answer destroys something, start on the harmless one.
    void open(std::string title, std::string detail, std::vector<std::string> options,
              int focus = 0);
    // A value on the right of each answer ("Connected"), turning the column
    // into a list: labels go to the left. Call after open; empty for none.
    void setValues(std::vector<std::string> values) { values_ = std::move(values); }
    // New answers while open (a Wi-Fi scan landing), keeping focus on the
    // same label if it is still there.
    void replace(std::vector<std::string> options, std::vector<std::string> values,
                 std::string detail);
    // STAYS UP WHEN AN ANSWER IS CHOSEN, for a window that acts on a choice
    // in place (Add a controller pairs the pad it lists). Back still closes.
    // open() clears it.
    void setStaysOpen(bool on) { staysOpen_ = on; }
    bool staysOpen() const { return staysOpen_; }
    // ONE WIDTH FOR AS LONG AS IT IS OPEN, the content's width in canvas
    // points; longer names and lines are cut short. For a window whose list
    // and line change while it is up: Add a controller grew wider when a
    // pairing failed, and MMagTech did not like it (2026-09-26). open()
    // clears it.
    void setFixedWidth(float w) { fixedW_ = w; }
    // A LEVEL OF THE PAUSE MENU, IN ITS PANEL. `w` and `h` are the pause
    // menu's panel, whose size it keeps for as long as it is open, rows
    // beyond what fits scrolling inside it (MMagTech, 2026-10-25: going more
    // than one menu deep, the pause menu's height and width must never
    // change). It draws no scrim, no panel and no title of its own: the
    // pause menu's panel stays where it is, and its answers are drawn as the
    // menu's own rows ("Button 1: A"), so only the words change: the menu's
    // rows go at once as these fade in, and when it closes these fade out,
    // then the menu's fade back in. MMagTech at the TV, the same day: the
    // panel flashed, gone for a moment and fading back in as the level's
    // own; with it kept, he asked for a fade on the way out. The pause menu
    // reads menuRows(). open() clears it.
    void setOverPauseMenu(float w, float h) {
        panelW_ = w;
        panelH_ = h;
        overMenu_ = true;
    }
    bool overPauseMenu() const { return overMenu_; }
    // How much the pause menu's own rows show under a level of it, 0 to 1:
    // none while it is up, fading back in after close().
    float menuRows() const;
    // The pause menu's own fade, which a level of it is drawn under (the App
    // sets it as it draws the menu).
    void setMenuFade(float a) { menuFade_ = a; }
    // A LIST THAT FILLS WHILE IT IS OPEN GROWS SMOOTHLY. It opens one row
    // tall with `placeholder` said in it, and glides taller as rows arrive.
    // Add a controller first jumped from a small panel to a big one, which
    // read as a second window; then it opened six rows tall, which left an
    // empty band (MMagTech, 2026-09-26). open() clears it.
    void setGrows(std::string placeholder) {
        grows_ = true;
        placeholder_ = std::move(placeholder);
    }
    // GREYED ANSWERS, one flag per answer: drawn dim, never focused, never
    // chosen. Steam's Continue while the console is offline (#223): grey it
    // out, do not explain (no spoon-feeding). Call after open; focus moves
    // off a greyed answer to the next one that is not.
    void setDisabled(std::vector<bool> disabled);
    // ONE ROW WHOSE VALUE LEFT AND RIGHT CHANGE, with an arrow either side
    // of it while focused, as a Settings choice row (Steam's size, #223).
    // `canLeft`/`canRight` dim the arrow at an end. -1 for none; open()
    // clears it.
    void setStepper(int slot, bool canLeft, bool canRight) {
        stepper_ = slot;
        canLeft_ = canLeft;
        canRight_ = canRight;
    }
    int stepped() const { return stepDir_; }
    // A PANEL WITH NOTHING TO PRESS BUT A BUTTON, said rather than focused:
    // "Press (B) to cancel", the letter drawn as a button badge, in place of
    // the answers (open it with none). A focused Cancel under a run of A
    // presses gets pressed by the next one (Steam's install, #223; the
    // unearned-focus rule). Back answers Cancelled, as for any panel. A
    // prompt that names A ("Press (A) to insert and restart") is answered by A,
    // as Chosen with chosen() 0. open() clears it.
    void setPrompt(std::string before, std::string button, std::string after) {
        promptBefore_ = std::move(before);
        promptButton_ = std::move(button);
        promptAfter_ = std::move(after);
    }
    // A PANEL FOR SOMETHING THAT CANNOT BE STOPPED (Format, #236): a
    // spinner where the answers go, and Back does nothing. The app closes it
    // when the work is done. open() clears it.
    void setBusy(bool on) { busy_ = on; }
    bool busy() const { return busy_; }
    // The title alone, keeping everything else ("Paired successfully").
    void setTitle(std::string title) { title_ = std::move(title); }
    // The detail line alone, keeping the answers and focus.
    void setDetail(std::string detail) { detail_ = std::move(detail); }
    // A level of the pause menu fades out, then the menu's rows fade back in.
    void close();
    // The pause menu is closing with a level of it up: the level fades with
    // the menu's panel instead, and the menu's rows stay hidden.
    void closeWithMenu() {
        open_ = false;
        withMenu_ = true;
    }
    // Nothing left of it, at once (the pause menu opening again).
    void forget();
    bool isOpen() const { return open_; }
    // Open, or a level of the pause menu still fading out.
    bool visible() const { return open_ || (overMenu_ && appear_.value() > 0.001f); }
    const std::string& title() const { return title_; }
    // The answer, after Chosen.
    int chosen() const { return slot_; }

    Outcome key(Nav n);
    void tick(float dt);
    // For a capture: the resting frame.
    void settle() { appear_.settle(1.0f); focus_.settle(1.0f); }
    void draw(Ctx& c);

private:
    bool open_ = false;
    bool staysOpen_ = false;
    bool busy_ = false;
    float spin_ = 0.0f;
    float fixedW_ = 0.0f;
    float panelW_ = 0.0f, panelH_ = 0.0f;
    bool overMenu_ = false;
    bool withMenu_ = false;    // closed with the pause menu: fading with it
    float menuFade_ = 1.0f;
    int fit_ = 0;              // rows a level of the pause menu has room for, 0 otherwise
    int fit() const;
    float levelShown() const;  // how much of a level of the pause menu shows
    void drawMenuRows(Ctx& c, float a);
    bool grows_ = false;
    std::string placeholder_;
    design::Animated listH_;
    bool listHSet_ = false;
    std::string title_, detail_;
    std::vector<std::string> options_;
    std::vector<std::string> values_;
    std::vector<bool> disabled_;
    std::string promptBefore_, promptButton_, promptAfter_;
    int stepper_ = -1;
    bool canLeft_ = false, canRight_ = false;
    int stepDir_ = 0;
    bool isDisabled(int i) const {
        return i >= 0 && i < static_cast<int>(disabled_.size()) && disabled_[i];
    }
    int slot_ = 0;
    int top_ = 0;              // first answer shown, once there are too many
    design::Animated appear_;
    design::Animated focus_;
};

}  // namespace screens
