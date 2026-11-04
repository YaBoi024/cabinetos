#include "vtouch.h"

#include "proc.h"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace cab::vtouch {
namespace {

// The virtual controllers' vendor, and the next product after theirs (vpad.cpp),
// so neither is taken for the other.
constexpr uint16_t kVendor = 0x1209;
constexpr uint16_t kProduct = 0xCAB1;
// Positions run 0 to kRange on both axes; gamescope takes them as fractions.
constexpr int kRange = 32767;

int gFd = -1;
bool gDown[kFingers] = {};
int gAt[kFingers][2] = {};
int gNextId = 1;

void emit(int type, int code, int value) {
    input_event ev{};
    ev.type = static_cast<uint16_t>(type);
    ev.code = static_cast<uint16_t>(code);
    ev.value = value;
    if (::write(gFd, &ev, sizeof ev) < 0) {
        // Nothing useful to do mid-game; open() already said whether it worked.
    }
}

int toRange(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return static_cast<int>(v * kRange + 0.5f);
}

// After a finger goes down or up: the single-touch keys and position that
// a reader of only one finger looks at, from the lowest finger still down.
void summary() {
    int down = 0, first = -1;
    for (int f = 0; f < kFingers; ++f)
        if (gDown[f]) {
            ++down;
            if (first < 0) first = f;
        }
    emit(EV_KEY, BTN_TOUCH, down > 0);
    emit(EV_KEY, BTN_TOOL_FINGER, down == 1);
    emit(EV_KEY, BTN_TOOL_DOUBLETAP, down == 2);
    if (first >= 0) {
        emit(EV_ABS, ABS_X, gAt[first][0]);
        emit(EV_ABS, ABS_Y, gAt[first][1]);
    }
}

std::atomic<unsigned> gAsked{0};
std::mutex gApply;

}  // namespace

bool open() {
    if (gFd >= 0) return true;
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "[vtouch] could not open /dev/uinput: %s\n", std::strerror(errno));
        return false;
    }
    // A DIRECT touch screen, so libinput takes it as one rather than as a
    // touchpad, with two slots of the kernel's multi-touch protocol (type B).
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 &&
              ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT) == 0;
    for (const int key : {BTN_TOUCH, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP})
        ok = ok && ioctl(fd, UI_SET_KEYBIT, key) == 0;
    struct Axis {
        int code, max;
    };
    const Axis axes[] = {
        {ABS_X, kRange},
        {ABS_Y, kRange},
        {ABS_MT_SLOT, kFingers - 1},
        {ABS_MT_TRACKING_ID, 65535},
        {ABS_MT_POSITION_X, kRange},
        {ABS_MT_POSITION_Y, kRange},
    };
    for (const Axis& a : axes) ok = ok && ioctl(fd, UI_SET_ABSBIT, a.code) == 0;
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = kVendor;
    setup.id.product = kProduct;
    setup.id.version = 1;
    std::snprintf(setup.name, sizeof setup.name, "CabinetOS touch screen");
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0;
    for (const Axis& a : axes) {
        uinput_abs_setup abs{};
        abs.code = static_cast<uint16_t>(a.code);
        abs.absinfo.minimum = 0;
        abs.absinfo.maximum = a.max;
        ok = ok && ioctl(fd, UI_ABS_SETUP, &abs) == 0;
    }
    ok = ok && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        std::fprintf(stderr, "[vtouch] could not create the touch screen: %s\n",
                     std::strerror(errno));
        ::close(fd);
        return false;
    }
    gFd = fd;
    for (bool& d : gDown) d = false;
    std::fprintf(stderr, "[vtouch] touch screen made\n");
    return true;
}

void close() {
    if (gFd < 0) return;
    liftAll();
    ioctl(gFd, UI_DEV_DESTROY);
    ::close(gFd);
    gFd = -1;
    std::fprintf(stderr, "[vtouch] touch screen gone\n");
}

void touch(int f, float x, float y) {
    if (gFd < 0 || f < 0 || f >= kFingers) return;
    const int px = toRange(x), py = toRange(y);
    if (gDown[f] && gAt[f][0] == px && gAt[f][1] == py) return;
    emit(EV_ABS, ABS_MT_SLOT, f);
    const bool wasDown = gDown[f];
    if (!wasDown) {
        emit(EV_ABS, ABS_MT_TRACKING_ID, gNextId);
        gNextId = gNextId % 65535 + 1;
        gDown[f] = true;
    }
    gAt[f][0] = px;
    gAt[f][1] = py;
    emit(EV_ABS, ABS_MT_POSITION_X, px);
    emit(EV_ABS, ABS_MT_POSITION_Y, py);
    if (!wasDown) summary();
    else {
        int first = 0;
        while (first < kFingers && !gDown[first]) ++first;
        if (first == f) {
            emit(EV_ABS, ABS_X, px);
            emit(EV_ABS, ABS_Y, py);
        }
    }
    emit(EV_SYN, SYN_REPORT, 0);
}

void lift(int f) {
    if (gFd < 0 || f < 0 || f >= kFingers || !gDown[f]) return;
    emit(EV_ABS, ABS_MT_SLOT, f);
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    gDown[f] = false;
    summary();
    emit(EV_SYN, SYN_REPORT, 0);
}

void liftAll() {
    for (int f = 0; f < kFingers; ++f) lift(f);
}

void passThrough(bool on) {
    const unsigned mine = ++gAsked;
    std::thread([on, mine] {
        std::lock_guard<std::mutex> lk(gApply);
        if (mine != gAsked) return;
        // gamescope's TouchClickModes (backend.h): 1 is Left, its default,
        // 4 is Passthrough.
        const proc::Result r = proc::run({"gamescopectl", "touch_click_mode", on ? "4" : "1"}, 5);
        std::fprintf(stderr, "[vtouch] gamescope touch %s: %s\n",
                     on ? "passed through" : "as clicks again",
                     r.ok() ? "gamescope took it" : "gamescope did not answer");
    }).detach();
}

}  // namespace cab::vtouch
