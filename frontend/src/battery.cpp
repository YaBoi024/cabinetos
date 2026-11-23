#include "battery.h"

#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace battery {
namespace {

constexpr const char* kSupplies = "/sys/class/power_supply";

std::string firstLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    return line;
}

std::string realDir(const std::string& path) {
    char buf[PATH_MAX];
    if (!::realpath(path.c_str(), buf)) return {};
    return buf;
}

struct Entry {
    Reading reading;
    std::string model;
};

std::mutex gLock;
std::map<std::string, Entry> gByDevice;   // the HID device's sysfs dir -> its reading
std::once_flag gStarted;

// Every battery the kernel has for a device (scope Device: never the machine's
// own), read again. On the worker thread only: a HID battery's capacity can be
// asked of the pad over the air.
void scan() {
    std::map<std::string, Entry> now;
    std::map<std::string, Entry> before;
    {
        std::lock_guard<std::mutex> lk(gLock);
        before = gByDevice;
    }
    if (DIR* d = ::opendir(kSupplies)) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            // NEVER THE KERNEL WII DRIVER'S (#548): a Remote's battery is the
            // bridge's (wiiremote.h, W1-W4), and each read of this one asks
            // the Remote for a status report, which a Wii game then gets too.
            if (std::strncmp(e->d_name, "wiimote_battery", 15) == 0) continue;
            const std::string p = std::string(kSupplies) + "/" + e->d_name;
            if (firstLine(p + "/scope") != "Device") continue;
            const std::string dev = realDir(p + "/device");
            if (dev.empty()) continue;
            Entry en;
            en.model = firstLine(p + "/model_name");
            if (en.model.empty()) en.model = e->d_name;   // hid-nintendo gives none
            const auto was = before.find(dev);
            const int wasSegments = was == before.end() ? 0 : was->second.reading.segments;
            const std::string cap = firstLine(p + "/capacity");
            char* end = nullptr;
            const long pct = cap.empty() ? -1 : std::strtol(cap.c_str(), &end, 10);
            if (!cap.empty() && end && *end == '\0' && pct >= 0)
                en.reading.segments = segmentsOf(static_cast<int>(pct), wasSegments);
            else
                en.reading.segments = segmentsOfLevel(firstLine(p + "/capacity_level"));
            en.reading.charging = firstLine(p + "/status") == "Charging";
            if (en.reading.segments == 0) en.reading.charging = false;
            if (was == before.end() || was->second.reading.segments != en.reading.segments ||
                was->second.reading.charging != en.reading.charging)
                std::fprintf(stderr, "[battery] %s: %d of 4%s\n", en.model.c_str(),
                             en.reading.segments, en.reading.charging ? ", charging" : "");
            now[dev] = std::move(en);
        }
        ::closedir(d);
    }
    std::lock_guard<std::mutex> lk(gLock);
    gByDevice = std::move(now);
}

// The kernel's own events, the ones udev hears: a battery added, gone or
// changed says so here, so nothing is read on a timer. Without the socket (it
// cannot fail on a normal system) a minute's re-read stands in.
void run() {
    scan();
    const int fd = ::socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = 1;
    if (fd < 0 || ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        std::fprintf(stderr, "[battery] no kernel events (%s); reading every minute\n",
                     std::strerror(errno));
        if (fd >= 0) ::close(fd);
        for (;;) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            scan();
        }
    }
    char buf[8192];
    for (;;) {
        const ssize_t n = ::recv(fd, buf, sizeof buf - 1, 0);
        if (n <= 0) continue;
        buf[n] = '\0';
        // "change@/devices/...\0ACTION=change\0...SUBSYSTEM=power_supply\0..."
        bool battery = false;
        for (ssize_t i = 0; i < n; i += static_cast<ssize_t>(std::strlen(buf + i)) + 1)
            if (std::strcmp(buf + i, "SUBSYSTEM=power_supply") == 0) battery = true;
        if (battery) scan();
    }
}

}  // namespace

void start() {
    std::call_once(gStarted, [] { std::thread(run).detach(); });
}

std::string deviceDir(const char* sdlPath) {
    if (!sdlPath) return {};
    const std::string path = sdlPath;
    // The input device's parent and the hidraw node's device are both the HID
    // device, which is what a pad's battery entry hangs from.
    if (path.rfind("/dev/input/", 0) == 0)
        return realDir("/sys/class/input/" + path.substr(11) + "/device/device");
    if (path.rfind("/dev/hidraw", 0) == 0)
        return realDir("/sys/class/hidraw/" + path.substr(5) + "/device");
    return {};
}

Reading kernel(const std::string& dir) {
    if (dir.empty()) return {};
    std::lock_guard<std::mutex> lk(gLock);
    auto it = gByDevice.find(dir);
    return it == gByDevice.end() ? Reading{} : it->second.reading;
}

Reading sdl(SDL_Gamepad* gp) {
    Reading r;
    if (!gp) return r;
    int pct = -1;
    switch (SDL_GetGamepadPowerInfo(gp, &pct)) {
        case SDL_POWERSTATE_ON_BATTERY:
            r.segments = segmentsOf(pct, 0);
            break;
        case SDL_POWERSTATE_CHARGING:
            r.segments = segmentsOf(pct, 0);
            r.charging = r.segments > 0;
            break;
        case SDL_POWERSTATE_CHARGED:
            r.segments = 4;
            break;
        default:
            break;
    }
    return r;
}

int segmentsOf(int percent, int was) {
    if (percent < 0) return 0;
    percent = std::min(percent, 100);
    auto raw = [](int p) { return p > 75 ? 4 : p > 50 ? 3 : p > 25 ? 2 : 1; };
    const int r = raw(percent);
    if (was <= 0 || r <= was) return r;
    // Up a segment only once clear of the line by 3%.
    return std::max(was, raw(percent - 3));
}

int segmentsOfLevel(const std::string& level) {
    if (level == "Full") return 4;
    if (level == "High") return 3;
    if (level == "Normal") return 2;
    if (level == "Low" || level == "Critical") return 1;
    return 0;
}

int segmentsOfWii(uint8_t byte) {
    if (byte >= 0x55) return 4;
    if (byte >= 0x44) return 3;
    if (byte >= 0x33) return 2;
    return 1;
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    check(segmentsOf(100, 0) == 4 && segmentsOf(76, 0) == 4, "above 75% is four");
    check(segmentsOf(75, 0) == 3 && segmentsOf(52, 0) == 3, "51-75% is three (the Xbox's 52%)");
    check(segmentsOf(50, 0) == 2 && segmentsOf(26, 0) == 2, "26-50% is two");
    check(segmentsOf(25, 0) == 1 && segmentsOf(0, 0) == 1, "25% and under is the last, the warning");
    check(segmentsOf(10, 0) == 1 && segmentsOf(40, 0) == 2 && segmentsOf(70, 0) == 3,
          "SDL's Xbox steps 10/40/70/100 come out 1/2/3/4");
    check(segmentsOf(25, 0) == 1 && segmentsOf(50, 0) == 2 && segmentsOf(75, 0) == 3,
          "a pad stepping in 25s reaches the warning before empty");
    check(segmentsOf(-1, 3) == 0, "no percentage is no reading");
    check(segmentsOf(51, 2) == 2, "at 51% from two it stays two (no flicker)");
    check(segmentsOf(53, 2) == 2 && segmentsOf(54, 2) == 3, "and goes to three 3% past the line");
    check(segmentsOf(50, 3) == 2, "going down is at once");
    check(segmentsOf(27, 1) == 1 && segmentsOf(29, 1) == 2,
          "the warning segment lets go only 3% past 25");
    check(segmentsOf(90, 1) == 4, "a big jump up (a pad charged off the console) is not held back");
    check(segmentsOfLevel("Full") == 4 && segmentsOfLevel("High") == 3 &&
              segmentsOfLevel("Normal") == 2,
          "levels: Full 4, High 3, Normal 2");
    check(segmentsOfLevel("Low") == 1 && segmentsOfLevel("Critical") == 1,
          "Low and Critical are the warning");
    check(segmentsOfLevel("Unknown") == 0 && segmentsOfLevel("") == 0, "Unknown is no reading");
    check(segmentsOfWii(0xc8) == 4 && segmentsOfWii(112) == 4 && segmentsOfWii(0x55) == 4,
          "Wii: 0x55 and up is four (the alkalines measured at 112)");
    check(segmentsOfWii(0x54) == 3 && segmentsOfWii(0x44) == 3, "Wii: 0x44-0x54 is three");
    check(segmentsOfWii(0x43) == 2 && segmentsOfWii(0x33) == 2, "Wii: 0x33-0x43 is two");
    check(segmentsOfWii(0x32) == 1 && segmentsOfWii(0) == 1, "Wii: under 0x33 is the last");
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace battery
