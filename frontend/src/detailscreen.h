// A game's page, the launch screen: what the app hands it (GameDetail,
// StateChoice) and the screen (DetailScreen). The app fills it in detail.cpp.
// Moved out of screens.h in the restructure (#398); what every screen shares
// is in screens.h.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

// --- The launch screen ------------------------------------------------------

// What the app knows about a game when it opens this screen. Handed over whole
// rather than looked up, so the screen holds no opinion about where any of it
// came from.
struct GameDetail {
    int romId = 0;
    int cardIndex = -1;
    std::string title;
    std::string platform;
    std::string cover;
    // THE BIG ONE, because this screen draws the biggest cover in the product.
    // 340x460 design points is 680x920 real pixels on a 4K panel, and it was
    // being drawn from RomM's 162x216 thumbnail — a 4.2x upscale, the worst
    // anywhere. The backdrop behind it was the same 162 pixels blurred, which
    // is why it came out as mud rather than as the art's own colours.
    std::string coverLarge;
    ui::Color art;
    int64_t sizeBytes = 0;
    // A game on a platform this console cannot play gets the screen and not the
    // Play button, with the reason where the button was. That is the fourth
    // thing `catalog::coverageFor` distinguishes, finally said out loud.
    bool playable = true;
    std::string reason;
    // Kept means downloaded deliberately: it is not part of the cache and
    // eviction may never take it. Unlike the cache, this IS visible — the cache
    // is invisible by decision, a kept game is a promise the person made.
    bool kept = false;
    // ONE OF THIS PERSON'S FAVOURITES (#267): the mark beside the title is
    // filled. It is theirs, so it follows the account, not the console.
    bool favorite = false;
    // THE GAME HAS A MANUAL (#364): RomM has one, or one is on the drive.
    // A small book on the cover says so; R1 opens it.
    bool hasManual = false;
};

// One of the three states on the launch screen (docs/PROJECT.md, "The in-game
// shortcuts, and three states per game"), newest first.
struct StateChoice {
    int id = 0;            // RomM's
    std::string picture;   // RomM path of its picture, empty for none
    std::string when;      // "Today, 8:13 PM"
};

class DetailScreen {
public:
    void open(GameDetail d);
    // CONTINUE FROM: the game's states, arriving after the screen opens because
    // RomM has to be asked. None, and the section is not drawn at all.
    void setStates(std::vector<StateChoice> states);
    // THE GAME'S OWN SAVE, when there is one: "Saved today, 8:17 PM", shown as
    // the Play row's value, because that save is what Play starts from.
    void setSaveWhen(std::string when);
    // What RomM knows of the game, for the line under the title: "1997",
    // "Cave", "1-2 players". Any may be empty. `disc` is "Disc 2 of 4", on a
    // game with several discs only (#162).
    struct Facts { std::string year, maker, players, played, disc; };
    void setFacts(Facts f);
    // RomM HAS ANSWERED: the facts, the save and Continue from fade in
    // together now, rather than each popping in as it arrived.
    void detailsArrived();

    // RETROACHIEVEMENTS (#74), for the person using the console. `show` only
    // when they signed in: for anybody else the page is exactly as it was.
    // While `known` is false the answer is still coming; `none` is
    // RetroAchievements having no set for the game, said greyed, never focused.
    struct Achievements {
        bool show = false;
        bool known = false;
        bool none = false;
        // RetroAchievements could not be asked and nothing was kept from a
        // visit before: not the same as having none, so not said as it.
        bool unavailable = false;
        int unlocked = 0, total = 0;
    };
    // Shown as a cup beside the heart, which opens the list (MMagTech chose
    // it on the TV over a third row, 2026-10-06: the row pushed Continue
    // from to the foot of the screen). The page fades in once the answer is
    // here, as it waits for RomM's.
    void setAchievements(Achievements a);
    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);
    void drawGlass(Ctx& c);

    const GameDetail& game() const { return game_; }
    // The rows are rebuilt too, so "Download and keep" becomes "Remove
    // download" (and back) the moment it happens. They used to be built only
    // on open, so the row went on offering Download for a game already kept
    // until the screen was left and opened again; MMagTech pressed it four
    // times on Air Zonk, 2026-09-26. Focus stays on the same slot, which is
    // the same row with its other label.
    void setKept(bool kept) {
        game_.kept = kept;
        rebuildRows();
        if (slot_ >= static_cast<int>(rows_.size())) slot_ = rows_.empty() ? 0 : static_cast<int>(rows_.size()) - 1;
    }

    // THE PAGE SLIDING ASIDE FOR THE MANUAL (#364), in design points, 0 at
    // rest and minus the canvas's width with the manual over it. Everything
    // on the page moves; the game's art behind it stays, as the ground does
    // in every other move between screens.
    void setSlide(float dx) { slideX_ = dx; }

    // THE MARK BESIDE THE TITLE, filled or not, the moment it is pressed:
    // the console answers at once and the server hears about it after.
    void setFavorite(bool on) { game_.favorite = on; }

    // A refusal, or anything else the person needs to read once. The screen
    // shows it under the actions until they do something else.
    void setNotice(std::string notice) { notice_ = std::move(notice); }

    // PROGRESS BELONGS ON THE ROW THAT STARTED IT — 2026-09-21.
    //
    // MMagTech asked whether a download should be its own window or shown on
    // the normal screen. It was a centred panel over a dimmed screen, and the
    // fact that settled it is that the panel never blocked anything: input
    // still reached the screen underneath, so it was obstruction with no
    // behaviour behind it — the worst of both.
    //
    // Now the row fills. Press Play and the Play row becomes the progress; press
    // Download and keep and that one does. Nothing is covered, nothing is
    // blocked, and the thing that is loading is the thing you pressed.
    //
    // `action` says which row, so that a background download started by Download
    // does not light up Play. `total` of zero means the server did not say how
    // big it is, which is common — the row then says what has arrived and draws
    // no bar, because a progress bar that invents its own total is a lie.
    struct Progress {
        bool active = false;
        Action action = Action::Play;
        int64_t got = 0, total = 0;
        bool unpacking = false;
        // A PS3 game going into the emulator after its download (#171).
        bool installing = false;
    };
    void setProgress(const Progress& p) { progress_ = p; }

private:
    // key()'s moves, without the sound; key() says what happened (#389).
    Result step(Nav n);
    Progress progress_;
    float slideX_ = 0.0f;
    // Where draw() put the small book, for the glass pass.
    float bookX_ = 0, bookY_ = 0;

    struct Row {
        Action action = Action::None;
        std::string label;
        std::string value;   // on the right, as a Settings row's is
    };
    void rebuildRows();
    // Whether row `i` is the one showing the download's progress.
    bool rowBusy(int i) const;

    GameDetail game_;
    std::vector<Row> rows_;
    std::vector<StateChoice> states_;
    std::string saveWhen_;
    Facts facts_;
    // The line, fitted to `width`: if it is too long, the maker is what gets
    // shortened, so the time played at its end is always whole.
    std::string factsLine(ui::TextRenderer& text, float sc, float width) const;
    // Continue from's card size, worked out in draw() with the rest of the
    // layout so the block can be centred as a whole.
    float cardW_ = 0, cardH_ = 0;
    float statesX_ = 0, statesY_ = 0;
    // Focus is in Continue from rather than the rows, and on which state.
    bool inStates_ = false;
    // FOCUS IS ON THE FAVOURITE MARK, Up from the top row (#267). Where
    // draw() put it, for the glass pass, as with the rows.
    bool onFav_ = false;
    float favX_ = 0, favY_ = 0, favS_ = 0;
    Achievements ach_;
    // The cup beside the heart, focused Right from the heart. Where draw()
    // put it, for the glass pass.
    bool onTrophy_ = false;
    float trophyX_ = 0, trophyY_ = 0, trophyW_ = 0, trophyH_ = 0;
    // ONLY WHERE THERE IS SOMETHING TO EARN (MMagTech, 2026-10-06): a game
    // with no set, or one that could not be asked about, has no cup. Beside
    // the heart, its absence moves nothing else on the page.
    bool trophyShown() const {
        return ach_.show && ach_.known && !ach_.none && !ach_.unavailable;
    }
    bool trophyFocusable() const { return trophyShown(); }
    int stateSlot_ = 0;
    std::string notice_;
    // Where draw() put the action column, so the glass pass can put the rows in
    // the same place without computing the layout twice and drifting from it.
    // draw() runs before drawGlass() every frame, which is what makes this safe.
    float rowsX_ = 0, rowsY_ = 0, rowsW_ = 0;
    int slot_ = 0;
    design::Animated focus_;
    design::Animated appear_;
    // RomM's parts of the page (the facts line, the save on Play, Continue
    // from) share one fade, started when they arrive or after kDetailsWait.
    design::Animated details_;
    float waited_ = 0.0f;
    bool detailsIn_ = false;
    bool started_ = false;
};

}  // namespace screens
