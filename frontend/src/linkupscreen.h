// LinkUp's two screens (#654). LinkUpScreen is the top-bar destination: Status above
// the signed-in person's friends, a tile each in a colour worked out from who they are
// (linkup::coloursApart), so nobody picks one and nothing is stored or sent.
// LinkUpPlayScreen is what a console shows in a session with no game to show: the
// person who invited waiting for their friend to join, and on the friend's console
// "Waiting for Alex" and "Reconnecting". Like every screen they draw, own their focus
// and return an Action; what a press means is the app's (linkup.cpp).

#pragma once

#include <string>
#include <vector>

#include "linkuprules.h"
#include "screens.h"

namespace screens {

class LinkUpScreen {
public:
    // What Action::LinkUp says in its value. 0 and up is a friend's index in the
    // list; a friend who cannot be pressed says nothing.
    static constexpr int kAppearOffline = -2;
    // A on a console taken off its server: type a code for it again.
    static constexpr int kEnterCode = -8;
    // A friend's colour, bright enough for a small dot. Amber for nobody on the list.
    static ui::Color dotColour(const std::vector<linkup::Friend>& friends, const std::string& name);

    // `inUse` is a Remote Play stream: nobody can be invited or joined.
    void setState(const linkup::State& state, bool inUse);
    void enter();
    void settleArrival();
    void setHasFocus(bool on);
    // With no friends there is nothing to focus, not even Status (App::screenTakesFocus), unless
    // the console was taken off its server, where A types a code again.
    bool takesFocus() const { return !tiles_.empty() || enterCode_; }

    void tick(float dt);
    Result key(Nav n);
    // Everything here is glass, so it all draws after presentScene.
    void drawGlass(Ctx& c);

private:
    struct FriendTile {
        int friendAt = 0;
        linkup::Status kind = linkup::Status::Offline;
        std::string name, status;
        bool pressable = false;
        bool dark = false;
        ui::Color top, bottom;
    };
    void moved();
    void drawTile(Ctx& c, const FriendTile& tile, float x, float y, float w, float h, float f);
    void drawNoFriends(Ctx& c, float stars, float picture);

    std::vector<FriendTile> tiles_;
    std::string pill_, instead_;
    bool inUse_ = false;
    bool enterCode_ = false;   // taken off the server, and no stream: "Press (A) to enter a code"
    int at_ = -1;          // the focused friend, or -1 on Status
    bool hasFocus_ = true;
    design::Animated appear_;
    design::Animated focus_;
    design::Animated scroll_;
    float sinceChange_ = 1.0f;   // since the list went from nobody to somebody, or back
};

class LinkUpPlayScreen {
public:
    // What Action::LinkUp says: B asking to leave, and B while an invite waits.
    static constexpr int kLeave = -3;
    static constexpr int kCancelInvite = -6;
    // A on a friend's console while the person hosting has paused: it resumes both.
    static constexpr int kResumeBoth = -7;

    enum class Shows {
        Inviting,      // after inviting: the game starts when the friend joins
        Waiting,       // "Waiting for Alex": the host is on anything but the game
        Reconnecting,  // the friend's console trying again, for up to a minute
        Picture,       // the game. The real one is the LinkUp program's own window under
                       // this one, so this draws only the bars around it (setPlace); only
                       // the gallery has a stand-in
    };
    void open(Shows shows, std::string title, std::string detail, bool friendSide);
    Shows shows() const { return shows_; }
    void setFade(float fade) { fade_ = fade; }
    // A line under the rest, for a game still downloading. Empty for none.
    void setNote(std::string note) { note_ = std::move(note); }
    bool friendSide() const { return friendSide_; }
    // The gallery has no program to show a picture. Off, Picture draws only the bars.
    void setStandIn(bool on) { standIn_ = on; }
    // The person hosting has paused, and these words say so: A resumes both.
    void setResumes(bool on) { resumes_ = on; }
    // Where the host's game sits, in canvas points (#692). Picture then draws black bars around it
    // and the glow over them, as the host's TV shows, over the stream's own. None, nothing.
    void setPlace(float x, float y, float w, float h, float glowPeak);
    void clearPlace() { placed_ = false; }
    bool placed() const { return placed_; }
    void drawPlace(ui::Renderer& r) const;

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);

private:
    Shows shows_ = Shows::Waiting;
    bool friendSide_ = true;
    bool standIn_ = false;
    bool resumes_ = false;
    bool placed_ = false;
    float placeX_ = 0.0f, placeY_ = 0.0f, placeW_ = 0.0f, placeH_ = 0.0f, glowPeak_ = 0.0f;
    std::string title_, detail_, note_;
    float clock_ = 0.0f;
    float fade_ = 1.0f;
};

}  // namespace screens
