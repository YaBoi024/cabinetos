// The pads as the core sees them: the players and their ports, the answer to
// inputState, the keys held on the core's keyboard, the rumble interface, the
// port count and the button names a core declares, kept per port
// (environmentInput), and the Core calls that set and read them. Moved
// out of core.cpp in the restructure (#398). Telling the core what is plugged
// in stays in Core::loadGame (core.cpp), after retro_load_game, because that
// is when the core has said how many ports it has.

#include "core.h"
#include "core_state.h"

#include <algorithm>
#include <bitset>
#include <cstdio>
#include <optional>
#include <string>

#include "libretro.h"
#include "ps2.h"
#include "rumble.h"

namespace cab {
namespace corestate {

// Four players (players.h). PlayStation 2's own host takes two of them.
constexpr int kMaxPorts = 4;
PadState gPads[kMaxPorts];
// How many players the app says there are, and how many ports the core has
// been told hold a controller.
int gPlayers = 1;
unsigned gPortsPlugged = 0;
// What goes into each player's port. See Core::setPadDevice.
unsigned gPadDevice = RETRO_DEVICE_JOYPAD;
// The first port given to real Wii Remotes, or -1 for none. See
// Core::setRealRemotesFrom.
int gRealRemotesFrom = -1;

// How many controller ports the CORE says it has, from
// RETRO_ENVIRONMENT_SET_CONTROLLER_INFO. Four for Flycast, and it matters that
// the answer comes from the core rather than from a number chosen here — see
// the note in loadGame about telling a core what is plugged in.
unsigned gCorePorts = 0;

// Once per game, like the rotation line. Cleared in loadGame.
bool gInputDescriptorsLogged = false;

// EACH PORT'S BUTTONS AS THE CORE LAST DECLARED THEM (Core::inputDescriptors),
// copied, since the core's array and its strings are its own. A core may
// declare the same again mid-game (MAME 2003-Plus does when a port is
// plugged in), which leaves the version where it was.
std::vector<InputDescriptor> gDescriptors[kMaxPorts];
unsigned gDescriptorsVersion = 0;

// THE KEYS HELD ON THE CORE'S KEYBOARD (Core::setKey), port 0 only. No real
// keyboard reaches a core: the console's keyboard is player 1's pad
// (play.cpp), and the one key ever pressed here is a MAME's menu key
// (arcadesettings.h).
std::bitset<RETROK_LAST> gKeys;

// AND THE CORE'S OWN KEYBOARD CALLBACK, for a core that is told of keys as
// they go down and up (RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK) rather than
// asking whether one is held: MAME 0.289 hears keys only this way
// (retro_keyboard_event in its libretro.cpp at our pin), MAME 2003-Plus only
// by asking. Told of each change Core::setKey makes. The core's, so it goes
// when the core does.
retro_keyboard_event_t gKeyboardEvent = nullptr;

void releaseKeys() { gKeys.reset(); }

void forgetKeyboard() { gKeyboardEvent = nullptr; }

void setDescriptors(std::vector<InputDescriptor> (&now)[kMaxPorts]) {
    if (std::equal(std::begin(now), std::end(now), std::begin(gDescriptors))) return;
    for (int p = 0; p < kMaxPorts; ++p) gDescriptors[p] = std::move(now[p]);
    ++gDescriptorsVersion;
}

void keepDescriptors(const retro_input_descriptor* d) {
    std::vector<InputDescriptor> now[kMaxPorts];
    for (; d && d->description; ++d) {
        if (d->port >= kMaxPorts || d->device != RETRO_DEVICE_JOYPAD || d->index != 0 ||
            d->id > RETRO_DEVICE_ID_JOYPAD_R3)
            continue;
        now[d->port].push_back({d->id, d->description});
    }
    setDescriptors(now);
}

void clearInputDescriptors() { keepDescriptors(nullptr); }

// RetroPad ids as a person says them, for the input descriptor log. The order
// is libretro's own and matches cab::Button.
const char* padIdName(unsigned id) {
    switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_B: return "B";
        case RETRO_DEVICE_ID_JOYPAD_Y: return "Y";
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return "Select";
        case RETRO_DEVICE_ID_JOYPAD_START: return "Start";
        case RETRO_DEVICE_ID_JOYPAD_UP: return "Up";
        case RETRO_DEVICE_ID_JOYPAD_DOWN: return "Down";
        case RETRO_DEVICE_ID_JOYPAD_LEFT: return "Left";
        case RETRO_DEVICE_ID_JOYPAD_RIGHT: return "Right";
        case RETRO_DEVICE_ID_JOYPAD_A: return "A";
        case RETRO_DEVICE_ID_JOYPAD_X: return "X";
        case RETRO_DEVICE_ID_JOYPAD_L: return "L (shoulder)";
        case RETRO_DEVICE_ID_JOYPAD_R: return "R (shoulder)";
        case RETRO_DEVICE_ID_JOYPAD_L2: return "L2 (trigger)";
        case RETRO_DEVICE_ID_JOYPAD_R2: return "R2 (trigger)";
        case RETRO_DEVICE_ID_JOYPAD_L3: return "L3 (stick click)";
        case RETRO_DEVICE_ID_JOYPAD_R3: return "R3 (stick click)";
        default: return nullptr;
    }
}

void inputPoll(void) {}

int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port >= kMaxPorts) return 0;
    const PadState& pad = gPads[port];
    if (device == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(pad.buttons);
        return (pad.buttons >> id) & 1;
    }
    if (device == RETRO_DEVICE_ANALOG) {
        // HOW FAR A TRIGGER IS PRESSED. A third index, and it is NOT a stick:
        // its `id` is a joypad button id rather than an axis, so L2 is 12 and
        // R2 is 13. Flycast asks for exactly these two, because the
        // Dreamcast's triggers are analogue.
        //
        // THIS USED TO RETURN THE RIGHT STICK'S Y AXIS. The old line tested
        // only for the LEFT index and treated everything else as the right
        // stick, so "how far is the left trigger pressed" was answered with
        // where the right stick was sitting. Two faults in one: the triggers
        // could not be pressed, and the right stick drove them.
        //
        // AND IT WAS WORSE THAN RETURNING NOTHING, which is the part worth
        // remembering. Flycast reads the analogue value FIRST and only falls
        // back to the digital L2/R2 bit when that value is zero — so the
        // reference implementation, which answers this index with a plain 0,
        // works by taking the fallback. A real pad's right stick rests a few
        // hundred counts off centre, that is not zero, and a non-zero answer
        // means "the trigger is very slightly pressed" and suppresses the
        // fallback entirely. Found 2026-09-19 by MMagTech, who said the
        // shoulder buttons did not work and was right.
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
            const float t = id == RETRO_DEVICE_ID_JOYPAD_L2   ? pad.leftTrigger
                            : id == RETRO_DEVICE_ID_JOYPAD_R2 ? pad.rightTrigger
                                                              : 0.0f;
            // Anything else the core asks about here is an ordinary button,
            // which is pressed or it is not.
            if (id != RETRO_DEVICE_ID_JOYPAD_L2 && id != RETRO_DEVICE_ID_JOYPAD_R2)
                return ((pad.buttons >> id) & 1u) ? 32767 : 0;
            return static_cast<int16_t>(std::clamp(t, 0.0f, 1.0f) * 32767.0f);
        }
        // Sticks. Only the two indices that ARE sticks, and only the two ids
        // that are axes — the old fall-through is what caused the above.
        if (index != RETRO_DEVICE_INDEX_ANALOG_LEFT &&
            index != RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            return 0;
        if (id != RETRO_DEVICE_ID_ANALOG_X && id != RETRO_DEVICE_ID_ANALOG_Y) return 0;
        const float v = (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
                            ? (id == RETRO_DEVICE_ID_ANALOG_X ? pad.leftX : pad.leftY)
                            : (id == RETRO_DEVICE_ID_ANALOG_X ? pad.rightX : pad.rightY);
        return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    if (device == RETRO_DEVICE_KEYBOARD)
        return port == 0 && id < gKeys.size() && gKeys[id] ? 1 : 0;
    return 0;
}

// The input calls of the environment callback. core.cpp's environment asks
// here before its own switch; nothing for any other call.
std::optional<bool> environmentInput(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
            gKeyboardEvent = data ? static_cast<const retro_keyboard_callback*>(data)->callback
                                  : nullptr;
            return true;
        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
            // Every core that has motors, as RetroArch does (#149). Answered
            // even with the Settings switch off: the switch is applied at the
            // pad (rumble.cpp), so turning it on mid-game works at once.
            static_cast<retro_rumble_interface*>(data)->set_rumble_state =
                [](unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
                    rumble::set(port, effect == RETRO_RUMBLE_STRONG ? 0 : 1, strength);
                    return true;
                };
            return true;

        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
            // Acknowledged before, and the data thrown away. It is the only
            // place a core says how many ports it has, and loadGame has to
            // know: a core that is told about some of its ports and not the
            // rest can sit waiting for the rest forever. The array is
            // terminated by an entry with no types.
            gCorePorts = 0;
            if (const auto* info = static_cast<const retro_controller_info*>(data))
                while (info[gCorePorts].types || info[gCorePorts].num_types) ++gCorePorts;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: {
            // WHAT EACH BUTTON ACTUALLY DOES IN THIS GAME, IN THE CORE'S OWN
            // WORDS. Accepted and thrown away until 2026-09-19, and throwing
            // it away is what made "the shoulder buttons do not work" an
            // unanswerable question: a RetroPad has sixteen ids, a real
            // machine has fewer, and the ones a core does not use are silent
            // by design rather than broken. The Dreamcast pad, for one, has no
            // shoulder BUTTONS at all — its L and R are analogue triggers —
            // so a press that does nothing is the correct behaviour and looks
            // identical to a bug.
            //
            // Printed once per game, port 0 only, and only for the joypad:
            // Flycast declares four ports and a mouse and a lightgun besides,
            // and a wall of those is not a diagnostic. Kept, every port and
            // every time, for a fight stick's layout (fightstick.h).
            keepDescriptors(static_cast<const retro_input_descriptor*>(data));
            if (gInputDescriptorsLogged) return true;
            const auto* d = static_cast<const retro_input_descriptor*>(data);
            if (!d) return true;
            std::string line;
            for (; d->description; ++d) {
                if (d->port != 0 || d->device != RETRO_DEVICE_JOYPAD) continue;
                if (d->index != 0) continue;
                const char* name = padIdName(d->id);
                if (!name) continue;
                if (!line.empty()) line += ", ";
                line += name;
                line += "=";
                line += d->description;
            }
            // The flag is set only once something was actually printed. A core
            // is free to call this more than once and to describe other ports
            // first, and latching on a call that said nothing about port 0
            // would silently throw away the one that does.
            if (line.empty()) return true;
            gInputDescriptorsLogged = true;
            std::fprintf(stderr, "[input] port 0: %s\n", line.c_str());
            // Say the silent ones too, because that is the half that answers
            // the question somebody actually asked.
            std::string unused;
            for (unsigned id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; ++id) {
                const char* name = padIdName(id);
                if (!name) continue;
                bool found = false;
                for (const auto* e = static_cast<const retro_input_descriptor*>(data);
                     e->description; ++e) {
                    if (e->port == 0 && e->device == RETRO_DEVICE_JOYPAD &&
                        e->index == 0 && e->id == id) {
                        found = true;
                        break;
                    }
                }
                if (found) continue;
                if (!unused.empty()) unused += ", ";
                unused += name;
            }
            if (!unused.empty())
                std::fprintf(stderr, "[input] port 0 does nothing in this game: %s\n",
                             unused.c_str());
            return true;
        }

        default:
            return std::nullopt;
    }
}

}  // namespace corestate

using namespace corestate;

void Core::setPlayers(int n) {
    gPlayers = std::clamp(n, 1, kMaxPorts);
    // A player who arrived mid-game: plug their port in now. libretro allows
    // it at any time, and it is what a person plugging a pad into the real
    // machine did.
    if (!gameLoaded_ || !g.set_controller_port_device || gIsPs2) return;
    const unsigned ports = std::max(gCorePorts, 1u);
    const unsigned want = std::min(static_cast<unsigned>(gPlayers), ports);
    while (gPortsPlugged < want) {
        g.set_controller_port_device(gPortsPlugged, gPadDevice);
        std::fprintf(stderr, "[input] controller plugged into port %u\n", gPortsPlugged + 1);
        ++gPortsPlugged;
    }
}

void Core::setPadDevice(unsigned device) {
    gPadDevice = device ? device : RETRO_DEVICE_JOYPAD;
}

void Core::setRealRemotesFrom(int firstPort) { gRealRemotesFrom = firstPort; }

const std::vector<InputDescriptor>& Core::inputDescriptors(int port) const {
    static const std::vector<InputDescriptor> none;
    return port >= 0 && port < kMaxPorts ? gDescriptors[port] : none;
}

unsigned Core::inputDescriptorsVersion() const { return gDescriptorsVersion; }

void Core::setKey(unsigned retrok, bool down) {
    if (retrok >= gKeys.size() || gKeys[retrok] == down) return;
    gKeys[retrok] = down;
    if (gKeyboardEvent) gKeyboardEvent(down, retrok, 0, 0);
}

void Core::declareButtons(const std::vector<InputDescriptor>& buttons) {
    std::vector<InputDescriptor> now[kMaxPorts];
    for (auto& port : now) port = buttons;
    setDescriptors(now);
}

void Core::setPad(int port, const PadState& pad) {
    if (port < 0 || port >= kMaxPorts) return;
    gPads[port] = pad;
    if (gIsPs2) {
        ps2::setPad(port, pad.buttons, pad.leftX, pad.leftY, pad.rightX, pad.rightY,
                    pad.leftTrigger, pad.rightTrigger);
    }
}

}  // namespace cab
