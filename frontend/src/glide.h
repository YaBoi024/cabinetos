// A ROW OR A GRID WHOSE THINGS CHANGE WHILE THE PERSON IS LOOKING AT IT (#730)
// glides to how it is now, where it used to change between one frame and the
// next. What kept its place among the others slides from where it was; what
// came, or jumped past another, fades in at its new place; what went, or
// jumped, fades out at the one it had. Home's rows and the Library's tiles.
//
// It says only where each thing is drawn meanwhile. What changed, and where
// the highlight goes, are libraryrefresh.h's and the screens' own.

#pragma once

#include <cstddef>
#include <vector>

#include "motion.h"

namespace glide {

struct Point {
    float x = 0.0f, y = 0.0f;
};

// How a thing is drawn this frame: moved from its place by this much, at this
// strength. A coming one is drawn under what slides, which uncovers it.
struct Look {
    float dx = 0.0f, dy = 0.0f, alpha = 1.0f;
    bool coming = false;
};

class Glide {
public:
    // `from[i]` is where in `was` the i-th thing now is found, -1 for one that
    // was not there. The places are the content's own, whatever it has
    // scrolled to.
    void start(const std::vector<int>& from, const std::vector<Point>& was,
               const std::vector<Point>& now);
    void stop();
    void tick(float dt);
    bool going() const { return clock_.elapsed < clock_.duration; }

    Look look(size_t i) const;
    // Which of `was` fade out at the place they had, and how strong they are.
    const std::vector<int>& leaving() const { return leaving_; }
    float leavingAlpha() const;

private:
    std::vector<Point> offset_;   // per thing now, at the start
    std::vector<bool> coming_;    // per thing now: it fades in
    std::vector<int> leaving_;
    design::Animated clock_ = {.from = 0, .to = 0, .elapsed = 1, .duration = 1, .smooth = true};
};

}  // namespace glide
