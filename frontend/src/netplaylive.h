// FORK (handheld): netplay in the real player (NETPLAY-PLAN.md, phase 2). The
// scripted loop in netplayrun.cpp proved two consoles stay in step; this is
// the same lockstep driven by real controllers on the real screen.
//
// One copy hosts and the others join; the host is player 1 and the joiners
// players 2, 3 and 4 in the order they came, and each plays with its own first
// controller. A frame is run only when every player's buttons for it are in
// hand, so a late or paused side makes the others wait, never drift. A player
// who leaves does not end the game for the rest: their controller goes still.
// While a session is on, rewind, fast forward and the pause redraw frames are
// off (play.cpp, pace.cpp).
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
    int delay = 2;   // frames at the start; the game raises it by itself if it has to wait (netplaylive.cpp, look)
    int romId = 0;   // the game it was asked for; 0 from the command line, for any
    // The most players, the host included, for a host: 2 starts the game as soon
    // as one friend is in; more waits in a lobby until the host presses Start
    // (or the game is full). Cut down to what the game's system can take.
    int players = 2;
};

// Filled in by the game page's "Play together" (playtogetherflow.cpp) or from
// the command line; read once a game has loaded. Cleared when the game closes.
Request& pending();

// Whether this game was started to be played together, from the press on its
// page until it closes, even after the session itself has ended. It runs in a
// save folder of its own (launch.cpp) that is never uploaded, so what it
// saves is neither the player's solo save nor reaches RomM; the shared saved
// game in that folder is made the same on both consoles when they connect
// (netplaysave.h).
bool playingTogether(int romId);

// Which games can be played together: the systems whose controller ports are
// the way to play two at once and whose cores hold still from one run to the
// next (NETPLAY-PLAN.md, section 5, tiers A and B). RomM's platform slug.
bool supportsPlatform(const std::string& platformSlug);
// The same, by the core's own name; the last check, once the core is loaded.
bool supportsCore(const std::string& coreName);

// The most players one game can have on this system, 2 to 4: the systems whose
// cores give every player a port of their own. RomM's platform slug, or the
// core's own name once it is loaded.
int maxPlayersForPlatform(const std::string& platformSlug);
int maxPlayersForCore(const std::string& coreName);

// Four digits for the host's screen, 1000 to 9999.
int newCode();

// The line to keep on screen while connecting ("Waiting for player 2 \xC2\xB7
// code 4821", or "2 of 4 here \xC2\xB7 press Start \xC2\xB7 code 4821"), else "".
std::string connecting();

// A game has just loaded: if a session was asked for, start connecting (on a
// thread of its own, so the screen never freezes).
// `saveDir` is the game's together folder and `cardRoot` the GameCube card
// folder inside it ("" for any other system).
void begin(cab::Core& core, const std::string& romPath, int romId, const std::string& saveDir,
           const std::string& cardRoot);

// True from begin() until the session ends or fails. While true the game is
// stepped by step() and the pads by setLocal().
bool active();

// This console's first controller as read this refresh. Only the latest
// matters. While a host with friends in the lobby is waiting, a press of Start
// begins the game.
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
