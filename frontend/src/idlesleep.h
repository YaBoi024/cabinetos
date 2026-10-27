// FORK (handheld): put the machine to sleep when nobody is using it and it is
// running on its battery.
//
// The screen already dims and blanks on its own (idle.h) and a blanked
// handheld still burns its battery for hours. This is the step after: the
// "Sleep when idle" setting, in Settings under Display and Sound. It acts only
// when ALL of these hold:
//   - the machine has a battery and is NOT plugged in (a console on a TV has
//     no mains supply entry to lose, so it never sleeps itself);
//   - no game is loaded (a paused game is left alone);
//   - nothing has been touched for the chosen time, and at least as long as
//     the screen-off time, so the screen always goes out first.
// Sleeping is logind's, through power::act(Rest): the same path as the Power
// menu's Rest, which leaves a game properly and takes the delay lock.

#pragma once

namespace idlesleep {

struct Choice {
    const char* name;
    const char* word;   // as saved in config/settings.json
    double seconds;     // 0 is never
};
static constexpr Choice kChoices[] = {
    {"Never", "never", 0.0},
    {"20 minutes", "20m", 20 * 60.0},
    {"30 minutes", "30m", 30 * 60.0},
    {"1 hour", "1h", 60 * 60.0},
};
constexpr int kCount = sizeof kChoices / sizeof kChoices[0];

// Reads the saved choice (default 30 minutes). Call once at start.
void load();
int index();
void set(int i);   // remembers it and saves it
double seconds();

// True when the machine has a mains supply entry and none is online. False on
// a machine with no such entry, and when the answer cannot be read.
bool onBattery();

}  // namespace idlesleep
