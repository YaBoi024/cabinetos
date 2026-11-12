// FORK (handheld): WHAT THE MACHINE UNDER THE PROGRAM DECIDES.
//
// The console owns its whole machine: its network, its pads, its keyboard
// handling. A build for a phone does not, and the phone's own way of doing each
// of these is better than copying the console's. This file is the short list of
// places where the program asks.
//
// Every answer here is the console's, and every call does nothing, in
// platform_linux.cpp. A build for another machine leaves that file out and
// provides its own (the Android app's is platform_android.cpp). So nothing in
// the program branches on an operating system: it asks one of these, and on the
// console the answer is the one it always was.
//
// Keep it short. A hook is added when a screen has no other way to do a job the
// machine already does, and each one is a line in FORK-CHANGES.md.

#pragma once

#include <string>

namespace platform {

// --- First run -----------------------------------------------------------------

// The machine has its own network screens (a phone's Settings), so first run's
// network step lists no networks. It says whether the machine is online, and
// when it is not, offers openNetworkSettings(). It moves on by itself once the
// machine is online.
bool ownsNetwork();

// The machine pairs its own controllers (a phone's Bluetooth settings), so first
// run has no step for pad one.
bool skipsControllerStep();

// --- Opening something outside the program ------------------------------------

// Whether openUrl() / openNetworkSettings() can do anything here. A screen shows
// the button only when it can.
bool canOpenUrl();
bool canOpenNetworkSettings();

// Hands a link to the machine's browser. The program never shows a page itself.
// Returns false when nothing could be opened.
bool openUrl(const std::string& url);

// Opens the machine's own network settings. False when nothing could be opened.
bool openNetworkSettings();

// --- Text entry ----------------------------------------------------------------

// The machine's own keyboard is used, and only while a text field is open:
// text input is not started when the program starts (startup.cpp, setup.cpp),
// textFieldOpened() starts it and textFieldClosed() stops it. The program's own
// on-screen keyboard still draws the field. On the console text input is on
// from the start, as it always was, and these two do nothing.
bool textInputOnDemand();
void textFieldOpened();
void textFieldClosed();

// The machine's keyboard does the typing, so the program draws no key grid of
// its own for a full-size field: the panel is the title, the field and a Done
// key, placed high on the screen so the machine's keyboard does not cover it.
// A field docked under search results (Config::dockedBottom) is the same: the
// field and a Done key, sitting where the machine's keyboard leaves room, and
// the results use the room above it. On the console this is false and the
// keyboard is what it always was.
bool ownsKeyboard();

// --- The screen's shape ----------------------------------------------------------

// The picture is drawn on a canvas 1080 high and 1920 wide, fitted into the
// screen. On a screen wider than that (a phone is about 2.2 to 1) the console
// letterboxes it. When this is true the canvas is made as wide as the screen
// (up to 2.4 to 1) so the screens use all of it. A screen taller than 16:9
// still gets bars, as before.
bool fillsWideScreens();

// FORK (iPhone): the same for a screen NARROWER than 16:9 (an iPhone Duo's two
// screens are about 1.4 to 1). When this is true the canvas is made as narrow as
// the screen (down to 4 to 3) so the screens use all of it, instead of leaving
// bars above and below. On the console this is false and nothing changes.
bool fillsNarrowScreens();

// FORK (iPhone): how much of the window's edge a phone covers: a camera
// cut-out, a rounded corner, the home indicator. In window pixels, from each
// edge. The picture is fitted inside what is left. windowW and windowH are the
// size beginFrame was asked to draw; a platform answers all zeros when they are
// not its window's (a capture drawn off screen). The console answers zeros.
void safeInsets(int windowW, int windowH, int* left, int* top, int* right, int* bottom);

// --- The library ----------------------------------------------------------------

// A system whose emulator is not in the program yet still shows its games,
// greyed with the reason, in grids and in Search, as a game that needs a Wii
// Remote does. The console never shows them: its emulators ship in the image
// and a missing one is a fault. A phone is given its emulators one at a time.
bool showsGamesWithoutACore();

// --- A game running -------------------------------------------------------------

// Told when a game starts (true) and when it ends (false), on the thread that
// runs the program. The console does nothing. A phone uses it to show its
// on-screen buttons and keep the screen on while a game runs, and to put both
// away afterwards. Not called for a game that never got as far as running.
// platformSlug is the system as RomM names it ("snes", "gba", "nds"), so a phone
// can show the buttons that system had; it is empty when on is false.
void gamePlaying(bool on, const char* platformSlug);

// FORK (handheld): AN EMULATOR THAT IS ITS OWN ANDROID APP. The emulators of
// standalone.h are programs this one starts; Android does not let an app start
// another as a child, but it lets it ask the system to open a file in the app
// that handles it. On a phone with Eden installed, a Switch game is therefore
// handed to Eden, which draws its own screen, and the person comes back here
// when they leave it. `core` is the catalog's name for the emulator ("eden").
//
// runsInAnotherApp is true only when the app is installed, so a phone without
// it answers "not installed" as the console does. openInAnotherApp hands over
// the game file at `path` and returns false when nothing was opened; then
// nothing started. Both are false on the console.
bool runsInAnotherApp(const char* core);
bool openInAnotherApp(const char* core, const std::string& path);

// FORK (handheld): WHERE TO GET AN EMULATOR THAT IS ITS OWN APP, when it is not installed: the
// address of its official download page, or empty when this machine installs it some other way
// (the console's first boot does, from Flathub). The game's page offers to open it.
std::string emulatorInstallUrl(const char* core);

// FORK (handheld): A SETTINGS ROW THAT MEANS NOTHING ON THIS MACHINE, by its title on screen
// ("Wii Remotes", "File access", "System update"...). True hides the row; a section left with no
// rows goes too. The console answers false for every title.
bool hidesSetting(const char* title);

// FORK (handheld): A GLES CORE'S OWN CONTEXT. A core drawing with OpenGL ES shares the UI's context
// on the console, and restoreGLState (core_hwrender.cpp) puts the UI's state back after each frame.
// GLideN64 (Nintendo 64) keeps its own record of what it left bound and does not bind it again,
// and on a phone's driver the next frame drew from nothing and crashed (iPhone and Android,
// 2026-11-12). RetroArch gives such a core a context of its own, and so does a phone here:
// make: a new OpenGL ES 3 context in the share group of the one current now, or null where the
// machine does not offer it (the console). use: make that one current; with null, the one that was
// current when it was made. free: done with it. Called on the thread that runs the game.
void* coreGLContextMake();
void coreGLContextUse(void* context);
void coreGLContextFree(void* context);

}  // namespace platform
