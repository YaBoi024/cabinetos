// FORK (phones): a list that follows the finger, as a phone's lists do.
//
// The screens are driven by focus, and the list scrolls to keep the focused
// item in view. A finger on the Library or a game grid moves the list itself
// instead: the content goes where the finger goes, and on after a flick,
// slowing to a stop (touchinput.cpp). While it does, focus does not pull the
// list back. The first press of the pad or a key after that puts focus on the
// first item in view and does nothing else, so focus never jumps to something
// out of sight. A tap opens what is under the finger, as before.

#pragma once

#include <algorithm>

namespace screens {

struct TouchScroll {
    bool free = false;    // the finger has the list
    float y = 0.0f;       // the list's offset while it does, in canvas points
    float max = 0.0f;     // how far it can go, from the last frame drawn

    // `from` is where the list is now; `dy` how far the finger went, down positive.
    // False at either end, which stops a flick.
    bool drag(float from, float dy) {
        if (!free) {
            free = true;
            y = from;
        }
        const float was = y;
        y = std::clamp(y - dy, 0.0f, std::max(0.0f, max));
        return y != was || dy == 0.0f;
    }
};

}  // namespace screens
