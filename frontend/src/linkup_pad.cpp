#include "linkup_pad.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "idle.h"
#include "players.h"
#include "tilt.h"

namespace linkuppad {

using linkupwire::kSenseAccel;
using linkupwire::kSenseGyro;
using linkupwire::Pad;

// ---- The friend's console -------------------------------------------------------

void senseHere(bool on) {
    tilt::set(0, tilt::Sensor::Accel, on);
    tilt::set(0, tilt::Sensor::Gyro, on);
}

Pad read(bool asleep, uint32_t extraButtons, const Pad& last) {
    Pad pad;
    pad.buttons = extraButtons;
    float a[3], g[3];
    const bool accel = tilt::read(0, tilt::Sensor::Accel, a);
    const bool gyro = tilt::read(0, tilt::Sensor::Gyro, g);
    pad.senses = static_cast<uint8_t>((accel ? kSenseAccel : 0) | (gyro ? kSenseGyro : 0));
    if (asleep) {
        // Let go and held still. Tilted as it was, or as it is when nothing was sent with a tilt yet.
        const bool held = (last.senses & kSenseAccel) != 0;
        if (accel)
            for (int i = 0; i < 3; ++i) pad.accel[i] = held ? last.accel[i] : a[i];
        return pad;
    }
    if (SDL_Gamepad* gp = players::gamepad(0)) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            // The console's own button never reaches the other console, and a shortcut button is this
            // console's: it opens this console's menu.
            if (b == SDL_GAMEPAD_BUTTON_GUIDE) continue;
            if (SDL_GetGamepadButton(gp, static_cast<SDL_GamepadButton>(b))) pad.buttons |= 1u << b;
        }
        for (int x = 0; x < SDL_GAMEPAD_AXIS_COUNT; ++x)
            pad.axes[x] = SDL_GetGamepadAxis(gp, static_cast<SDL_GamepadAxis>(x));
    }
    for (int i = 0; i < 3; ++i) {
        if (accel) pad.accel[i] = a[i];
        if (gyro) pad.gyro[i] = g[i];
    }
    return pad;
}

bool differs(const Pad& a, const Pad& b) {
    if (a.senses != b.senses || a.buttons != b.buttons) return true;
    for (int x = 0; x < SDL_GAMEPAD_AXIS_COUNT; ++x)
        if (a.axes[x] != b.axes[x]) return true;
    for (int i = 0; i < 3; ++i)
        if (a.accel[i] != b.accel[i] || a.gyro[i] != b.gyro[i]) return true;
    return false;
}

// ---- The host's console ---------------------------------------------------------

bool decode(const uint8_t* data, size_t len, Pad* out) {
    if (len < linkupwire::kPadWithoutTilt || data[0] != linkupwire::kTagPad) return false;
    *out = Pad{};
    std::memcpy(out, data, std::min(len, sizeof(Pad)));
    // An older console's second byte was spare and its state ends before the tilt.
    if (len < sizeof(Pad)) out->senses = 0;
    out->senses &= kSenseAccel | kSenseGyro;
    return true;
}

bool pressed(const Pad& was, const Pad& now) {
    if ((now.buttons & ~was.buttons) != 0) return true;
    for (int x = 0; x < SDL_GAMEPAD_AXIS_COUNT; ++x)
        if (now.axes[x] != was.axes[x] && std::abs(static_cast<int>(now.axes[x])) > idle::kAxisDeadzone)
            return true;
    return false;
}

namespace {

SDL_JoystickID gSeat = 0;
uint8_t gSenses = 0;

const char* sensesWords(uint8_t senses) {
    switch (senses & (kSenseAccel | kSenseGyro)) {
        case kSenseAccel | kSenseGyro: return "an accelerometer and a gyroscope";
        case kSenseAccel: return "an accelerometer";
        case kSenseGyro: return "a gyroscope";
        default: return "no motion sensors";
    }
}

}  // namespace

bool seat(uint8_t senses, RumbleTo rumble, void* userdata) {
    unseat();
    senses &= kSenseAccel | kSenseGyro;
    // The rate of the friend's controller, near enough: SDL only reports it.
    static const SDL_VirtualJoystickSensorDesc kBoth[] = {{SDL_SENSOR_ACCEL, 250.0f},
                                                          {SDL_SENSOR_GYRO, 250.0f}};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    desc.name = "LinkUp friend";
    desc.userdata = userdata;
    desc.Rumble = rumble;
    desc.nsensors = ((senses & kSenseAccel) ? 1 : 0) + ((senses & kSenseGyro) ? 1 : 0);
    desc.sensors = desc.nsensors == 0 ? nullptr : senses == kSenseGyro ? &kBoth[1] : kBoth;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        std::fprintf(stderr, "[linkup] no seat for the friend: %s\n", SDL_GetError());
        return false;
    }
    gSeat = id;
    gSenses = senses;
    players::seatFriend(id);
    std::fprintf(stderr, "[linkup] the friend is player 2, their controller with %s\n", sensesWords(senses));
    return true;
}

void unseat() {
    if (!gSeat) return;
    players::unseatFriend();
    SDL_DetachVirtualJoystick(gSeat);
    gSeat = 0;
    gSenses = 0;
}

bool seated() { return gSeat != 0; }

uint8_t seatSenses() { return gSenses; }

// Trigger axes run from the bottom of a joystick's range in SDL's virtual pad, where a pad's own
// triggers read from 0.
void play(const Pad& p) {
    SDL_Gamepad* gp = gSeat ? SDL_GetGamepadFromID(gSeat) : nullptr;
    SDL_Joystick* joy = gp ? SDL_GetGamepadJoystick(gp) : nullptr;
    if (!joy) return;
    SDL_LockJoysticks();
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
        SDL_SetJoystickVirtualButton(joy, b, (p.buttons >> b) & 1u);
    for (int x = 0; x < SDL_GAMEPAD_AXIS_COUNT; ++x) {
        int value = p.axes[x];
        if (x == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || x == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) value = value * 2 - 32768;
        SDL_SetJoystickVirtualAxis(joy, x, static_cast<Sint16>(std::clamp(value, -32768, 32767)));
    }
    // Copied out of the packed message, whose floats need not sit where a float may be read.
    const Uint64 now = SDL_GetTicksNS();
    if (gSenses & kSenseAccel) {
        const bool has = (p.senses & kSenseAccel) != 0;
        const float v[3] = {has ? p.accel[0] : 0.0f, has ? p.accel[1] : SDL_STANDARD_GRAVITY,
                            has ? p.accel[2] : 0.0f};
        SDL_SendJoystickVirtualSensorData(joy, SDL_SENSOR_ACCEL, now, v, 3);
    }
    if (gSenses & kSenseGyro) {
        const bool has = (p.senses & kSenseGyro) != 0;
        const float v[3] = {has ? p.gyro[0] : 0.0f, has ? p.gyro[1] : 0.0f, has ? p.gyro[2] : 0.0f};
        SDL_SendJoystickVirtualSensorData(joy, SDL_SENSOR_GYRO, now, v, 3);
    }
    SDL_UnlockJoysticks();
}

// ---- --linkup-pad-test ----------------------------------------------------------

namespace {

bool is(const float v[3], float x, float y, float z) {
    return std::fabs(v[0] - x) < 1e-5f && std::fabs(v[1] - y) < 1e-5f && std::fabs(v[2] - z) < 1e-5f;
}

bool tiltIs(const Pad& p, float ax, float ay, float az, float gx, float gy, float gz) {
    const float a[3] = {p.accel[0], p.accel[1], p.accel[2]};
    const float g[3] = {p.gyro[0], p.gyro[1], p.gyro[2]};
    return is(a, ax, ay, az) && is(g, gx, gy, gz);
}

void press(uint32_t pad, SDL_GamepadButton b, bool down) {
    if (SDL_Gamepad* gp = SDL_GetGamepadFromID(pad))
        SDL_SetJoystickVirtualButton(SDL_GetGamepadJoystick(gp), b, down);
    SDL_UpdateGamepads();
}

void move(uint32_t pad, SDL_GamepadAxis a, int16_t value) {
    if (SDL_Gamepad* gp = SDL_GetGamepadFromID(pad))
        SDL_SetJoystickVirtualAxis(SDL_GetGamepadJoystick(gp), a, value);
    SDL_UpdateGamepads();
}

bool noRumble(void*, Uint16, Uint16) { return true; }

// A message as the wire carries it, `len` bytes of it.
bool decodeBytes(const Pad& p, size_t len, Pad* out) {
    uint8_t bytes[sizeof(Pad)];
    std::memcpy(bytes, &p, sizeof bytes);
    return decode(bytes, len, out);
}

}  // namespace

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // The friend's seat has no vendor or product of its own, so the test takes that pair too.
    if (!tilt::testStart("0x0000/0x0000")) return 1;
    float v[3];

    std::printf("The friend's console\n");
    const uint32_t gyroPad = tilt::testPad(true);
    check(gyroPad && players::playerOf(gyroPad) == 0, "player 1 holds a controller with both sensors");
    senseHere(true);
    tilt::testFeel(gyroPad, tilt::Sensor::Accel, 1.5f, 9.80665f, -2.25f);
    tilt::testFeel(gyroPad, tilt::Sensor::Gyro, 0.5f, -1.25f, 3.0f);
    press(gyroPad, SDL_GAMEPAD_BUTTON_SOUTH, true);
    press(gyroPad, SDL_GAMEPAD_BUTTON_GUIDE, true);
    move(gyroPad, SDL_GAMEPAD_AXIS_LEFTX, -20000);
    const Pad awake = read(false, 0, Pad{});
    check(awake.senses == (kSenseAccel | kSenseGyro), "its message says it has an accelerometer and a gyroscope");
    check(tiltIs(awake, 1.5f, 9.80665f, -2.25f, 0.5f, -1.25f, 3.0f),
          "and carries its tilt as SDL hands it over, m/s2 and rad/s, x y z");
    check(awake.buttons == (1u << SDL_GAMEPAD_BUTTON_SOUTH) && awake.axes[SDL_GAMEPAD_AXIS_LEFTX] == -20000,
          "with its presses and sticks, and never the console's own button");

    tilt::testFeel(gyroPad, tilt::Sensor::Accel, 0.0f, 0.0f, 9.80665f);
    const Pad asleep = read(true, 1u << SDL_GAMEPAD_BUTTON_NORTH, awake);
    check(asleep.buttons == (1u << SDL_GAMEPAD_BUTTON_NORTH) && asleep.axes[SDL_GAMEPAD_AXIS_LEFTX] == 0,
          "asleep, everything is let go but the script's buttons");
    check(asleep.senses == awake.senses && tiltIs(asleep, 1.5f, 9.80665f, -2.25f, 0.0f, 0.0f, 0.0f),
          "and the controller is held still: no turning, tilted as the last message had it");
    const Pad firstAsleep = read(true, 0, Pad{});
    check(tiltIs(firstAsleep, 0.0f, 0.0f, 9.80665f, 0.0f, 0.0f, 0.0f),
          "asleep before any tilt was sent, it is tilted as it is now");

    Pad again = awake;
    again.seq = 7;
    check(!differs(awake, again), "a state that changed only its number is the same state");
    again.gyro[2] = 3.5f;
    check(differs(awake, again), "a turn alone is a change, and is sent");

    press(gyroPad, SDL_GAMEPAD_BUTTON_SOUTH, false);
    press(gyroPad, SDL_GAMEPAD_BUTTON_GUIDE, false);
    tilt::testUnplug(gyroPad);
    const uint32_t plainPad = tilt::testPad(false);
    check(players::playerOf(plainPad) == 0, "player 1 now holds a controller with no sensors");
    const Pad plain = read(false, 0, awake);
    check(plain.senses == 0 && tiltIs(plain, 0, 0, 0, 0, 0, 0), "its message says so, and carries no tilt");
    senseHere(false);
    tilt::testUnplug(plainPad);

    std::printf("\nThe host's console\n");
    Pad full = awake;
    full.seq = 41;
    Pad got;
    check(decodeBytes(full, sizeof(Pad), &got) && got.seq == 41 && got.senses == full.senses &&
              tiltIs(got, 1.5f, 9.80665f, -2.25f, 0.5f, -1.25f, 3.0f),
          "a newer friend's message reads back whole");
    Pad old = full;
    old.senses = 3;
    check(decodeBytes(old, linkupwire::kPadWithoutTilt, &got) && got.senses == 0 &&
              tiltIs(got, 0, 0, 0, 0, 0, 0) && got.buttons == full.buttons &&
              got.axes[SDL_GAMEPAD_AXIS_LEFTX] == -20000,
          "an older friend's, shorter, is its presses with no sensors and no tilt");
    const uint8_t rumble[8] = {linkupwire::kTagRumble, 0, 0, 0, 0, 0, 0, 0};
    check(!decodeBytes(full, linkupwire::kPadWithoutTilt - 1, &got) && !decode(rumble, sizeof rumble, &got),
          "a message too short, or a rumble, is no pad's state");

    check(seat(0, noRumble, nullptr) && players::gamepad(1) && seatSenses() == 0,
          "a friend whose controller has no sensors is player 2 on a seat with none");
    check(!tilt::set(1, tilt::Sensor::Accel, true) && !tilt::set(1, tilt::Sensor::Gyro, true),
          "so a game asking player 2's sensors hears none: the stand-in shows none");

    check(seat(kSenseAccel | kSenseGyro, noRumble, nullptr) && seatSenses() == (kSenseAccel | kSenseGyro) &&
              players::gamepad(1),
          "seated again with both sensors, still player 2");
    check(tilt::set(1, tilt::Sensor::Accel, true) && tilt::set(1, tilt::Sensor::Gyro, true),
          "and a game asking player 2's sensors hears both");
    play(full);
    SDL_UpdateGamepads();
    check(tilt::read(1, tilt::Sensor::Accel, v) && is(v, 1.5f, 9.80665f, -2.25f),
          "player 2's accelerometer reads the friend's numbers");
    check(tilt::read(1, tilt::Sensor::Gyro, v) && is(v, 0.5f, -1.25f, 3.0f), "and so does the gyroscope");
    SDL_Gamepad* friendPad = players::gamepad(1);
    check(friendPad && SDL_GetGamepadButton(friendPad, SDL_GAMEPAD_BUTTON_SOUTH) &&
              SDL_GetGamepadAxis(friendPad, SDL_GAMEPAD_AXIS_LEFTX) == -20000,
          "with the friend's press and stick");
    Pad trigger = full;
    trigger.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;
    play(trigger);
    SDL_UpdateGamepads();
    // SDL scales the virtual pad's whole range onto the trigger's half, which loses its last step.
    const int pulled = friendPad ? SDL_GetGamepadAxis(friendPad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) : 0;
    check(pulled >= 32766, "a trigger pulled all the way reads all the way, to SDL's last step but one");

    Pad swapped = full;
    swapped.senses = kSenseGyro;
    for (int i = 0; i < 3; ++i) swapped.accel[i] = 0.0f;
    play(swapped);
    SDL_UpdateGamepads();
    check(tilt::read(1, tilt::Sensor::Accel, v) && is(v, 0.0f, SDL_STANDARD_GRAVITY, 0.0f),
          "a sensor the friend's controller no longer has reads a controller lying flat and still");

    check(seat(kSenseGyro, noRumble, nullptr) && !tilt::set(1, tilt::Sensor::Accel, true) &&
              tilt::set(1, tilt::Sensor::Gyro, true),
          "a controller with a gyroscope alone gives player 2 a gyroscope alone");

    std::printf("\nWhat counts as a press, for the idle rule\n");
    Pad was = full;
    Pad now = full;
    now.seq = 42;
    check(!pressed(was, now), "the same state sent again is no press");
    now.gyro[0] = 2.0f;
    now.accel[1] = 4.0f;
    check(!pressed(was, now), "tilt alone is no press");
    now.buttons |= 1u << SDL_GAMEPAD_BUTTON_EAST;
    check(pressed(was, now), "a button going down is one");
    check(!pressed(now, was), "a button let go is not");
    Pad stick = full;
    stick.axes[SDL_GAMEPAD_AXIS_RIGHTX] = 5000;
    check(!pressed(full, stick), "a stick moved inside the deadzone is not, as on a console of its own");
    stick.axes[SDL_GAMEPAD_AXIS_RIGHTX] = 20000;
    check(pressed(full, stick), "a stick moved past it is");
    check(!pressed(stick, stick), "and held there, unchanged, is not again");

    unseat();
    check(!seated() && !players::gamepad(1), "unseated, there is no player 2");

    tilt::reset();
    SDL_Quit();
    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace linkuppad
