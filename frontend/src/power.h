// The Power menu's half that talks to the machine — docs/PROJECT.md, open
// question 10b.
//
// THE BUTTON IS BORROWED, NOT TAKEN. logind handles the power button by
// default (power off, on this image). The console asks logind for a
// `handle-power-key` inhibitor lock, which tells logind "somebody else is
// handling this key", and then reacts to the key itself by opening the Power
// menu — so a child pressing it mid-game gets a paused game and a menu with
// Resume focused, not a machine switching off.
//
// THE LOCK IS A FILE DESCRIPTOR AND IT DIES WITH THE PROCESS. That is the
// safety property: a frontend that crashes or hangs hard hands the button
// straight back to logind, so our bug can never make the button useless.
//
// NOTHING HERE IS A-NINE-SPECIFIC. The lock, the three actions and their
// permissions are logind's and polkit's stock behaviour for the active local
// session, which the console always is — `implicit active: yes` for all of
// them, read off the image, with no rule of ours. What does vary by machine is
// whether it can sleep at all, and canRest() asks rather than assumes.

#pragma once

#include <string>

namespace power {

// Take the power key (and the Sleep key some keyboards have) from logind.
// False, with a line on stderr, when logind will not hand it over — the
// button then keeps doing what logind does, which is the safe failure.
bool takeButtons();

// Whether this machine can suspend. Rest is only offered when it can.
bool canRest();

enum class Action { Rest, Restart, PowerOff };
const char* name(Action a);

// Asks logind, on a worker so the frame loop is not held. Says on stderr what
// logind answered.
void act(Action a);

// WHAT TO SAY WHEN LOGIND SAID NO (#241), once: "" when nothing was refused
// since the last ask. Polled from the frame thread, where the pill is. When a
// block inhibitor is the reason, it names what holds it in the holder's own
// words ("Can't restart: Formatting /dev/sda"), which is what logind reports.
std::string refusal();

// ---- Being warned before the machine goes down ----------------------------
//
// A SHUTDOWN MID-GAME MUST LEAVE THE GAME FIRST, HOWEVER IT STARTS — 2026-09-23.
// The Power menu already does, because it runs Exit to Home itself. Anything
// else — a remote `systemctl reboot`, a crash-and-restart, an automatic update
// one day — used to kill the game outright. PR #50 tried to fix that with an
// ExecStop script, and on the A9 it could not work: gamescope and the frontend
// live in the logind session's scope, which is stopped in parallel with the
// service, so the game was dead before the script ran.
//
// This is the standard mechanism instead: a logind DELAY lock on
// shutdown and sleep. logind announces PrepareForShutdown / PrepareForSleep
// and then waits — while every service, the network included, is still up —
// until the lock is released or InhibitDelayMaxSec runs out (30 s in the
// image; logind's default is 5).
bool takeShutdownDelay();

enum class Event { None, GoingDown, GoingToSleep, Woke };
// Non-blocking; call once a frame.
Event poll();

// Let the shutdown or sleep proceed. Taken again automatically on waking.
void releaseDelay();

// True for idle::kWakeSettleSeconds after the machine comes back from a sleep.
// The press that woke it reaches us as an ordinary power-key press, and
// without this the console would wake straight into its own Power menu; no
// other press does anything in a menu then either (input.cpp, a wake is not a
// choice). Call once a frame.
bool justWoke();

}  // namespace power
