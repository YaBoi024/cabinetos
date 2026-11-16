// The console's stand-in DualSense (#669): a virtual controller that carries
// the player's tilt, for an emulator that reads motion only from a pad's own
// sensors: Eden (Switch), Cemu (Wii U) and Vita3K (Vita), each chosen by the
// `dualsense` field on its row in standalone.cpp's table.
//
// WHY A DUALSENSE AND NOT THE PLAIN VIRTUAL PAD. The plain pads (vpad.h) are
// uinput devices, and SDL reads a pad's motion from the kernel's "Motion
// Sensors" device only when that device's uniq string is the pad's own (SDL
// 3.2.0 and later, 2.30 and later: SDL_sysjoystick.c, GetSensor); uinput can
// set no uniq, so no motion device can ever be paired to a plain pad. The
// kernel's own PlayStation driver, hid-playstation, can: handed a HID device
// with a DualSense's id it asks it three feature reports (calibration 0x05,
// pairing 0x09, firmware 0x20), then makes a gamepad, a "Motion Sensors"
// device and a touchpad, all carrying the pad's address as uniq, and applies
// the calibration to every input report 0x01 it is sent. So the console makes
// such a HID device through /dev/uhid for each player and sends it report
// 0x01 250 times a second, as a DualSense on a cable does. inputtino (MIT,
// what Sunshine uses for a Moonlight player's pad) is the worked example and
// the source of the report descriptor, a transcription of a DualSense's own.
//
// THE ID IS SONY'S DUALSENSE EDGE, 054C:0DF2, over USB
// (docs/decisions/2026-11-10-stand-in-dualsense-id.md): the kernel's driver
// takes only Sony's ids, this is the one no pad in a living room is likely to
// share, and both SDLs in question treat it as a PS5 pad. The name is the
// console's own, "CabinetOS player N", which is how the console tells its
// stand-ins from real pads (players.cpp); the address is made up,
// ca:b1:4e:70:50:0N, or ca:b1:4e:70:51:0N for a pad without sensors (below).
//
// A PAD WITHOUT SENSORS CHANGES NOTHING. The kernel's driver gives every
// stand-in a motion device, and SDL starts its six axes at zero and hears
// them only when they change, so a stand-in for a pad without sensors would
// read to an emulator as a pad weighing nothing, sent 250 times a second:
// Eden ignores that, but Cemu and Vita3K take it as tilt, and Vita3K, which
// reads every controller's motion into the one Vita, would mix it with
// another player's real tilt. So such a stand-in wears the second address,
// and the udev rule gives the seat's user the motion device of the first
// only (system_files/usr/lib/udev/rules.d/71-cabinetos-motion-devices.rules).
// An emulator's SDL, running as that user, cannot open the other, finds no
// sensors on the pad (SDL_sysjoystick.c, ConfigJoystick), and sees a pad
// without them, as the player's own is. The pad is judged when the stand-in
// is made, at the game's start, as everything else about the controllers is.
// Every player but the first wears it too in a Wii U or Vita game, whose
// machine has one gyro, player 1's (standalone.h, `onlyPlayerOneTilts`).
//
// THE UNITS, END TO END. tilt.h hands over SDL's units and axes, metres per
// second squared and radians per second in the DualSense's own frame, because
// the console's SDL (3.4.18, SDL_hidapi_ps5.c) passes a real DualSense's six
// raw values through its calibration with no axis swapped. The kernel's
// driver (7.2.11, dualsense_parse_report) maps the same six raw values to
// ABS_X, Y, Z and ABS_RX, RY, RZ unswapped, and SDL's Linux joystick code
// (the same in 3.2.30 and 3.4.18) reads them back as accel[0..2] and
// gyro[0..2], dividing by each axis's resolution: 8192 per g and 1024 per
// degree a second, the driver's own constants (DS_ACC_RES_PER_G,
// DS_GYRO_RES_PER_DEG_S). So the raw value this file writes is what Eden's
// SDL reads, scaled, and the calibration report is chosen to make that
// scaling plain: the accelerometer's plus and minus are +8192 and -8192, so
// the driver's bias is 0 and its factor 16384/16384, and a raw count is
// 1/8192 of a g on the node, 4 g at the end of the range; the gyroscope's
// plus and minus are +16384 and -16384 with a speed of 1024 each way, so the
// driver multiplies by 2048*1024/32768 = 64 and a raw count is 1/16 of a
// degree a second, 2047 degrees a second at the end, the range a DualSense
// has. The gyroscope's bias words are ignored by the driver and left 0.
//
// THE HAND-OVER, AND WHY IT IS SAFE. tilt.h reads a seated pad on the frame
// thread only, where SDL's events are pumped; this file's report thread
// never touches players.h or SDL. vpad.cpp's pump(), on the frame thread,
// writes each player's latest tilt, presses and stick positions into that
// player's state here under a small lock, and the report thread copies the
// state under the same lock and sends it. A frame that brings no new sample
// repeats the last one, as a real pad repeats its last reading. Rumble goes
// the other way: the driver's output report 0x02 lands on the report thread,
// which keeps the two motor strengths, and pump() reads them and hands them
// to rumble.h on the frame thread, where every other rumble call is made.
//
// THE CONSOLE'S OWN SDL SEES THESE TOO, as it sees every real pad: its HIDAPI
// PlayStation driver claims the stand-in's hidraw node by id, and its hint
// that ignores the plain pads by id (startup.cpp) cannot tell a Sony id
// apart. players.cpp refuses to seat a pad whose device is named "CabinetOS
// player ..." before it is opened, so the player count never moves and no
// press is passed back. An emulator's SDL, with its HID drivers off, finds
// the gamepad through its event node and pairs the motion node to it by uniq.
//
// THE LAYOUT IS SDL'S OWN RULE FOR AN evdev DEVICE: buttons numbered in the
// order of their key codes, axes in the order of theirs, the d-pad as hat 0.
// The driver's gamepad has BTN_SOUTH, EAST, NORTH, WEST, TL, TR, TL2, TR2,
// SELECT, START, MODE, THUMBL, THUMBR, then the Edge's four extra buttons,
// and axes X, Y, Z (L2), RX, RY, RZ (R2), so Cross is button 0 and Create is
// button 8, which is where the two shoulder buttons' own key codes put them.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace cab::vdualsense {

constexpr int kMaxPlayers = 4;

// Makes player `player`'s (from 0) stand-in and waits for the kernel's driver
// to make its devices, about 50 ms and 2 s at most. `tilt` says the player's
// pad has sensors; without it the stand-in's motion device is nobody's to
// open (above). False, with the reason in the log, when /dev/uhid cannot be
// opened or the driver did not take it; the caller then falls back to the
// plain pads (vpad.cpp).
bool open(int player, bool tilt);
void close(int player);
void closeAll();
bool isOpen(int player);

// The latest state, from the frame thread. Anything a DualSense does not have
// is dropped.
void button(int player, SDL_GamepadButton b, bool down);
void axis(int player, SDL_GamepadAxis a, int16_t value);
void releaseAll(int player);
// The pad's tilt in SDL's units and axes (tilt.h): x, y, z. nullptr for a
// sensor the player's pad lacks, or has lost when it left mid-game: the
// report then carries a pad lying still, face up, on that sensor. A stand-in
// made for a pad with neither is written the same, and nothing reads it.
void motion(int player, const float* accel, const float* gyro);
// What the emulator last asked of the motors, as SDL's two strengths, 0 to
// 65535, strong then weak. True when it changed since the last ask.
bool rumbleChanged(int player, uint16_t* strong, uint16_t* weak);

// The identity Eden knows the stand-ins by, as Eden writes it: SDL's GUID for
// the device with the two bytes of name checksum cleared (its GetGUID), the
// same for every player since the name is what differs. And where each input
// lands in SDL's numbering of the device, -1 when it has none.
std::string edenGuid();
// The same as SDL itself writes it, name checksum and all, so each player's
// is its own: what Cemu's profile binds to and Vita3K's mapping line names.
std::string sdlGuid(int player);
// Its vendor and product as SDL's IGNORE_DEVICES hints take them,
// "0x054c/0x0df2".
std::string sdlIds();
int buttonIndex(SDL_GamepadButton b);
int axisIndex(SDL_GamepadAxis a);
// "CabinetOS player N", the device's name and the kernel's gamepad's.
std::string name(int player);

// The devices the kernel made for player `player`, as paths under /dev/input,
// and the uniq they share; empty for one that is not up.
struct Nodes {
    std::string uniq, gamepad, motion, touchpad;
};
Nodes nodes(int player);

// --dualsense-test: two stand-ins made for real through /dev/uhid beside
// test pads made inside the test (tilt.h's), one with sensors and one with
// none. Asserts the kernel made the gamepad, motion and touchpad devices with
// one uniq; that the console's own seating refuses the stand-in and the
// player count stays; that SDL, standing in for an emulator's, takes it as a
// gamepad with the GUIDs Eden's lines, Cemu's profile and Vita3K's mapping
// carry, pairs its motion device (the udev rule) and reads a written tilt back
// in SDL's units, and a press and a stick where the layout puts them, by its
// own mapping and by the console's line; that a pad without sensors gets a
// stand-in on which SDL finds none; that rumble comes back; that closing
// takes every device away; and that for a Wii U or Vita game only player 1's
// stand-in carries tilt. Prints plainly and
// exits 0 on a machine with no /dev/uhid, which CI's builder is; it runs for
// real on a console.
int test();

}  // namespace cab::vdualsense
