#include "vdualsense.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uhid.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace cab::vdualsense {
namespace {

// Sony's DualSense Edge, over USB (the header says why). The kernel's driver
// marks every device it makes by setting the top bit of the version
// (hid-playstation.c, HID_PLAYSTATION_VERSION_PATCH), so that is the version
// SDL sees and puts in the GUID; 0x0111 is a DualSense's own.
constexpr uint16_t kVendor = 0x054C, kProduct = 0x0DF2, kVersion = 0x0111;
constexpr uint16_t kVersionSeen = kVersion | 0x8000;

// A DualSense's USB report descriptor, as inputtino carries it
// (src/uhid/include/uhid/ps5.hpp, MIT), itself a transcription of a real pad's
// (nondebug/dualsense, report-descriptor-usb.txt). Input report 1 is 64 bytes
// with its id, output report 2, and the feature reports the driver asks for
// are 5 (41 bytes), 9 (20) and 32 (64). The driver reads none of this beyond
// parsing it; the sizes are what the HID core and hidraw go by.
constexpr uint8_t kDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01,
    0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0F, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0F,
    0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x21, 0x95, 0x0D, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x22, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x34,
    0x81, 0x02,
    0x85, 0x02, 0x09, 0x23, 0x95, 0x2F, 0x91, 0x02,
    0x85, 0x05, 0x09, 0x33, 0x95, 0x28, 0xB1, 0x02,
    0x85, 0x08, 0x09, 0x34, 0x95, 0x2F, 0xB1, 0x02,
    0x85, 0x09, 0x09, 0x24, 0x95, 0x13, 0xB1, 0x02,
    0x85, 0x0A, 0x09, 0x25, 0x95, 0x1A, 0xB1, 0x02,
    0x85, 0x20, 0x09, 0x26, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x21, 0x09, 0x27, 0x95, 0x04, 0xB1, 0x02,
    0x85, 0x22, 0x09, 0x40, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x80, 0x09, 0x28, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x81, 0x09, 0x29, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x82, 0x09, 0x2A, 0x95, 0x09, 0xB1, 0x02,
    0x85, 0x83, 0x09, 0x2B, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x84, 0x09, 0x2C, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0x85, 0x09, 0x2D, 0x95, 0x02, 0xB1, 0x02,
    0x85, 0xA0, 0x09, 0x2E, 0x95, 0x01, 0xB1, 0x02,
    0x85, 0xE0, 0x09, 0x2F, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0xF0, 0x09, 0x30, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0xF1, 0x09, 0x31, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0xF2, 0x09, 0x32, 0x95, 0x0F, 0xB1, 0x02,
    0x85, 0xF4, 0x09, 0x35, 0x95, 0x3F, 0xB1, 0x02,
    0x85, 0xF5, 0x09, 0x36, 0x95, 0x03, 0xB1, 0x02,
    0xC0,
};
static_assert(sizeof kDescriptor == 273);

// The three feature reports the driver asks for, in the order it asks.
constexpr uint8_t kReportCalibration = 0x05, kReportPairing = 0x09, kReportFirmware = 0x20;
constexpr size_t kCalibrationSize = 41, kPairingSize = 20, kFirmwareSize = 64;
constexpr uint8_t kReportInput = 0x01, kReportOutput = 0x02;
constexpr size_t kInputSize = 64;

// THE CALIBRATION, as the header derives it: the accelerometer's plus and
// minus make the driver pass a raw count through as 1/8192 g, and the
// gyroscope's plus, minus and speed make it multiply by 64, so a raw count is
// 1/16 of a degree a second. In the report each is a little-endian 16-bit
// word at the offsets dualsense_get_calibration_data reads: gyro bias at 1, 3
// and 5, gyro plus and minus at 7 to 18, speed plus and minus at 19 and 21,
// accelerometer plus and minus at 23 to 34.
constexpr int16_t kGyroPlus = 16384, kGyroMinus = -16384, kGyroSpeed = 1024;
constexpr int16_t kAccelPlus = 8192, kAccelMinus = -8192;
constexpr float kAccelPerG = 8192.0f;
constexpr float kGyroPerDegreeASecond = 16.0f;
// The driver's own resolutions for the node it makes (DS_ACC_RES_PER_G,
// DS_GYRO_RES_PER_DEG_S), which the test reads the node back by.
constexpr int kNodePerG = 8192, kNodePerDegreeASecond = 1024;
constexpr int kNodeGyroFactor = 2 * kGyroSpeed * kNodePerDegreeASecond / (kGyroPlus - kGyroMinus);
static_assert(kNodeGyroFactor == 64);
static_assert(2 * kNodePerG == kAccelPlus - kAccelMinus);

void put16(uint8_t* at, int16_t v) {
    at[0] = static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF);
    at[1] = static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8);
}

void put32(uint8_t* at, uint32_t v) {
    for (int i = 0; i < 4; ++i) at[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

// The made-up address of player `player`'s stand-in and the bytes the pairing
// report carries it as: the driver copies six bytes from offset 1 and prints
// them last first (its "%pMR"), and every device it makes carries it as uniq.
// "ca:b1:4e:70:50:0N" for a stand-in that carries its pad's tilt, the only
// address the udev rule gives the seat's user the motion device of;
// "ca:b1:4e:70:51:0N" for one whose pad has no sensors (the header).
std::string addressOf(int player, bool tilt) {
    char out[18];
    std::snprintf(out, sizeof out, "ca:b1:4e:70:%02x:%02x", tilt ? 0x50 : 0x51, player + 1);
    return out;
}

void addressBytes(int player, bool tilt, uint8_t out[6]) {
    out[0] = static_cast<uint8_t>(player + 1);
    out[1] = tilt ? 0x50 : 0x51;
    out[2] = 0x70;
    out[3] = 0x4e;
    out[4] = 0xb1;
    out[5] = 0xca;
}

// What the frame thread last said about a player, kept in the report's own
// terms: sticks and triggers 0 to 255, buttons by SDL's name, the sensors as
// the raw words the report carries. A pad at rest, face up, before anything
// is said.
struct State {
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128, l2 = 0, r2 = 0;
    bool keys[SDL_GAMEPAD_BUTTON_COUNT] = {};
    int16_t gyro[3] = {0, 0, 0};
    int16_t accel[3] = {0, static_cast<int16_t>(kAccelPerG), 0};
};

struct Fake {
    int fd = -1;
    bool started = false;   // the kernel's UHID_START has come
    bool tilt = true;       // its pad has sensors: which address it wears
    State state;
    uint8_t seq = 0;
    uint16_t strong = 0, weak = 0;
    bool rumbleNew = false;
    Nodes nodes;
};

std::mutex gLock;
Fake gFakes[kMaxPlayers];
std::thread gThread;
std::atomic<bool> gStop{false};
int gWake[2] = {-1, -1};   // a pipe the report thread wakes on when the set of fds changes

int16_t clamp16(long v) {
    return static_cast<int16_t>(std::clamp<long>(v, -32768, 32767));
}

uint8_t hatOf(const bool keys[SDL_GAMEPAD_BUTTON_COUNT]) {
    const int x = (keys[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] ? 1 : 0) - (keys[SDL_GAMEPAD_BUTTON_DPAD_LEFT] ? 1 : 0);
    const int y = (keys[SDL_GAMEPAD_BUTTON_DPAD_DOWN] ? 1 : 0) - (keys[SDL_GAMEPAD_BUTTON_DPAD_UP] ? 1 : 0);
    // The DualSense's eight directions clockwise from north; 8 is centred
    // (hid-playstation.c, ps_gamepad_hat_mapping).
    if (y < 0) return x < 0 ? 7 : x > 0 ? 1 : 0;
    if (y > 0) return x < 0 ? 5 : x > 0 ? 3 : 4;
    return x < 0 ? 6 : x > 0 ? 2 : 8;
}

uint32_t sensorTimestamp() {
    // The report's clock counts thirds of a microsecond; the driver turns it
    // back into microseconds and handles the wrap.
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
    return static_cast<uint32_t>(ns * 3 / 1000);
}

// Input report 0x01, 64 bytes, laid out as the driver's dualsense_input_report
// after the id byte: sticks, triggers, a sequence number, four button bytes,
// the six sensor words, the sensor clock, two touch points (both lifted) and
// the status bytes, whose first says a full battery that is not charging.
void buildReport(const Fake& f, uint8_t seq, uint8_t out[kInputSize]) {
    std::memset(out, 0, kInputSize);
    const State& s = f.state;
    out[0] = kReportInput;
    out[1] = s.lx;
    out[2] = s.ly;
    out[3] = s.rx;
    out[4] = s.ry;
    out[5] = s.l2;
    out[6] = s.r2;
    out[7] = seq;
    const bool* k = s.keys;
    out[8] = static_cast<uint8_t>(hatOf(k) | (k[SDL_GAMEPAD_BUTTON_WEST] << 4) |
                                  (k[SDL_GAMEPAD_BUTTON_SOUTH] << 5) | (k[SDL_GAMEPAD_BUTTON_EAST] << 6) |
                                  (k[SDL_GAMEPAD_BUTTON_NORTH] << 7));
    // A real pad's L2 and R2 bits flip a little way into the pull.
    out[9] = static_cast<uint8_t>(
        (k[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] << 0) | (k[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] << 1) |
        ((s.l2 > 64) << 2) | ((s.r2 > 64) << 3) | (k[SDL_GAMEPAD_BUTTON_BACK] << 4) |
        (k[SDL_GAMEPAD_BUTTON_START] << 5) | (k[SDL_GAMEPAD_BUTTON_LEFT_STICK] << 6) |
        (k[SDL_GAMEPAD_BUTTON_RIGHT_STICK] << 7));
    out[10] = static_cast<uint8_t>(k[SDL_GAMEPAD_BUTTON_GUIDE] ? 1 : 0);
    for (int i = 0; i < 3; ++i) {
        put16(out + 16 + 2 * i, s.gyro[i]);
        put16(out + 22 + 2 * i, s.accel[i]);
    }
    put32(out + 28, sensorTimestamp());
    out[33] = 0x80;   // touch point 1: not touching
    out[37] = 0x80;   // touch point 2
    out[53] = 0x2A;   // battery: full (2 in the high nibble), 10 of 10 in the low
}

bool uhidWrite(int fd, const uhid_event& ev) {
    return ::write(fd, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev);
}

void sendReport(Fake& f) {
    uhid_event ev{};
    ev.type = UHID_INPUT2;
    ev.u.input2.size = kInputSize;
    buildReport(f, f.seq++, ev.u.input2.data);
    uhidWrite(f.fd, ev);
}

// The driver's three questions, answered the way a DualSense answers them;
// anything else is refused, which the driver reads as an error.
void answerGetReport(Fake& f, const uhid_get_report_req& req) {
    uhid_event ev{};
    ev.type = UHID_GET_REPORT_REPLY;
    ev.u.get_report_reply.id = req.id;
    uint8_t* d = ev.u.get_report_reply.data;
    if (req.rtype == UHID_FEATURE_REPORT && req.rnum == kReportCalibration) {
        d[0] = kReportCalibration;
        for (int axis = 0; axis < 3; ++axis) {
            put16(d + 7 + 4 * axis, kGyroPlus);
            put16(d + 9 + 4 * axis, kGyroMinus);
            put16(d + 23 + 4 * axis, kAccelPlus);
            put16(d + 25 + 4 * axis, kAccelMinus);
        }
        put16(d + 19, kGyroSpeed);
        put16(d + 21, kGyroSpeed);
        ev.u.get_report_reply.size = kCalibrationSize;
    } else if (req.rtype == UHID_FEATURE_REPORT && req.rnum == kReportPairing) {
        d[0] = kReportPairing;
        addressBytes(static_cast<int>(&f - gFakes), f.tilt, d + 1);
        ev.u.get_report_reply.size = kPairingSize;
    } else if (req.rtype == UHID_FEATURE_REPORT && req.rnum == kReportFirmware) {
        // Hardware and firmware versions at 24 and 28, the feature version at
        // 44: made up, and only logged by the driver. SDL's own PlayStation
        // driver, should it open the stand-in, reads the feature version too
        // and wants 2.36 or newer for a DualSense's rumble; the Edge's id
        // already decides that for it.
        d[0] = kReportFirmware;
        put32(d + 24, 0x00010001);
        put32(d + 28, 0x00010001);
        put16(d + 44, 0x0300);
        ev.u.get_report_reply.size = kFirmwareSize;
    } else {
        ev.u.get_report_reply.err = EIO;
    }
    uhidWrite(f.fd, ev);
}

// Output report 0x02 from the driver: rumble when its flags say so
// (dualsense_output_worker: HAPTICS_SELECT in the first flag byte and
// COMPATIBLE_VIBRATION2 in the third, or COMPATIBLE_VIBRATION in the first),
// the right motor at 3 and the left at 4, each 0 to 255, the left being the
// strong one (dualsense_play_effect). The lightbar, the player lights, the
// audio path and the microphone are ignored.
void takeOutput(Fake& f, const uhid_output_req& req) {
    if (req.rtype != UHID_OUTPUT_REPORT || req.size < 48 || req.data[0] != kReportOutput) return;
    const uint8_t flag0 = req.data[1], flag2 = req.data[39];
    if (!((flag0 & 0x03) || (flag2 & 0x04))) return;
    const uint16_t weak = static_cast<uint16_t>(req.data[3] * 257);
    const uint16_t strong = static_cast<uint16_t>(req.data[4] * 257);
    if (strong == f.strong && weak == f.weak) return;
    f.strong = strong;
    f.weak = weak;
    f.rumbleNew = true;
}

void takeEvent(Fake& f, const uhid_event& ev) {
    switch (ev.type) {
        case UHID_START: f.started = true; break;
        case UHID_STOP: f.started = false; break;
        case UHID_GET_REPORT: answerGetReport(f, ev.u.get_report); break;
        case UHID_SET_REPORT: {
            uhid_event reply{};
            reply.type = UHID_SET_REPORT_REPLY;
            reply.u.set_report_reply.id = ev.u.set_report.id;
            uhidWrite(f.fd, reply);
            break;
        }
        case UHID_OUTPUT: takeOutput(f, ev.u.output); break;
        default: break;   // OPEN and CLOSE: the driver keeps it open for good
    }
}

int64_t nowNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000ll + ts.tv_nsec;
}

constexpr int64_t kPeriodNs = 4000000;   // 250 reports a second, a DualSense's rate on a cable

// THE REPORT THREAD: answers the kernel's questions as they come and sends
// every stand-in its report on the tick. It touches only this file's state,
// under gLock, and never SDL or players.h.
void reportLoop() {
    int64_t next = nowNs() + kPeriodNs;
    while (!gStop.load()) {
        pollfd pfds[kMaxPlayers + 1];
        int who[kMaxPlayers + 1];
        int n = 0;
        pfds[n].fd = gWake[0];
        pfds[n].events = POLLIN;
        who[n++] = -1;
        {
            std::lock_guard<std::mutex> lk(gLock);
            for (int p = 0; p < kMaxPlayers; ++p) {
                if (gFakes[p].fd < 0) continue;
                pfds[n].fd = gFakes[p].fd;
                pfds[n].events = POLLIN;
                who[n++] = p;
            }
        }
        const int64_t wait = std::max<int64_t>(0, next - nowNs());
        timespec until{};
        until.tv_sec = static_cast<time_t>(wait / 1000000000ll);
        until.tv_nsec = static_cast<long>(wait % 1000000000ll);
        if (ppoll(pfds, static_cast<nfds_t>(n), &until, nullptr) < 0 && errno != EINTR) break;
        if (gStop.load()) break;
        for (int i = 0; i < n; ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;
            if (who[i] < 0) {
                char drain[16];
                while (::read(gWake[0], drain, sizeof drain) > 0) {}
                continue;
            }
            for (;;) {
                uhid_event ev{};
                std::lock_guard<std::mutex> lk(gLock);
                Fake& f = gFakes[who[i]];
                if (f.fd != pfds[i].fd) break;   // closed meanwhile
                const ssize_t got = ::read(f.fd, &ev, sizeof ev);
                if (got != static_cast<ssize_t>(sizeof ev)) break;
                takeEvent(f, ev);
            }
        }
        const int64_t now = nowNs();
        if (now < next) continue;
        // Behind by more than a period (the machine was busy): start again
        // from now rather than send a burst.
        next = (now - next > kPeriodNs) ? now + kPeriodNs : next + kPeriodNs;
        std::lock_guard<std::mutex> lk(gLock);
        for (Fake& f : gFakes)
            if (f.fd >= 0 && f.started) sendReport(f);
    }
}

void wakeThread() {
    if (gWake[1] >= 0) {
        const char c = 1;
        if (::write(gWake[1], &c, 1) < 0) {}
    }
}

void startThread() {
    if (gThread.joinable()) return;
    if (gWake[0] < 0 && ::pipe2(gWake, O_CLOEXEC | O_NONBLOCK) != 0) return;
    gStop.store(false);
    gThread = std::thread(reportLoop);
}

void stopThread() {
    if (!gThread.joinable()) return;
    gStop.store(true);
    wakeThread();
    gThread.join();
    for (int& fd : gWake) {
        if (fd >= 0) ::close(fd);
        fd = -1;
    }
}

std::string readLine(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return {};
    char buf[160] = {};
    if (!std::fgets(buf, sizeof buf, f)) buf[0] = 0;
    std::fclose(f);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

// The event node under an input device's sysfs folder, as a path under /dev.
std::string eventNodeIn(const std::string& inputDir) {
    std::string node;
    if (DIR* d = opendir(inputDir.c_str())) {
        while (const dirent* e = readdir(d))
            if (std::strncmp(e->d_name, "event", 5) == 0) node = std::string("/dev/input/") + e->d_name;
        closedir(d);
    }
    return node;
}

// What the kernel made for the stand-in with address `uniq`: every uhid
// device in sysfs is walked, since Sunshine's and the Wii bridge's stand-ins
// live there too, and only the input devices carrying our address are ours.
Nodes findNodes(const std::string& uniq) {
    Nodes out;
    DIR* root = opendir("/sys/devices/virtual/misc/uhid");
    if (!root) return out;
    while (const dirent* hid = readdir(root)) {
        if (hid->d_name[0] == '.') continue;
        const std::string inputs = std::string("/sys/devices/virtual/misc/uhid/") + hid->d_name + "/input";
        DIR* d = opendir(inputs.c_str());
        if (!d) continue;
        while (const dirent* in = readdir(d)) {
            if (std::strncmp(in->d_name, "input", 5) != 0) continue;
            const std::string dir = inputs + "/" + in->d_name;
            if (readLine(dir + "/uniq") != uniq) continue;
            const std::string name = readLine(dir + "/name");
            const std::string node = eventNodeIn(dir);
            if (node.empty()) continue;
            out.uniq = uniq;
            if (name.size() >= 15 && name.compare(name.size() - 15, 15, " Motion Sensors") == 0)
                out.motion = node;
            else if (name.size() >= 9 && name.compare(name.size() - 9, 9, " Touchpad") == 0)
                out.touchpad = node;
            else if (name.size() >= 13 && name.compare(name.size() - 13, 13, " Headset Jack") == 0)
                continue;
            else
                out.gamepad = node;
        }
        closedir(d);
    }
    closedir(root);
    return out;
}

}  // namespace

std::string name(int player) { return "CabinetOS player " + std::to_string(player + 1); }

bool open(int player, bool tilt) {
    if (player < 0 || player >= kMaxPlayers) return false;
    close(player);
    const std::string address = addressOf(player, tilt);
    const int fd = ::open("/dev/uhid", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        std::fprintf(stderr, "[vdualsense] player %d: /dev/uhid: %s\n", player + 1, std::strerror(errno));
        return false;
    }
    uhid_event ev{};
    ev.type = UHID_CREATE2;
    uhid_create2_req& c = ev.u.create2;
    std::snprintf(reinterpret_cast<char*>(c.name), sizeof c.name, "%s", name(player).c_str());
    std::snprintf(reinterpret_cast<char*>(c.phys), sizeof c.phys, "cabinetos/uhid/player%d", player + 1);
    std::snprintf(reinterpret_cast<char*>(c.uniq), sizeof c.uniq, "%s", address.c_str());
    c.rd_size = sizeof kDescriptor;
    std::memcpy(c.rd_data, kDescriptor, sizeof kDescriptor);
    c.bus = BUS_USB;
    c.vendor = kVendor;
    c.product = kProduct;
    c.version = kVersion;
    c.country = 0;
    {
        std::lock_guard<std::mutex> lk(gLock);
        startThread();
        if (!gThread.joinable() || !uhidWrite(fd, ev)) {
            const int err = errno;
            ::close(fd);
            std::fprintf(stderr, "[vdualsense] player %d: could not create the device: %s\n", player + 1,
                         std::strerror(err));
            return false;
        }
        Fake& f = gFakes[player];
        f = Fake{};
        f.fd = fd;
        f.tilt = tilt;
    }
    wakeThread();
    // The driver probes on a worker of the kernel's, asking its three
    // questions on the way, which the report thread answers; the devices
    // follow within tens of milliseconds.
    const int64_t deadline = nowNs() + 2000000000ll;
    Nodes nodes;
    while (nowNs() < deadline) {
        nodes = findNodes(address);
        if (!nodes.gamepad.empty() && !nodes.motion.empty() && !nodes.touchpad.empty()) break;
        usleep(20000);
    }
    if (nodes.gamepad.empty() || nodes.motion.empty() || nodes.touchpad.empty()) {
        std::fprintf(stderr,
                     "[vdualsense] player %d: the kernel's PlayStation driver made no devices within 2 s "
                     "(gamepad %s, motion %s, touchpad %s)\n",
                     player + 1, nodes.gamepad.empty() ? "missing" : "there",
                     nodes.motion.empty() ? "missing" : "there", nodes.touchpad.empty() ? "missing" : "there");
        close(player);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(gLock);
        gFakes[player].nodes = nodes;
    }
    std::fprintf(stderr, "[vdualsense] player %d is up: \"%s\", uniq %s, %s (pad), %s (motion), %s (touchpad)\n",
                 player + 1, name(player).c_str(), nodes.uniq.c_str(), nodes.gamepad.c_str(),
                 nodes.motion.c_str(), nodes.touchpad.c_str());
    return true;
}

void close(int player) {
    if (player < 0 || player >= kMaxPlayers) return;
    bool last = true;
    {
        std::lock_guard<std::mutex> lk(gLock);
        Fake& f = gFakes[player];
        if (f.fd >= 0) {
            uhid_event ev{};
            ev.type = UHID_DESTROY;
            uhidWrite(f.fd, ev);
            ::close(f.fd);
        }
        f = Fake{};
        for (const Fake& other : gFakes)
            if (other.fd >= 0) last = false;
    }
    wakeThread();
    if (last) stopThread();
}

void closeAll() {
    for (int p = 0; p < kMaxPlayers; ++p) close(p);
}

bool isOpen(int player) {
    if (player < 0 || player >= kMaxPlayers) return false;
    std::lock_guard<std::mutex> lk(gLock);
    return gFakes[player].fd >= 0;
}

void button(int player, SDL_GamepadButton b, bool down) {
    if (player < 0 || player >= kMaxPlayers || b < 0 || b >= SDL_GAMEPAD_BUTTON_COUNT) return;
    std::lock_guard<std::mutex> lk(gLock);
    gFakes[player].state.keys[b] = down;
}

void axis(int player, SDL_GamepadAxis a, int16_t value) {
    if (player < 0 || player >= kMaxPlayers) return;
    // SDL's -32768 to 32767 onto the pad's 0 to 255, up and left low as on a
    // real DualSense; a trigger's 0 to 32767 onto 0 to 255.
    const uint8_t stick = static_cast<uint8_t>((static_cast<int>(value) + 32768) >> 8);
    const uint8_t pull = static_cast<uint8_t>(std::max<int>(value, 0) >> 7);
    std::lock_guard<std::mutex> lk(gLock);
    State& s = gFakes[player].state;
    switch (a) {
        case SDL_GAMEPAD_AXIS_LEFTX: s.lx = stick; break;
        case SDL_GAMEPAD_AXIS_LEFTY: s.ly = stick; break;
        case SDL_GAMEPAD_AXIS_RIGHTX: s.rx = stick; break;
        case SDL_GAMEPAD_AXIS_RIGHTY: s.ry = stick; break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: s.l2 = pull; break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: s.r2 = pull; break;
        default: break;
    }
}

void releaseAll(int player) {
    if (player < 0 || player >= kMaxPlayers) return;
    std::lock_guard<std::mutex> lk(gLock);
    State& s = gFakes[player].state;
    const State rest;
    s.lx = rest.lx;
    s.ly = rest.ly;
    s.rx = rest.rx;
    s.ry = rest.ry;
    s.l2 = rest.l2;
    s.r2 = rest.r2;
    std::memset(s.keys, 0, sizeof s.keys);
}

void motion(int player, const float* accel, const float* gyro) {
    if (player < 0 || player >= kMaxPlayers) return;
    const State rest;
    std::lock_guard<std::mutex> lk(gLock);
    State& s = gFakes[player].state;
    for (int i = 0; i < 3; ++i) {
        s.accel[i] = accel ? clamp16(std::lround(accel[i] / SDL_STANDARD_GRAVITY * kAccelPerG)) : rest.accel[i];
        s.gyro[i] = gyro ? clamp16(std::lround(gyro[i] * (180.0f / SDL_PI_F) * kGyroPerDegreeASecond))
                         : rest.gyro[i];
    }
}

bool rumbleChanged(int player, uint16_t* strong, uint16_t* weak) {
    if (player < 0 || player >= kMaxPlayers) return false;
    std::lock_guard<std::mutex> lk(gLock);
    Fake& f = gFakes[player];
    *strong = f.strong;
    *weak = f.weak;
    const bool changed = f.rumbleNew;
    f.rumbleNew = false;
    return changed;
}

namespace {

// SDL's GUID for a USB evdev device (SDL_joystick.c, SDL_CreateJoystickGUID):
// bus, the name's CRC, vendor, 0, product, 0, version, then two driver bytes,
// each 16-bit field little endian. The version carries the driver's mark.
std::string guidWith(uint16_t crc) {
    const uint16_t words[8] = {0x0003, crc, kVendor, 0, kProduct, 0, kVersionSeen, 0};
    char out[33];
    for (int i = 0; i < 8; ++i)
        std::snprintf(out + i * 4, 5, "%02x%02x", words[i] & 0xFF, words[i] >> 8);
    return out;
}

}  // namespace

// Eden clears the CRC, so it is written as zero.
std::string edenGuid() { return guidWith(0); }

// SDL's Linux joystick code checksums the device's name alone, there being no
// vendor name for an evdev device (IsJoystick), as for the plain pads.
std::string sdlGuid(int player) {
    const std::string n = name(player);
    return guidWith(SDL_crc16(0, n.c_str(), n.size()));
}

std::string sdlIds() {
    char out[16];
    std::snprintf(out, sizeof out, "0x%04x/0x%04x", kVendor, kProduct);
    return out;
}

int buttonIndex(SDL_GamepadButton b) {
    // The driver's gamepad buttons in key-code order (the header), which is
    // SDL's numbering: the two shoulder buttons' own key codes, BTN_TL2 and
    // BTN_TR2, sit at 6 and 7 and have no SDL button, since SDL reads the
    // triggers as axes.
    constexpr SDL_GamepadButton kOrder[] = {
        SDL_GAMEPAD_BUTTON_SOUTH,          // 0  BTN_SOUTH   Cross
        SDL_GAMEPAD_BUTTON_EAST,           // 1  BTN_EAST    Circle
        SDL_GAMEPAD_BUTTON_NORTH,          // 2  BTN_NORTH   Triangle
        SDL_GAMEPAD_BUTTON_WEST,           // 3  BTN_WEST    Square
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  // 4  BTN_TL      L1
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, // 5  BTN_TR      R1
        SDL_GAMEPAD_BUTTON_INVALID,        // 6  BTN_TL2     L2
        SDL_GAMEPAD_BUTTON_INVALID,        // 7  BTN_TR2     R2
        SDL_GAMEPAD_BUTTON_BACK,           // 8  BTN_SELECT  Create
        SDL_GAMEPAD_BUTTON_START,          // 9  BTN_START   Options
        SDL_GAMEPAD_BUTTON_GUIDE,          // 10 BTN_MODE    PS
        SDL_GAMEPAD_BUTTON_LEFT_STICK,     // 11 BTN_THUMBL  L3
        SDL_GAMEPAD_BUTTON_RIGHT_STICK,    // 12 BTN_THUMBR  R3
    };
    if (b == SDL_GAMEPAD_BUTTON_INVALID) return -1;
    for (size_t i = 0; i < sizeof kOrder / sizeof kOrder[0]; ++i)
        if (kOrder[i] == b) return static_cast<int>(i);
    return -1;
}

int axisIndex(SDL_GamepadAxis a) {
    // ABS_X, ABS_Y, ABS_Z (L2), ABS_RX, ABS_RY, ABS_RZ (R2), in code order;
    // the d-pad is hat 0, not an axis.
    switch (a) {
        case SDL_GAMEPAD_AXIS_LEFTX: return 0;
        case SDL_GAMEPAD_AXIS_LEFTY: return 1;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return 2;
        case SDL_GAMEPAD_AXIS_RIGHTX: return 3;
        case SDL_GAMEPAD_AXIS_RIGHTY: return 4;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return 5;
        default: return -1;
    }
}

Nodes nodes(int player) {
    if (player < 0 || player >= kMaxPlayers) return {};
    std::lock_guard<std::mutex> lk(gLock);
    return gFakes[player].fd >= 0 ? gFakes[player].nodes : Nodes{};
}

}  // namespace cab::vdualsense

// ---- --dualsense-test ---------------------------------------------------------
//
// REAL DEVICES, through the same path a Switch, Wii U or Vita game takes
// (vpad.h), read back through SDL as the emulator's SDL reads them. Never
// while one of those games runs on the same console: the console's own
// stand-ins would be made beside the game's.

#include "players.h"
#include "tilt.h"
#include "vpad.h"

namespace cab::vdualsense {
namespace {

// Lets SDL see what has arrived: a few turns of its event pump, which is where
// SDL's Linux joystick code polls udev for new devices (LINUX_JoystickDetect:
// "Polling will happen in the main event loop"), and of its gamepad update.
void settle(int ms) {
    for (int t = 0; t < ms; t += 10) {
        SDL_Delay(10);
        SDL_PumpEvents();
        SDL_UpdateGamepads();
    }
}

// The stand-in SDL knows at `gamepadNode`, found through its path, or 0.
SDL_JoystickID sdlIdOf(const std::string& gamepadNode) {
    int n = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&n);
    SDL_JoystickID found = 0;
    for (int i = 0; ids && i < n; ++i) {
        const char* path = SDL_GetJoystickPathForID(ids[i]);
        if (path && gamepadNode == path) found = ids[i];
    }
    SDL_free(ids);
    return found;
}

bool near3(const float v[3], float x, float y, float z, float within) {
    return std::fabs(v[0] - x) < within && std::fabs(v[1] - y) < within && std::fabs(v[2] - z) < within;
}

// What the node holds now for `code`, through the ioctl Eden's SDL reads the
// axis's range by; INT32_MIN when it cannot be read.
int32_t nodeValue(int fd, int code) {
    input_absinfo a{};
    if (ioctl(fd, EVIOCGABS(code), &a) < 0) return INT32_MIN;
    return a.value;
}

int rawAccel(float g) { return static_cast<int>(std::lround(g * kAccelPerG)); }
int rawGyro(float radiansASecond) {
    return static_cast<int>(std::lround(radiansASecond * (180.0f / SDL_PI_F) * kGyroPerDegreeASecond)) *
           kNodeGyroFactor;
}

}  // namespace

int test() {
    if (::access("/dev/uhid", F_OK) != 0) {
        std::printf("no /dev/uhid on this machine (CI's builder has none), so there is nothing to test here; "
                    "it runs for real on a console\n\nall passed\n");
        return 0;
    }
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // SDL takes the stand-ins (Sony's id, the kernel's event nodes) and the
    // test's own pads, and no real pad on the machine; its HID drivers are
    // off, as the emulators' are.
    if (!tilt::testStart(sdlIds().c_str())) return 1;
    players::setInGame(true);
    const uint32_t sensors = tilt::testPad(true), plain = tilt::testPad(false);
    check(sensors && plain && players::playerOf(sensors) == 0 && players::playerOf(plain) == 1,
          "two test pads seated: player 1's with an accelerometer and a gyroscope, player 2's with none");
    const int seated = players::count();

    const bool up = cab::vpad::open(2, /*motion=*/false, /*dualsense=*/true, /*onlyPlayerOneTilts=*/false);
    check(up, "two stand-ins made through /dev/uhid, as for a Switch, Wii U or Vita game");
    check(up && cab::vpad::carriesMotion(0) && !cab::vpad::carriesMotion(1),
          "player 1's carries its pad's tilt; player 2's, whose pad has no sensors, does not");
    check(up && cab::vpad::sdlIds() == "0x054c/0x0df2",
          "the emulator's SDL is told to take Sony's DualSense Edge, 0x054c/0x0df2, and nothing else");
    const Nodes n1 = nodes(0), n2 = nodes(1);
    check(!n1.gamepad.empty() && !n1.motion.empty() && !n1.touchpad.empty() && n1.uniq == "ca:b1:4e:70:50:01",
          "the kernel's driver made player 1's gamepad, motion and touchpad devices, uniq ca:b1:4e:70:50:01");
    check(!n2.gamepad.empty() && !n2.motion.empty() && !n2.touchpad.empty() && n2.uniq == "ca:b1:4e:70:51:02",
          "and player 2's, uniq ca:b1:4e:70:51:02, the address of a stand-in for a pad without sensors");

    // THE CONSOLE'S OWN SDL sees them as pads and must not seat them.
    SDL_JoystickID fake1 = 0, fake2 = 0;
    for (int tries = 0; (fake1 == 0 || fake2 == 0) && tries < 150; ++tries) {
        settle(20);
        if (!n1.gamepad.empty()) fake1 = sdlIdOf(n1.gamepad);
        if (!n2.gamepad.empty()) fake2 = sdlIdOf(n2.gamepad);
    }
    check(fake1 && fake2, "the console's own SDL sees both, by their event nodes, within 3 s");
    if (fake1) players::added(fake1);
    if (fake2) players::added(fake2);
    check(fake1 && fake2 && players::playerOf(fake1) < 0 && players::playerOf(fake2) < 0 &&
              players::count() == seated,
          "and the console refuses to seat either: the player count stays at 2");

    // THE EMULATOR'S SDL: a game controller, the GUIDs the lines carry, the
    // motion device paired by uniq and opened (the udev rule), the tilt in
    // SDL's units.
    check(fake1 && SDL_IsGamepad(fake1),
          "it is a game controller to SDL (its own Linux mapping), which Eden needs before it turns motion on");
    char guid[33] = {}, whole1[33] = {}, whole2[33] = {};
    if (fake1) {
        SDL_GUID g = SDL_GetJoystickGUIDForID(fake1);
        SDL_GUIDToString(g, whole1, sizeof whole1);
        g.data[2] = g.data[3] = 0;
        SDL_GUIDToString(g, guid, sizeof guid);
    }
    if (fake2) SDL_GUIDToString(SDL_GetJoystickGUIDForID(fake2), whole2, sizeof whole2);
    check(fake1 && guid == cab::vpad::edenGuid() && guid == edenGuid(),
          "its GUID with the name checksum cleared is the one Eden's lines carry");
    if (fake1 && guid != cab::vpad::edenGuid())
        std::printf("        SDL says %s, the lines say %s\n", guid, cab::vpad::edenGuid().c_str());
    check(fake1 && fake2 && whole1 == cab::vpad::sdlGuid(0) && whole2 == cab::vpad::sdlGuid(1) &&
              std::string(whole1) != whole2,
          "its whole GUID, name checksum and all, is the one Cemu's profile binds and the mapping line names, "
          "and player 2's is its own");
    if (fake1 && whole1 != cab::vpad::sdlGuid(0))
        std::printf("        SDL says %s, the console says %s\n", whole1, cab::vpad::sdlGuid(0).c_str());
    SDL_Gamepad* gp1 = fake1 ? SDL_OpenGamepad(fake1) : nullptr;
    check(gp1 && SDL_GamepadHasSensor(gp1, SDL_SENSOR_ACCEL) && SDL_GamepadHasSensor(gp1, SDL_SENSOR_GYRO),
          "opened as Eden opens it, it has an accelerometer and a gyroscope: the motion device paired by uniq");
    const bool on = gp1 && SDL_SetGamepadSensorEnabled(gp1, SDL_SENSOR_ACCEL, true) &&
                    SDL_SetGamepadSensorEnabled(gp1, SDL_SENSOR_GYRO, true);
    check(on, "its sensors turn on: the seat's user may open the motion device (the udev rule)");
    if (gp1 && !on) std::printf("        SDL: %s\n", SDL_GetError());

    // Player 1's pad tilted: half a g along x, three quarters of a g up, a
    // quarter g toward the player; half a radian a second about x, minus one
    // about y, two about z. Every value differs from the resting one, since
    // the kernel passes an axis on only when it changes.
    const float ax = 0.5f * SDL_STANDARD_GRAVITY, ay = 0.75f * SDL_STANDARD_GRAVITY,
                az = -0.25f * SDL_STANDARD_GRAVITY;
    const float gx = 0.5f, gy = -1.0f, gz = 2.0f;
    tilt::testFeel(sensors, tilt::Sensor::Accel, ax, ay, az);
    tilt::testFeel(sensors, tilt::Sensor::Gyro, gx, gy, gz);
    cab::vpad::pump(0);
    settle(60);
    float a[3] = {}, g[3] = {};
    const bool readA = gp1 && SDL_GetGamepadSensorData(gp1, SDL_SENSOR_ACCEL, a, 3);
    const bool readG = gp1 && SDL_GetGamepadSensorData(gp1, SDL_SENSOR_GYRO, g, 3);
    check(readA && near3(a, ax, ay, az, 0.01f),
          "the tilt reads back through SDL as it went in: 4.90, 7.35, -2.45 m/s2 within 0.01");
    check(readG && near3(g, gx, gy, gz, 0.002f),
          "and the turn: 0.5, -1.0, 2.0 rad/s within 0.002 (a sixteenth of a degree a second)");
    if (readA && readG && !(near3(a, ax, ay, az, 0.01f) && near3(g, gx, gy, gz, 0.002f)))
        std::printf("        SDL read accel %.3f %.3f %.3f, gyro %.4f %.4f %.4f\n", a[0], a[1], a[2], g[0], g[1],
                    g[2]);
    const int motionFd = n1.motion.empty() ? -1 : ::open(n1.motion.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    check(motionFd >= 0, "the motion node opens for this user");
    check(motionFd >= 0 && nodeValue(motionFd, ABS_X) == rawAccel(0.5f) && nodeValue(motionFd, ABS_Y) == rawAccel(0.75f) &&
              nodeValue(motionFd, ABS_Z) == rawAccel(-0.25f),
          "on the node itself the accelerometer reads 4096, 6144, -2048: 1/8192 g a count, the driver's own unit");
    check(motionFd >= 0 && nodeValue(motionFd, ABS_RX) == rawGyro(gx) && nodeValue(motionFd, ABS_RY) == rawGyro(gy) &&
              nodeValue(motionFd, ABS_RZ) == rawGyro(gz),
          "and the gyroscope 29312, -58688, 117312: 1/1024 degree a second a count, 64 to our raw count");
    if (motionFd >= 0)
        std::printf("        node: X %d Y %d Z %d RX %d RY %d RZ %d\n", nodeValue(motionFd, ABS_X),
                    nodeValue(motionFd, ABS_Y), nodeValue(motionFd, ABS_Z), nodeValue(motionFd, ABS_RX),
                    nodeValue(motionFd, ABS_RY), nodeValue(motionFd, ABS_RZ));

    // Presses and sticks land where the layout says.
    cab::vpad::button(0, SDL_GAMEPAD_BUTTON_SOUTH, true);
    cab::vpad::button(0, SDL_GAMEPAD_BUTTON_BACK, true);
    cab::vpad::button(0, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
    cab::vpad::axis(0, SDL_GAMEPAD_AXIS_LEFTX, 16384);
    cab::vpad::axis(0, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    settle(40);
    SDL_Joystick* js1 = gp1 ? SDL_GetGamepadJoystick(gp1) : nullptr;
    check(js1 && SDL_GetJoystickButton(js1, cab::vpad::buttonIndex(SDL_GAMEPAD_BUTTON_SOUTH)) &&
              !SDL_GetJoystickButton(js1, cab::vpad::buttonIndex(SDL_GAMEPAD_BUTTON_EAST)) &&
              SDL_GetJoystickButton(js1, cab::vpad::buttonIndex(SDL_GAMEPAD_BUTTON_BACK)),
          "Cross is button 0 and Create is button 8, past the two shoulder buttons' own key codes");
    check(js1 && SDL_GetJoystickHat(js1, 0) == SDL_HAT_UP, "the d-pad is hat 0");
    check(js1 && std::abs(SDL_GetJoystickAxis(js1, cab::vpad::axisIndex(SDL_GAMEPAD_AXIS_LEFTX)) - 16384) < 400 &&
              SDL_GetJoystickAxis(js1, cab::vpad::axisIndex(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) > 32000,
          "the left stick's x is axis 0 and R2 axis 5, within a step of the pad's 255");
    check(gp1 && SDL_GetGamepadButton(gp1, SDL_GAMEPAD_BUTTON_SOUTH) && SDL_GetGamepadButton(gp1, SDL_GAMEPAD_BUTTON_BACK) &&
              SDL_GetGamepadButton(gp1, SDL_GAMEPAD_BUTTON_DPAD_UP),
          "and SDL's own mapping of it agrees: South, Back and Up");
    // The line Cemu and Vita3K are handed (vpad::sdlMapping), which names the
    // whole GUID checked above, so SDL takes it for this device before any
    // mapping of its own: accepted, and the same layout.
    const std::string line = cab::vpad::sdlMapping(0);
    const bool taken = line.compare(0, 33, cab::vpad::sdlGuid(0) + ",") == 0 &&
                       SDL_AddGamepadMapping(line.c_str()) >= 0;
    // A mapping added to an open pad recentres its axes in SDL, and the
    // kernel passes an axis on only when it changes, so R2 is let go and
    // pulled again; an emulator is handed the line before it opens the pad.
    cab::vpad::axis(0, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    settle(20);
    cab::vpad::axis(0, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    settle(20);
    check(taken && gp1 && SDL_GetGamepadButton(gp1, SDL_GAMEPAD_BUTTON_SOUTH) &&
              SDL_GetGamepadButton(gp1, SDL_GAMEPAD_BUTTON_BACK) && SDL_GetGamepadButton(gp1, SDL_GAMEPAD_BUTTON_DPAD_UP) &&
              !SDL_GetGamepadButton(gp1, SDL_GAMEPAD_BUTTON_EAST) &&
              SDL_GetGamepadAxis(gp1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 32000,
          "the console's mapping line for it is accepted and puts South, Back, Up and R2 where SDL's own does");
    if (char* now = gp1 ? SDL_GetGamepadMapping(gp1) : nullptr) {
        std::printf("        SDL maps it now: %s\n", now);
        SDL_free(now);
    }
    cab::vpad::releaseAll();

    // Rumble comes back through the driver: SDL's strong and weak become the
    // report's left and right motors, each in 255ths.
    uint16_t strong = 0, weak = 0;
    rumbleChanged(0, &strong, &weak);
    check(gp1 && SDL_RumbleGamepad(gp1, 0x8000, 0x4000, 400), "SDL rumbles it, as Eden would");
    bool came = false;
    for (int tries = 0; !came && tries < 30; ++tries) {
        SDL_Delay(10);
        came = rumbleChanged(0, &strong, &weak);
    }
    check(came && std::abs(static_cast<int>(strong) - 0x8080) < 300 && std::abs(static_cast<int>(weak) - 0x4040) < 300,
          "and comes back within 0.3 s: strong 128 and weak 64 of 255");
    if (came) std::printf("        rumble back: strong %u, weak %u\n", strong, weak);

    // A player whose pad has no sensors gets a stand-in whose motion device
    // the udev rule leaves to the input group (the header), so an emulator's
    // SDL, running as this user, finds no sensors on it: a pad without them,
    // as the player's own is.
    cab::vpad::pump(0);
    settle(60);
    errno = 0;
    const int motionFd2 = n2.motion.empty() ? -1 : ::open(n2.motion.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    check(!n2.motion.empty() && motionFd2 < 0 && errno == EACCES,
          "player 2's stand-in, whose pad has no sensors, keeps its motion device from this user");
    if (motionFd2 >= 0) ::close(motionFd2);
    SDL_Gamepad* gp2 = fake2 ? SDL_OpenGamepad(fake2) : nullptr;
    check(gp2 && !SDL_GamepadHasSensor(gp2, SDL_SENSOR_ACCEL) && !SDL_GamepadHasSensor(gp2, SDL_SENSOR_GYRO),
          "so SDL finds no accelerometer and no gyroscope on it, and no emulator reads tilt from it");
    check(readA && gp1 && SDL_GetGamepadSensorData(gp1, SDL_SENSOR_ACCEL, a, 3) && near3(a, ax, ay, az, 0.01f),
          "and player 1's still reads its own pad's tilt");

    // Closing takes every device away.
    if (motionFd >= 0) ::close(motionFd);
    if (gp1) SDL_CloseGamepad(gp1);
    if (gp2) SDL_CloseGamepad(gp2);
    cab::vpad::close();
    settle(100);
    check(!cab::vpad::carriesMotion(0) && nodes(0).gamepad.empty() && !isOpen(0) && !isOpen(1),
          "closing forgets both stand-ins");
    check(!n1.gamepad.empty() && ::access(n1.gamepad.c_str(), F_OK) != 0 && ::access(n1.motion.c_str(), F_OK) != 0 &&
              ::access(n1.touchpad.c_str(), F_OK) != 0 && ::access(n2.gamepad.c_str(), F_OK) != 0,
          "and the kernel has taken their devices away");

    // WII U AND VITA: the machine's one gyro is player 1's, so a third
    // player's pad with sensors still gets a stand-in without them, and its
    // seat's sensors stay off.
    const uint32_t third = tilt::testPad(true);
    const bool up3 = third && players::playerOf(third) == 2 &&
                     cab::vpad::open(3, /*motion=*/false, /*dualsense=*/true, /*onlyPlayerOneTilts=*/true);
    SDL_Gamepad* gp3 = third ? SDL_GetGamepadFromID(third) : nullptr;
    check(up3 && cab::vpad::carriesMotion(0) && !cab::vpad::carriesMotion(2) && nodes(2).uniq == "ca:b1:4e:70:51:03" &&
              gp3 && !SDL_GamepadSensorEnabled(gp3, SDL_SENSOR_ACCEL) && !SDL_GamepadSensorEnabled(gp3, SDL_SENSOR_GYRO),
          "for a Wii U or Vita game only player 1's stand-in carries tilt: player 3's, whose pad has sensors, "
          "has none, and that pad's sensors stay off");
    cab::vpad::close();
    settle(100);

    tilt::testUnplug(third);
    tilt::testUnplug(sensors);
    tilt::testUnplug(plain);
    players::setInGame(false);
    SDL_Quit();
    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace cab::vdualsense
