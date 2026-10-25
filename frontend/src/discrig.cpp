// The disc rig (#162, --disc-script): a test instrument that drives a game
// by script through the calls the pause menu makes. Its script is split
// before the loop and run one step at a time in the frame (#398,
// restructure batch g).

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "capture.h"
#include "core.h"
#include "fileio.h"
#include "imagecodec.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// The rig's script (--disc-script) split into its steps, once, before the loop.
void App::parseDiscScript() {
    std::string step;
    for (char ch : discScriptText + ";") {
        if (ch != ';') { step += ch; continue; }
        while (!step.empty() && step.front() == ' ') step.erase(step.begin());
        while (!step.empty() && step.back() == ' ') step.pop_back();
        if (!step.empty()) discScript.steps.push_back(step);
        step.clear();
    }
}

// THE DISC RIG (#162, --disc-script). One step at a time, each when
// the last is done; `wait` counts the game's own frames, so a step
// means the same on any machine. Every step calls what the pause menu
// calls.
void App::runDiscScript() {
    if (discScript.at < discScript.steps.size() && playing &&
        cab::Core::shared().running()) {
        cab::Core& core = cab::Core::shared();
        const uint64_t now = core.framesRun();
        // A Start over begins the game again, and its frames from zero.
        if (!discScript.started || now < discScript.from) {
            discScript.started = true;
            discScript.from = now;
            discScript.wallFrom = SDL_GetTicksNS();
        }
        const std::string& stepText = discScript.steps[discScript.at];
        std::istringstream in(stepText);
        std::string verb;
        in >> verb;
        bool done = true;
        auto say = [&](const std::string& what) {
            std::fprintf(stderr, "[disc-script] frame %llu: %s%s%s\n",
                         static_cast<unsigned long long>(now), stepText.c_str(),
                         what.empty() ? "" : " -> ", what.c_str());
        };
        if (verb == "wait") {
            uint64_t n = 0;
            in >> n;
            done = now >= discScript.from + n;
        } else if (verb == "sleep") {
            double sec = 0;
            in >> sec;
            done = (SDL_GetTicksNS() - discScript.wallFrom) / 1e9 >= sec;
        } else if (verb == "insert") {
            int n = 0;
            in >> n;
            beginInsert(n);
            say("");
        } else if (verb == "startover") {
            int n = 0;
            in >> n;
            say("");
            ++discScript.at;
            discScript.started = false;
            done = false;
            startOverOnDisc(n);
        } else if (verb == "state") {
            saveStateNow(session, uploader, menuNotice);
            say(menuNotice.text);
        } else if (verb == "load") {
            beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
            say("");
        } else if (verb == "resume") {
            // What A does once a state has loaded and the game waits: the
            // step waits for that hold (a load is fetched on a worker),
            // then ends it. Ten seconds with no hold and it moves on.
            if (stateHold) {
                stateHold = false;
                stateHoldJustEnded = true;
                say("continuing");
            } else {
                done = (SDL_GetTicksNS() - discScript.wallFrom) / 1e9 >= 10.0;
                if (done) say("no state was holding the game");
            }
        } else if (verb == "press") {
            // press <button> <frames>: held for the game, then let go.
            std::string b;
            int frames = 6;
            in >> b >> frames;
            using cab::bit;
            const uint32_t m = b == "a" ? bit(cab::A) : b == "b" ? bit(cab::B)
                             : b == "x" ? bit(cab::X) : b == "y" ? bit(cab::Y)
                             : b == "start" ? bit(cab::Start) : b == "select" ? bit(cab::Select)
                             : b == "up" ? bit(cab::Up) : b == "down" ? bit(cab::Down)
                             : b == "left" ? bit(cab::Left) : b == "right" ? bit(cab::Right)
                             : b == "l" ? bit(cab::L) : b == "r" ? bit(cab::R)
                             : b == "l2" ? bit(cab::L2) : b == "r2" ? bit(cab::R2)
                             : b == "l3" ? bit(cab::L3) : b == "r3" ? bit(cab::R3) : 0u;
            // A mistyped button presses nothing and says so, as --nav does.
            if (m == 0u) {
                say("UNKNOWN button " + b);
            } else {
                discScript.pad = m;
                discScript.padUntil = now + static_cast<uint64_t>(std::max(1, frames));
                say("");
            }
        } else if (verb == "shot") {
            // The game's own frame, the menu never in it, as a PNG.
            std::string name;
            in >> name;
            std::vector<uint8_t> rgba, png;
            unsigned w = 0, h = 0;
            const std::string path = "/tmp/disc-" + name + ".png";
            const bool ok = core.snapshot(rgba, w, h) && ui::encodePNG(rgba, w, h, png) &&
                            cab::writeBytes(path, png);
            say(ok ? path : std::string("NO PICTURE"));
        } else if (verb == "ui") {
            // The whole screen, menus and all, as the capture signal does.
            std::string name;
            in >> name;
            discScript.captureTo = "/tmp/disc-" + name + ".bmp";
            gCaptureRequested = 1;
            say(discScript.captureTo);
        } else if (verb == "menu") {
            if (!overlayOpen) toggleOverlay();
            for (size_t k = 0; k < pauseItems.size(); ++k)
                if (pauseItems[k] == OvDisc) overlaySlot = static_cast<int>(k);
            say(std::string("Change disc row ") +
                (std::find(pauseItems.begin(), pauseItems.end(), OvDisc) != pauseItems.end()
                     ? "shown" : "NOT SHOWN"));
        } else if (verb == "activate") {
            overlayActivate();
            say("");
        } else if (verb == "nav") {
            // nav up|down|left|right|a|b, into whatever has the press.
            std::string k;
            in >> k;
            // Anything else is refused, not read as A: in the pause menu a
            // stray A can be Exit to Home.
            if (k != "up" && k != "down" && k != "left" && k != "right" && k != "a" &&
                k != "b") {
                say("UNKNOWN nav " + k);
            } else {
                const screens::Nav n = k == "up"     ? screens::Nav::Up
                                     : k == "down"  ? screens::Nav::Down
                                     : k == "left"  ? screens::Nav::Left
                                     : k == "right" ? screens::Nav::Right
                                     : k == "b"     ? screens::Nav::Back
                                                    : screens::Nav::Activate;
                if (choiceScreen.isOpen()) navigate(n);
                else if (n == screens::Nav::Up) ovStep(-1);
                else if (n == screens::Nav::Down) ovStep(+1);
                else if (n == screens::Nav::Activate) overlayActivate();
                else if (n == screens::Nav::Back) closeOverlay();
                say(choiceScreen.isOpen() ? "panel: " + choiceScreen.title() : "");
            }
        } else if (verb == "close") {
            if (choiceScreen.isOpen()) choiceScreen.close();
            if (overlayOpen) closeOverlay();
            say("");
        } else if (verb == "check") {
            say("disc " + std::to_string(session.disc) + " of " +
                std::to_string(disc::highest(session.discs)) + ", core index " +
                std::to_string(core.discIndex()) + " of " + std::to_string(core.discCount()) +
                (core.discEjected() ? ", lid OPEN" : ", lid closed") + ", last disc " +
                std::to_string(disc::lastDisc(storage::currentUser(), session.romId)));
        } else if (verb == "play") {
            // play <romId>: leave this game and start another in the same
            // run, the way Home's Play would.
            int id = 0;
            in >> id;
            say("");
            ++discScript.at;
            discScript.started = false;
            done = false;
            exitToHome();
            // As --launch does: fetched by id if the library lacks it.
            autoLaunchId = id;
            autoLaunchAfter = 1.0f;
        } else if (verb == "quit") {
            say("");
            ++discScript.at;
            discScript.started = false;
            done = false;
            exitToHome();
            if (discScript.at == discScript.steps.size())
                std::fprintf(stderr, "[disc-script] done\n");
        } else {
            say("UNKNOWN STEP");
        }
        if (done) {
            ++discScript.at;
            discScript.started = false;
            if (discScript.at == discScript.steps.size())
                std::fprintf(stderr, "[disc-script] done\n");
        }
    }
    if (!discScript.steps.empty() && discScript.at == discScript.steps.size() &&
        !playing && !discScript.said) {
        discScript.said = true;
        std::fprintf(stderr, "[disc-script] back on Home\n");
    }
}
