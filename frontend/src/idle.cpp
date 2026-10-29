#include "idle.h"

#include "app.h"
#include "prefs.h"
#include "proc.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace idle {

Offset pixelShift(double seconds, float scale, double everySeconds) {
    // A walk over a (2n+1) x (2n+1) square of one-point steps, row by row and
    // back again, so every move is exactly one point and the path never jumps
    // from one side to the other. At two points that is 25 positions, and the
    // whole cycle takes 75 minutes.
    const int n = static_cast<int>(kShiftPoints);
    const int side = n * 2 + 1;
    const long step = static_cast<long>(seconds / (everySeconds > 0 ? everySeconds : 1.0));
    // There and back rather than wrapping, so the last position steps to its
    // neighbour and not across the square.
    // It STARTS IN THE MIDDLE, so a fresh boot and every short capture draw
    // the picture exactly where the design put it.
    const long cycle = static_cast<long>(side) * side;
    long i = (step + cycle / 2) % (cycle * 2 - 2);
    if (i >= cycle) i = cycle * 2 - 2 - i;
    const int row = static_cast<int>(i / side);
    int col = static_cast<int>(i % side);
    if (row % 2 == 1) col = side - 1 - col;
    // Whole device pixels per point, never less than one.
    const int px = std::max(1, static_cast<int>(std::lround(scale)));
    return Offset{(col - n) * px, (row - n) * px};
}

namespace {
int gDim = -1;   // read from the settings on first use
}  // namespace

int dimIndex() {
    if (gDim < 0) {
        const std::string w = prefs::get("dim_after", "5m");
        gDim = kDimCount - 1;
        for (int i = 0; i < kDimCount; ++i)
            if (w == kDimChoices[i].word) gDim = i;
        std::fprintf(stderr, "[idle] dim after %s\n", kDimChoices[gDim].name);
    }
    return gDim;
}

void setDimIndex(int i) {
    if (i < 0 || i >= kDimCount) return;
    gDim = i;
    prefs::set("dim_after", kDimChoices[i].word);
}

double dimSeconds() { return kDimChoices[dimIndex()].seconds; }

const char* name(Level l) {
    switch (l) {
        case Level::Awake: return "awake";
        case Level::Dim: return "dim";
        case Level::Blank: return "blank";
    }
    return "?";
}

bool Watch::input(double now) {
    last_ = now;
    started_ = true;
    if (level_ == Level::Awake) return false;
    level_ = Level::Awake;
    return true;
}

Level Watch::update(double now, bool playing) {
    if (!started_) {
        // Boot counts as somebody having just arrived.
        last_ = now;
        started_ = true;
    }
    if (!enabled_) return level_ = Level::Awake;
    const double idle = (now - last_) / scale_;
    Level want = Level::Awake;
    if (playing) {
        if (idle >= kGameDimAfter) want = Level::Dim;
    } else if (blankAfter_ > 0.0) {
        if (idle >= blankAfter_) want = Level::Blank;
        else if (idle >= dimAfter_) want = Level::Dim;
    }
    // Only ever deepen on a timer. Coming back up is input()'s job, so pausing
    // or unpausing a game cannot light a dark screen by itself.
    if (want > level_) level_ = want;
    return level_;
}

namespace {
// Every request numbered; one worker at a time talks to gamescope, and only
// the latest request is applied (idle.h). Before `first` existed a sleep
// took milliseconds and the race was academic; ending a stream through
// Sunshine's API can take seconds.
std::atomic<unsigned> gAsked{0};
std::mutex gApply;
}  // namespace

void setDisplayAsleep(bool asleep, bool wait, std::function<void()> first) {
    const unsigned mine = ++gAsked;
    std::thread worker([asleep, mine, first = std::move(first)] {
        if (first) first();
        std::lock_guard<std::mutex> lk(gApply);
        if (mine != gAsked) {
            std::fprintf(stderr, "[idle] display %s: a later request came first, not applied\n",
                         asleep ? "asleep" : "awake");
            return;
        }
        const proc::Result r = proc::run(
            {"gamescopectl", "drm_sleep_external_screen", asleep ? "1" : "0"}, 5);
        // Say what was APPLIED, not what was asked for. A setting that did not
        // take has cost this project an evening once already.
        std::fprintf(stderr, "[idle] display %s: %s\n", asleep ? "asleep" : "awake",
                     r.ok() ? "gamescope took it"
                            : "gamescope did not answer; the black layer stands in");
    });
    if (wait) worker.join();
    else worker.detach();
}

}  // namespace idle

int savedScreenOff() {
    const std::string w = prefs::get("screen_off_after", "15m");
    for (int i = 0; i < kScreenOffCount; ++i)
        if (w == kScreenOff[i].word) return i;
    return 1;
}

// ---- The dim over the screen, and the television woken at exit (#398) ----
// Members of the App (app.h), moved out of main.cpp in batch (g).

// ---- The dim, over absolutely everything --------------------------
//
// After the keyboard and the guides, because it is not part of the
// interface; it is the television resting. At full depth it is also
// the blank wherever gamescope cannot put the output to sleep.
void App::drawDim() {
    const float d = dimLayer.value();
    if (d > 0.001f) {
        const float keep = renderer.contentAlpha();
        renderer.setContentAlpha(1.0f);
        renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                               ui::Color::black(d)});
        renderer.setContentAlpha(keep);
    }
}

// Never leave the television asleep behind us: the next thing on it —
// this program restarting, or a person at a console — must be seen.
void App::wakeTvAtExit() {
    if (idleShown == idle::Level::Blank) idle::setDisplayAsleep(false, /*wait=*/true);
}
