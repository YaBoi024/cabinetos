#include "motionserver.h"

#include "players.h"

#include <SDL3/SDL.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace cab::motion {
namespace {

constexpr uint16_t kProtocol = 1001;
constexpr uint32_t kTypeVersion = 0x100000;
constexpr uint32_t kTypeInfo = 0x100001;
constexpr uint32_t kTypeData = 0x100002;
constexpr uint32_t kServerId = 0xCAB1E705u;
constexpr int kSlots = 4;
constexpr int64_t kSubscriptionMs = 15000;
constexpr float kRadToDeg = 57.2957795f;

int gSock = -1;
bool gTried = false;
bool gDebug = false;
uint32_t gPacketNo[kSlots] = {};

struct Client {
    sockaddr_in addr{};
    int64_t seenMs = 0;
    bool all = false;
    bool slot[kSlots] = {};
};
std::vector<Client> gClients;

uint32_t crc32(const uint8_t* data, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// The protocol is little-endian.
void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = static_cast<uint8_t>(v >> 8); }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF; }
void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (v >> (8 * i)) & 0xFF; }
void putF(uint8_t* p, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(p, v); }
uint32_t get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// "+x+y+z": the three outputs (pitch, yaw, roll) each taken from one of SDL's
// axes with a sign. Changeable from session.env while tuning.
struct Map { int axis[3]; float sign[3]; };

bool parseInto(const char* s, Map& m) {
    if (!s || std::strlen(s) != 6) return false;
    for (int i = 0; i < 3; ++i) {
        const char sg = s[2 * i], ax = s[2 * i + 1];
        if ((sg != '+' && sg != '-') || ax < 'x' || ax > 'z') return false;
        m.sign[i] = sg == '-' ? -1.0f : 1.0f;
        m.axis[i] = ax - 'x';
    }
    return true;
}

Map mapFrom(const char* env) {
    Map m{};
    if (!parseInto(std::getenv(env), m)) parseInto("+x+y+z", m);
    return m;
}

const Map& gyroMap() { static const Map m = mapFrom("CABINETOS_MOTION_GYRO"); return m; }
const Map& accelMap() { static const Map m = mapFrom("CABINETOS_MOTION_ACCEL"); return m; }

// The pad seated as `slot`, if it has a gyro.
SDL_Gamepad* padOf(int slot) {
    SDL_Gamepad* gp = ::players::gamepad(slot);
    return gp && SDL_GamepadHasSensor(gp, SDL_SENSOR_GYRO) ? gp : nullptr;
}

bool readMotion(SDL_Gamepad* gp, float accel[3], float gyro[3]) {
    for (SDL_SensorType t : {SDL_SENSOR_GYRO, SDL_SENSOR_ACCEL})
        if (SDL_GamepadHasSensor(gp, t) && !SDL_GamepadSensorEnabled(gp, t))
            SDL_SetGamepadSensorEnabled(gp, t, true);
    float g[3] = {}, a[3] = {};
    if (!SDL_GetGamepadSensorData(gp, SDL_SENSOR_GYRO, g, 3)) return false;
    if (SDL_GamepadHasSensor(gp, SDL_SENSOR_ACCEL))
        SDL_GetGamepadSensorData(gp, SDL_SENSOR_ACCEL, a, 3);
    const Map& gm = gyroMap();
    const Map& am = accelMap();
    for (int i = 0; i < 3; ++i) {
        gyro[i] = gm.sign[i] * g[gm.axis[i]] * kRadToDeg;           // rad/s -> deg/s
        accel[i] = am.sign[i] * a[am.axis[i]] / SDL_STANDARD_GRAVITY;  // m/s^2 -> g
    }
    return true;
}

// The 11-byte "who is this" block both reply types carry.
void putInfo(uint8_t* p, int slot, bool connected) {
    std::memset(p, 0, 11);
    p[0] = static_cast<uint8_t>(slot);
    p[1] = connected ? 2 : 0;      // slot state: 2 = connected
    p[2] = connected ? 2 : 0;      // model: 2 = full gyro
    p[3] = connected ? 1 : 0;      // connection: 1 = USB
    p[9] = static_cast<uint8_t>(slot + 1);   // last byte of a made-up MAC
    p[10] = connected ? 5 : 0;     // battery: full
}

void header(uint8_t* b, size_t len, uint32_t type) {
    std::memset(b, 0, len);
    std::memcpy(b, "DSUS", 4);
    put16(b + 4, kProtocol);
    put16(b + 6, static_cast<uint16_t>(len - 16));
    put32(b + 12, kServerId);
    put32(b + 16, type);
}

void seal(uint8_t* b, size_t len) { put32(b + 8, crc32(b, len)); }

void reply(const uint8_t* b, size_t n, const sockaddr_in& to) {
    ::sendto(gSock, b, n, 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
}

void buildData(uint8_t* b, int slot, SDL_Gamepad* gp, uint64_t nowUs) {
    constexpr size_t kLen = 100;
    header(b, kLen, kTypeData);
    float accel[3] = {}, gyro[3] = {};
    const bool live = gp && readMotion(gp, accel, gyro);
    putInfo(b + 20, slot, live);
    b[31] = live ? 1 : 0;
    put32(b + 32, ++gPacketNo[slot]);
    b[40] = b[41] = b[42] = b[43] = 128;   // sticks centred; buttons stay zero
    put64(b + 68, nowUs);
    putF(b + 76, accel[0]); putF(b + 80, accel[1]); putF(b + 84, accel[2]);
    putF(b + 88, gyro[0]);  putF(b + 92, gyro[1]);  putF(b + 96, gyro[2]);
    seal(b, kLen);
    if (gDebug && live && slot == 0) {
        static uint64_t last = 0;
        if (nowUs - last > 1000000) {
            last = nowUs;
            std::fprintf(stderr, "[motion] slot 0 gyro %.1f %.1f %.1f deg/s, accel %.2f %.2f %.2f g\n",
                         gyro[0], gyro[1], gyro[2], accel[0], accel[1], accel[2]);
        }
    }
}

void handle(const uint8_t* b, size_t n, const sockaddr_in& from, int64_t nowMs) {
    if (n < 20 || std::memcmp(b, "DSUC", 4) != 0) return;
    const uint32_t type = get32(b + 16);
    uint8_t out[100];
    if (type == kTypeVersion) {
        header(out, 22, kTypeVersion);
        put16(out + 20, kProtocol);
        seal(out, 22);
        reply(out, 22, from);
    } else if (type == kTypeInfo && n >= 24) {
        const int count = std::clamp(static_cast<int>(get32(b + 20)), 0, kSlots);
        for (int i = 0; i < count && 24 + static_cast<size_t>(i) < n; ++i) {
            const int slot = b[24 + i];
            if (slot >= kSlots) continue;
            header(out, 32, kTypeInfo);
            putInfo(out + 20, slot, padOf(slot) != nullptr);
            seal(out, 32);
            reply(out, 32, from);
        }
    } else if (type == kTypeData && n >= 22) {
        const uint8_t flags = b[20];
        const uint8_t slot = b[21];
        Client* c = nullptr;
        for (Client& x : gClients)
            if (x.addr.sin_addr.s_addr == from.sin_addr.s_addr && x.addr.sin_port == from.sin_port) {
                c = &x;
                break;
            }
        if (!c) {
            if (gClients.size() >= 8) return;
            gClients.push_back(Client{});
            c = &gClients.back();
            c->addr = from;
            std::fprintf(stderr, "[motion] a client subscribed (port %u)\n", ntohs(from.sin_port));
        }
        c->seenMs = nowMs;
        if (flags == 0 || (flags & 2)) c->all = true;
        if ((flags & 1) && slot < kSlots) c->slot[slot] = true;
    }
}

bool openSocket() {
    gSock = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (gSock < 0) return false;
    int one = 1;
    ::setsockopt(gSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(gSock, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        std::fprintf(stderr, "[motion] cannot bind 127.0.0.1:%d: %s\n", kPort, std::strerror(errno));
        ::close(gSock);
        gSock = -1;
        return false;
    }
    return true;
}

}  // namespace

void pump(int64_t nowMs) {
    if (gSock < 0) {
        if (gTried) return;
        gTried = true;
        const char* d = std::getenv("CABINETOS_MOTION_DEBUG");
        gDebug = d && *d && *d != '0';
        if (!openSocket()) return;
        std::fprintf(stderr, "[motion] serving gyro on 127.0.0.1:%d\n", kPort);
    }

    uint8_t in[64];
    for (;;) {
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const ssize_t n = ::recvfrom(gSock, in, sizeof in, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n < 0) break;
        handle(in, static_cast<size_t>(n), from, nowMs);
    }

    gClients.erase(std::remove_if(gClients.begin(), gClients.end(),
                                  [&](const Client& c) { return nowMs - c.seenMs > kSubscriptionMs; }),
                   gClients.end());
    if (gClients.empty()) return;

    const uint64_t nowUs = SDL_GetTicksNS() / 1000;
    for (int slot = 0; slot < kSlots; ++slot) {
        bool wanted = false;
        for (const Client& c : gClients) wanted = wanted || c.all || c.slot[slot];
        if (!wanted) continue;
        uint8_t out[100];
        buildData(out, slot, padOf(slot), nowUs);
        for (const Client& c : gClients)
            if (c.all || c.slot[slot]) reply(out, sizeof out, c.addr);
    }
}

std::string edenGuid() {
    if (const char* g = std::getenv("CABINETOS_MOTION_GUID"); g && *g) return g;
    return "0000000000000000000000007F000001";   // 127.0.0.1 as a hex UUID
}

}  // namespace cab::motion