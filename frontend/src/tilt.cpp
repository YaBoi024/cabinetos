#include "tilt.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>

#include "players.h"

namespace tilt {
namespace {

// What was asked, per seat and sensor (Accel, Gyro).
bool gAsked[players::kMax][2];

int slot(Sensor s) { return s == Sensor::Gyro ? 1 : 0; }

SDL_SensorType typeOf(Sensor s) { return s == Sensor::Gyro ? SDL_SENSOR_GYRO : SDL_SENSOR_ACCEL; }

// The pad in `player`'s seat with `s` on, or null when the seat is empty or
// its pad has no such sensor. Turned on here when it is not on yet: the pad
// that took the seat after the ask.
SDL_Gamepad* padWith(int player, Sensor s) {
    SDL_Gamepad* gp = players::gamepad(player);
    const SDL_SensorType type = typeOf(s);
    if (!gp || !SDL_GamepadHasSensor(gp, type)) return nullptr;
    if (!SDL_GamepadSensorEnabled(gp, type) && !SDL_SetGamepadSensorEnabled(gp, type, true))
        return nullptr;
    return gp;
}

}  // namespace

bool set(int player, Sensor s, bool on) {
    if (player < 0 || player >= players::kMax) return !on;
    gAsked[player][slot(s)] = on;
    if (on) return padWith(player, s) != nullptr;
    SDL_Gamepad* gp = players::gamepad(player);
    if (gp && SDL_GamepadHasSensor(gp, typeOf(s))) SDL_SetGamepadSensorEnabled(gp, typeOf(s), false);
    return true;
}

bool read(int player, Sensor s, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (player < 0 || player >= players::kMax || !gAsked[player][slot(s)]) return false;
    SDL_Gamepad* gp = padWith(player, s);
    if (gp && SDL_GetGamepadSensorData(gp, typeOf(s), out, 3)) return true;
    out[0] = out[1] = out[2] = 0.0f;
    return false;
}

void reset() {
    for (auto& seat : gAsked) seat[0] = seat[1] = false;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; i < n; ++i) {
        SDL_Gamepad* gp = SDL_GetGamepadFromID(ids[i]);
        if (!gp) continue;
        for (const SDL_SensorType type : {SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO})
            if (SDL_GamepadSensorEnabled(gp, type)) SDL_SetGamepadSensorEnabled(gp, type, false);
    }
    SDL_free(ids);
}

// ---- The tests' own pads ----------------------------------------------------

// Under a made-up vendor and product, which testStart lets SDL take.
constexpr Uint16 kTestVendor = 0x1209, kTestProduct = 0x7117;

bool testStart(const char* alsoTake) {
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    char only[96];
    std::snprintf(only, sizeof only, "0x%04x/0x%04x%s%s", kTestVendor, kTestProduct, alsoTake ? "," : "",
                  alsoTake ? alsoTake : "");
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, only);
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

uint32_t testPad(bool sensors) {
    static const SDL_VirtualJoystickSensorDesc both[] = {{SDL_SENSOR_ACCEL, 250.0f},
                                                         {SDL_SENSOR_GYRO, 250.0f}};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = kTestVendor;
    desc.product_id = kTestProduct;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    desc.name = "tilt test pad";
    if (sensors) {
        desc.nsensors = 2;
        desc.sensors = both;
    }
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id) players::added(id);
    return id;
}

void testUnplug(uint32_t pad) {
    players::removed(pad);
    SDL_DetachVirtualJoystick(pad);
}

void testFeel(uint32_t pad, Sensor s, float x, float y, float z) {
    const float v[3] = {x, y, z};
    if (SDL_Gamepad* gp = SDL_GetGamepadFromID(pad))
        SDL_SendJoystickVirtualSensorData(SDL_GetGamepadJoystick(gp), typeOf(s), SDL_GetTicksNS(), v,
                                          3);
    SDL_UpdateGamepads();
}

// ---- --tilt-test ------------------------------------------------------------

namespace {

bool is(const float v[3], float x, float y, float z) {
    return std::fabs(v[0] - x) < 1e-5f && std::fabs(v[1] - y) < 1e-5f && std::fabs(v[2] - z) < 1e-5f;
}

}  // namespace

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    if (!testStart()) return 1;

    float v[3];
    // Player 1's pad has no sensors; player 2's has both.
    const SDL_JoystickID plain = testPad(false);
    const SDL_JoystickID gyro = testPad(true);
    check(plain && gyro && players::playerOf(plain) == 0 && players::playerOf(gyro) == 1,
          "two test pads are seated, the one with no sensors as player 1");

    check(!set(0, Sensor::Accel, true) && !set(0, Sensor::Gyro, true),
          "asking player 1's sensors answers no: that pad has none");
    check(set(1, Sensor::Accel, true) && set(1, Sensor::Gyro, true),
          "asking player 2's sensors answers yes");
    testFeel(gyro, Sensor::Accel, 1.5f, 9.80665f, -2.25f);
    testFeel(gyro, Sensor::Gyro, 0.5f, -1.25f, 3.0f);
    check(read(1, Sensor::Accel, v) && is(v, 1.5f, 9.80665f, -2.25f),
          "player 2's accelerometer reads as SDL hands it over, m/s2, x y z");
    check(read(1, Sensor::Gyro, v) && is(v, 0.5f, -1.25f, 3.0f),
          "player 2's gyroscope reads as SDL hands it over, rad/s, x y z");
    check(!read(0, Sensor::Accel, v) && is(v, 0, 0, 0) && !read(0, Sensor::Gyro, v) && is(v, 0, 0, 0),
          "player 1 reads nothing, and never player 2's pad");
    check(!set(2, Sensor::Gyro, true) && !read(2, Sensor::Gyro, v) && is(v, 0, 0, 0),
          "an empty seat answers no and reads nothing");
    check(!set(-1, Sensor::Gyro, true) && !set(players::kMax, Sensor::Gyro, true) &&
              set(players::kMax, Sensor::Gyro, false),
          "a seat that cannot exist answers no to on and yes to off");

    check(set(1, Sensor::Gyro, false), "turning player 2's gyroscope off answers yes");
    SDL_Gamepad* g = SDL_GetGamepadFromID(gyro);
    check(!read(1, Sensor::Gyro, v) && is(v, 0, 0, 0) && !SDL_GamepadSensorEnabled(g, SDL_SENSOR_GYRO),
          "and it is off: the pad's gyroscope is stopped and reads nothing");
    check(read(1, Sensor::Accel, v) && SDL_GamepadSensorEnabled(g, SDL_SENSOR_ACCEL),
          "its accelerometer stays on");

    // Outside a game the seats close up: the sensor pad leaves, the plain pad
    // is player 1 alone, and a new sensor pad takes seat 2.
    check(set(1, Sensor::Gyro, true), "player 2's gyroscope back on");
    testUnplug(gyro);
    check(!read(1, Sensor::Gyro, v) && is(v, 0, 0, 0), "with player 2's pad gone, seat 2 reads nothing");
    const SDL_JoystickID again = testPad(true);
    check(players::playerOf(again) == 1, "a new pad with both sensors takes seat 2");
    SDL_Gamepad* a = SDL_GetGamepadFromID(again);
    check(!SDL_GamepadSensorEnabled(a, SDL_SENSOR_GYRO), "its gyroscope is off until seat 2 is read");
    read(1, Sensor::Gyro, v);
    testFeel(again, Sensor::Gyro, -0.75f, 0.25f, 1.0f);
    check(read(1, Sensor::Gyro, v) && is(v, -0.75f, 0.25f, 1.0f),
          "and the seat's request turns it on with nothing asked again");

    // A swap moves the request with the seat, not the pad.
    check(players::swap(0, 1), "a swap puts the sensor pad in seat 1");
    check(read(0, Sensor::Gyro, v),
          "seat 1 was asked earlier, when its pad had none, so the sensor pad there reads now");
    check(!read(1, Sensor::Gyro, v) && is(v, 0, 0, 0), "seat 2 now holds the plain pad and reads nothing");
    check(set(0, Sensor::Gyro, false) && !read(0, Sensor::Gyro, v), "seat 1 turned off reads nothing");
    check(players::swap(0, 1) && read(1, Sensor::Gyro, v), "swapped back, seat 2 reads its gyroscope again");

    reset();
    check(!read(1, Sensor::Gyro, v) && !read(1, Sensor::Accel, v), "after reset no seat reads anything");
    check(!SDL_GamepadSensorEnabled(a, SDL_SENSOR_GYRO) && !SDL_GamepadSensorEnabled(a, SDL_SENSOR_ACCEL),
          "and every pad's sensors are off");

    testUnplug(again);
    testUnplug(plain);
    SDL_Quit();
    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace tilt
