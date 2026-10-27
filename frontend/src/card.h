// A game's card as the shelves, the grids and Search hold it (design::Card),
// and the two helpers that read cards: hasTwin and colorForTitle. Moved out of
// design.h in the restructure (#398).

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "motion.h"
#include "ui.h"

namespace design {

// --- A card -----------------------------------------------------------------

struct Card {
    // The RomM ROM id, and the only safe way to match a card to anything else.
    // Titles collide: "Altered Beast" is a Game & Watch entry AND a Genesis one
    // in the reference library, so matching Recent to the library by name can
    // show the wrong platform's cover for the game that was actually played.
    int id = 0;
    ui::Color art;        // shown until the cover arrives, and if it never does
    std::string title;
    // RomM's sort key, empty when the server did not give one (romm_types.h).
    std::string sortKey;
    // A local path in the sample library, a RomM cover path with live data.
    // Empty means there is no art, which is a normal state and not a failure:
    // arcade sets often have none, and Game & Watch has none at all.
    //
    // `cover` is the thumbnail and `coverLarge` is the 810x1080 original —
    // see romm_types.h for why both exist and what they measure. They are
    // separate cache keys, so asking for the large one where it matters does
    // not evict the shelf's thumbnails. With the sample library and with a
    // game the server has no art for, the two are the same string.
    std::string cover;
    std::string coverLarge;
    // What it runs on, for the one line under a shelf heading that names the
    // focused game. Titles collide across platforms — "Altered Beast" is a Game
    // & Watch entry AND a Genesis one — so the name alone is not an answer.
    std::string platform;
    // ON THE MACHINE AND PROMISED TO STAY. Not "is it cached" — the cache is
    // invisible by decision, and pressing Play fetches a game and says nothing
    // about it. This is the deliberate act: somebody chose to keep this game,
    // and eviction may never take it.
    //
    // Cached on the card rather than asked per frame: `cache::isKeptBy` is a
    // stat() on the filesystem, and twelve covers on screen at sixty frames a
    // second is seven hundred stats a second to draw a dot.
    bool kept = false;
    // WHY THIS GAME CANNOT BE PLAYED HERE, when its platform can: a Wii game
    // that needs a Wii Remote nobody has paired. Empty for every playable
    // game. Such a card shows greyed, and the line that names it says why
    // (screens::drawFocusedLine); it opens onto the game's screen, which says
    // it again and offers no Play.
    std::string unavailable;
    // A WII GAME: the one kind a Wii Remote starts (App::refuseStartWithoutController).
    // Every other game's line says "Needs a controller" while only Remotes
    // are on (screens::drawFocusedLine).
    bool wii = false;
    // A WII GAME THAT NEEDS A REMOTE: it takes neither a Classic Controller
    // nor a GameCube pad (wii::padDevice). Its line says "Needs a Wii Remote"
    // while no Remote is on.
    bool needsRemote = false;
    // RomM's platform id, and RomM's tags joined for the heading
    // (romm_types.h, `tagLabel`); empty for a game RomM read no tags for.
    int platformId = 0;
    std::string tagLabel;
    Animated focus;
    Animated press;
};

// THE SAME GAME TWICE IN ONE LIST (#383, PROJECT.md question 48): another
// card in `list` with this card's name on this card's system. Only then does
// a heading show the card's tags after its name; two copies side by side are
// the one case where the name alone does not say which is which. Games of
// different systems never count: the system already tells them apart.
//
// A card with no tags returns at once, which is most of any library, so the
// walk over the list is paid only for a card that has something to show.
inline bool hasTwin(const std::vector<Card>& pool, const std::vector<int>& list, int idx) {
    if (idx < 0 || idx >= static_cast<int>(pool.size())) return false;
    const Card& me = pool[static_cast<size_t>(idx)];
    if (me.tagLabel.empty() || me.platformId == 0) return false;
    for (const int other : list) {
        if (other == idx || other < 0 || other >= static_cast<int>(pool.size())) continue;
        const Card& c = pool[static_cast<size_t>(other)];
        if (c.platformId == me.platformId && c.title == me.title) return true;
    }
    return false;
}

// A stable colour for a card with no art, from its title. Better than one grey
// for everything: a shelf of coverless games stays distinguishable, and the
// same game is the same colour every time the library is opened.
inline ui::Color colorForTitle(const std::string& title) {
    uint32_t h = 2166136261u;
    for (unsigned char c : title) { h ^= c; h *= 16777619u; }
    // Fixed saturation and value, hue from the hash: keeps every generated
    // colour inside the design system's range instead of producing mud.
    const float hue = static_cast<float>(h % 360u);
    const float s = 0.45f, v = 0.62f;
    const float c2 = v * s;
    const float x = c2 * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float m = v - c2;
    float r = 0, g = 0, b = 0;
    if (hue < 60)       { r = c2; g = x; }
    else if (hue < 120) { r = x; g = c2; }
    else if (hue < 180) { g = c2; b = x; }
    else if (hue < 240) { g = x; b = c2; }
    else if (hue < 300) { r = x; b = c2; }
    else                { r = c2; b = x; }
    return ui::Color{r + m, g + m, b + m, 1.0f};
}

}  // namespace design
