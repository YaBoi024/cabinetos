// FORK (handheld): the battery's charge limit, for a Steam Deck.
//
// The Deck's controller keeps the battery from charging past a level it is
// given, in `max_battery_charge_level` on the `steamdeck_hwmon` device (0 is no
// limit). Held at 80% a battery lasts far more charge cycles. Root owns the
// file and the console does not run as root, so the write is done by
// cabinetos-charge-limit@<percent>.service (system_files/usr/libexec/
// cabinetos-charge-limit), which a polkit rule lets the console start.
//
// The setting is the "Battery charge limit" row under Display and Sound. It is
// shown only where the file exists. The saved choice is written again at every
// start, in case the controller forgets it across a restart.

#pragma once

namespace chargelimit {

struct Choice {
    const char* name;
    const char* word;   // as saved in config/settings.json
    int percent;        // 100 is no limit
};
static constexpr Choice kChoices[] = {
    {"80%", "80", 80},
    {"90%", "90", 90},
    {"No limit", "100", 100},
};
constexpr int kCount = sizeof kChoices / sizeof kChoices[0];

// Whether this machine has the Deck's charge-limit file.
bool available();

// The saved choice's index (default: no limit).
int savedIndex();

// Asks for choice `i`, saves it, and returns at once: the write happens on a
// worker. Safe to call on a machine without the file (it does nothing).
void set(int i);

// Applies the saved choice, once per run. Called at start.
void applySaved();

}  // namespace chargelimit
