// FORK (handheld): the bar that shows the volume or the brightness when it
// changes, near the foot of the screen, above the notice pill (notice.cpp).
//
// The volume comes from volume.cpp, which says when a key press changed it.
// The brightness is read from the kernel's backlight, so the bar shows it
// whoever changed it: the console has no brightness keys of its own.
//
// Drawn only where the console draws: a separate emulator that has the screen
// (Switch, PS2 and the rest) covers it, as it covers the notice.

#pragma once

#include <string>

namespace levelbar {

// The kernel's backlight as a percent, 0 to 100; -1 when the machine has none
// (a television has no backlight of its own). Cheap: two small files.
int backlightPercent();

// What the bar shows now, and for how long it has been up.
struct Bar {
    std::string label;      // "Volume", "Muted", "Brightness"
    int percent = 0;
    float age = 99.0f;      // seconds since it was shown
    void show(std::string what, int pct);
    void tick(float dt) { age += dt; }
    // 0 while not shown, rising in a moment, then falling away.
    float alpha() const;
};

}  // namespace levelbar
