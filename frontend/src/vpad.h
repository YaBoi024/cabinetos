// Virtual controllers: the pads an emulator of its own is actually given.
//
// WHY THE REAL PADS ARE NOT HANDED OVER. Eden, like every emulator built on
// SDL, maps a pad by its RAW button numbers, and those come from the SDL inside
// its Flatpak: 3.2.30 under sdl2-compat, measured 2026-09-28, against 3.4.16
// in the console itself. Two SDLs two versions apart can number the same pad
// differently, and a map written from one and read by the other scrambles the
// buttons with no error anywhere. So the emulator never sees the real pads.
// It sees one of these per player, whose identity and layout are fixed here,
// and the console passes each real pad's presses to its player's virtual one.
// That is what Steam Input does for the same reason.
//
// WHAT IT BUYS BEYOND THAT. The same four pads serve PS3, Xbox and Wii U; the
// player order is the console's (players.h), not the emulator's guess; and the
// pause menu takes the controllers simply by passing nothing on.
//
// WHAT A PLAIN VIRTUAL CONTROLLER CANNOT DO: carry motion. It is a uinput
// device, and uinput cannot give it the serial string SDL pairs a sensor
// device by, so an emulator that reads motion from a pad's own sensors sees
// a pad lying still. Two emulators get motion another way, each chosen by a
// switch on its row in standalone.cpp's table, the way everything that one
// emulator needs and another does not is (standalone.h):
//
// - RPCS3 (#683, the row's `motion`) reads motion from a second device it
//   is told by name, its "Buddy Device", so beside each player whose real pad
//   has an accelerometer or a gyroscope the console makes a motion device,
//   "CabinetOS player N motion", and pump() writes the pad's tilt into it.
// - Eden, Cemu and Vita3K (#669, the row's `dualsense`) are given a stand-in
//   DualSense in place of the plain controller (vdualsense.h): a /dev/uhid
//   device with Sony's id that the kernel's own PlayStation driver turns
//   into a gamepad, a motion device and a touchpad sharing one uniq, which
//   the emulator's SDL pairs by itself. pump() writes the pad's tilt into it
//   too. A player whose pad has no sensors gets a stand-in on which the
//   emulator finds none, and so does every player but the first in a Wii U
//   or Vita game, whose machine has one gyro (standalone.h). Should the stand-in fail to come up, the plain
//   controllers stand in for it, with a line in the log, and the game plays
//   as it did before #669. The console tells its stand-ins from real pads by
//   their name (players.cpp), since their id is Sony's.
//
// Xbox, Xbox 360, Model 2 and Model 3 keep the plain controller. Rumble comes
// back the other way from both kinds: each virtual controller takes the
// emulator's force-feedback the way a real pad does, and pump() hands it to
// rumble.h, which drives the player's real pad under the console's Rumble
// switch.
//
// THE MOTION DEVICE IS A DUALSHOCK 3'S SENSORS, in RPCS3's units: an
// accelerometer property (INPUT_PROP_ACCELEROMETER) and six absolute axes,
// X, Y and Z the accelerometer and RX, RY and RZ the gyroscope, each 0 to
// 1023 so RPCS3's scaling of them is the identity: 512 at rest, 113 per g,
// the yaw rate on RY. RPCS3 runs as the console's user, which a udev rule
// gives access to it, as systemd gives it to pads
// (system_files/usr/lib/udev/rules.d/71-cabinetos-motion-devices.rules).
// Nothing else takes it for a pad or a pad's sensors: it has no buttons, and
// no serial string at all, and SDL gives a pad the sensor device whose
// serial string is the pad's own.
//
// THE LAYOUT IS SDL'S OWN RULE FOR AN evdev DEVICE: buttons numbered in the
// order of their key codes, axes in the order of theirs, the d-pad as hat 0.
// That rule is in SDL's Linux joystick code and has not changed across the
// versions in question, which is the whole reason for choosing it. The
// stand-in DualSense follows the same rule over the key codes the kernel's
// driver gives it, so its numbering differs (vdualsense.h), and edenGuid,
// sdlGuid, sdlIds, sdlMapping, buttonIndex and axisIndex answer for whichever
// kind is open.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace cab::vpad {

constexpr int kMaxPlayers = 4;

// Creates `players` virtual controllers, one per player, and keeps them until
// close(). False when /dev/uinput cannot be opened, which leaves an emulator
// of its own with no controllers at all; the caller says so.
//
// With `motion` (an emulator that reads a motion device, RPCS3), each player
// whose pad has an accelerometer or a gyroscope also gets a motion device,
// and that seat's sensors are turned on (tilt.h) until close(), which turns
// them off. A player without one gets none, and a motion device that cannot
// be made leaves that player's game as it was, with a line in the log.
//
// With `dualsense` (an emulator that reads a pad's own sensors: Eden, Cemu,
// Vita3K), every player gets a stand-in DualSense of vdualsense.h in place of
// the plain controller, and every seat's sensors are turned on until close();
// a player whose pad has none gets a stand-in with none. With
// `onlyPlayerOneTilts` too (Cemu, Vita3K: the machine's one gyro is player
// 1's), only player 1's stand-in carries tilt and only that seat's sensors are
// turned on; every other player's stand-in has none. When one cannot be made,
// the plain controllers are made instead, with a line in the log.
bool open(int players, bool motion, bool dualsense, bool onlyPlayerOneTilts);
void close();
int count();
// True while player `player`'s controller is a stand-in DualSense carrying
// its pad's tilt: Eden's motion lines and Cemu's profile then turn motion on
// for that player.
bool carriesMotion(int player);

// The name of player `player`'s motion device, the one its emulator is told
// to read, or empty when that player has none.
std::string motionName(int player);

// A real pad's change, passed to its player's virtual controller. Player
// numbers are players.h's, from 0. Anything a virtual controller does not
// have (paddles, a touchpad) is dropped.
void button(int player, SDL_GamepadButton b, bool down);
void axis(int player, SDL_GamepadAxis a, int16_t value);
// Everything let go and centred: the pause menu opening, a pad unplugged.
void releaseAll();

// Once a frame while an emulator runs: answers its force-feedback requests
// and passes each player's rumble to rumble.h, and stops a rumble whose time
// is up; and writes each motion device what its player's pad reads now
// (tilt.h), at rest when the seat's pad has gone or has no such sensor. On
// the frame thread only, as tilt.h is: the loop that calls it wakes for
// every sensor update, so the tilt is passed on as it arrives. `nowMs` is
// any steady clock in milliseconds.
void pump(int64_t nowMs);

// The identity an emulator knows them by, as Eden writes it: SDL's GUID for
// the device, with the two bytes of name checksum cleared (Eden's GetGUID).
// For the kind of controller open now, so it is asked after open().
std::string edenGuid();

// The same, as SDL itself writes it (SDL_GUIDToString), name checksum and
// all: what xemu and Cemu bind a port to. Each player's controller is named
// for its seat ("CabinetOS player 2"), so each has its own. `player` from 0.
std::string sdlGuid(int player);

// The vendor and product of the kind open, as SDL's IGNORE_DEVICES hints
// take them: what each emulator's SDL is told to take and nothing else
// (SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT), so it never sees the real pads.
std::string sdlIds();

// A line of SDL's controller database for player `player`'s controller:
// every button and axis where the layout above puts it, so an emulator that
// asks SDL for a "gamepad" rather than a joystick gets it without guessing.
std::string sdlMapping(int player);

// Where each input lands in SDL's numbering of a virtual controller, of the
// kind open now (the plain controller's when none is).
int buttonIndex(SDL_GamepadButton b);   // -1 when it has none
int axisIndex(SDL_GamepadAxis a);       // -1 when it has none

// --motion-device-test: the motion devices above, made for real through
// /dev/uinput beside pads made inside the test (SDL's virtual joysticks, two
// with both sensors and one with none), and read back through their event
// nodes as RPCS3 reads them. Needs /dev/uinput and the udev rule, so it runs
// on a console, not in CI. 0 when all pass.
int motionTest();

}  // namespace cab::vpad
