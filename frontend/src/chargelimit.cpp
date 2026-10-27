#include "chargelimit.h"

#include <cstdio>
#include <string>
#include <thread>

#include "prefs.h"
#include "proc.h"

namespace chargelimit {
namespace {

bool fileThere() {
    static const bool there = [] {
        const proc::Result r = proc::run(
            {"sh", "-c", "for d in /sys/class/hwmon/hwmon*; do "
                         "[ \"$(cat $d/name 2>/dev/null)\" = steamdeck_hwmon ] && "
                         "[ -e $d/max_battery_charge_level ] && exit 0; done; exit 1"}, 5);
        return r.ok();
    }();
    return there;
}

void write(int percent) {
    std::thread([percent] {
        const std::string unit =
            "cabinetos-charge-limit@" + std::to_string(percent) + ".service";
        const proc::Result r = proc::run({"systemctl", "--no-ask-password", "start", unit}, 15);
        std::fprintf(stderr, "[charge] limit %d%%: %s\n", percent,
                     r.ok() ? "set" : ("not set: " + r.err).c_str());
    }).detach();
}

}  // namespace

bool available() { return fileThere(); }

int savedIndex() {
    const std::string w = prefs::get("charge_limit", "100");
    for (int i = 0; i < kCount; ++i)
        if (w == kChoices[i].word) return i;
    return kCount - 1;
}

void set(int i) {
    if (i < 0 || i >= kCount || !fileThere()) return;
    prefs::set("charge_limit", kChoices[i].word);
    write(kChoices[i].percent);
}

void applySaved() {
    static bool done = false;
    if (done || !fileThere()) return;
    done = true;
    write(kChoices[savedIndex()].percent);
}

}  // namespace chargelimit
