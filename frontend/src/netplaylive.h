// FORK (handheld): netplay in the real player (NETPLAY-PLAN.md, phase 2). The
// scripted loop in netplayrun.cpp proved two consoles stay in step; this is
// the same lockstep driven by real controllers on the real screen.
//
// One copy hosts and one joins; the host is player 1 and the joiner player 2,
// and each plays with its own first controller. A frame is run only when both
// players' buttons for it are in hand, so a late or paused side makes the
// other wait, never drift. While a session is on, rewind, fast forward and the
// pause redraw frames are off (play.cpp, pace.cpp).
//
// Step one starts it from the command line (--play-host, --play-join with
// --launch); the screens come after.

#pragma once

#include <string>

#include "core.h"

namespace netplaylive {

struct Request {
    bool wanted = false;
    bool host = false;
    std::string address = "127.0.0.1";
    int port = 55435;
    int code = 4821;
    int delay = 2;
    int romId = 0;   // the game it was asked for; 0 from the command line, for any
};

// Filled in by the game page's "Play together" (playtogetherflow.cpp) or from
// the command line; read once a game has loaded. Cleared when the game closes.
Request& pending();

// Whether this game was started to be played together, from the press on its
// page until it closes, even after the session itself has ended. Its saves are
// neither read nor written (launch.cpp, play.cpp): both consoles start from a
// blank save, and what the game plays never reaches RomM or replaces the
// player's own.
bool blankSave(int romId);

// Which games can be played together: the systems whose controller ports are
// the way to play two at once and whose cores hold still from one run to the
// next (NETPLAY-PLAN.md, section 5, tiers A and B). RomM's platform slug.
bool supportsPlatform(const std::string& platformSlug);
// The same, by the core's own name; the last check, once the core is loaded.
bool supportsCore(const std::string& coreName);

// Four digits for the host's screen, 1000 to 9999.
int newCode();

// The line to keep on screen while connecting ("Waiting for player 2 \xC2\xB7
// code 4821"), else "".
std::string connecting();

// A game has just loaded: if a session was asked for, start connecting (on a
// thread of its own, so the screen never freezes).
void begin(cab::Core& core, const std::string& romPath, int romId);

// True from begin() until the session ends or fails. While true the game is
// stepped by step() and the pads by setLocal().
bool active();

// Player one's buttons as read this refresh. Only the latest matters.
void setLocal(const cab::PadState& pad);

// Run up to maxFrames game frames, as many as both players' buttons allow.
// Returns how many were run.
int step(cab::Core& core, int maxFrames);

// What to tell the player when it changes, in the console's notice pill
// (notice.h). `mood` picks the dot: Busy while waiting, Done once connected,
// Problem when it ended badly. Said once per change; text is "" when none.
enum class Mood { Info, Busy, Done, Problem };
struct Message {
    std::string text;
    Mood mood = Mood::Info;
};
Message takeMessage();

// The game is closing, or the player left.
void stop();

}  // namespace netplaylive
