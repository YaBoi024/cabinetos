// A grid of games (GridScreen): one system's or one collection's games, and
// the letter index. Moved out of screens.h in the restructure (#398); what
// every screen shares is in screens.h.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

// --- A grid of games --------------------------------------------------------

class GridScreen {
public:
    // `title` is drawn as ordinary content at the top of the scroll view,
    // never as chrome. `names` is what the focused game's line names after
    // it (drawFocusedLine): the system in a collection's grid, whose title
    // names no system (#579), the folder in the Arcade tile's (#611), and
    // nothing in any other platform's. `all` is the card table the indices
    // point into — the screen needs the NAMES once, at open, to build its
    // letter index, and holding a reference to the table would outlive the
    // call.
    void open(std::string title, LineNames names, std::vector<int> cards,
              const std::vector<design::Card>& all);

    // More of the same grid, arriving after it was opened.
    //
    // A GRID IS PROPORTIONAL TO ITS OWN PLATFORM and boot is not — the fault
    // open question 28 fixed one level down. About 1 ms a game, so 141 is
    // fine and a full MAME set in one platform is ten seconds of staring at
    // nothing. The first page is drawn and the rest arrive behind it.
    //
    // APPENDED, NEVER RE-SORTED. RomM returns roms in title order, so pages
    // concatenate already in order; re-sorting here would move every card out
    // from under the slot the person is focused on.
    void append(const std::vector<int>& more, const std::vector<design::Card>& all);

    // Whether more is still coming, so the heading can say so rather than
    // showing a count that is about to change.
    void setLoadingMore(bool on) { loadingMore_ = on; }

    // FAST NAVIGATION BY LETTER — new 2026-09-21.
    //
    // MMagTech: *"when holding down or maybe r2 and l2 it should be fast nav
    // with the letters showing on the right side of the screen."* A platform
    // with 141 games is 21 rows, and reaching the S's a row at a time is the
    // kind of thing that makes a person stop browsing their own library.
    //
    // The TRIGGERS rather than a held direction, and both were on the table.
    // A held d-pad already means "keep moving one at a time" and giving it a
    // second meaning after some interval makes the first one feel broken while
    // you wait for it. The triggers mean nothing else anywhere in this product.
    //
    // `dir` is -1 or +1. Moves focus to the first game of the previous or next
    // letter, and shows the index for as long as somebody is using it.
    // False at either end of the alphabet, which also ends a held trigger's
    // repeat rather than thudding against the wall every 160 ms.
    bool jumpLetter(int dir);

    // The Ctx::cards index focus is on, or -1. The app turns it into a cover
    // for the backdrop; this screen holds indices and no opinion about art.
    int focusedCard() const {
        return (slot_ >= 0 && slot_ < static_cast<int>(cards_.size())) ? cards_[slot_] : -1;
    }
    void tick(float dt, Ctx& c);
    Result key(Nav n);
    void draw(Ctx& c);
    void drawGlass(Ctx& c);

private:
    bool loadingMore_ = false;
    int columns() const;
    float coverWidth() const;

    int letterOf(int slot) const;
    // The letter index, drawn on its own: it is not part of the heading and
    // must not go when the heading scrolls away (see drawGlass).
    void drawIndex(Ctx& c);

    std::string title_;
    LineNames names_ = LineNames::Nothing;
    std::vector<int> cards_;
    // The distinct initials, in order, and the first slot of each. Built once
    // at open: a 141-game platform is scanned once rather than per keypress.
    std::vector<char> letters_;
    std::vector<int> letterFirst_;
    int slot_ = 0;
    design::Animated appear_;
    design::Animated scroll_;
    // The index fades in when it is used and out when it is not. It is a
    // navigation aid, not chrome, and a permanent alphabet down the side of a
    // screen of artwork is a menu bar nobody asked for.
    design::Animated index_;
    // Seconds the index stays up after the last jump, before it starts to go.
    // Separate from the animation so a run of jumps holds it steady instead of
    // restarting a fade that has not begun.
    float indexHold_ = 0.0f;
    // WHERE THE WAVE IS, as a position along the letters (2.0 is the third),
    // gliding from one letter to the next rather than snapping, so a held
    // trigger reads as a swell rolling down the alphabet.
    design::Animated wave_;
};

}  // namespace screens
