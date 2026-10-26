// Each controller's battery, for the row on Home and the low-battery notice
// (issue #293, decided with MMagTech 2026-10-08; docs/PROJECT.md question 40).
//
// ONE RULE FOR EVERY PAD, first answer wins:
//   1. The kernel's battery entry for the pad (/sys/class/power_supply, scope
//      Device, whose device is the pad's HID device). Every Linux driver that
//      knows a pad's battery fills one in: Xbox, Switch-type, PlayStation,
//      Steam and the rest. A percentage or a level word, and charging.
//   2. SDL's own reading, for a pad the kernel has nothing for and one of
//      SDL's own drivers does (some 8BitDo models in their own mode).
//   3. Nothing: the pad shows its label only.
// Measured on the A9: an Xbox One S read 52% from the kernel and nothing from
// SDL in twenty minutes; an 8BitDo in Switch mode read a level and charging
// from the kernel and nothing from SDL. SDL's Linux path for pads the kernel
// drives has no battery code at all (3.4.16).
//
// A stream's pad is label only whatever its entry says: Sunshine's stand-in
// carries a battery entry that stays at 100% (A9, 2026-10-08, with the real
// pad's light amber). Wii Remotes are read by the console itself
// (wiiremote.cpp), because the bridge hides them from the kernel.
//
// EVERY READING BECOMES FOUR SEGMENTS. Exact is not the point: the last
// segment is the warning, and every pad reaches it before it is empty,
// whatever steps it reports in (some step 100/75/50/25/0 and never show 10%).

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace battery {

struct Reading {
    int segments = 0;        // 1-4; 0 is no reading, the label alone
    bool charging = false;   // a bolt; a pad that says full has none
    bool low() const { return segments == 1; }
};

// Starts the thread that keeps the kernel's readings: one look at start, then
// again whenever the kernel says a battery changed (a uevent), never on a
// timer. Reading a HID battery can ask the pad over the air, so it is never
// done on the frame thread. Idempotent.
void start();

// The HID device a pad's SDL path belongs to ("/dev/hidraw0" or
// "/dev/input/event14"), as a sysfs directory; empty when there is none.
// Cheap (two symlinks): called once as a pad connects.
std::string deviceDir(const char* sdlPath);
// FORK (handheld): the key for the machine's own battery, which kernel() can
// answer for a handheld's built-in controls. Empty reading when there is none.
std::string systemDir();

// The latest kernel reading for that device. Never blocks on the pad.
Reading kernel(const std::string& dir);

// SDL's reading, for rule 2.
Reading sdl(SDL_Gamepad* gp);

// --- The rules, pure, for --battery-test ----------------------------------------

// A percentage as segments, sticky by a few percent at each line so a pad at
// 50-51% does not flicker between two and three. `was` is the last answer, 0
// for none.
int segmentsOf(int percent, int was);
// "Full", "High", "Normal", "Low", "Critical"; anything else is 0.
int segmentsOfLevel(const std::string& level);
// A Wii Remote's battery byte, on the Wii's own scale (RVL SDK, WPADHIDParser.c,
// __a1_20_status_report): 0x55 and up 4, 0x44 3, 0x33 2, else 1.
int segmentsOfWii(uint8_t byte);

// --battery-test: every rule above. 0 when all pass.
int test();

}  // namespace battery
