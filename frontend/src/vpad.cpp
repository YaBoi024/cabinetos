#include "vpad.h"

#include "players.h"
#include "rumble.h"
#include "tilt.h"
#include "vdualsense.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace cab::vpad {
namespace {

// pid.codes' open-source vendor ID and a product number nobody else ships, so
// no real controller can ever be mistaken for one of these, and the console's
// own SDL can be told to leave them alone (see startup.cpp).
constexpr uint16_t kVendor = 0x1209;
constexpr uint16_t kProduct = 0xCAB0;
constexpr uint16_t kVersion = 0x0001;

// In key-code order, which is SDL's numbering: SOUTH is button 0.
struct Key {
    SDL_GamepadButton button;
    int code;
};
constexpr Key kKeys[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, BTN_SOUTH},             // 0x130
    {SDL_GAMEPAD_BUTTON_EAST, BTN_EAST},               // 0x131
    {SDL_GAMEPAD_BUTTON_NORTH, BTN_NORTH},             // 0x133
    {SDL_GAMEPAD_BUTTON_WEST, BTN_WEST},               // 0x134
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, BTN_TL},        // 0x136
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, BTN_TR},       // 0x137
    {SDL_GAMEPAD_BUTTON_BACK, BTN_SELECT},             // 0x13a
    {SDL_GAMEPAD_BUTTON_START, BTN_START},             // 0x13b
    {SDL_GAMEPAD_BUTTON_GUIDE, BTN_MODE},              // 0x13c
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, BTN_THUMBL},       // 0x13d
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, BTN_THUMBR},      // 0x13e
};

// Likewise in code order: ABS_X is axis 0. The hat is not an axis to SDL.
struct Abs {
    SDL_GamepadAxis axis;
    int code;
    int min;
};
constexpr Abs kAxes[] = {
    {SDL_GAMEPAD_AXIS_LEFTX, ABS_X, -32768},
    {SDL_GAMEPAD_AXIS_LEFTY, ABS_Y, -32768},
    {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, ABS_Z, 0},
    {SDL_GAMEPAD_AXIS_RIGHTX, ABS_RX, -32768},
    {SDL_GAMEPAD_AXIS_RIGHTY, ABS_RY, -32768},
    {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, ABS_RZ, 0},
};

int gFd[kMaxPlayers] = {-1, -1, -1, -1};
// THE KIND OPEN NOW: the plain uinput controllers, or the stand-in DualSenses
// of vdualsense.h, one per player, when the emulator's row asked for them;
// and which of those carry their player's tilt, the ones whose pad had an
// accelerometer or a gyroscope when they were made.
bool gDualSense = false;
bool gTilt[kMaxPlayers] = {};

// What the emulator uploaded, per pad: a rumble's two strengths and length.
// SDL uploads one effect per pad and replays it; sixteen is plenty.
constexpr int kEffects = 16;
struct Effect {
    bool used = false;
    uint16_t strong = 0, weak = 0;
    uint16_t lengthMs = 0;
};
Effect gEffects[kMaxPlayers][kEffects];
// The rumble playing on each pad, and when it ends (0: until stopped).
int gPlaying[kMaxPlayers] = {-1, -1, -1, -1};
int64_t gEndsAt[kMaxPlayers] = {};
int gCount = 0;
// The d-pad is four buttons to SDL and one hat to evdev, so each pad's four
// are remembered to work out the hat.
bool gDpad[kMaxPlayers][4] = {};

// THE MOTION DEVICES (#683), one beside each pad whose player has a sensor,
// or -1; and what each was last told, so a sample that changes nothing is
// not written again.
int gMotionFd[kMaxPlayers] = {-1, -1, -1, -1};
constexpr int kMotionAxes = 6;
constexpr int kMotionCodes[kMotionAxes] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ};
int gMotionSaid[kMaxPlayers][kMotionAxes] = {};

// THE DUALSHOCK 3'S UNITS, turned from SDL's the way RPCS3 itself turns them
// for a pad it reads through SDL (Input/sdl_pad_handler.cpp,
// get_extended_info), so the motion device reads as RPCS3's own SDL handler
// would read the same pad: each accelerometer axis is 512 less 113 for every
// g along it (MOTION_ONE_G, Emu/Io/pad_types.h), each gyroscope axis 512
// plus 123 for every 90 degrees a second. Lying flat and still, Y reads 399,
// which is RPCS3's DEFAULT_MOTION_Y, and the rest read 512: that is what a
// motion device says before its first sample, and what it says when its
// seat's pad has gone or has no such sensor.
constexpr int kMotionMax = 1023, kMotionMid = 512;
constexpr float kPerG = 113.0f;
constexpr float kPerDegreeASecond = 123.0f / 90.0f;
constexpr int kAtRest[kMotionAxes] = {kMotionMid, kMotionMid - 113, kMotionMid,
                                      kMotionMid, kMotionMid, kMotionMid};

int motionUnits(float v) {
    return std::clamp(static_cast<int>(std::lround(v)), 0, kMotionMax);
}

// What player `player`'s pad reads now, in those units: X, Y, Z, RX, RY, RZ.
void motionSample(int player, int out[kMotionAxes]) {
    float a[3], g[3];
    const bool accel = tilt::read(player, tilt::Sensor::Accel, a);
    const bool gyro = tilt::read(player, tilt::Sensor::Gyro, g);
    for (int i = 0; i < 3; ++i) {
        out[i] = accel ? motionUnits(kMotionMid - a[i] / SDL_STANDARD_GRAVITY * kPerG) : kAtRest[i];
        out[3 + i] = gyro ? motionUnits(kMotionMid + g[i] * (180.0f / SDL_PI_F) * kPerDegreeASecond)
                          : kAtRest[3 + i];
    }
}

std::string motionNameOf(int index) {
    return "CabinetOS player " + std::to_string(index + 1) + " motion";
}

void emit(int fd, int type, int code, int value) {
    input_event ev{};
    ev.type = static_cast<uint16_t>(type);
    ev.code = static_cast<uint16_t>(code);
    ev.value = value;
    if (::write(fd, &ev, sizeof ev) < 0) {
        // Nothing useful to do mid-game; a pad that stopped is reported by
        // the emulator not moving, and open() already said whether it worked.
    }
}

void sync(int fd) { emit(fd, EV_SYN, SYN_REPORT, 0); }

int create(int index) {
    // Read as well as written: force-feedback requests come back on it.
    const int fd = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0;
    for (const Key& k : kKeys) ok = ok && ioctl(fd, UI_SET_KEYBIT, k.code) == 0;
    for (const Abs& a : kAxes) ok = ok && ioctl(fd, UI_SET_ABSBIT, a.code) == 0;
    ok = ok && ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X) == 0 && ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y) == 0;
    ok = ok && ioctl(fd, UI_SET_EVBIT, EV_FF) == 0 && ioctl(fd, UI_SET_FFBIT, FF_RUMBLE) == 0;

    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = kVendor;
    setup.id.product = kProduct;
    setup.id.version = kVersion;
    std::snprintf(setup.name, sizeof setup.name, "CabinetOS player %d", index + 1);
    setup.ff_effects_max = kEffects;
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0;

    auto absSetup = [&](int code, int min, int max) {
        uinput_abs_setup a{};
        a.code = static_cast<uint16_t>(code);
        a.absinfo.minimum = min;
        a.absinfo.maximum = max;
        return ioctl(fd, UI_ABS_SETUP, &a) == 0;
    };
    for (const Abs& a : kAxes) ok = ok && absSetup(a.code, a.min, 32767);
    ok = ok && absSetup(ABS_HAT0X, -1, 1) && absSetup(ABS_HAT0Y, -1, 1);
    ok = ok && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// A MOTION DEVICE, as RPCS3's evdev handler finds one (add_motion_device):
// its name, the accelerometer property, absolute axes and nothing else. The
// same identity as the pads, which the udev rule picks it out by, with the
// property. Written only, so opened write-only.
int createMotion(int index) {
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 &&
              ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_ACCELEROMETER) == 0;
    for (const int code : kMotionCodes) ok = ok && ioctl(fd, UI_SET_ABSBIT, code) == 0;

    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = kVendor;
    setup.id.product = kProduct;
    setup.id.version = kVersion;
    std::snprintf(setup.name, sizeof setup.name, "%s", motionNameOf(index).c_str());
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0;
    for (int i = 0; i < kMotionAxes; ++i) {
        uinput_abs_setup a{};
        a.code = static_cast<uint16_t>(kMotionCodes[i]);
        a.absinfo.minimum = 0;
        a.absinfo.maximum = kMotionMax;
        a.absinfo.value = kAtRest[i];
        ok = ok && ioctl(fd, UI_ABS_SETUP, &a) == 0;
    }
    ok = ok && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        const int err = errno;
        ::close(fd);
        errno = err;
        return -1;
    }
    std::copy(kAtRest, kAtRest + kMotionAxes, gMotionSaid[index]);
    return fd;
}

// Player `p`'s tilt, into its motion device: only the axes that changed,
// then one report.
void writeMotion(int p) {
    int now[kMotionAxes];
    motionSample(p, now);
    bool changed = false;
    for (int i = 0; i < kMotionAxes; ++i) {
        if (now[i] == gMotionSaid[p][i]) continue;
        emit(gMotionFd[p], EV_ABS, kMotionCodes[i], now[i]);
        gMotionSaid[p][i] = now[i];
        changed = true;
    }
    if (changed) sync(gMotionFd[p]);
}

// THE SEAT'S SENSORS ON, AND ITS MOTION DEVICE, for each player whose pad has
// an accelerometer or a gyroscope. What is asked stays with the seat
// (tilt.h), so the pad there when pump() reads it is the one read.
void openMotion(int players) {
    for (int i = 0; i < players; ++i) {
        const bool accel = tilt::set(i, tilt::Sensor::Accel, true);
        const bool gyro = tilt::set(i, tilt::Sensor::Gyro, true);
        if (accel || gyro) gMotionFd[i] = createMotion(i);
        if (gMotionFd[i] >= 0) {
            std::fprintf(stderr, "[vpad] player %d's motion: \"%s\" (accelerometer %s, gyroscope %s)\n",
                         i + 1, motionNameOf(i).c_str(), accel ? "on" : "none",
                         gyro ? "on" : "none");
            continue;
        }
        if (accel || gyro)
            std::fprintf(stderr, "[vpad] could not create player %d's motion device: %s\n", i + 1,
                         std::strerror(errno));
        else
            std::fprintf(stderr, "[vpad] player %d's pad has no motion sensors\n", i + 1);
        tilt::set(i, tilt::Sensor::Accel, false);
        tilt::set(i, tilt::Sensor::Gyro, false);
    }
}

// THE SEAT'S SENSORS ON, for a stand-in DualSense, which carries the tilt
// itself; and whether the seat's pad has any, which decides the stand-in it
// gets (vdualsense.h, "A pad without sensors"). What is asked stays with the
// seat (tilt.h), so a pad that has none reads nothing and pump() writes its
// stand-in a pad lying still.
bool openSenses(int player) {
    const bool accel = tilt::set(player, tilt::Sensor::Accel, true);
    const bool gyro = tilt::set(player, tilt::Sensor::Gyro, true);
    if (accel || gyro)
        std::fprintf(stderr, "[vpad] player %d's stand-in carries its pad's tilt (accelerometer %s, gyroscope %s)\n",
                     player + 1, accel ? "on" : "none", gyro ? "on" : "none");
    else
        std::fprintf(stderr, "[vpad] player %d's pad has no motion sensors; neither has its stand-in\n",
                     player + 1);
    return accel || gyro;
}

void closeSenses() {
    for (int p = 0; p < kMaxPlayers; ++p) {
        tilt::set(p, tilt::Sensor::Accel, false);
        tilt::set(p, tilt::Sensor::Gyro, false);
    }
}

// Each player's tilt into its stand-in, and the rumble the stand-in was asked
// for to the player's real pad: pump()'s half for the DualSenses.
void pumpDualSense() {
    for (int p = 0; p < gCount; ++p) {
        float a[3], g[3];
        const bool accel = tilt::read(p, tilt::Sensor::Accel, a);
        const bool gyro = tilt::read(p, tilt::Sensor::Gyro, g);
        vdualsense::motion(p, accel ? a : nullptr, gyro ? g : nullptr);
        uint16_t strong = 0, weak = 0;
        if (vdualsense::rumbleChanged(p, &strong, &weak)) {
            rumble::set(static_cast<unsigned>(p), 0, strong);
            rumble::set(static_cast<unsigned>(p), 1, weak);
        }
    }
}

}  // namespace

bool open(int players, bool motion, bool dualsense, bool onlyPlayerOneTilts) {
    close();
    if (players < 1) players = 1;
    if (players > kMaxPlayers) players = kMaxPlayers;
    if (dualsense) {
        bool ok = true;
        for (int i = 0; i < players && ok; ++i) {
            if (i > 0 && onlyPlayerOneTilts) {
                gTilt[i] = false;
                std::fprintf(stderr, "[vpad] player %d's stand-in carries no tilt: this game reads player 1's alone\n",
                             i + 1);
            } else {
                gTilt[i] = openSenses(i);
            }
            ok = vdualsense::open(i, gTilt[i]);
        }
        if (ok) {
            gDualSense = true;
            gCount = players;
            rumble::reset();
            std::fprintf(stderr, "[vpad] %d stand-in DualSense(s)\n", gCount);
            return true;
        }
        vdualsense::closeAll();
        closeSenses();
        std::memset(gTilt, 0, sizeof gTilt);
        std::fprintf(stderr, "[vpad] falling back to the plain virtual controllers, which carry no tilt\n");
    }
    for (int i = 0; i < players; ++i) {
        gFd[i] = create(i);
        if (gFd[i] < 0) {
            std::fprintf(stderr, "[vpad] could not create player %d's controller: %s\n", i + 1,
                         std::strerror(errno));
            close();
            return false;
        }
        ++gCount;
    }
    std::memset(gDpad, 0, sizeof gDpad);
    for (auto& pad : gEffects)
        for (Effect& e : pad) e = Effect{};
    for (int& p : gPlaying) p = -1;
    rumble::reset();
    std::fprintf(stderr, "[vpad] %d virtual controller(s)\n", gCount);
    if (motion) openMotion(players);
    return true;
}

void close() {
    if (gCount > 0) rumble::reset();
    if (gDualSense) {
        vdualsense::closeAll();
        closeSenses();
        gDualSense = false;
        std::memset(gTilt, 0, sizeof gTilt);
        gCount = 0;
        return;
    }
    for (int& fd : gFd) {
        if (fd < 0) continue;
        ioctl(fd, UI_DEV_DESTROY);
        ::close(fd);
        fd = -1;
    }
    for (int p = 0; p < kMaxPlayers; ++p) {
        if (gMotionFd[p] < 0) continue;
        ioctl(gMotionFd[p], UI_DEV_DESTROY);
        ::close(gMotionFd[p]);
        gMotionFd[p] = -1;
        tilt::set(p, tilt::Sensor::Accel, false);
        tilt::set(p, tilt::Sensor::Gyro, false);
    }
    gCount = 0;
}

int count() { return gCount; }

bool carriesMotion(int player) {
    return gDualSense && player >= 0 && player < gCount && gTilt[player];
}

std::string motionName(int player) {
    if (player < 0 || player >= kMaxPlayers || gMotionFd[player] < 0) return "";
    return motionNameOf(player);
}

void button(int player, SDL_GamepadButton b, bool down) {
    if (player < 0 || player >= gCount) return;
    if (gDualSense) {
        vdualsense::button(player, b, down);
        return;
    }
    const int fd = gFd[player];
    const int dpad = b == SDL_GAMEPAD_BUTTON_DPAD_UP      ? 0
                     : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN  ? 1
                     : b == SDL_GAMEPAD_BUTTON_DPAD_LEFT  ? 2
                     : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 3
                                                          : -1;
    if (dpad >= 0) {
        bool* d = gDpad[player];
        d[dpad] = down;
        emit(fd, EV_ABS, ABS_HAT0Y, d[0] == d[1] ? 0 : d[0] ? -1 : 1);
        emit(fd, EV_ABS, ABS_HAT0X, d[2] == d[3] ? 0 : d[2] ? -1 : 1);
        sync(fd);
        return;
    }
    for (const Key& k : kKeys) {
        if (k.button != b) continue;
        emit(fd, EV_KEY, k.code, down ? 1 : 0);
        sync(fd);
        return;
    }
}

void axis(int player, SDL_GamepadAxis a, int16_t value) {
    if (player < 0 || player >= gCount) return;
    if (gDualSense) {
        vdualsense::axis(player, a, value);
        return;
    }
    for (const Abs& x : kAxes) {
        if (x.axis != a) continue;
        emit(gFd[player], EV_ABS, x.code, value);
        sync(gFd[player]);
        return;
    }
}

void releaseAll() {
    if (gDualSense) {
        for (int p = 0; p < gCount; ++p) vdualsense::releaseAll(p);
        return;
    }
    for (int p = 0; p < gCount; ++p) {
        const int fd = gFd[p];
        for (const Key& k : kKeys) emit(fd, EV_KEY, k.code, 0);
        for (const Abs& x : kAxes) emit(fd, EV_ABS, x.code, 0);
        emit(fd, EV_ABS, ABS_HAT0X, 0);
        emit(fd, EV_ABS, ABS_HAT0Y, 0);
        sync(fd);
        std::memset(gDpad[p], 0, sizeof gDpad[p]);
    }
}

void pump(int64_t nowMs) {
    if (gDualSense) {
        pumpDualSense();
        return;
    }
    for (int p = 0; p < gCount; ++p) {
        const int fd = gFd[p];
        input_event ev;
        while (::read(fd, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev)) {
            if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
                uinput_ff_upload up{};
                up.request_id = static_cast<uint32_t>(ev.value);
                if (ioctl(fd, UI_BEGIN_FF_UPLOAD, &up) != 0) continue;
                const int id = up.effect.id;
                if (up.effect.type == FF_RUMBLE && id >= 0 && id < kEffects) {
                    Effect& e = gEffects[p][id];
                    e.used = true;
                    e.strong = up.effect.u.rumble.strong_magnitude;
                    e.weak = up.effect.u.rumble.weak_magnitude;
                    e.lengthMs = up.effect.replay.length;
                    up.retval = 0;
                } else {
                    up.retval = -EINVAL;
                }
                ioctl(fd, UI_END_FF_UPLOAD, &up);
            } else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
                uinput_ff_erase er{};
                er.request_id = static_cast<uint32_t>(ev.value);
                if (ioctl(fd, UI_BEGIN_FF_ERASE, &er) != 0) continue;
                if (er.effect_id < kEffects) gEffects[p][er.effect_id] = Effect{};
                if (gPlaying[p] == static_cast<int>(er.effect_id)) {
                    gPlaying[p] = -1;
                    rumble::set(static_cast<unsigned>(p), 0, 0);
                    rumble::set(static_cast<unsigned>(p), 1, 0);
                }
                er.retval = 0;
                ioctl(fd, UI_END_FF_ERASE, &er);
            } else if (ev.type == EV_FF && ev.code < kEffects) {
                const Effect& e = gEffects[p][ev.code];
                if (ev.value > 0 && e.used) {
                    gPlaying[p] = ev.code;
                    gEndsAt[p] = e.lengthMs ? nowMs + e.lengthMs : 0;
                    rumble::set(static_cast<unsigned>(p), 0, e.strong);
                    rumble::set(static_cast<unsigned>(p), 1, e.weak);
                } else if (gPlaying[p] == ev.code) {
                    gPlaying[p] = -1;
                    rumble::set(static_cast<unsigned>(p), 0, 0);
                    rumble::set(static_cast<unsigned>(p), 1, 0);
                }
            }
        }
        if (gPlaying[p] >= 0 && gEndsAt[p] > 0 && nowMs >= gEndsAt[p]) {
            gPlaying[p] = -1;
            rumble::set(static_cast<unsigned>(p), 0, 0);
            rumble::set(static_cast<unsigned>(p), 1, 0);
        }
        if (gMotionFd[p] >= 0) writeMotion(p);
    }
}

std::string edenGuid() {
    if (gDualSense) return vdualsense::edenGuid();
    // SDL's GUID for a USB evdev device: bus, the name's CRC, vendor, 0,
    // product, 0, version, then two driver bytes, each 16-bit field little
    // endian. Eden clears the CRC, so it is written as zero here.
    const uint16_t words[8] = {0x0003, 0, kVendor, 0, kProduct, 0, kVersion, 0};
    char out[33];
    for (int i = 0; i < 8; ++i)
        std::snprintf(out + i * 4, 5, "%02x%02x", words[i] & 0xFF, words[i] >> 8);
    return out;
}

std::string sdlGuid(int player) {
    if (gDualSense) return vdualsense::sdlGuid(player);
    // SDL_CreateJoystickGUID for a Linux evdev device (SDL_sysjoystick.c,
    // IsJoystick): no vendor name, so the checksum is of the device name
    // alone, the one create() gives it.
    char name[32];
    std::snprintf(name, sizeof name, "CabinetOS player %d", player + 1);
    const uint16_t crc = SDL_crc16(0, name, std::strlen(name));
    const uint16_t words[8] = {0x0003, crc, kVendor, 0, kProduct, 0, kVersion, 0};
    char out[33];
    for (int i = 0; i < 8; ++i)
        std::snprintf(out + i * 4, 5, "%02x%02x", words[i] & 0xFF, words[i] >> 8);
    return out;
}

std::string sdlIds() {
    if (gDualSense) return vdualsense::sdlIds();
    char out[16];
    std::snprintf(out, sizeof out, "0x%04x/0x%04x", kVendor, kProduct);
    return out;
}

std::string sdlMapping(int player) {
    auto b = [](SDL_GamepadButton x) { return "b" + std::to_string(buttonIndex(x)); };
    auto a = [](SDL_GamepadAxis x) { return "a" + std::to_string(axisIndex(x)); };
    return sdlGuid(player) + ",CabinetOS player " + std::to_string(player + 1) +
           ",a:" + b(SDL_GAMEPAD_BUTTON_SOUTH) + ",b:" + b(SDL_GAMEPAD_BUTTON_EAST) +
           ",x:" + b(SDL_GAMEPAD_BUTTON_WEST) + ",y:" + b(SDL_GAMEPAD_BUTTON_NORTH) +
           ",back:" + b(SDL_GAMEPAD_BUTTON_BACK) + ",guide:" + b(SDL_GAMEPAD_BUTTON_GUIDE) +
           ",start:" + b(SDL_GAMEPAD_BUTTON_START) +
           ",leftshoulder:" + b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) +
           ",rightshoulder:" + b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) +
           ",leftstick:" + b(SDL_GAMEPAD_BUTTON_LEFT_STICK) +
           ",rightstick:" + b(SDL_GAMEPAD_BUTTON_RIGHT_STICK) +
           ",leftx:" + a(SDL_GAMEPAD_AXIS_LEFTX) + ",lefty:" + a(SDL_GAMEPAD_AXIS_LEFTY) +
           ",rightx:" + a(SDL_GAMEPAD_AXIS_RIGHTX) + ",righty:" + a(SDL_GAMEPAD_AXIS_RIGHTY) +
           ",lefttrigger:" + a(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) +
           ",righttrigger:" + a(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) +
           ",dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,platform:Linux,";
}

int buttonIndex(SDL_GamepadButton b) {
    if (gDualSense) return vdualsense::buttonIndex(b);
    for (size_t i = 0; i < sizeof kKeys / sizeof kKeys[0]; ++i)
        if (kKeys[i].button == b) return static_cast<int>(i);
    return -1;
}

int axisIndex(SDL_GamepadAxis a) {
    if (gDualSense) return vdualsense::axisIndex(a);
    for (size_t i = 0; i < sizeof kAxes / sizeof kAxes[0]; ++i)
        if (kAxes[i].axis == a) return static_cast<int>(i);
    return -1;
}

// ---- --motion-device-test ---------------------------------------------------
//
// REAL DEVICES, so never while a PS3 game runs on the same console: its
// motion devices have these names, and RPCS3 finds them by name.

namespace {

// Where player `p`'s motion device is in sysfs (`/sys/class/input/inputN`),
// or empty.
std::string motionSysPath(int p) {
    char sys[64] = {};
    if (gMotionFd[p] < 0 || ioctl(gMotionFd[p], UI_GET_SYSNAME(sizeof sys - 1), sys) < 0) return "";
    return std::string("/sys/class/input/") + sys;
}

// Its event node, opened read-only as RPCS3 opens it once udev has given the
// console's user access (up to three seconds), or -1.
int openMotionNode(int p) {
    std::string node;
    if (DIR* d = opendir(motionSysPath(p).c_str())) {
        while (const dirent* e = readdir(d))
            if (std::strncmp(e->d_name, "event", 5) == 0) node = std::string("/dev/input/") + e->d_name;
        closedir(d);
    }
    for (int tries = 0; !node.empty() && tries < 60; ++tries) {
        const int fd = ::open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) return fd;
        SDL_Delay(50);
    }
    return -1;
}

bool hasBit(const unsigned long* bits, int n) {
    constexpr int kPer = 8 * sizeof(unsigned long);
    return (bits[n / kPer] >> (n % kPer)) & 1ul;
}

// What RPCS3's add_motion_device and the issue ask of the device: its name,
// the accelerometer property, absolute axes X Y Z RX RY RZ and no others,
// each 0 to 1023, and no buttons.
bool shapedForRpcs3(int fd, const std::string& name) {
    char got[128] = {};
    unsigned long props[(INPUT_PROP_MAX + 1 + 63) / 64] = {};
    unsigned long types[(EV_MAX + 1 + 63) / 64] = {};
    unsigned long axes[(ABS_MAX + 1 + 63) / 64] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof got - 1), got) < 0 || name != got ||
        ioctl(fd, EVIOCGPROP(sizeof props), props) < 0 || !hasBit(props, INPUT_PROP_ACCELEROMETER) ||
        ioctl(fd, EVIOCGBIT(0, sizeof types), types) < 0 || !hasBit(types, EV_ABS) ||
        hasBit(types, EV_KEY) || ioctl(fd, EVIOCGBIT(EV_ABS, sizeof axes), axes) < 0)
        return false;
    for (int code = 0; code <= ABS_MAX; ++code) {
        const bool ours = std::find(kMotionCodes, kMotionCodes + kMotionAxes, code) !=
                          kMotionCodes + kMotionAxes;
        if (hasBit(axes, code) != ours) return false;
        input_absinfo a{};
        if (ours && (ioctl(fd, EVIOCGABS(code), &a) < 0 || a.minimum != 0 || a.maximum != kMotionMax))
            return false;
    }
    return true;
}

// What the node reads now, X Y Z RX RY RZ, against what is wanted.
bool reads(int fd, std::initializer_list<int> want) {
    int i = 0;
    for (const int w : want) {
        input_absinfo a{};
        if (ioctl(fd, EVIOCGABS(kMotionCodes[i++]), &a) < 0 || a.value != w) return false;
    }
    return true;
}

bool sensorsOn(uint32_t pad, bool on) {
    SDL_Gamepad* gp = SDL_GetGamepadFromID(pad);
    return gp && SDL_GamepadSensorEnabled(gp, SDL_SENSOR_ACCEL) == on &&
           SDL_GamepadSensorEnabled(gp, SDL_SENSOR_GYRO) == on;
}

}  // namespace

int motionTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    if (!tilt::testStart()) return 1;
    // As in a game, a pad that leaves keeps its seat (players.h).
    players::setInGame(true);
    const uint32_t one = tilt::testPad(true), two = tilt::testPad(true), plain = tilt::testPad(false);
    check(one && two && plain && players::playerOf(plain) == 2,
          "three test pads seated: players 1 and 2 with both sensors, player 3 with none");

    // AN EMULATOR THAT READS NO MOTION DEVICE.
    if (!open(3, /*motion=*/false, /*dualsense=*/false, /*onlyPlayerOneTilts=*/false)) {
        std::printf("  FAIL  no virtual controllers: /dev/uinput cannot be opened\n");
        players::setInGame(false);
        SDL_Quit();
        return 1;
    }
    check(motionName(0).empty() && motionName(1).empty() && sensorsOn(one, false),
          "without motion asked, no motion device and no sensor on, whatever the pads have");
    close();

    // RPCS3.
    check(open(3, /*motion=*/true, /*dualsense=*/false, /*onlyPlayerOneTilts=*/false),
          "three virtual controllers, motion asked");
    check(motionName(0) == "CabinetOS player 1 motion" && motionName(1) == "CabinetOS player 2 motion" &&
              motionName(2).empty(),
          "players 1 and 2 get a motion device; player 3, whose pad has no sensors, none");
    check(sensorsOn(one, true) && sensorsOn(two, true),
          "players 1's and 2's accelerometers and gyroscopes are on");
    const std::string sys1 = motionSysPath(0), sys2 = motionSysPath(1);
    const int n1 = openMotionNode(0), n2 = openMotionNode(1);
    check(n1 >= 0 && n2 >= 0,
          "the console's user can open both motion devices, as RPCS3 must (the udev rule)");
    check(n1 >= 0 && n2 >= 0 && shapedForRpcs3(n1, motionName(0)) && shapedForRpcs3(n2, motionName(1)),
          "each has its name, the accelerometer property, axes X Y Z RX RY RZ from 0 to 1023, "
          "and nothing else");
    // SDL gives a pad, as its sensors, the sensor device whose serial string
    // is the pad's own, and skips one it cannot read a serial string from
    // (SDL_sysjoystick.c, GetSensor). A uinput device has none at all, so the
    // console's own SDL never takes a motion device for any pad's sensors.
    char uniq[64] = {};
    check(n1 >= 0 && ioctl(n1, EVIOCGUNIQ(sizeof uniq - 1), uniq) < 0 && errno == ENOENT,
          "it has no serial string, so SDL never gives it to a pad as that pad's sensors");
    check(n1 >= 0 && reads(n1, {512, 399, 512, 512, 512, 512}),
          "before any sample it reads at rest, lying flat: 512, 399 and 512, and 512 on each "
          "gyroscope axis");

    // Player 1's pad lying flat and still; player 2's tilted, in SDL's units:
    // one g along x, two against z; one radian a second about x, a quarter
    // turn a second about y (the yaw), minus one about z.
    tilt::testFeel(one, tilt::Sensor::Accel, 0.0f, SDL_STANDARD_GRAVITY, 0.0f);
    tilt::testFeel(one, tilt::Sensor::Gyro, 0.0f, 0.0f, 0.0f);
    tilt::testFeel(two, tilt::Sensor::Accel, SDL_STANDARD_GRAVITY, 0.0f, -2.0f * SDL_STANDARD_GRAVITY);
    tilt::testFeel(two, tilt::Sensor::Gyro, 1.0f, SDL_PI_F / 2.0f, -1.0f);
    pump(0);
    check(n2 >= 0 && reads(n2, {399, 512, 738, 590, 635, 434}),
          "player 2's tilt lands in the DualShock 3's units: 512 less 113 a g, and 512 plus 123 "
          "per 90 degrees a second, the yaw on RY");
    check(n1 >= 0 && reads(n1, {512, 399, 512, 512, 512, 512}), "and player 1's device has not moved");

    tilt::testFeel(two, tilt::Sensor::Accel, 0.0f, 10.0f * SDL_STANDARD_GRAVITY,
                   -10.0f * SDL_STANDARD_GRAVITY);
    tilt::testFeel(two, tilt::Sensor::Gyro, 0.0f, -20.0f, 20.0f);
    pump(0);
    check(n2 >= 0 && reads(n2, {512, 0, 1023, 512, 0, 1023}),
          "past the DualShock 3's range a reading stops at its ends, 0 and 1023");

    tilt::testFeel(one, tilt::Sensor::Accel, 0.0f, 0.0f, SDL_STANDARD_GRAVITY);
    pump(0);
    check(n1 >= 0 && reads(n1, {512, 512, 399, 512, 512, 512}),
          "player 1's own pad, stood on its end, moves player 1's device");

    tilt::testUnplug(two);
    pump(0);
    check(n2 >= 0 && reads(n2, {512, 399, 512, 512, 512, 512}),
          "with player 2's pad gone, its device goes back to rest");

    if (n1 >= 0) ::close(n1);
    if (n2 >= 0) ::close(n2);
    close();
    check(sensorsOn(one, false), "closing turns the seats' sensors off");
    check(!sys1.empty() && !sys2.empty() && ::access(sys1.c_str(), F_OK) != 0 &&
              ::access(sys2.c_str(), F_OK) != 0 && motionName(0).empty(),
          "and both motion devices are gone with their pads");

    tilt::testUnplug(one);
    tilt::testUnplug(plain);
    players::setInGame(false);
    SDL_Quit();
    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace cab::vpad
