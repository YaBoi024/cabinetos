#include "wiiremote.h"

#include "prefs.h"
#include "proc.h"
#include "storage.h"

#include <SDL3/SDL.h>

#include <dirent.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace wiiremote {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kPrefix = "Nintendo RVL-CNT";
// The bridge's stand-ins say this as their HID phys (wiibridge/bridge.cpp).
constexpr const char* kBridgePhys = "cabinetos-wii-bridge";

bool isRemoteName(const std::string& name) { return name.rfind(kPrefix, 0) == 0; }

// --- What is paired -------------------------------------------------------------

std::atomic<bool> gAnyPaired{false};
std::atomic<bool> gAnyWithMotionPlus{false};
std::mutex gPairedLock;
std::vector<Paired> gPaired;

std::vector<std::pair<std::string, std::string>> listed(const char* filter) {
    std::vector<std::pair<std::string, std::string>> out;
    const proc::Result r = proc::run({"bluetoothctl", "devices", filter}, 10);
    if (!r.ok()) return out;
    std::istringstream in(r.out);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Device ", 0) != 0 || line.size() < 7 + 17) continue;
        const std::string address = line.substr(7, 17);
        const std::string name = line.size() > 25 ? line.substr(25) : "";
        if (isRemoteName(name)) out.emplace_back(address, name);
    }
    return out;
}

// NINTENDO'S OWN REMOTE, by the id bluetoothd read off it (its Device ID
// record, "Modalias: usb:v057Ep0306..."): the two the kernel's Wii Remote
// driver takes (hid-wiimote; 0330 is the Wii Remote Plus). The TechKens say
// v04E8p7021. When bluetoothd cannot say, it is not counted as Nintendo's,
// so nothing greys that did not before.
bool nintendosOwn(const std::string& address) {
    const proc::Result r = proc::run({"bluetoothctl", "info", address}, 10);
    if (!r.ok()) return false;
    std::string out = r.out;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out.find("modalias: usb:v057ep0306") != std::string::npos ||
           out.find("modalias: usb:v057ep0330") != std::string::npos;
}

// --- The running Remotes ----------------------------------------------------------
//
// Each Remote is a hidraw node. Reports read from it start with their report
// id; the core buttons are the two bytes after it in every report that has
// them (WiiBrew, "Wiimote": status 0x20, read 0x21, ack 0x22, data 0x30-0x37,
// 0x3e and 0x3f). Written reports start with the id too, and bit 0 of the
// first byte after it is the rumble motor, kept off.

enum : uint16_t {
    kLeft = 0x0001, kRight = 0x0002, kDown = 0x0004, kUp = 0x0008, kPlus = 0x0010,
    kTwo = 0x0100, kOne = 0x0200, kB = 0x0400, kA = 0x0800, kMinus = 0x1000, kHome = 0x8000,
};

// Upright, as the Wii's own menus are used. A is A and B (the trigger) is
// Back, as on a Wii; 1 and 2 are the on-screen keyboard's delete and shift,
// where a pad's X and Y are; + is Start.
struct Mapping {
    uint16_t bit;
    SDL_GamepadButton button;
};
constexpr Mapping kMap[] = {
    {kUp, SDL_GAMEPAD_BUTTON_DPAD_UP},       {kDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {kLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},   {kRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {kA, SDL_GAMEPAD_BUTTON_SOUTH},          {kB, SDL_GAMEPAD_BUTTON_EAST},
    {kOne, SDL_GAMEPAD_BUTTON_WEST},         {kTwo, SDL_GAMEPAD_BUTTON_NORTH},
    {kPlus, SDL_GAMEPAD_BUTTON_START},
};

// Not any real pad's id: SDL's start at 1 and count up.
constexpr SDL_JoystickID kRemoteId = 0x57494900;

constexpr auto kHomeHold = std::chrono::milliseconds(1000);

struct Remote {
    std::string node;      // "hidraw3"
    std::string address;   // from the HID's uniq
    int fd = -1;
    int slot = 0;          // 0..3, its player light
    bool bridged = false;  // the bridge's stand-in, not the Remote itself
    uint16_t buttons = 0;
    Clock::time_point homeSince{};
    bool homeFired = false;
    // ON OR OFF: a stand-in stays when its Remote goes off (the bridge keeps
    // it, wiibridge/bridge.cpp, detach), so the sign is reports: a Remote that
    // is on streams them (continuous mode, setUp).
    Clock::time_point lastReport{};
    Clock::time_point lastActive{};   // the last button change
    bool switchedOff = false;
    // ITS BATTERY (#293), from its status report: the byte the Wii reads
    // (WiiBrew, "Wiimote", 0x20: byte 6) and the Remote's own low flag (byte 3,
    // bit 0), which is what a Wii warns on. -1 until it has answered.
    int battery = -1;
    bool lowFlag = false;
    Clock::time_point lastAsk{};
};

// A Remote reports its battery only when asked. Once a minute, out of a game
// and only while it is on: one tiny request, its answer carries the buttons
// too, and it does not count as the Remote being used (lastActive).
constexpr auto kBatteryAsk = std::chrono::minutes(1);

constexpr auto kQuiet = std::chrono::milliseconds(1500);
// A WII'S OWN IDLE TIME: a Remote untouched this long out of a game is switched
// off, as a Wii does (a Wii Remote has no timer of its own; the console turns it
// off). In a game the bridge does it when the game lets the Remote go.
constexpr auto kIdleOff = std::chrono::minutes(5);

bool isOn(const Remote& r) { return Clock::now() - r.lastReport < kQuiet; }

// Asks bluetoothd to disconnect it, which turns a Wii Remote off. Off the
// reading thread: bluetoothctl can take a second.
void switchOff(const std::string& address) {
    std::string a = address;
    for (char& c : a) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::thread([a] { proc::run({"bluetoothctl", "disconnect", a}, 10); }).detach();
}

std::mutex gLock;
std::vector<Remote> gRemotes;
std::atomic<bool> gGame{false};
std::atomic<bool> gHomeHold{false};
std::once_flag gStarted;

void send(int fd, std::initializer_list<uint8_t> report) {
    const std::vector<uint8_t> b(report);
    if (::write(fd, b.data(), b.size()) < 0 && errno != EAGAIN)
        std::fprintf(stderr, "[wiiremote] write: %s\n", std::strerror(errno));
}

// What a Wii sends a Remote that has just connected.
void setUp(const Remote& r) {
    send(r.fd, {0x15, 0x00});                                         // status, please
    send(r.fd, {0x11, static_cast<uint8_t>(0x10u << r.slot)});         // its player light
    // Buttons, continuous. Continuous because Dolphin's own reset is the same
    // mode without it (12 00 30), which the bridge reads as a game letting the
    // Remote go (wiibridge/bridge.cpp, checkDropped). A copy streams either way.
    send(r.fd, {0x12, 0x04, 0x30});
}

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string ueventField(const std::string& uevent, const char* key) {
    const std::string want = std::string(key) + "=";
    std::istringstream in(uevent);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(want, 0) == 0) return line.substr(want.size());
    return {};
}

void pushButton(SDL_GamepadButton b, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    ev.gbutton.timestamp = SDL_GetTicksNS();
    ev.gbutton.which = kRemoteId;
    ev.gbutton.button = static_cast<Uint8>(b);
    ev.gbutton.down = down;
    SDL_PushEvent(&ev);
}

// New Remotes in, gone ones out. Called with gLock held.
void rescan() {
    // Gone: its node is no longer there.
    for (auto it = gRemotes.begin(); it != gRemotes.end();) {
        if (::access(("/dev/" + it->node).c_str(), F_OK) != 0) {
            std::fprintf(stderr, "[wiiremote] %s left (light %d)\n", it->address.c_str(), it->slot + 1);
            ::close(it->fd);
            it = gRemotes.erase(it);
        } else {
            ++it;
        }
    }
    // THE BRIDGE'S STAND-IN, WHEN THERE IS ONE, NOT THE REAL REMOTE
    // (wiibridge/bridge.cpp): every Remote that has a stand-in is driven
    // through it, the console's light and menus included, and the real node
    // is the bridge's alone (the udev rule hides it from everybody else).
    struct Node {
        std::string node, uevent;
    };
    std::vector<Node> nodes;
    std::vector<std::string> bridged;   // addresses that have a stand-in
    DIR* d = ::opendir("/sys/class/hidraw");
    if (!d) return;
    while (dirent* e = ::readdir(d)) {
        const std::string node = e->d_name;
        if (node.rfind("hidraw", 0) != 0) continue;
        Node n{node, readFile("/sys/class/hidraw/" + node + "/device/uevent")};
        if (!isRemoteName(ueventField(n.uevent, "HID_NAME"))) continue;
        if (ueventField(n.uevent, "HID_PHYS") == kBridgePhys)
            bridged.push_back(ueventField(n.uevent, "HID_UNIQ"));
        nodes.push_back(std::move(n));
    }
    ::closedir(d);
    auto hasStandIn = [&](const std::string& address) {
        for (const std::string& b : bridged)
            if (strcasecmp(b.c_str(), address.c_str()) == 0) return true;
        return false;
    };
    // A real Remote taken before its stand-in appeared gives way to it.
    for (auto it = gRemotes.begin(); it != gRemotes.end();) {
        if (!it->bridged && hasStandIn(it->address)) {
            std::fprintf(stderr, "[wiiremote] %s now through the bridge\n", it->address.c_str());
            ::close(it->fd);
            it = gRemotes.erase(it);
        } else {
            ++it;
        }
    }
    for (const Node& n : nodes) {
        const std::string& node = n.node;
        const std::string& uevent = n.uevent;
        bool have = false;
        for (const Remote& r : gRemotes) have = have || r.node == node;
        if (have) continue;
        const bool standIn = ueventField(uevent, "HID_PHYS") == kBridgePhys;
        if (!standIn && hasStandIn(ueventField(uevent, "HID_UNIQ"))) continue;
        Remote r;
        r.node = node;
        r.address = ueventField(uevent, "HID_UNIQ");
        r.bridged = standIn;
        r.fd = ::open(("/dev/" + node).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (r.fd < 0) {
            // A real Remote the bridge has hidden is no news: its stand-in
            // follows within half a second.
            if (errno != EACCES)
                std::fprintf(stderr, "[wiiremote] cannot open /dev/%s: %s\n", node.c_str(),
                             std::strerror(errno));
            continue;
        }
        // Its light: the lowest one no other Remote has, so lights follow the
        // order Remotes connected in.
        for (int s = 0; s < 4; ++s) {
            bool taken = false;
            for (const Remote& o : gRemotes) taken = taken || o.slot == s;
            if (!taken) {
                r.slot = s;
                break;
            }
        }
        std::fprintf(stderr, "[wiiremote] %s connected on %s%s, light %d\n", r.address.c_str(),
                     node.c_str(), standIn ? " (through the bridge)" : "", r.slot + 1);
        r.lastActive = Clock::now();
        r.lastReport = Clock::now();
        r.lastAsk = Clock::now();   // setUp asks
        // While a game runs Dolphin sets it up, with its own light.
        if (!gGame) setUp(r);
        gRemotes.push_back(std::move(r));
    }
}

void buttonsChanged(Remote& r, uint16_t now) {
    const uint16_t was = r.buttons;
    r.buttons = now;
    r.lastActive = Clock::now();
    for (const Mapping& m : kMap) {
        if ((now & m.bit) && !(was & m.bit)) pushButton(m.button, true);
        if (!(now & m.bit) && (was & m.bit)) pushButton(m.button, false);
    }
    if ((now & kHome) && !(was & kHome)) {
        r.homeSince = Clock::now();
        r.homeFired = false;
    }
}

void readReports(Remote& r) {
    uint8_t buf[32];
    for (;;) {
        const ssize_t n = ::read(r.fd, buf, sizeof buf);
        if (n <= 0) return;
        if (!isOn(r)) {
            // BACK ON (a button, after it was switched off or went away): its
            // light again, and the idle time starts over.
            r.lastActive = Clock::now();
            if (r.lastReport != Clock::time_point{})
                std::fprintf(stderr, "[wiiremote] %s is back on\n", r.address.c_str());
            r.switchedOff = false;
            r.lastReport = Clock::now();
            if (!gGame) setUp(r);
        }
        r.lastReport = Clock::now();
        const uint8_t id = buf[0];
        if (id == 0x20 && n >= 7) {
            r.battery = buf[6];
            r.lowFlag = (buf[3] & 0x01) != 0;
        }
        // A status report stops a Remote's reports until the mode is set
        // again: an extension went in or out, or it answered our request.
        if (id == 0x20 && !gGame) send(r.fd, {0x12, 0x04, 0x30});
        const bool hasButtons = (id >= 0x20 && id <= 0x22) || (id >= 0x30 && id <= 0x37) ||
                                id == 0x3e || id == 0x3f;
        if (hasButtons && n >= 3) {
            const uint16_t b = static_cast<uint16_t>(((buf[1] & 0x1f) | ((buf[2] & 0x9f) << 8)));
            if (b != r.buttons) buttonsChanged(r, b);
        }
    }
}

void run() {
    auto lastScan = Clock::time_point{};
    for (;;) {
        std::vector<pollfd> fds;
        {
            std::lock_guard<std::mutex> lk(gLock);
            if (Clock::now() - lastScan > std::chrono::milliseconds(500)) {
                rescan();
                lastScan = Clock::now();
            }
            for (const Remote& r : gRemotes) fds.push_back({r.fd, POLLIN, 0});
        }
        if (fds.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        ::poll(fds.data(), fds.size(), 50);
        std::lock_guard<std::mutex> lk(gLock);
        for (Remote& r : gRemotes) {
            readReports(r);
            if (!gGame && isOn(r) && Clock::now() - r.lastAsk >= kBatteryAsk) {
                r.lastAsk = Clock::now();
                send(r.fd, {0x15, 0x00});
            }
            // HOME HELD, while a game runs: the console's own menu. A press is
            // the game's HOME menu, which Dolphin passes on as a Wii would.
            if ((r.buttons & kHome) && !r.homeFired && gGame &&
                Clock::now() - r.homeSince >= kHomeHold) {
                r.homeFired = true;
                gHomeHold = true;
                std::fprintf(stderr, "[wiiremote] HOME held on %s\n", r.address.c_str());
            }
            if (!gGame && isOn(r) && !r.switchedOff && Clock::now() - r.lastActive >= kIdleOff) {
                r.switchedOff = true;
                std::fprintf(stderr, "[wiiremote] %s untouched for 5 minutes; switching it off\n",
                             r.address.c_str());
                switchOff(r.address);
            }
        }
    }
}

}  // namespace

// --- Public ---------------------------------------------------------------------------

std::vector<Paired> known() {
    std::vector<Paired> out;
    {
        std::lock_guard<std::mutex> lk(gPairedLock);
        out = gPaired;
    }
    std::lock_guard<std::mutex> lk(gLock);
    for (Paired& p : out)
        for (const Remote& r : gRemotes)
            if (strcasecmp(r.address.c_str(), p.address.c_str()) == 0 && isOn(r)) {
                p.connected = true;
                p.light = gGame ? 0 : r.slot + 1;
            }
    return out;
}

bool anyPaired() { return gAnyPaired; }
bool anyWithMotionPlus() { return gAnyWithMotionPlus; }

void refresh() {
    std::vector<Paired> now;
    size_t nintendos = 0;
    for (const auto& [address, name] : listed("Paired")) {
        Paired p;
        p.address = address;
        p.name = name;
        now.push_back(p);
        if (nintendosOwn(address)) ++nintendos;
    }
    const bool any = !now.empty();
    const bool motionPlus = now.size() > nintendos;
    if (any != gAnyPaired || motionPlus != gAnyWithMotionPlus)
        std::fprintf(stderr, "[wiiremote] %zu Remote(s) paired, %zu of them Nintendo's own\n", now.size(),
                     nintendos);
    {
        std::lock_guard<std::mutex> lk(gPairedLock);
        gPaired = std::move(now);
    }
    gAnyPaired = any;
    gAnyWithMotionPlus = motionPlus;
}

// THE BRIDGE (wiibridge/bridge.cpp, its own program, GPL because it carries
// Dolphin's extension encryption): started with the console and stopped with
// it. Without it Remotes still work, Nunchuk games do not on copies, and Cemu
// does not see a copy at all.
//
// Its one argument is where it keeps the Miis sent to a Remote, one file per
// player light (#275): the console's, beside its other state, not synced.
void startBridge() {
    const char* env = std::getenv("CABINETOS_WII_BRIDGE");
    const std::string path = env && *env ? env : "/usr/libexec/cabinetos-wii-bridge";
    if (::access(path.c_str(), X_OK) != 0) {
        std::fprintf(stderr, "[wiiremote] no bridge at %s\n", path.c_str());
        return;
    }
    const std::string miis = storage::root() + "/wii-remote-miis";
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        ::execl(path.c_str(), path.c_str(), miis.c_str(), static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    if (pid > 0) std::fprintf(stderr, "[wiiremote] bridge started (%s, pid %d)\n", path.c_str(), pid);
}

void start() {
    std::call_once(gStarted, [] {
        refresh();
        startBridge();
        std::thread(run).detach();
    });
}

void setGameRunning(bool running) {
    if (gGame == running) return;
    gGame = running;
    gHomeHold = false;
    if (running) return;
    // Back from a game: each Remote gets its own light and mode again, which
    // Dolphin changed.
    std::lock_guard<std::mutex> lk(gLock);
    for (Remote& r : gRemotes) {
        setUp(r);
        r.homeFired = true;   // a HOME still held from the game does nothing here
    }
}

bool takeHomeHold() { return gHomeHold.exchange(false); }

std::vector<Battery> batteries() {
    std::vector<Battery> out;
    if (gGame) return out;
    std::lock_guard<std::mutex> lk(gLock);
    for (const Remote& r : gRemotes) {
        if (!isOn(r)) continue;
        Battery b;
        b.light = r.slot + 1;
        b.address = r.address;
        b.byte = r.battery;
        b.low = r.lowFlag;
        out.push_back(std::move(b));
    }
    std::sort(out.begin(), out.end(),
              [](const Battery& a, const Battery& b) { return a.light < b.light; });
    return out;
}

bool sensorBarAbove() { return prefs::get("wii_sensor_bar", "below") == "above"; }

void setSensorBarAbove(bool above) { prefs::set("wii_sensor_bar", above ? "above" : "below"); }

}  // namespace wiiremote
