// Search's screen (SearchScreen). The app owns the keyboard, the debounce and
// the asking (search.cpp); this shows what it is handed. Moved out of
// screens.h in the restructure (#398); what every screen shares is in
// screens.h.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

// --- Search -----------------------------------------------------------------

// MMagTech, 2026-09-21: *"we also need to implement the search as well."* The
// bar has said "Search" since the bar existed and pressing it printed a line to
// stderr saying it was not built.
//
// IT ASKS THE SERVER — changed 2026-09-22, open question 28.
//
// It used to filter a catalogue held in memory, on the grounds that the whole
// library was loaded at boot anyway so a substring match was free. That was
// true, and the reason it was true is exactly what open question 28 removed:
// boot no longer fetches sixteen hundred games, so there is nothing here to
// filter. `/api/roms` takes `search_term`, checked against the live server.
//
// THE OLD COMMENT ALSO ARGUED THAT ASKING THE SERVER WOULD BE "THE ONE SCREEN
// IN THE PRODUCT THAT STOPS WORKING OFFLINE", AND THAT WAS WRONG. Nothing is
// kept between boots but three config files, so an offline boot has no
// catalogue in memory either and this screen has always filtered an empty
// vector with no server. Nobody noticed because the startup screen waits for
// the server and Search is never reached. See open question 28's offline
// section, and 29 for what offline actually means here.
//
// THE OTHER HALF OF THAT COMMENT STANDS: this needs debouncing, which the
// substring filter did not. The keyboard is docked and the results are live,
// so without it every keystroke is a request.
//
// THE KEYBOARD IS DOCKED AND THE RESULTS ARE LIVE. Typing blind and pressing
// Done to find out what you got is the thing that makes console search
// miserable; see ui::Keyboard::Config::dockedBottom for what that cost.
class SearchScreen {
public:
    void open();

    // What the person has typed. Recorded immediately so the screen can say
    // what it is doing; the results arrive separately and later.
    void setQuery(const std::string& q);

    // The answer, once the server has given one. `total` is what the server
    // says matched, which is not the same as how many came back — a search
    // that matched four hundred shows the first page and says so.
    void setResults(const std::string& forQuery, std::vector<int> results, int total);

    // Three states that must never read as one, which is the rule this file
    // keeps running into: nothing typed yet, waiting for the server, and the
    // server answered with nothing. A fourth — the server could not be asked —
    // is `failed`, and it is NOT the same as "no matches".
    enum class State { Empty, Waiting, Results, NoMatches, Failed };
    void setWaiting();
    void setFailed(const std::string& why);

    // Where the results have to stop, in canvas points: the top of the docked
    // keyboard, where it is this frame while it slides. The covers are sized
    // to the room that leaves rather than to a constant, so the layout
    // survives the keyboard changing height, and they grow and shrink with
    // the slide.
    void setResultsBottom(float y) { resultsBottom_ = y; }
    // Whether the keyboard is up over this screen; the heading needs to know.
    void setKeysUp(bool up) { keysUp_ = up; }

    // Whether focus is in the results rather than in the keyboard. The app owns
    // this because the app owns the keyboard.
    void setFocused(bool on) { focused_ = on; }
    bool focused() const { return focused_; }

    void tick(float dt, Ctx& c);
    Result key(Nav n);
    void draw(Ctx& c);

    int focusedCard() const {
        return (slot_ >= 0 && slot_ < static_cast<int>(results_.size())) ? results_[slot_] : -1;
    }
    size_t resultCount() const { return results_.size(); }

private:
    std::string query_;
    std::vector<int> results_;
    // The query the results in hand actually answer, which lags `query_` by
    // one round trip. Drawing results against a query somebody has since typed
    // past is how a search screen comes to show the wrong thing confidently.
    std::string resultsFor_;
    int total_ = 0;
    State state_ = State::Empty;
    std::string failure_;
    int slot_ = 0;
    bool focused_ = false;
    float resultsBottom_ = ui::kCanvasHeight;
    bool keysUp_ = true;
    design::Animated scroll_;
};

}  // namespace screens
