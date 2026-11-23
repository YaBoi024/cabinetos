// A LinkUp friend's controller, at both ends (#654, #669): what the friend's console reads from its
// player 1's controller and sends, and the seat on the host's console that plays it as player two.
//
// THE FRIEND'S CONSOLE sends its player 1's presses, sticks and tilt (tilt.h) as one message,
// linkup_wire.h's Pad, with the motion sensors that controller has. linkupsession.cpp sends it each
// time it changes and every tenth of a second when it does not.
//
// THE HOST'S CONSOLE seats the friend as a virtual SDL pad (players.h, seatFriend) made with the
// sensors the friend's controller has, and each message moves it. So every game hears player two
// the way it hears anybody, and tilt.h reads the friend's tilt from that seat as from any pad's:
// vpad.h carries it into a Switch game's stand-in DualSense and a PS3 game's motion device with
// nothing of LinkUp in either. A friend whose controller has no sensors gets a seat with none, so
// the game shows none, as for a pad on the host's own console (the decision of 2026-11-11, "a pad
// without sensors gives none"). The seat's sensors are made when it is, so the host seats the friend
// once it has heard what their controller has, and seats them again between games when that
// changes (linkupsession.cpp); a controller swapped during a game counts from the next one, as
// on one console.
//
// AN OLDER CONSOLE AT EITHER END. One from before #669's last step sends the state without its tilt
// (kPadWithoutTilt): read here, it is a controller with no sensors, and the game plays as before. An
// older host reads the start of a newer friend's message, which is the state it knew, and ignores
// the tilt after it.
//
// WHAT COUNTS AS A PRESS for LinkUp's idle rule (linkupsession.cpp): what counts on a console of its
// own (input.cpp), a button going down or a stick or trigger moving past idle.h's deadzone. A state
// sent again and tilt alone are not somebody there: a controller lying on a table still reads a
// little tilt.
//
// THE FRAME THREAD ONLY, as players.h and tilt.h are.

#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>

#include "linkup_wire.h"

namespace linkuppad {

// ---- The friend's console -------------------------------------------------------

// Turns player 1's motion sensors on for a session, or off (tilt.h).
void senseHere(bool on);

// What player 1's controller is doing now, as one message, `seq` left for the caller. Asleep (the
// friend's own panel is up), it is let go and held still: no button, sticks centred, no turning,
// tilted as it was in `last`, the message sent before. `extraButtons` are --linkup-script's.
linkupwire::Pad read(bool asleep, uint32_t extraButtons, const linkupwire::Pad& last);

// True when `a` and `b` differ in anything but `seq`.
bool differs(const linkupwire::Pad& a, const linkupwire::Pad& b);

// ---- The host's console ---------------------------------------------------------

// A message from the friend's console as a pad's state, or false when it is not one. A shorter one,
// from an older console, comes out with no sensors and no tilt.
bool decode(const uint8_t* data, size_t len, linkupwire::Pad* out);

// True when `now`, the state after `was`, holds a press as the idle rule counts one.
bool pressed(const linkupwire::Pad& was, const linkupwire::Pad& now);

// Seats the friend as player two with the sensors `senses` names, and none when it names none.
// Rumble a game asks of the seat goes to `rumble`. False, with a line in the log, when SDL makes no
// virtual pad; an earlier seat is taken out first.
using RumbleTo = bool (*)(void* userdata, Uint16 low, Uint16 high);
bool seat(uint8_t senses, RumbleTo rumble, void* userdata);
void unseat();
bool seated();
// The sensors the seat was made with.
uint8_t seatSenses();

// A state into the seat: its buttons, sticks and triggers, and its tilt into each sensor the seat
// has. A sensor the state no longer carries (the friend picked up another controller) reads a
// controller lying flat and still, as vpad.h's stand-ins do for a pad that has gone.
void play(const linkupwire::Pad& p);

// --linkup-pad-test: both ends on controllers made inside the test (tilt.h's test pads and the
// friend's virtual seat), no real controller opened and no network. 0 when all pass.
int test();

}  // namespace linkuppad
