// What the console does when nobody is using it — docs/PROJECT.md, open
// question 10b.
//
// BURN-IN IS THE REASON THIS EXISTS. Until 2026-09-22 the console had no idle
// handling of any kind: measured on the A9, the panel was still lit after
// nearly eleven hours on Home, with the top bar, the account chip and the shelf
// headers bright and static in fixed positions. That is the burn-in shape
// exactly, on somebody's television.
//
// Two mechanisms, in the order the question recorded and for its reasons:
//
//   1. PIXEL SHIFT, always. The whole picture walks a few points round a small
//      square, one point every few minutes. It needs no idle detection, so it
//      runs in menus AND in games — including a paused game, whose frozen HUD
//      is the worst case because nothing else moves it.
//
//   2. DIM, THEN BLANK, on idle. The dim is a black layer over everything we
//      draw. The blank is gamescope putting the output to sleep, which is what
//      lets the set itself go to standby. It is NOT the sysfs `dpms` node the
//      question once named: that file is read-only (0444), measured on the A9.
//      `gamescopectl drm_sleep_external_screen 1` is the route, because
//      gamescope is the DRM master and owns the connector — tried on the LG,
//      2026-09-22, and the set went dark and came back by itself.
//
// A screensaver is deliberately absent. It keeps the panel lit, which makes it
// the weakest of the three against burn-in.

#pragma once

#include <functional>

namespace idle {

// --- Pixel shift -------------------------------------------------------------

// How far, in design points, the picture may move from where it belongs. Two
// points is four device pixels on a 4K set: below anything a person notices
// from a sofa, and the edge it uncovers is inside every television's overscan.
// A starting value, like every number here.
constexpr float kShiftPoints = 2.0f;
// Seconds between one-point steps. Slow enough that a step is never seen
// happening; fast enough that nothing stays on one pixel for more than a few
// minutes out of the cycle.
constexpr double kShiftEverySeconds = 180.0;

// Where the picture sits now, in whole DEVICE pixels — whole, because a
// half-pixel offset resamples every glyph and the text goes soft. `scale` is
// device pixels per design point.
struct Offset {
    int dx = 0, dy = 0;
};
Offset pixelShift(double seconds, float scale, double everySeconds = kShiftEverySeconds);

// --- Dim and blank -----------------------------------------------------------

// NOT PLAYING: Home, the Library, a platform grid, and a PAUSED GAME — a paused
// game is a menu over a frozen picture, and a frozen HUD is the case the
// question singled out as worse than a menu.
//
// THE BLANK IS A SETTING NOW, "Turn off screen after" (docs/SETTINGS.md,
// Display and Sound): 10, 15 or 30 minutes. THE DIM IS FIXED AT 5 MINUTES,
// whatever the choice. It was a third of the choice for a day, which gave
// times like 3 min 20 s; MMagTech: "the numbers seem weirdly determined". One
// rule a person can predict, and an early dim is the better burn-in guard.
//
// FORK (handheld): THE DIM HAS A CHOICE OF ITS OWN, "Dim screen after": 2, 3 or
// 5 minutes, 5 to start. A handheld in the hand wants the panel down sooner
// than a television across the room. Still one rule a person can read off a
// row, and still always earlier than the blank (10 minutes at the soonest).
constexpr double kMenuDimAfter = 5 * 60.0;
constexpr double kMenuBlankAfter = 15 * 60.0;   // the starting value

// PLAYING, UNPAUSED. A game running is not idle just because nothing is
// pressed — an attract loop, a cut-scene, somebody thinking about a puzzle — so
// the screen never goes out under a running game. It does dim, much later,
// because a game left running overnight is still a static HUD for hours.
constexpr double kGameDimAfter = 20 * 60.0;

// How dark the dim is: black at this opacity over everything. 0.6 leaves the
// picture at 40% — clearly asleep, clearly still there. Whether this reads as
// "resting" or as "broken" is the question only the television can answer.
constexpr float kDimDepth = 0.6f;
constexpr float kDimFadeSeconds = 2.0f;    // going down is unhurried
constexpr float kWakeFadeSeconds = 0.25f;  // coming back answers the press

enum class Level { Awake, Dim, Blank };

// FORK (handheld): the choices for the dim in the menus, saved as the word in
// config/settings.json. A saved word this list does not know is 5 minutes.
struct DimChoice { const char* name; const char* word; double seconds; };
static constexpr DimChoice kDimChoices[] = {
    {"2 minutes", "2m", 2 * 60.0}, {"3 minutes", "3m", 3 * 60.0}, {"5 minutes", "5m", 5 * 60.0},
};
constexpr int kDimCount = sizeof kDimChoices / sizeof kDimChoices[0];
int dimIndex();
void setDimIndex(int i);   // remembers it and saves it
double dimSeconds();

const char* name(Level l);

class Watch {
public:
    // `timeScale` shrinks every idle timer, so a test can see a dim in seconds
    // rather than minutes (`--idle-scale 0.01` dims Home at three seconds).
    void setTimeScale(double s) { scale_ = s > 0 ? s : 1.0; }
    void setEnabled(bool on) { enabled_ = on; }
    // Seconds before the screen goes out on a menu; 0 is never.
    void setBlankAfter(double seconds) { blankAfter_ = seconds; }
    // Seconds before the screen dims on a menu (kDimChoices).
    void setDimAfter(double seconds) { dimAfter_ = seconds; }

    // Somebody touched something. Returns TRUE when this press did nothing but
    // wake the screen, so the menus can swallow it: the first press on a dark
    // console must not also launch whatever happened to have focus, which
    // nobody could see.
    bool input(double now);

    // Called once a frame. `playing` means a game is running and not paused.
    // Returns the level, and changes it — the caller acts on the edges.
    Level update(double now, bool playing);

    double idleFor(double now) const { return now - last_; }

private:
    bool enabled_ = true;
    double scale_ = 1.0;
    double blankAfter_ = kMenuBlankAfter;
    double dimAfter_ = kMenuDimAfter;
    double last_ = 0.0;
    bool started_ = false;
    Level level_ = Level::Awake;
};

// Puts the television's input to sleep, or wakes it, through gamescope. On a
// worker, because it runs a program and the frame loop must not wait on one.
// Outside gamescope — the VM under cage, an offscreen capture — it fails
// harmlessly, and the black layer the dim already draws is the blank instead.
// `wait` blocks until gamescope has answered, for the one caller that cannot
// leave it to a worker: the program exiting with the screen still asleep.
// `first` runs on the same worker before gamescope is asked: a Remote Play
// session ended before the screen goes out (#294). THE LAST REQUEST WINS: a
// press while `first` runs asks for a wake, and the sleep queued behind it
// then does nothing.
void setDisplayAsleep(bool asleep, bool wait = false, std::function<void()> first = nullptr);

// A stick at rest is not somebody being there. Pads drift, and a drifting
// stick counted as input would hold the console awake for ever — the same
// mistake `ds-inhibit` exists to stop the rest of Linux making.
constexpr int kAxisDeadzone = 12000;   // of 32767

}  // namespace idle

// TURN OFF SCREEN AFTER, docs/SETTINGS.md, Display and Sound. Saved as the word in
// config/settings.json; the frame loop hands the seconds to idle::Watch.
// The starting value is 15 minutes, what the console did before there
// was a choice. NO "NEVER" AND NO HOUR: both were built and dropped the
// same day, on MMagTech's word, because on an OLED a lit menu is burn-in
// and half an hour is long enough. A saved word this list does not know
// is 15. At file scope because the startup screen's wait uses it too.
struct ScreenOff { const char* name; const char* word; double seconds; };
static constexpr ScreenOff kScreenOff[] = {
    {"10 minutes", "10m", 10 * 60.0}, {"15 minutes", "15m", 15 * 60.0},
    {"30 minutes", "30m", 30 * 60.0},
};
constexpr int kScreenOffCount = sizeof kScreenOff / sizeof kScreenOff[0];
int savedScreenOff();
