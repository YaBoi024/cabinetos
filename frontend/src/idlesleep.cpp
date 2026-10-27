#include "idlesleep.h"

#include <dirent.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "prefs.h"

namespace idlesleep {
namespace {

int gIndex = 2;   // 30 minutes

std::string readWord(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

}  // namespace

void load() {
    const std::string w = prefs::get("sleep_after", "30m");
    gIndex = 2;
    for (int i = 0; i < kCount; ++i)
        if (w == kChoices[i].word) gIndex = i;
    std::fprintf(stderr, "[idle] sleep on battery after %s\n", kChoices[gIndex].name);
}

int index() { return gIndex; }

void set(int i) {
    if (i < 0 || i >= kCount) return;
    gIndex = i;
    prefs::set("sleep_after", kChoices[i].word);
}

double seconds() { return kChoices[gIndex].seconds; }

bool onBattery() {
    DIR* d = ::opendir("/sys/class/power_supply");
    if (!d) return false;
    bool sawMains = false, pluggedIn = false;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string dir = std::string("/sys/class/power_supply/") + e->d_name;
        const std::string type = readWord(dir + "/type");
        if (type != "Mains" && type.rfind("USB", 0) != 0) continue;
        sawMains = true;
        if (readWord(dir + "/online") == "1") pluggedIn = true;
    }
    ::closedir(d);
    return sawMains && !pluggedIn;
}

}  // namespace idlesleep
