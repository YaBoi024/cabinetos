#include "volume.h"

#include "proc.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace volume {
namespace {

constexpr const char* kInput = "/dev/input";
constexpr const char* kSink = "@DEFAULT_AUDIO_SINK@";

std::once_flag gStarted;
// FORK (handheld): the last level a key press left, read by the frame thread.
std::atomic<int> gPercent{0};
std::atomic<bool> gMuted{false};
std::atomic<unsigned> gGeneration{0};
// A volume key was used this run: the start's reset stops putting full back.
std::atomic<bool> gPressed{false};

// "Volume: 0.50" or "Volume: 0.50 [MUTED]" from wpctl; empty when there is no
// default output (yet).
std::string level() {
    const proc::Result r = proc::run({"wpctl", "get-volume", kSink}, 3);
    if (!r.ok()) return {};
    const std::string out = proc::trimmed(r.out);
    return out.rfind("Volume: ", 0) == 0 ? out.substr(8) : std::string();
}

bool setFull(std::string* why) {
    const proc::Result v = proc::run({"wpctl", "set-volume", kSink, "1.0"}, 3);
    const proc::Result m = proc::run({"wpctl", "set-mute", kSink, "0"}, 3);
    if (why) *why = proc::trimmed(v.ok() ? m.err : v.err);
    return v.ok() && m.ok();
}

// AT EVERY START, BACK TO FULL, unless a built-in screen. The output may not
// exist yet when the console starts (WirePlumber is still setting up the
// HDMI card), and setting nothing then would leave WirePlumber to restore the
// last level when it appears; so wait for it, a minute at most.
//
// AND THEN WATCH IT FOR HALF A MINUTE. A session start remakes the TV's sound
// output: measured on the A9, 2026-10-11, the sink was node 65, then 70, then
// 55 within a second of gamescope coming up, and the last one came back at
// WirePlumber's saved level. Usually that is over before the console starts;
// after a PS2 game it finished just after, and the volume stayed at 0.95 with
// the log saying full. So a level that is not full in that time is set again,
// unless somebody has used a volume key since.
void resetAtStart() {
    for (const std::string& c : connectors()) {
        if (builtInScreen({c})) {
            std::fprintf(stderr, "[volume] built-in screen (%s): keeps its level %s\n",
                         c.c_str(), level().c_str());
            return;
        }
    }
    std::string was;
    for (int i = 0; i < 60 && (was = level()).empty(); ++i)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    if (was.empty()) {
        std::fprintf(stderr, "[volume] no sound output after a minute; not reset\n");
        return;
    }
    std::string why;
    if (!setFull(&why)) {
        std::fprintf(stderr, "[volume] could not reset to full: %s\n", why.c_str());
        return;
    }
    std::fprintf(stderr, "[volume] full at start (was %s)\n", was.c_str());
    for (int s = 1; s <= 30 && !gPressed; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const std::string now = level();
        if (now.empty() || now == "1.00" || gPressed) continue;
        if (setFull(nullptr))
            std::fprintf(stderr, "[volume] the sound output was made again at %d s; full again (was %s)\n",
                         s, now.c_str());
    }
}

void apply(int steps) {
    const proc::Result r =
        proc::run({"wpctl", "set-volume", "-l", "1.0", kSink, change(steps)}, 3);
    if (!r.ok()) {
        std::fprintf(stderr, "[volume] %s refused: %s\n", change(steps).c_str(),
                     proc::trimmed(r.err).c_str());
        return;
    }
    const std::string now = level();
    std::fprintf(stderr, "[volume] %s: %s\n", change(steps).c_str(), now.c_str());
    // FORK (handheld): "0.50" or "0.50 [MUTED]", kept for the bar on screen.
    if (!now.empty()) {
        gPercent = static_cast<int>(std::strtod(now.c_str(), nullptr) * 100.0 + 0.5);
        gMuted = now.find("MUTED") != std::string::npos;
        ++gGeneration;
    }
}

// The kernel's list of the keys a device can send, from sysfs, which anyone
// can read: "capabilities/key" is hex words, most significant first, each
// the width of a long. So a device is only opened when it has a volume key,
// and "cannot read" means one that matters.
bool sendsVolumeKeys(const std::string& node) {
    FILE* f = std::fopen(("/sys/class/input/" + node + "/device/capabilities/key").c_str(), "r");
    if (!f) return false;
    char line[2048] = {};
    const bool got = std::fgets(line, sizeof line, f) != nullptr;
    std::fclose(f);
    return got && (keyInBitmap(line, KEY_VOLUMEUP) || keyInBitmap(line, KEY_VOLUMEDOWN));
}

struct Device {
    int fd = -1;
    std::string name;
};

// THE KEYS, for the whole run. Every key device under /dev/input that can send
// a volume key is held open (read only, never grabbed: the window and the
// emulators still get every key). New ones are found through inotify: a
// Bluetooth keyboard, or a node whose access grant udev adds a moment after
// creating it (IN_ATTRIB).
void readKeys() {
    std::map<std::string, Device> open;   // node name ("event3") -> device
    std::set<std::string> passed;         // looked at: no volume keys

    // A refusal is said only for the devices there at start, which is where
    // a missing access grant shows. A node that has just appeared is refused
    // for the moment before udev grants it (measured on the A9: twice, then
    // read), and saying so would read as a fault in a report.
    auto look = [&](const std::string& node, bool atStart) {
        if (open.count(node) || passed.count(node)) return;
        if (!sendsVolumeKeys(node)) {
            passed.insert(node);
            return;
        }
        const std::string path = std::string(kInput) + "/" + node;
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (atStart)
                std::fprintf(stderr, "[volume] cannot read %s: %s\n", node.c_str(),
                             std::strerror(errno));
            return;
        }
        char name[256] = "?";
        ::ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        std::fprintf(stderr, "[volume] keys from %s (%s)\n", name, node.c_str());
        open[node] = Device{fd, name};
    };
    auto scan = [&](bool atStart) {
        if (DIR* d = ::opendir(kInput)) {
            while (dirent* e = ::readdir(d))
                if (std::strncmp(e->d_name, "event", 5) == 0) look(e->d_name, atStart);
            ::closedir(d);
        }
    };

    const int watch = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch < 0 || ::inotify_add_watch(watch, kInput, IN_CREATE | IN_ATTRIB | IN_DELETE) < 0)
        std::fprintf(stderr, "[volume] no word of new keyboards (%s)\n", std::strerror(errno));
    scan(true);

    for (;;) {
        std::vector<pollfd> fds;
        std::vector<std::string> nodes;
        if (watch >= 0) fds.push_back({watch, POLLIN, 0});
        for (const auto& [node, dev] : open) {
            fds.push_back({dev.fd, POLLIN, 0});
            nodes.push_back(node);
        }
        if (fds.empty()) {
            // No inotify and nothing open: look again now and then.
            std::this_thread::sleep_for(std::chrono::seconds(10));
            scan(false);
            continue;
        }
        if (::poll(fds.data(), fds.size(), -1) < 0) {
            if (errno != EINTR) std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        size_t i = 0;
        if (watch >= 0) {
            if (fds[0].revents & POLLIN) {
                alignas(inotify_event) char buf[4096];
                ssize_t n;
                while ((n = ::read(watch, buf, sizeof buf)) > 0) {
                    for (ssize_t off = 0; off < n;) {
                        const auto* ev = reinterpret_cast<const inotify_event*>(buf + off);
                        if (ev->len && std::strncmp(ev->name, "event", 5) == 0) {
                            // A node number is reused by the next device, so
                            // whatever was known about the name is forgotten.
                            if (ev->mask & (IN_CREATE | IN_DELETE)) passed.erase(ev->name);
                        }
                        off += static_cast<ssize_t>(sizeof(inotify_event) + ev->len);
                    }
                }
                scan(false);
            }
            ++i;
        }
        int steps = 0;
        bool lost = false;
        for (size_t k = 0; i < fds.size(); ++i, ++k) {
            if (!fds[i].revents) continue;
            const std::string& node = nodes[k];
            input_event evs[64];
            ssize_t n;
            bool gone = (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            while ((n = ::read(fds[i].fd, evs, sizeof evs)) > 0)
                for (ssize_t e = 0; e < n / static_cast<ssize_t>(sizeof(input_event)); ++e)
                    steps += stepOf(evs[e].type, evs[e].code, evs[e].value);
            if (n < 0 && errno != EAGAIN && errno != EINTR) gone = true;
            if (gone) {
                std::fprintf(stderr, "[volume] %s (%s) gone\n", open[node].name.c_str(),
                             node.c_str());
                ::close(open[node].fd);
                open.erase(node);
                lost = true;
            }
        }
        // Its node name may already belong to the next device, whose arrival
        // was skipped while the old one still held the name.
        if (lost) scan(false);
        // Everything that arrived together is one change, so a held key
        // costs one wpctl per wake, not one per repeat.
        if (steps) {
            gPressed = true;
            apply(steps);
        }
    }
}

void run() {
    std::thread(resetAtStart).detach();
    readKeys();
}

}  // namespace

unsigned generation() { return gGeneration; }
int percent() { return gPercent; }
bool muted() { return gMuted; }

void start() {
    std::call_once(gStarted, [] { std::thread(run).detach(); });
}

bool keyInBitmap(const std::string& bitmap, int code) {
    constexpr int w = static_cast<int>(sizeof(unsigned long) * CHAR_BIT);
    std::vector<std::string> words;
    std::string word;
    for (char c : bitmap) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            word += c;
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(word);
    // The last word holds keys 0 to w-1.
    const size_t index = static_cast<size_t>(code / w);
    if (index >= words.size()) return false;
    const unsigned long v = std::strtoul(words[words.size() - 1 - index].c_str(), nullptr, 16);
    return (v >> (code % w)) & 1UL;
}

bool builtInScreen(const std::vector<std::string>& names) {
    for (const std::string& n : names) {
        // "card1-eDP-1": the type is between the card and the last dash.
        const size_t a = n.find('-');
        const size_t b = n.rfind('-');
        if (n.rfind("card", 0) != 0 || a == std::string::npos || b <= a) continue;
        const std::string type = n.substr(a + 1, b - a - 1);
        if (type == "eDP" || type == "DSI" || type == "LVDS") return true;
    }
    return false;
}

std::vector<std::string> connectors() {
    std::vector<std::string> out;
    if (DIR* d = ::opendir("/sys/class/drm")) {
        while (dirent* e = ::readdir(d))
            if (std::strncmp(e->d_name, "card", 4) == 0 && std::strchr(e->d_name, '-'))
                out.emplace_back(e->d_name);
        ::closedir(d);
    }
    return out;
}

int stepOf(int type, int code, int value) {
    if (type != EV_KEY || (value != 1 && value != 2)) return 0;
    if (code == KEY_VOLUMEUP) return 1;
    if (code == KEY_VOLUMEDOWN) return -1;
    return 0;
}

std::string change(int steps) {
    const int pct = (steps < 0 ? -steps : steps) * kStepPercent;
    return std::to_string(pct) + (steps < 0 ? "%-" : "%+");
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    check(builtInScreen({"card1-eDP-1"}), "eDP is built in (the Steam Deck's card1-eDP-1)");
    check(builtInScreen({"card0-DSI-1"}) && builtInScreen({"card0-LVDS-1"}),
          "DSI and LVDS are built in");
    check(!builtInScreen({"card1-HDMI-A-1", "card1-DP-1", "card1-DP-2", "card1-Writeback-1"}),
          "the A9's HDMI, DisplayPort and writeback are not");
    check(builtInScreen({"card1-HDMI-A-1", "card1-eDP-1"}),
          "a handheld with a video out is still built in");
    check(!builtInScreen({"card1", "renderD128", "version", "card1-eDPX-1"}),
          "the other names in /sys/class/drm are not connectors");
    check(stepOf(EV_KEY, KEY_VOLUMEUP, 1) == 1 && stepOf(EV_KEY, KEY_VOLUMEDOWN, 1) == -1,
          "115 up, 114 down");
    check(stepOf(EV_KEY, KEY_VOLUMEUP, 2) == 1, "held: the kernel's repeat steps again");
    check(stepOf(EV_KEY, KEY_VOLUMEUP, 0) == 0, "a release is nothing");
    check(stepOf(EV_KEY, KEY_MUTE, 1) == 0 && stepOf(EV_MSC, KEY_VOLUMEUP, 1) == 0,
          "other keys and other events are nothing");
    // The A9's "AT Raw Set 2 keyboard" and its "Power Button", as sysfs has them.
    const std::string at = "402000007 ff803078f800d001 feffffdfffcfffff fffffffffffffffe";
    check(keyInBitmap(at, KEY_VOLUMEUP) && keyInBitmap(at, KEY_VOLUMEDOWN),
          "a keyboard's sysfs list has 114 and 115");
    check(keyInBitmap(at, KEY_ESC) && !keyInBitmap(at, KEY_RESERVED), "and Escape, not 0");
    check(!keyInBitmap("8000 10000000000000 0", KEY_VOLUMEUP) &&
          keyInBitmap("8000 10000000000000 0", KEY_POWER), "the power button has neither");
    check(!keyInBitmap("", KEY_VOLUMEUP) && !keyInBitmap("0", KEY_VOLUMEUP),
          "an empty list has neither");
    check(change(1) == "5%+" && change(-3) == "15%-", "steps as wpctl's percent");
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace volume
