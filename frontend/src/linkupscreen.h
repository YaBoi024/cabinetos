// LinkUp's two screens (#654). LinkUpScreen is the top-bar destination: Status
// Offline on the left and the signed-in person's friends on the right, the same
// two columns as Settings. LinkUpPlayScreen is what a console shows in a session
// with no game to show: the person who invited waiting for their friend to join,
// and on the friend's console "Waiting for Alex" and "Reconnecting". Like every
// screen they draw, own their focus and return an Action; what a press means is
// the app's (linkup.cpp).

#pragma once

#include <string>
#include <vector>

#include "linkuprules.h"
#include "screens.h"

namespace screens {

class LinkUpScreen {
public:
    // What Action::LinkUp says in its value. 0 and up is a friend's index in the
    // list; a friend who cannot be pressed never gets focus.
    static constexpr int kAppearOffline = -2;

    // `inUse` is a Remote Play stream: nobody can be invited or joined, and the
    // rows that would read "In use".
    void setState(const linkup::State& state, bool inUse);
    void enter();
    void settleArrival() { appear_.settle(1.0f); }
    void setHasFocus(bool on);

    void tick(float dt);
    Result key(Nav n);
    // Everything here is glass, so it all draws after presentScene.
    void drawGlass(Ctx& c);

private:
    struct Line {
        std::string heading;  // a heading row when this is set
        int friendAt = -1;
        std::string name, detail, value;
        bool pressable = false;
        bool dim = false;
        bool chevron = false;
    };
    int firstPressable() const;
    void moveFocus();

    linkup::State state_;
    bool inUse_ = false;
    std::vector<Line> lines_;
    int line_ = -1;        // the focused line on the right, or -1 on Status
    bool hasFocus_ = true;
    design::Animated appear_;
    design::Animated focus_;
    design::Animated scroll_;
};

// THE SAME FRIENDS AS ARCADE CABINETS, the console's own mark, one per person: lit
// when they are on, dark when they are not. A person has no box art, so this is what
// gives the screen a Library's life, and every friend always has one. Being compared
// with the rows above and the Library's tiles; two of the three go when one is picked.
class LinkUpCabinets {
public:
    void setState(const linkup::State& state, bool inUse);
    void enter();
    void settleArrival() { appear_.settle(1.0f); }
    void setHasFocus(bool on);
    void tick(float dt);
    Result key(Nav n);
    void drawGlass(Ctx& c);

private:
    struct Cab {
        int friendAt = 0;
        linkup::Status kind = linkup::Status::Offline;
        std::string name, status;
        bool pressable = false;
        bool dark = false;
    };
    void moved();

    std::vector<Cab> cabs_;
    std::string pill_, emptyLine_;
    int at_ = -1;          // the focused cabinet, or -1 on Status
    bool hasFocus_ = true;
    float clock_ = 0.0f;
    design::Animated appear_;
    design::Animated focus_;
    design::Animated scroll_;
};

class LinkUpPlayScreen {
public:
    // What Action::LinkUp says: B asking to leave, and B while an invite waits.
    static constexpr int kLeave = -3;
    static constexpr int kCancelInvite = -6;

    enum class Shows {
        Inviting,      // after inviting: the game starts when the friend joins
        Waiting,       // "Waiting for Alex": the host is on anything but the game
        Reconnecting,  // the friend's console trying again, for up to a minute
        Picture,       // the game. The real one is the LinkUp program's own window
                       // over this screen, so this draws a stand-in until step 3
    };
    void open(Shows shows, std::string title, std::string detail, bool friendSide);
    Shows shows() const { return shows_; }
    bool friendSide() const { return friendSide_; }

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);

private:
    Shows shows_ = Shows::Waiting;
    bool friendSide_ = true;
    std::string title_, detail_;
    float clock_ = 0.0f;
};

}  // namespace screens
