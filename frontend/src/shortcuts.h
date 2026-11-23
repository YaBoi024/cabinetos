// The in-game shortcuts: one button, held with a second, while a game plays.
// docs/PROJECT.md, "The in-game shortcuts, and three states per game" and
// "The shortcut button is set by pressing it" (issues #76 to #80).
//
// OFF BY DEFAULT, one switch in Settings. With it on, a tap of the shortcut
// button opens the pause menu and holding it with a second button runs a
// shortcut. The combinations are fixed (play.cpp, readGameInput, where the
// pads are read).
//
// THE BUTTON IS SET BY PRESSING IT, and it is read RAW: the joystick's own
// button number, not the gamepad mapping. That is the point of it. On the A9,
// 2026-09-27, the community list mapped an 8BitDo Lite 2's heart to the wrong
// button, so the app never saw Home from that pad; a raw button works whether
// or not the list is right. A raw number means different things on different
// models, so it is remembered per KIND of pad (SDL's GUID), not per pad: set
// once for a Lite 2, every Lite 2 uses it.
//
// Unset, a pad uses its mapped Home, which is right for most pads. A chosen
// button is ADDED to Home, never put in its place, and Select, Start, L3 and
// R3 can never be chosen: the coin and the pause inside games, and the menu
// chord (#313). The row reads "Shortcuts use: Home" (MMagTech, 2026-10-20).

#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace shortcuts {

// The switch. Saved in config/settings.json as "on" or "off".
bool enabled();
void setEnabled(bool on);

// Whether this pad's shortcut button is down now.
bool held(SDL_Gamepad* gp);
// Whether `b` is that pad's shortcut button, as SDL names it.
bool isShortcut(SDL_Gamepad* gp, SDL_GamepadButton b);

// What the button is called, for the Settings row: "Home", "Select",
// "L3", or "Button 3" for one the mapping does not name. The second form
// names a raw button that was pressed, saved or not.
std::string label(SDL_Gamepad* gp);
std::string label(SDL_Gamepad* gp, int raw);

// Setting it. `raw` is the joystick button number from a
// SDL_EVENT_JOYSTICK_BUTTON_DOWN on this pad.
enum class Pick { Taken, Cancel, Ignore, Refused };
// What a press means while the row is listening: B cancels, and a button the
// game or the shortcuts need (the face buttons, the shoulders, the d-pad) is
// ignored so listening goes on; Select, Start, L3 and R3 are refused, which
// the row says, and listening goes on. Anything else is taken and saved.
Pick pick(SDL_Gamepad* gp, int raw);

}  // namespace shortcuts
