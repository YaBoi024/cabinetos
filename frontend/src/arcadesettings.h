// Holding Start in an arcade game opens the game's own settings (#577,
// docs/decisions/2026-10-25-hold-start-opens-arcade-settings.md).
//
// THE CONSOLE DRAWS NOTHING. It opens what each emulator already has: MAME
// 2003-Plus's own menu (the DIP switches, among the rest), and on FinalBurn
// Neo the board's own test menu. A game that has neither gets nothing.
//
// ONE RULE FOR BOTH: player 1 holds Start for 60 frames, about a second, as
// FinalBurn Neo does by itself (its fbneo-diagnostic-input, "Hold Start", left
// at its default: SetDiagInpHoldFrameDelay(60) in src/burner/libretro/
// retro_common.cpp at our pin). Start reaches the game the whole time, as it
// does on FinalBurn Neo, so a tap to join a game is never held back. A hold
// counts once; Start is let go before the next one counts. The frames are the
// game's own, so a hold does not count while the pause menu is up.
//
// MAME 2003-PLUS OPENS ITS MENU ON TAB OR ON PLAYER 1'S NINTH BUTTON
// (IPT_UI_CONFIGURE in src/inptport.c at our pin), and its ninth button is
// player 1's left stick click in every one of its layouts. So:
//   * the console presses Tab for it, kTabFrames long, when a hold counts.
//     MAME reads the keyboard through libretro's input state on port 0
//     (osd_is_key_pressed, src/mame2003/mame2003.c) as long as its
//     mame2003-plus_input_interface is "simultaneous", its default, which the
//     console leaves alone. A second hold presses Tab again, which closes it.
//   * player 1's left stick click never reaches MAME 2003-Plus. It opened the
//     menu when L3 + R3 was pressed to reach the pause menu and the clicks
//     were more than kStickPairMs apart (app.h), MMagTech, 2026-10-24. No
//     game loses anything it had: a game whose player 1 has a ninth button
//     opened MAME's menu with it as well.
// Both lean on MAME's internals; the core's build stops when either reads
// otherwise at a new pin (cores/build-core.sh, its mame2003_plus block).

#pragma once

#include <cstdint>
#include <string>

namespace arcadesettings {

// libretro's RETROK_TAB, the key MAME 2003-Plus opens its menu with
// (asserted against libretro.h in arcadesettings.cpp).
constexpr unsigned kMenuKey = 9;
// How long a hold is, in the game's frames: FinalBurn Neo's own.
constexpr uint64_t kHoldFrames = 60;
// How long Tab is held for MAME: long enough for its once-a-frame check of
// the menu key to see it go down, then let go so it sees one press.
constexpr uint64_t kTabFrames = 4;

// Player 1's hold, kept by the App from frame to frame.
struct Hold {
    bool down = false;       // Start held, as the game hears it
    bool counted = false;    // this hold has opened the menu
    uint64_t since = 0;      // the frame Start went down
    uint64_t tabUntil = 0;   // Tab is down until this frame
};

// Once a frame, before the core runs, with player 1's buttons as the game is
// about to hear them (cab::Button bits) and the core's frame count. Changes
// them to what the game should hear, and returns whether the menu key is down.
bool step(Hold& h, const std::string& core, uint32_t& buttons, uint64_t frame);

// --arcade-settings-test: the rule above, with no core and no pad.
int test();

}  // namespace arcadesettings
