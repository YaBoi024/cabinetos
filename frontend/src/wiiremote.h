// Real Wii Remotes: pairing them, setting each one up as it connects, and
// reading its buttons for the console's own menus. docs/PROJECT.md open
// question 35, "DECIDED, MMagTech 2026-10-02"; issue #200.
//
// WHAT A WII REMOTE IS HERE: anything whose Bluetooth name starts
// "Nintendo RVL-CNT", the test Dolphin and Wii software use. Never the device
// id: MMagTech's TechKen copies report Samsung's wireless keyboard id
// (04E8:7021), so the kernel's Wii Remote driver and SDL both pass them by.
// A Remote with Nintendo's id gets the kernel's driver as well, whose own
// controller devices udev keeps from everyone (72-cabinetos-wii-remote.rules,
// #548). Either way, what the console gets is a raw HID node (/dev/hidrawN)
// and nothing else, which is also what Dolphin reads, so the console drives
// the Remote itself.
//
// PAIRING, measured on the A9 2026-10-02:
//   1. A copy answers only the Wii's own search, the limited inquiry, which
//      only root can run: cabinetos-wii-search.service does it.
//   2. bluetoothd must be told of the Remote before it can pair it; an L2CAP
//      connect to its control channel does that, as `cabinet`.
//   3. Device1.Pair through bluetoothd, so its PIN callbacks run. BlueZ offers
//      the Wii PIN first (the console's address, reversed), which Nintendo's
//      Remotes take; the TechKens refuse it, and BlueZ retries and asks this
//      session's agent, which answers 0000. One rule for both.
//   4. Trusted, so it may reconnect by itself when a button is pressed.
//
// SET UP ON EVERY CONNECT, or it sits connected in the dark: a TechKen left
// without host commands showed no light, hung up after 3 seconds and called
// again, about 230 times in 17 minutes. Sent each time: a status request, its
// player light, and a report mode (buttons, continuous). A status report
// from the Remote (an extension plugged in or out) stops its reports until the
// mode is sent again, so it is.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "wiipair.h"

namespace wiiremote {

struct Paired {
    std::string address;
    std::string name;
    bool connected = false;
    int light = 0;   // 1-4 while connected out of a Wii game, else 0
};
// Every paired Wii Remote, as refresh() last read them, with whether each is
// connected now. Cheap: for drawing Settings.
std::vector<Paired> known();

// --- Whether Remote games are playable ---------------------------------------

// True while any Wii Remote is paired. Cheap: answered from what refresh()
// last read. Remote games un-grey on it (PROJECT.md question 35).
bool anyPaired();
// True while a Remote whose MotionPlus works here is paired: any but
// Nintendo's own, by the id bluetoothd read off it. The kernel's driver takes
// a Remote with Nintendo's id and turns its MotionPlus off whenever a game
// turns it on, so a game that requires MotionPlus un-greys only on another
// (MMagTech, 2026-10-22: the TechKens; PROJECT.md question 35). Cheap.
bool anyWithMotionPlus();
// Reads both again from bluetoothd. Slow; run at start and after pair/remove.
void refresh();

// --- The running Remotes ------------------------------------------------------

// Reads whether a Remote is paired (one bluetoothctl call), then starts the
// thread that finds connected Remotes, sets each up with its player light,
// and turns their buttons into the console's own controller presses (SDL
// gamepad events from a pad id no real pad has). Idempotent.
void start();

// While a Wii game runs Dolphin drives the Remotes: the console stops sending
// them anything and reads only HOME, held, for its own menu. When the game
// ends every Remote is set up again with its own light.
void setGameRunning(bool running);

// While any game runs, a Wii game or not, an emulator of its own included.
// HOME held counts only then.
void setInGame(bool inGame);

// True once per hold: HOME held about a second on any Remote while any game
// runs. In a Wii game a press of HOME is the game's own HOME menu, as on a
// Wii. In any other game the Remote plays nothing, and the hold is its way to
// the pause menu and out: a Remote that started a game with no pad on was
// stuck in it (a Discord report, 2026-10-23).
bool takeHomeHold();

// True for a press made by a Wii Remote: its buttons reach the menus as
// presses from a pad id no real pad has (start). A Remote starts only Wii
// games (App::refuseRemoteStart, launch.cpp).
bool isRemote(uint32_t padId);

// EACH REMOTE THAT IS ON, out of a Wii game, for the battery row on Home
// (#293): its light (W1-W4), its battery byte (-1 until it has answered; the
// Wii's own scale, battery::segmentsOfWii) and its own low flag. In a Wii game
// Dolphin has the Remotes and this is empty. Cheap.
struct Battery {
    int light = 0;
    std::string address;
    int byte = -1;
    bool low = false;
};
std::vector<Battery> batteries();

// --- The sensor bar -----------------------------------------------------------

// Where the sensor bar sits, as a Wii's own settings ask it: above the TV or
// below it. Below unless set, which is Dolphin's default too. Saved in
// config/settings.json; Dolphin is told through dolphin_sensor_bar_position.
bool sensorBarAbove();
void setSensorBarAbove(bool above);

}  // namespace wiiremote
