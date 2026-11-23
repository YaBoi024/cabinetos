// Which of a DS or 3DS game's two screens show: both, the top one alone or
// the bottom one alone, moved on by one button (MMagTech 2026-10-26,
// docs/decisions/2026-10-26-ds-3ds-screen-view.md).
//
// THE BUTTON IS L3 ON BOTH, player 1's left stick click. Each press moves the
// view on one: both, top, bottom, both again. Neither console has a stick to
// click, so no game reads it; it was each emulator's extra:
//   * 3DS: Azahar's "Home/Swap screens" (src/citra_libretro/citra_libretro.cpp
//     at our pin). The click never reaches it, so its swap, which only traded
//     the screens' sides, never happens. HOME does nothing unless the 3DS Home
//     Menu was started first (AppletManager::ButtonUpdateEvent,
//     src/core/hle/service/apt/applet_manager.cpp), and a game here starts
//     straight in.
//   * DS: melonDS's lid, closed while it is held (src/libretro/input.cpp at
//     our pin), and closed, the DS sleeps. THE LID MOVES TO R2, where melonDS
//     has its swap: R2 held is the lid closed, so the few games that ask for
//     it (Zelda: Phantom Hourglass's map) still get it, and melonDS's swap,
//     which only stacked the screens the other way up, never happens. L2,
//     the microphone, stays.
//
// THE VIEW IS THE EMULATOR'S LAYOUT, set while the game runs
// (Core::setOptionLive): melonDS's "Top Only" and "Bottom Only", Azahar's
// "single_screen" with citra_swap_screen saying which. Both re-read it on
// their next frame and send the picture's new shape (#220). One screen is
// smaller than both, so a hardware renderer's target, made at load for both,
// holds it.
//
// TOUCH ONLY WHILE THE TOUCH SCREEN SHOWS: with the top screen alone both
// emulators stop touching by themselves; with the bottom one alone the right
// stick and R3 touch as they do with both.
//
// EVERY GAME STARTS WITH BOTH: the options set at launch replace what a press
// set (Core::setOptionOverrides), and the press's count starts again with the
// game's frames.
//
// THE PAUSE MENU NEVER MOVES IT ON: a stick click reaches a game only alone
// (play.cpp, the App's StickPair), so the L3 of L3 + R3 is no press here, and
// a button held while the menu is up is kept from the game.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace screenview {

enum class View { Both, Top, Bottom };
constexpr int kViewCount = 3;

// Player 1's button, kept by the App from frame to frame.
struct Press {
    bool down = false;        // the button held, as the game would hear it
    View view = View::Both;   // what the emulator draws now
    uint64_t frame = 0;       // the last frame seen
};

// The options that draw `view` on `core`; empty for a core with one screen.
std::map<std::string, std::string> coreOptions(const std::string& core, View view);

// Once a frame, before the core runs, with player 1's buttons as the game is
// about to hear them (cab::Button bits) and the core's frame count. Takes L3
// out of them (and on the DS puts R2 there in its place, the lid), and on
// L3's press returns the view to draw now.
std::optional<View> step(Press& p, const std::string& core, uint32_t& buttons, uint64_t frame);

// --screen-view-test: the rule above, with no core and no pad.
int test();

}  // namespace screenview
