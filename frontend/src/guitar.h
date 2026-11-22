// Guitars: a Guitar Hero or Rock Band guitar plays every PS2 and PS3 game as
// a guitar (decided with MMagTech 2026-11-20,
// docs/decisions/2026-11-20-guitars-on-ps2-and-ps3.md).
//
// ONLY PS2 AND PS3, AND NOTHING TO SET. PCSX2 is told the player's port holds
// a PS2 guitar, RPCS3 that the player holds a PS3 guitar, as the game starts;
// every other system plays the guitar as the pad it says it is, as before.
// A pad that is not a guitar is never touched here.
//
// A CONTROLLER IS A GUITAR WHEN IT SAYS SO, with no list of brands:
// - by type: an Xbox 360-type USB device (interface ff/5d/01) whose vendor
//   descriptor, the type 0x21 one right after that interface, has byte 4 =
//   0x06 guitar, 0x07 guitar alternate or 0x0B bass, Microsoft's XInput
//   subtypes (SDL 3.4.18's names for them, src/joystick/SDL_joystick.c
//   L3430-L3434; PlasticBand's USB dumps at a7b5ca30, Docs/Descriptor Dumps/USB,
//   show a Guitar Hero Live dongle at 0x07 and a Rock Band drum kit at 0x08);
// - or by name: the word "guitar", in any case, in the USB product string or
//   in the kernel's name for the device.
// SDL cannot be asked: 3.4.18 types a guitar only for XInput, Windows and
// virtual devices and for its HIDAPI drivers, and a Linux evdev pad comes out
// a gamepad (SDL_joystick.c L3394). Both facts are read from sysfs, once, as
// the pad connects (players.cpp).
//
// KNOWN GAPS, read, not fixed: a wireless Xbox 360 guitar through Microsoft's
// own receiver plays as a pad (xpad names it "Xbox 360 Wireless Receiver" and
// keeps no subtype); a PS3 Guitar Hero drum kit's dongle names itself "Guitar
// Hero4/5 for PlayStation (R) 3" and is taken for a guitar.
//
// THE LAYOUTS. Every guitar is read through SDL's gamepad positions, as any
// pad is, and a layout says only where its whammy and its tilt are:
// 1. Microsoft's Xbox 360 guitar: frets South (green), East (red), North
//    (yellow), West (blue) and the left shoulder (orange); the strum on the
//    d-pad; star power on Back; Start; the whammy on the right stick's X from
//    its minimum at rest to its maximum; tilt on the right stick's Y toward
//    its minimum. Read: PlasticBand's Guitar Hero and Rock Band "Xbox 360.md"
//    at a7b5ca30 (their structs give yellow Y and blue X), RetroBat's map for
//    the X-plorer (whammy +RightX, tilt -RightY), xpad v7.2 L926-929.
//    Measured on CRKD's Les Paul on its dial 8, by cable, receiver and
//    Bluetooth. Used for every guitar of Xbox 360 type, and for one told by
//    name that has no row below: it is the only published layout.
// 2. ROWS MEASURED ON A CONSOLE, as fightstick.h's table is: never guessed.
//    CRKD's Les Paul on its dial 1 ("Guitar Controller Default"): the whammy
//    on the left trigger, tilt as Back held (which games take as star power,
//    so nothing is done for it). Over Bluetooth on dial 1 tilt sends nothing.
//
// TILT FROM AN AXIS IS HELD ON for a moment after each reading that says
// tilted: the dial-8 cable's tilt flickers (three pulses in 0.7 s of a
// three-second hold, measured 2026-11-20).
//
// THE KIND IS SET WHEN THE GAME STARTS. A guitar turned on mid-game plays as a
// pad until the game starts again; RPCS3 rereads its pads only on a reset of
// its own (Input/pad_thread.cpp L566 at our pin).

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core.h"

namespace guitar {

enum class Whammy { RightXFromMin, LeftTrigger };
enum class Tilt { RightYToMin, Back };

struct Layout {
    const char* name;   // for the log and the test
    Whammy whammy;
    Tilt tilt;
};

// What a pad says it is, as the rule reads it.
struct Identity {
    uint16_t vendor = 0;
    uint16_t product = 0;
    int subtype = -1;         // the XInput subtype in its USB descriptors, -1 for none
    std::string usbProduct;   // its USB product string, empty for none
    std::string device;       // the kernel's name for its device, empty for none
};

// The rule: the layout a pad plays with as a guitar, or null for a pad that
// is not one.
const Layout* match(const Identity& id);

// The XInput subtype in a USB device's descriptors (sysfs's `descriptors`:
// the device descriptor, then each configuration's), read from the vendor
// descriptor after the Xbox 360 interface numbered `iface`, or after the
// first one when `iface` is -1. -1 when there is none.
int xinputSubtype(const std::vector<uint8_t>& descriptors, int iface);

// The identity of the pad SDL opened at `path` (a /dev/input or /dev/hidraw
// node), with `device` the kernel's name for it (players.cpp reads that):
// its USB product string and subtype from the USB device above it in sysfs,
// none for a Bluetooth or virtual one.
Identity identify(uint16_t vendor, uint16_t product, const std::string& device,
                  const char* path);

// The layout of the guitar seated as `player`, from 0, or null when that
// player's controller is not a guitar (players.h, guitar).
const Layout* seated(int player);

// ---- PlayStation 2 ----------------------------------------------------------

// THE TILT HELD ON, per seat: `level` is how far tilted, 0 to 1, read now;
// the answer is that, or the last tilted reading for kTiltHoldMs after it.
struct TiltHold {
    uint64_t lastOnMs = 0;
    float heldLevel = 0.0f;
    float step(float level, uint64_t nowMs);
};
constexpr uint64_t kTiltHoldMs = 300;
constexpr float kTilted = 0.5f;   // half the way to the end counts as tilted

// A guitar's state as PCSX2's guitar takes it (frontend/ps2/CabinetPS2Pad.cpp):
// the frets, strum, Select and Start bits as the reader made them (play.cpp,
// readGameInput), the whammy in `leftTrigger` from 0 at rest to 1, and tilt
// as the L2 bit. Nothing else.
cab::PadState ps2State(const Layout& layout, const cab::PadState& pad, TiltHold& hold,
                       uint64_t nowMs);
// The same for the controller in `port` now, on the frame thread: its own
// layout, or Microsoft's for a pad that took a guitar's seat mid-game.
cab::PadState ps2(int port, const cab::PadState& pad);

// ---- PlayStation 3 ----------------------------------------------------------

// One change to a virtual controller (vpad.h): a button down or up, or an
// axis's new value.
struct Change {
    bool isAxis = false;
    int code = 0;    // an SDL_GamepadButton, or an SDL_GamepadAxis
    int value = 0;   // 1 or 0, or the axis value
};

// A GUITAR SEAT'S VIRTUAL CONTROLLER, IN ONE SHAPE whatever the guitar: its
// buttons as they are (frets, strum, star power on Back, Start), the whammy on
// the right stick's X from the centre (rest) to the right, as a PS3 guitar's
// is (PlasticBand, Guitar Hero "PS3.md": 0x80 at rest to 0xFF), and tilt
// pressing R1, Rock Band's tilt and the port of Guitar Hero's star power
// pedal (Rock Band "PS3 and Wii.md", Guitar Hero "PS3.md"), and moving the
// player's motion device as a Guitar Hero guitar's accelerometer moves, its X
// from 0x200 level to 0x180 with the neck up (vpad.cpp, guitarSample). The right stick's Y, and the guitar's own axes the layout reads,
// are the shape's: the right stick's Y stays at rest.
class Ps3Shape {
public:
    // Null for a seat that is not a guitar's: nothing is shaped.
    void reset(const Layout* layout);
    const Layout* layout() const { return layout_; }
    // What the virtual controller is sent for the guitar's axis `a` moving to
    // `value`, appended to `out`.
    void axis(SDL_GamepadAxis a, int16_t value, uint64_t nowMs, std::vector<Change>& out);
    // Once a frame: R1 let go once a tilt's hold is over.
    void tick(uint64_t nowMs, std::vector<Change>& out);
    // How far the neck is raised now, for the motion device: 0 level to 1
    // straight up, with the hold (vpad.cpp turns it into what a Guitar Hero
    // guitar's own accelerometer reads). 0 for a seat that is not a guitar's.
    float motionTilt(uint64_t nowMs);
    // Everything let go: the pause menu opening.
    void release();

private:
    const Layout* layout_ = nullptr;
    TiltHold hold_;
    float tilt_ = 0.0f;
    bool r1_ = false;
    void tiltTo(uint64_t nowMs, std::vector<Change>& out);
};

// RPCS3'S TWO GUITARS (Input/product_info.cpp at our pin: RedOctane's Guitar
// Hero guitar 0x12BA:0x0100, Harmonix's Rock Band guitar 0x12BA:0x0200, both
// class 1, CELL_PAD_PCLASS_TYPE_GUITAR). EVERY PS3 GAME GETS THE ROCK BAND
// GUITAR, except Guitar Hero III and Guitar Hero: Aerosmith, told by their
// names, which get the Guitar Hero guitar. Every game since the 2008
// agreement that made the two series' instruments work in each other's games
// (Guitar Hero: World Tour, Rock Band 2) takes a Rock Band guitar, and on
// RPCS3 Warriors of Rock reads tilt only from it: with the Guitar Hero guitar
// its tilt does nothing (RPCS3 issue 9149, where Guitar Hero III's tilt
// works). The two earlier games were not made for a Rock Band guitar.
struct Rpcs3Guitar {
    int product = 0;
    const char* square = "";     // RPCS3's evdev name for the button Square is on
    const char* triangle = "";   // and Triangle
};
Rpcs3Guitar rpcs3For(const std::string& gameName);
// The three keys of a player's `Config:` in RPCS3's Default.yml that make that
// player's pad the guitar (Emu/Io/pad_config.h L157-159), indented as the rest.
std::string rpcs3Lines(const Rpcs3Guitar& g);

// --guitar-test: the rule, the layouts, both shapes, the tilt's hold and
// RPCS3's lines, every case, no SDL device. 0 when all pass.
int test();

}  // namespace guitar
