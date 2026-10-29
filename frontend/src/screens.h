// What every screen past Home shares: what it draws with (Ctx), the keys it
// is given (Nav) and what it asks the app to do (Action, Result). The screens
// are in their own files: libraryscreen.h, grid.h, searchscreen.h,
// detailscreen.h (the launch screen), accountpanel.h and qrlink.h, moved out
// of this file in the restructure (#398), and settings.h, choice.h,
// manualview.h, downloads.h, achlist.h and pin.h.
//
// Home is resume-first and it is one screen. Everything else in the product is
// reached from the Library, and until this existed 1100 playable games had
// exactly fifty of them reachable — the ones Home happened to show.
//
// WHY THESE ARE NOT IN THE APP. Each screen owns focus, scroll and remembered
// state that only it understands, and the alternative is one function holding
// four screens' worth of indices at once. What they deliberately do NOT own is
// anything that touches the network, the disk or a core: a screen returns an
// Action and the app decides what that means. So a screen can be drawn in a
// test, and nothing here can start a download by accident.
//
// THE NAVIGATION MODEL, from docs/PROJECT.md:
//
//   - Each tab owns a navigation stack. Library pushes to a platform's grid.
//   - The launch screen is a FULL-SCREEN COVER, not a push. It replaces the
//     screen entirely, with the artwork as its own backdrop.
//   - The player is a full-screen cover over that. So quitting a game returns
//     to the launch screen, and backing out again returns to where the person
//     was browsing.
//   - No screen titles that repeat the tab: Library has no "Library" heading.
//     A pushed page carries its title as ordinary content at the top of its own
//     scroll view, never as system chrome.

#pragma once

#include <string>
#include <vector>

#include "card.h"
#include "design.h"
#include "image.h"
#include "motion.h"
#include "text.h"
#include "ui.h"

namespace screens {

// WHICH KINDS OF CONTROLLER ARE ON, as the top bar's battery row shows them:
// a Wii Remote (W1 to W4) and any other (P1 to P4, a stream's included). The
// focused game's line reads them (drawFocusedLine): "Needs a controller" on a
// game that is not a Wii game while only Remotes are on, "Needs a Wii Remote"
// on a Wii game that needs one while none is.
struct ControllersOn {
    bool remote = false;
    bool other = false;
};
// The two notes, and the toasts a press on Play gets in their place
// (App::refuseStartWithoutController): the same words in both places.
inline const std::string kNeedsController = "Needs a controller";
inline const std::string kNeedsRemote = "Needs a Wii Remote";

// Everything a screen needs to draw itself. References, because the app owns
// all of it and a screen outlives none of it.
struct Ctx {
    ui::Renderer& r;
    ui::TextRenderer& text;
    ui::ImageCache& images;
    float sc = 1.0f;                       // device pixels per design point
    std::vector<design::Card>* cards = nullptr;
    // Which controllers are on now (App::controllersOn).
    ControllersOn on = {};
};

// One keypress, already translated out of SDL so a screen never sees an event.
enum class Nav { Left, Right, Up, Down, Activate, Back };

// What a screen asks the app to do. The app decides whether it can.
enum class Action {
    None,
    Back,
    OpenTile,          // value is the tile index within the visible switcher tab
    OpenGame,          // value is a card index
    Play,              // value is a rom id
    Download,          // value is a rom id — fetch it AND keep it
    RemoveDownload,    // value is a rom id — release the keep AND delete the game
    PlayState,         // value is a RomM state id — play, starting from that state
    ToggleFavorite,    // value is a rom id — the game's page heart/star (#267)
    ShowAchievements,  // value is a rom id — its RetroAchievements list (#74)
    // UP OUT OF THE TOP ROW, INTO THE BAR. MMagTech: *"if the library has the
    // top bar in view shouldnt i be able to up and access it."* Yes — chrome
    // that is on screen and cannot be reached is worse than chrome that is
    // hidden, because it looks like the console stopped responding.
    //
    // The screen says "focus left me upwards" and the app decides what is up
    // there, which is the same division every other Action here follows: the
    // screen knows its own rows and nothing about the product around it.
    FocusBar,
    // Down out of the search results, back into the keyboard docked under
    // them. Only Search sends this, and only the app can act on it: the
    // keyboard belongs to the app, not to a screen.
    FocusKeyboard,
    // Become this account. The value is a RomM user id, not a row index —
    // the screen is handed ids and hands one back, so a list that changed
    // underneath it cannot switch the console to the wrong person.
    SwitchAccount,
    // Pair somebody new against the server this console already uses. The app
    // owns it because pairing needs a client and a worker, and because it must
    // happen on a SEPARATE client: adding an account mid-session must not be
    // able to throw the console back into setup.
    AddAccount,
    // A Settings row was pressed. The value is the row's own id, which the
    // app assigned when it built the rows; see settings.h.
    Setting,
    // A Settings choice row moved to another choice. The value is the row's
    // id; SettingsScreen::choiceOf says which choice it is on now.
    SettingChoice,
};

struct Result {
    Action action = Action::None;
    int value = 0;
};

}  // namespace screens
