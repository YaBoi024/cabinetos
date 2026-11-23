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

// THE SAME DIM OVER A GAME IN ITS OWN EMULATOR (#606). This window mixes the
// dim into its picture as the values stand, but gamescope mixes this window
// over another program's in linear light, where the same black lifts less:
// at 0.6 it left that game's white at 170 of 255 on the A9, not 102. This is
// the opacity there that leaves the picture as dark as `depth` does here.
float overlayDepth(float depth);

// A WAKE IS NOT A CHOICE (#411, docs/decisions/2026-10-27-a-wake-is-not-a-choice.md).
// After the screen comes back from off, the machine comes back from Rest
// (power::justWoke) or a device starts streaming, no press does anything in a
// menu for this long. Measured on the A9: a game started 0.8 s after a press
// lit a dark screen, before the television showed the picture again, and
// streams connecting started the game Home had focused with presses nobody
// made. A starting value, for the television to judge.
constexpr double kWakeSettleSeconds = 3.0;

enum class Level { Awake, Dim, Blank };

const char* name(Level l);

class Watch {
public:
    // `timeScale` shrinks every idle timer, so a test can see a dim in seconds
    // rather than minutes (`--idle-scale 0.01` dims Home at three seconds).
    void setTimeScale(double s) { scale_ = s > 0 ? s : 1.0; }
    void setEnabled(bool on) { enabled_ = on; }
    // Seconds before the screen goes out on a menu; 0 is never.
    void setBlankAfter(double seconds) { blankAfter_ = seconds; }

    // Somebody touched something. Returns TRUE when this press must do nothing
    // in a menu, so the menus can swallow it: the first press on a dark
    // console must not also launch whatever happened to have focus, which
    // nobody could see. On a screen that was off, neither may any press for
    // kWakeSettleSeconds after it.
    bool input(double now);

    // Something other than a press woke the console: a device starting to
    // stream. The screen lifts and the timer starts again, as for a press,
    // and the presses of the next kWakeSettleSeconds do nothing in a menu.
    void woke(double now);

    // Called once a frame. `playing` means a game is running and not paused.
    // Returns the level, and changes it — the caller acts on the edges.
    Level update(double now, bool playing);

    double idleFor(double now) const { return now - last_; }

private:
    bool enabled_ = true;
    double scale_ = 1.0;
    double blankAfter_ = kMenuBlankAfter;
    double last_ = 0.0;
    double settleUntil_ = -1.0;
    bool started_ = false;
    Level level_ = Level::Awake;
};

// --idle-test: the dim, the blank and the wait after a wake, every case, no
// SDL. 0 when all pass.
int test();

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
