// The virtual touch screen: how a game in an emulator of its own is touched,
// for the PlayStation Vita's front screen (docs/PROJECT.md, open question 49,
// decision 2). Its own job beside vpad.h, which is the virtual controllers.
//
// WHY A TOUCH SCREEN. Vita3K's SDL frontend, the one the console starts it
// with, takes touch from a touch screen and from a pad's own touchpad, and
// nothing from a mouse (sdl-frontend/session.cpp). The console has no touch
// screen, and the pads Vita3K is given (vpad.h) have no touchpad. So the
// console makes one: a uinput touch screen with two fingers, which it moves
// with the sticks (the pointer, standalone_play.cpp) and draws as dots over
// the game. No change to Vita3K.
//
// HOW A TOUCH REACHES THE GAME. gamescope reads the device like any touch
// screen and, by default, turns a touch into a mouse click on the window
// under it (touch_click_mode 1, Left). With touch_click_mode 4, Passthrough,
// it passes the touch on as a touch, mapped from the screen into the game
// window's own coordinates (wlserver.cpp, wlserver_touchdown, gamescope
// 3.16.31); Xwayland makes it an X11 touch, SDL a finger, and Vita3K the
// front screen. So the mode is set while a Vita game runs and put back after
// (passThrough).
//
// POSITIONS ARE FRACTIONS OF THE SCREEN, 0 to 1 across and down, which is
// what gamescope takes from any touch screen (libinput's transformed
// coordinates), so a dot drawn at a fraction of the console's own full-screen
// window is where the touch lands.

#pragma once

namespace cab::vtouch {

// Two fingers, as Sony's PS TV pointer has two dots.
constexpr int kFingers = 2;

// Creates the touch screen, and keeps it until close(). False when
// /dev/uinput cannot be opened, which leaves the game untouchable; the
// caller says so.
bool open();
void close();

// Finger `f` (0 or 1) on the screen at (x, y), or moved there while it is.
void touch(int f, float x, float y);
// Finger `f` off the screen; every finger off.
void lift(int f);
void liftAll();

// gamescope passing touches through to the window under them (on), or
// turning them into mouse clicks, its default (off). Asked on a worker, as
// the display's sleep is (idle.cpp), so the frame never waits on gamescope;
// the latest request is the one applied.
void passThrough(bool on);

}  // namespace cab::vtouch
