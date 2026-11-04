// First run, as a person sees it.
//
// `firstrun.h` owns the RULES — the chain, and what may be skipped. This owns
// the experience: the screens, the keyboard, and the blocking work that has to
// happen off the frame loop so the console never stops drawing while it waits.
//
// --- IT RUNS ITS OWN LOOP, AND THAT IS THE DESIGN ---------------------------
//
// Setup is not a mode inside the main frame loop; it is a loop of its own that
// runs to completion and returns. Three reasons, in the order they matter:
//
//   1. **The app cannot start without a server** and setup is what supplies
//      one. The library is fetched from RomM before the main loop exists, so a
//      first run woven into that loop would have to survive a state where the
//      thing the loop is built around does not exist yet.
//   2. **It genuinely is linear and happens once.** docs/PROJECT.md open
//      question 15b: there is no skip and no branch, because on the far side of
//      a skip there is nothing to show. A mode flag would be modelling a
//      freedom the product does not have.
//   3. Main is already five thousand lines. Threading a sixth state through it
//      would make both halves harder to read for no gain.
//
// --- NOTHING HERE BLOCKS THE FRAME ------------------------------------------
//
// A Wi-Fi scan is seconds, a Bluetooth scan is ten, and waiting for somebody to
// pick up a phone and approve a pairing is minutes. Every one of those runs on
// a worker and is polled once a frame, the same shape `LaunchJob` (launch.h)
// and `StateLoad` (states.h) already use. A setup screen that freezes while it
// looks for networks is indistinguishable from a console that has crashed —
// and it would be the FIRST thing anybody ever saw it do.

#pragma once

#include <string>

#include "keyboard.h"
#include "startscreen.h"

namespace setup {

struct Options {
    // Open at a named step — timezone, network, server, pair, controller,
    // done. With none, first run opens at the time zone on a console that has
    // no zone, and at the network on any other.
    //
    // The same reasoning as `--screen`: every screen has to be photographable
    // from a machine with no controller and nobody to describe it. What it does
    // NOT do is fake the facts, so a step opened on a machine that could not
    // reach it still draws the truth about that machine.
    const char* startStep = nullptr;

    // Capture, exactly as the rest of the frontend does it.
    const char* screenshotPath = nullptr;
    int frames = 0;

    // Render at a size this machine's window is not, so a layout can be checked
    // against a television nobody here owns. Without it a headless capture
    // comes out at the offscreen driver's 1024x768 — a 4:3 frame, on which
    // every judgement about a 16:9 layout is worthless.
    int renderWidth = 0, renderHeight = 0;

    // NEVER WRITE ANYTHING. For captures and for looking at the flow on a
    // console that is already set up, which is the only way anybody here can
    // look at it at all — the reference machine is configured and taking that
    // away to see a screen would be a silly way to lose an afternoon.
    bool dryRun = false;

    // BACK FROM SIGN OUT (issue #61): the server, then pairing, then the
    // console. The network and the controllers stayed, so their steps and
    // the Ready screen ("You can unplug the keyboard") are not shown.
    // MMagTech, 2026-09-25.
    bool signedOut = false;
};

enum class Outcome {
    Completed,   // setup finished; the console is configured
    Quit,        // the person quit, or a capture run ended
};

// Runs first run to completion. On success the server address and the token are
// on disk and the marker is written, so the caller can carry on as if the
// console had always been configured.
Outcome run(const Deps& d, const Options& o);

}  // namespace setup

// The keyboard for a server address in Settings and on the startup screen's
// server wait, and its trim. First run's own keyboard (Flow, Act::Edit) is
// this one with its own title and hint. Moved out of main.cpp in the
// restructure (#398).
ui::Keyboard::Config serverKeyboardConfig(const std::string& initial);
std::string trimmedAddress(std::string a);
