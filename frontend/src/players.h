// Which controller is which player (issue #64).
//
// UNTIL THIS EXISTED, ONE PAD PLAYED. Every pad could walk the menus, but a
// game read only the first pad SDL listed, as player one, and a second person
// holding a second pad pressed buttons that went nowhere.
//
// THE RULES, decided with MMagTech 2026-09-26 (docs/SETTINGS.md, Controllers):
//
// - Each pad that connects takes the next player, up to four. A system with
//   fewer ports uses the first ones.
// - OUTSIDE A GAME THE NUMBERS CLOSE UP: the pads that are on are 1, 2, 3 in
//   the order they connected, with no gaps. Two people finish, both pads go
//   to sleep, and whichever one is picked up next is player one.
// - DURING A GAME a pad that goes off keeps its number while any other pad is
//   still on, so player one's pad dying mid-race does not hand player two the
//   first car. It gets its number back when it returns.
// - DURING A GAME, IF EVERY PAD GOES OFF, the first one back is player one.
//   The two people put the pads down and picked up the wrong ones.
// - A person can give a pad a number in Settings; the two pads swap. Closing
//   up keeps the order, so the swap holds while both stay on.
//
// Every pad still drives the menus. This decides only who a game hears.
//
// THE RULES ARE A CLASS WITH NO SDL IN IT, `Seats`, so `--players-test` can
// walk every case headless. The functions below it are the SDL side.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace fightstick {
struct Stick;
}

namespace players {

constexpr int kMax = 4;

struct Seat {
    uint32_t id = 0;     // the pad in it now, 0 while held for one that went off
    std::string key;     // what the pad is, across a reconnect (its serial)
};

class Seats {
public:
    // Seats a pad that just connected and returns its player, 0-based, or -1
    // when all four are taken. A pad already seated keeps its seat.
    int add(uint32_t id, const std::string& key);
    void remove(uint32_t id);
    // Leaving a game closes up the seats held for pads that went off.
    void setInGame(bool on);
    // Gives the pad in seat `a` the number `b` and the other pad `a`. Refused
    // during a game, and for a seat with nobody in it.
    bool swap(int a, int b);

    int playerOf(uint32_t id) const;
    uint32_t idOf(int player) const;
    bool inGame() const { return inGame_; }
    const std::vector<Seat>& seats() const { return seats_; }

private:
    void closeUp();
    std::vector<Seat> seats_;
    bool inGame_ = false;
};

// ---- The console's pads ----------------------------------------------------

// THE COMMUNITY'S CONTROLLER LIST, on top of SDL's own: SDL_GameControllerDB,
// in the image at /usr/share/cabinetos/gamecontrollerdb.txt (CABINETOS_PAD_DB
// overrides it). Call once, after SDL_Init and before any pad is opened.
//
// THIS IS INSTEAD OF A BUTTON-MAPPING SCREEN (issue #66, decided with
// MMagTech 2026-09-26). A pad SDL does not know arrives with meaningless
// buttons; rather than hand the person a screen to fix it, the console
// carries the larger list, and a pad reported missing is added to it. The
// mapping stays ours, as docs/PROJECT.md has always said.
void loadMappings();

// A pad SDL just reported, or one found at start. Opens it and seats it.
void added(SDL_JoystickID id);
void removed(SDL_JoystickID id);
void setInGame(bool on);
bool swap(int a, int b);

// REMOTE PLAY'S CONTROLLERS (issue #286, decided with MMagTech 2026-10-07).
// A pad that arrives through a stream (Sunshine's virtual controller, known
// by the name Sunshine gives it) is seated apart from the console's own. While
// a stream is on, a game hears the stream's pads, players 1, 2... in the order
// they came, and the console's own pads are set aside; when it ends, the
// console's pads have their numbers back exactly as they were, and the
// stream's are set aside (Sunshine keeps them after a dropped connection, so
// they cannot be waited on to leave). A set-aside pad is ignored everywhere,
// menus included (setAside, read by the app's event filter).
void setStreaming(bool on);
bool streaming();
bool setAside(SDL_JoystickID id);

// The pad a game hears as `player`, or null.
SDL_Gamepad* gamepad(int player);
// The fight stick an open pad is (fightstick.h), or null for any other pad:
// the table is read once, as the pad is opened.
const fightstick::Stick* stick(SDL_JoystickID id);
int playerOf(SDL_JoystickID id);
// How many players there are, counting a seat held for a pad that went off.
int count();

struct Pad {
    int player = -1;
    SDL_JoystickID id = 0;
    std::string name;      // as the pad gives it
    std::string address;   // "AA:BB:CC:DD:EE:01", Bluetooth only
};
// The seated pads that are on, player one first.
std::vector<Pad> connected();

// THE SEATED PADS THAT ARE ON, for the battery row on Home (#293): no sysfs
// read, so it can be asked every frame. `dir` is the pad's HID device
// (battery::deviceDir), found once as it connected.
struct Held {
    int player = -1;
    std::string key;    // the pad, across a reconnect (its address or serial)
    std::string dir;
    SDL_Gamepad* gp = nullptr;
    bool stream = false;   // a stream's pad: its battery entry is Sunshine's, not the pad's
};
std::vector<Held> held();

// Bumped on every change, so a screen showing the list knows to redraw it.
int generation();

// --players-test: the rules above, every case, no SDL. 0 when all pass.
int test();
// --pads: what SDL says about each pad, for as long as `seconds`, printing
// connections and presses as they happen.
int report(int seconds);

}  // namespace players
