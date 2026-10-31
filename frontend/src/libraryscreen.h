// The Library: a tile (Tile) and the screen of tiles (LibraryScreen). A tile
// is also built by library.cpp, which loads them. Moved out of screens.h in
// the restructure (#398); what every screen shares is in screens.h.

#pragma once

#include <string>
#include <vector>

#include "screens.h"
#include "tapspots.h"

namespace screens {

// --- The Library ------------------------------------------------------------

// One tile: a platform, or a collection.
//
// A PLATFORM THIS CONSOLE CANNOT PLAY STILL GETS A TILE. docs/PROJECT.md is
// explicit that the answer is "neither show everything nor hide quietly — it is
// to KNOW, per platform, and to say so", and until now the saying-so went to
// stderr. Somebody who owns Switch games and sees none will reasonably conclude
// the scan failed; somebody who sees "no core for this system" has been told
// the truth. So an unplayable tile is drawn, dimmed, carrying its reason where
// a playable one carries its count, and it cannot be entered.
struct Tile {
    int id = 0;               // platform id, or collection id
    // What lists its games, and so what the tile IS: "platform_ids=5",
    // "collection_id=2", "smart_collection_id=2". The id alone is not: RomM
    // counts platforms, collections and smart collections separately, so
    // platform 2, collection 2 and smart collection 2 are three tiles (#352).
    // The Arcade tile names every arcade platform, "platform_ids=45&
    // platform_ids=55", and its `id` is the first of them (#611).
    std::string query;
    std::string title;        // "Arcade", "SHMUP"
    // The Arcade tile: its grid's line names each game's folder (#611).
    bool namesFolder = false;
    std::string detail;       // "141 games", or why it cannot be played
    std::string cover;        // a cover path for the tile's artwork
    bool enterable = true;
    // The heading the tile sits under, empty for none. Collections only: a
    // person's own come first with none, then each other person's shared ones
    // under "From <their name>" (#352). A group starts a new row.
    std::string group;
    std::vector<int> cards;   // indices into Ctx::cards
    // When `cards` was last read from the server, in SDL_GetTicks
    // milliseconds; 0 for never. A grid reopened a while later is read again
    // (App::openTile, #387).
    unsigned long long loadedAtMs = 0;
    // HOW FAR THE SERVER'S LIST HAS COME, so a big grid left before its
    // later pages all arrived carries on from there when opened again
    // (#471). `fetched` counts every game the server sent, kept or not: a
    // collection's games this console cannot play are left out of `cards`,
    // and the next page starts after them. `total` is the server's count.
    // `readId` names this reading, new each time the first page is read
    // (loadTileGames), so a page of an older reading or of a library since
    // replaced is never added to it (App::drainGridPages).
    size_t fetched = 0;
    size_t total = 0;
    unsigned readId = 0;
    design::Animated focus;
};

class LibraryScreen {
public:
    void build(std::vector<Tile> platforms, std::vector<Tile> collections);

    // FOCUS LANDS ON THE SWITCHER THE FIRST TIME AND ONLY THE FIRST TIME.
    // Re-entering from a pushed screen must leave focus where back-navigation
    // put it. The reference implementation got this wrong first: forcing focus
    // on every appearance yanked it away whenever somebody came back.
    void enter();
    // Arrived already, with no fade of its own: a top-bar dissolve is doing
    // the arriving, and two fades at once dip through the background.
    void settleArrival() { appear_.settle(1.0f); }

    // Puts focus on a given tile, for a capture. The unplayable systems sort
    // last, so without this the only part of this screen a screenshot can ever
    // show is the part that works.
    void focusTile(int index);

    void tick(float dt);
    Result key(Nav n);
    // FORK (handheld): a finger on the screen. True when it landed on a pill or
    // a tile, with what pressing that would do in `out`. See tapspots.h.
    bool tapAt(float x, float y, Result* out);
    void draw(Ctx& c);
    // Glass reads the scene through itself, so anything frosted has to be drawn
    // AFTER Renderer::presentScene. That is a property of the renderer rather
    // than of these screens, and the split is the same one Home already makes
    // for the hero's band.
    void drawGlass(Ctx& c);

    // THE TRIGGERS JUMP PERSON TO PERSON in Collections, as they jump letter
    // to letter in a game grid (MMagTech, 2026-10-12, #352): `dir` -1 or +1,
    // to the first tile of the previous or next group. Back from inside a
    // group goes to its own start first, as jumpLetter does. False, with the
    // edge sound, when there is no group that way (and on Platforms, which
    // has none).
    bool jumpGroup(int dir);

    // Which tab is showing, so the app can resolve an OpenTile.
    const std::vector<Tile>& visible() const {
        return tab_ == 0 ? platforms_ : collections_;
    }
    // Platforms or collections, for the same reason: the two are fetched from
    // different endpoints, and OpenTile has to know which one it is resolving.
    int tab() const { return tab_; }

    // What a tile learned the first time somebody walked into it — its cover,
    // and a count corrected to what is actually playable here.
    //
    // WHY THE SCREEN HAS TO BE TOLD. It holds its OWN copies of the tiles, so
    // the app mutating the vectors it built them from changes nothing on
    // screen. That is fine while a tile is fixed at boot and wrong the moment
    // a tile can learn something — open question 28, where a platform's games
    // are not fetched until it is opened.
    // `query` is the tile's own (Tile::query), never a bare id.
    void learnedTile(const std::string& query, const std::string& detail,
                     const std::string& cover);

    // The cover of whatever focus is on, so the app can keep lighting the room
    // with it. The backdrop follows focus on Home and it follows focus here for
    // the same reason and to the same rule — and the continuity is most of what
    // makes arriving at this screen read as a move rather than a cut.
    //
    // On the switcher row it answers the first tile rather than nothing: the
    // grid below is still what the screen is about, and a backdrop that blanked
    // whenever focus touched a pill would flicker on the way past.
    std::string focusedCover() const;

private:
    // Where every tile of the visible tab sits, rows and headings. Rows are
    // the grid's own; a group starts a new one. `y` is from the grid's top.
    struct Layout {
        std::vector<int> rowOf, colOf;           // per tile
        std::vector<int> rowFirst, rowCount;     // per row
        std::vector<float> rowY;                 // per row
        std::vector<bool> rowHeaded;             // a heading sits above it
        std::vector<std::pair<float, std::string>> headings;   // y, text
        float bottom = 0.0f;                     // under the last row
    };
    Layout layout(float headingH) const;
    int columns() const;
    float tileWidth() const;
    void moveFocus(int dx, int dy);
    void focusOn(int row, int slot);   // FORK (handheld)
    TapSpots taps_;                    // FORK (handheld)

    std::vector<Tile> platforms_;
    std::vector<Tile> collections_;
    int tab_ = 0;              // 0 platforms, 1 collections
    bool entered_ = false;     // has this screen ever been arrived at
    // Row 0 is the switcher; rows 1.. are the tile grid.
    int row_ = 0;
    int slot_ = 0;
    int rememberedTileSlot_ = 0;
    design::Animated appear_;
    design::Animated pillFocus_[2];
    design::Animated scroll_;
};

}  // namespace screens
