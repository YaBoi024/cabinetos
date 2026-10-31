// Holding Start in an arcade game opens the game's own settings (#577,
// docs/decisions/2026-10-25-hold-start-opens-arcade-settings.md).
//
// THE CONSOLE DRAWS NOTHING. It opens what each emulator already has: MAME's
// own menu in MAME 2003-Plus and MAME 0.289 (the DIP switches, among the
// rest), and on FinalBurn Neo and on a Naomi or Atomiswave board in Flycast
// the board's own test menu. A game that has neither gets nothing.
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
//   * the console presses Tab for it, kPressFrames long, when a hold counts.
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
//
// MAME 0.289 OPENS ITS MENU ON TAB ALONE (its UI menu key; no button opens
// it), and hears keys only through its keyboard callback, which Core::setKey
// tells (core_input.cpp). So the console presses Tab for it the same way, a
// second hold closes it, and its left stick click, its button 7, reaches the
// game. cores/fetch-mame.sh stops when its joypad gives a button the menu.
//
// FLYCAST HAS A BOARD'S TEST AND SERVICE BUTTONS ON L3 AND R3 (its nao_joymap
// and aw_joymap), which no cabinet's player could reach. So, in a Naomi or
// Atomiswave game:
//   * the console presses Test for player 1, kPressFrames long, when a hold
//     counts: the board's test menu, where its own Test press leaves it.
//   * every player's L3 and R3 never reach the board (keepClicks), so the
//     pause menu's pair opens no test menu and takes no credit.
// The core's build stops when the maps move (cores/build-core.sh, its flycast
// block).

#pragma once

#include <cstdint>
#include <string>

namespace arcadesettings {

// libretro's RETROK_TAB, the key both MAMEs open their menu with (asserted
// against libretro.h in arcadesettings.cpp).
constexpr unsigned kMenuKey = 9;
// How long a hold is, in the game's frames: FinalBurn Neo's own.
constexpr uint64_t kHoldFrames = 60;
// How long Tab is held for MAME, or Test for a Naomi or Atomiswave board:
// long enough for a once-a-frame check to see it go down, then let go so it
// sees one press.
constexpr uint64_t kPressFrames = 4;

// Player 1's hold, kept by the App from frame to frame.
struct Hold {
    bool down = false;        // Start held, as the game hears it
    bool counted = false;     // this hold has opened the menu
    uint64_t since = 0;       // the frame Start went down
    uint64_t pressUntil = 0;  // Tab, or Test, is down until this frame
};

// Once a frame, before the core runs, with player 1's buttons as the game is
// about to hear them (cab::Button bits) and the core's frame count. Changes
// them to what the game should hear, and returns whether the menu key is down.
// `core` is the session's arcadeCore (gamesession.h): empty for a game that
// is not an arcade game.
bool step(Hold& h, const std::string& core, uint32_t& buttons, uint64_t frame);

// The same for every other player: their L3 and R3 kept from a Naomi or
// Atomiswave board, and nothing changed for any other game.
void keepClicks(const std::string& core, uint32_t& buttons);

// --arcade-settings-test: the rule above, with no core and no pad.
int test();

}  // namespace arcadesettings
