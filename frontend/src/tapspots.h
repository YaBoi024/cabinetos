// FORK (handheld): where a screen's items were drawn last frame, so a finger
// can tap the one under it (touchinput.cpp).
//
// A screen clears its spots at the start of its draw, adds one for each item it
// draws, and answers tapAt() from them. They are in canvas points, the space
// everything is drawn in, and only the last frame's: an item that has scrolled
// off or is hidden under a heading has no spot.

#pragma once

#include <algorithm>
#include <vector>

namespace screens {

class TapSpots {
public:
    void clear() { spots_.clear(); }

    // `x`, `y`, `w`, `h`: the item's rectangle. `clipTop`: nothing above it can
    // be tapped, for a list scrolling under a heading or the bar.
    void add(int id, float x, float y, float w, float h, float clipTop = 0.0f) {
        const float top = std::max(y, clipTop);
        if (y + h <= top || w <= 0.0f) return;
        spots_.push_back({id, x, top, w, y + h - top});
    }

    // The id of the item at (px, py), or -1. Later spots win, so an item drawn
    // over another is the one tapped.
    int at(float px, float py) const {
        for (auto it = spots_.rbegin(); it != spots_.rend(); ++it)
            if (px >= it->x && px < it->x + it->w && py >= it->y && py < it->y + it->h)
                return it->id;
        return -1;
    }

private:
    struct Spot {
        int id;
        float x, y, w, h;
    };
    std::vector<Spot> spots_;
};

}  // namespace screens
