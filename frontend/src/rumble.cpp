#include "rumble.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>

#include "players.h"
#include "prefs.h"

namespace rumble {
namespace {

int gEnabled = -1;  // -1: not read yet

// What the game asked for, per player and motor. Written by the core's
// thread, read by the main thread; a torn read of a strength is impossible
// with an atomic and harmless anyway.
std::atomic<uint16_t> gAsked[players::kMax][2];

// What each pad was last sent, and when, so an unchanged request is renewed
// rather than resent every frame.
struct Sent {
    SDL_JoystickID id = 0;
    uint16_t strong = 0, weak = 0;
    uint64_t at = 0;
};
Sent gSent[players::kMax];

// A send lasts this long and is renewed at half of it while it holds. Short,
// so that a process that dies mid-rumble leaves a pad still for at most this
// long. RetroArch sends without an end and stops explicitly; a console that
// can crash is safer with an end on every send.
constexpr uint32_t kHoldMs = 250;

void stopPlayer(int p) {
    Sent& s = gSent[p];
    if (s.strong == 0 && s.weak == 0) return;
    if (SDL_Gamepad* gp = SDL_GetGamepadFromID(s.id)) SDL_RumbleGamepad(gp, 0, 0, 0);
    s = Sent{};
}

}  // namespace

bool enabled() {
    if (gEnabled < 0) gEnabled = prefs::get("rumble", "on") == "on";
    return gEnabled == 1;
}

void setEnabled(bool on) {
    gEnabled = on;
    prefs::set("rumble", on ? "on" : "off");
    if (!on) {
        for (int p = 0; p < players::kMax; ++p) stopPlayer(p);
    }
}

void set(unsigned port, unsigned motor, uint16_t strength) {
    if (port >= static_cast<unsigned>(players::kMax) || motor > 1) return;
    gAsked[port][motor].store(strength, std::memory_order_relaxed);
}

void update(bool live) {
    const uint64_t now = SDL_GetTicks();
    for (int p = 0; p < players::kMax; ++p) {
        SDL_Gamepad* gp = players::gamepad(p);
        const bool on = live && enabled() && gp;
        const uint16_t strong = on ? gAsked[p][0].load(std::memory_order_relaxed) : 0;
        const uint16_t weak = on ? gAsked[p][1].load(std::memory_order_relaxed) : 0;
        Sent& s = gSent[p];
        // The pad in this seat changed (a swap, a pad dropping): the old one
        // is stopped before anything goes to the new one.
        const SDL_JoystickID id = gp ? SDL_GetGamepadID(gp) : 0;
        if (s.id != 0 && s.id != id) stopPlayer(p);
        if (strong == 0 && weak == 0) {
            stopPlayer(p);
            continue;
        }
        if (s.id == id && s.strong == strong && s.weak == weak && now - s.at < kHoldMs / 2)
            continue;
        if (!SDL_RumbleGamepad(gp, strong, weak, kHoldMs)) {
    static bool said[players::kMax];
    if (!said[p]) {
        said[p] = true;
        std::fprintf(stderr, "[rumble] pad %d: %s\n", p, SDL_GetError());
    }
} else {
    static bool ok[players::kMax];
    if (!ok[p]) {
        ok[p] = true;
        std::fprintf(stderr, "[rumble] pad %d: first rumble sent (strong=%u weak=%u)\n", p,
                     static_cast<unsigned>(strong), static_cast<unsigned>(weak));
    }
}
        s = Sent{id, strong, weak, now};
    }
}

void reset() {
    for (int p = 0; p < players::kMax; ++p) {
        gAsked[p][0].store(0);
        gAsked[p][1].store(0);
        stopPlayer(p);
    }
}

}  // namespace rumble
