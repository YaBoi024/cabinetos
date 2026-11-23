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

float overlayDepth(float depth) {
    // What this window keeps of the picture, as the light it stands for
    // (the sRGB curve), is what gamescope must keep.
    const float keep = 1.0f - depth;
    const float light = keep <= 0.04045f ? keep / 12.92f
                                         : std::pow((keep + 0.055f) / 1.055f, 2.4f);
    return 1.0f - light;
}

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
    if (level_ == Level::Awake) return now < settleUntil_;
    // Back from off, the television takes a moment to show the picture, and
    // a press until it does is a press nobody could aim. A dim needs no
    // wait: the picture was there under it all along.
    if (level_ == Level::Blank) settleUntil_ = now + kWakeSettleSeconds;
    level_ = Level::Awake;
    return true;
}

void Watch::woke(double now) {
    input(now);
    settleUntil_ = now + kWakeSettleSeconds;
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
        else if (idle >= kMenuDimAfter) want = Level::Dim;
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

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    const double dim = kMenuDimAfter, off = kMenuBlankAfter, settle = kWakeSettleSeconds;
    {
        Watch w;
        w.update(0.0, false);
        check(!w.input(10.0), "an awake screen takes every press");
        check(w.update(10.0 + dim, false) == Level::Dim, "a menu dims after 5 minutes");
        check(w.input(10.0 + dim), "the press that lights a dim screen does nothing");
        check(!w.input(10.2 + dim), "and the next one acts: the picture was there");
    }
    {
        Watch w;
        w.update(0.0, false);
        check(w.update(off, false) == Level::Blank, "a menu goes off at the setting");
        check(w.input(off + 1.0), "the press that lights a dark screen does nothing");
        check(w.input(off + 1.8), "nor one 0.8 s later, before the picture is back (#411)");
        check(w.input(off + 0.9 + settle), "nor one just inside the wait");
        check(!w.input(off + 1.1 + settle), "after the wait a press acts");
        check(w.update(off + 1.1 + settle, false) == Level::Awake,
              "and a press in the wait kept the screen awake");
    }
    {
        Watch w;
        w.update(0.0, false);
        w.update(off, false);
        w.woke(off + 1.0);
        check(w.update(off + 1.0, false) == Level::Awake, "a stream starting lights a dark screen");
        check(w.input(off + 2.0), "and its presses do nothing for the wait");
        check(!w.input(off + 1.1 + settle), "then act");
    }
    {
        Watch w;
        w.update(0.0, false);
        w.woke(100.0);
        check(w.input(100.5), "a stream starting on a lit screen waits too");
        check(w.update(100.0 + dim - 1.0, false) == Level::Awake,
              "and counts as somebody there for the timer");
    }
    {
        Watch w;
        w.update(0.0, true);
        check(w.update(kGameDimAfter, true) == Level::Dim, "a game in play dims at 20 minutes");
        check(w.update(kGameDimAfter + off, true) == Level::Dim, "and never goes off");
    }
    {
        // gamescope's mix of the overlay, in linear light, back to sRGB.
        const float light = 1.0f - overlayDepth(kDimDepth);
        const float shown = 1.055f * std::pow(light, 1.0f / 2.4f) - 0.055f;
        check(std::fabs(shown - (1.0f - kDimDepth)) < 0.002f,
              "the dim over a game in its own emulator leaves white at 40%, as everywhere");
        check(overlayDepth(0.0f) == 0.0f, "and no dim is no dim");
    }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
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
// the blank wherever gamescope cannot put the output to sleep. ON THE
// OVERLAY, over a game in its own emulator, it is drawn deeper so it looks
// the same (idle::overlayDepth).
void App::drawDim(bool onOverlay) {
    const float d = dimLayer.value();
    if (d > 0.001f) {
        const float keep = renderer.contentAlpha();
        renderer.setContentAlpha(1.0f);
        renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                               ui::Color::black(onOverlay ? idle::overlayDepth(d) : d)});
        renderer.setContentAlpha(keep);
    }
}

// Never leave the television asleep behind us: the next thing on it —
// this program restarting, or a person at a console — must be seen.
void App::wakeTvAtExit() {
    if (idleShown == idle::Level::Blank) idle::setDisplayAsleep(false, /*wait=*/true);
}
