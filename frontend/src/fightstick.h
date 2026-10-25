// Fight sticks: an arcade game's buttons on a stick as a cabinet had them
// (decided with MMagTech 2026-10-24,
// docs/decisions/2026-10-24-fight-stick-layout.md).
//
// ONLY A STICK ON THE TABLE, AND ONLY IN AN ARCADE GAME. A pad that is not
// on the table (fightstick.cpp) is a pad, and nothing here touches it: no
// setting, no screen, no change in how it is read. A stick that is on it
// plays an arcade game (FBNeo, MAME 2003-Plus) through a layout made from
// the buttons the game says it uses, and every other game as the pad it says
// it is. Its Home opens the pause menu in every game, switch or not, since it
// has no stick clicks for the pair that opens it on a pad (play.cpp).
//
// THE TABLE IS MEASURED, NEVER GUESSED. A row is a stick somebody connected
// and read: its id, and the name the kernel gives its device where that id is
// another pad's. No auto-detection: a stick that is not on it is a pad until
// a row is measured.
//
// THE EIGHT BUTTONS are numbered as a Vewlix panel reads: the top row 1 to 4
// and the bottom row 5 to 8, left to right. With N of the game's buttons, in
// its order:
//   5 or more  1 2 3 on top and 4 5 6 below it (punches above kicks, light
//              to heavy left to right), the 7th top right, the 8th bottom right
//   4          the bottom row, as a Neo Geo cabinet's one row
//   1 to 3     the bottom row from the left, the third after a gap (5 6 8)
// A ninth and beyond gets no button and is logged. The lever, Select, Start
// and Home are never moved.
//
// WHICH BUTTONS, IN WHICH ORDER. The game's buttons are the descriptors the
// core declared for the port (core.h, inputDescriptors) whose id is B, Y, A,
// X, L, R, L2 or R2; one on L3 or R3 is not counted, since the stick has no
// clicks. FBNeo declares them in the game's own order, with combination
// buttons of its own that press two or three at once ("Buttons AB",
// "Buttons 3x Punch"); no cabinet had those, so they are not counted and get
// no button, the one place a label decides anything. MAME 2003-Plus
// declares them in RetroPad id order, so they are put in its Classic table's
// order (fightstick.cpp). Otherwise labels are the core's words, shown as
// they are. A port the core said nothing about takes port 0's.
//
// THE CORE IS TOLD NOTHING DIFFERENT: it hears RetroPad ids, as from any pad,
// sent from other buttons.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core.h"

struct SDL_Gamepad;

namespace fightstick {

// A row of the table.
struct Stick {
    uint16_t vendor = 0;
    uint16_t product = 0;
    const char* device = "";   // the kernel's name for its device; "" matches on the id alone
    const char* name = "";     // what this kind of stick is called (kind)
};

// The row of the table a pad is, or null. `device` is the kernel's name for
// the pad's device, which players.cpp reads once as the pad is opened.
const Stick* match(uint16_t vendor, uint16_t product, const std::string& device);

// WHAT KIND OF CONTROLLER A PAD IS, for anything kept per kind rather than
// per pad: the stick's row name, else SDL's GUID as a string (the key
// shortcuts.cpp keeps its button under, which keeps its own in this pass).
std::string kind(SDL_Gamepad* gp);

// Whether a core is one of the two arcade cores, FBNeo and MAME 2003-Plus:
// the games a stick gets a layout in and Controls is offered in (controls.h).
bool isArcadeCore(const std::string& core);

// The button number a joypad id is in MAME 2003-Plus's Classic table, 1 to 8
// (B A Y X L R L2 R2), or 0 for an id that is not one of the eight; and
// whether a core declares a game's buttons in RetroPad id order, so they are
// read in that table's order (MAME 2003-Plus) rather than as declared.
int classicNumber(unsigned id);
bool readsClassic(const std::string& core);

constexpr int kSlots = 8;
constexpr int kNone = -1;

// A game's buttons on the eight: the RetroPad id each sends, or kNone.
struct Layout {
    std::array<int, kSlots> id{kNone, kNone, kNone, kNone, kNone, kNone, kNone, kNone};
    std::array<std::string, kSlots> label;
    int count = 0;                    // N, the game's buttons
    std::vector<std::string> left;    // the ninth and beyond, with no button
    std::vector<std::string> warnings;
};

// The rules, with no SDL: the layout `stick` gives the game on `core` in a
// port with these descriptors (`port0` is port 0's, for a port the core said
// nothing about). None for a pad not on the table, a core that is not an
// arcade core, or a game that named no buttons.
std::optional<Layout> make(const Stick* stick, const std::string& core,
                           const std::vector<cab::InputDescriptor>& port,
                           const std::vector<cab::InputDescriptor>& port0);

// The layout the game in play gives the pad in `port`, or null. Made again
// when another pad takes the port or the core declares its buttons again.
const Layout* forPort(int port, SDL_Gamepad* gp, const std::string& core);

// Moves the stick's buttons, as the reader made them, to what the layout
// sends (play.cpp, readGameInput).
void apply(const Layout& layout, cab::PadState& pad);

// --fightstick-test: the rules above, every case, no SDL. 0 when all pass.
int test();

}  // namespace fightstick
