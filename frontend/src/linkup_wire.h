// What the main program and the LinkUp program say to each other on their local socket, and what
// the friend's presses and the host's rumble look like on their way between two consoles. Both
// programs include this one file (#654).
//
// A SEQPACKET Unix socket, so each message arrives whole. A message that starts with `{` is JSON;
// one of these two small ones is a pad's state or a rumble, and their numbers are SDL's own, the
// same on both consoles. Both are x86-64, so the bytes are written as the machine holds them.

#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace linkupwire {

inline std::string socketPath() {
    const char* dir = std::getenv("XDG_RUNTIME_DIR");
    return std::string(dir && *dir ? dir : "/run/user/1000") + "/cabinetos-linkup.sock";
}

constexpr uint8_t kTagPad = 'P';
constexpr uint8_t kTagRumble = 'R';

#pragma pack(push, 1)
// A pad's whole state, each time it changes. A later one replaces an earlier one, so these may be
// lost or overtake each other and only the highest `seq` counts.
struct Pad {
    uint8_t tag = kTagPad;
    uint8_t reserved = 0;
    uint16_t seq = 0;
    uint32_t buttons = 0;     // a bit for each SDL_GamepadButton
    int16_t axes[6] = {};     // SDL_GamepadAxis order
};
struct Rumble {
    uint8_t tag = kTagRumble;
    uint8_t reserved = 0;
    uint16_t low = 0;
    uint16_t high = 0;
    uint16_t ms = 0;
};
#pragma pack(pop)

}  // namespace linkupwire
