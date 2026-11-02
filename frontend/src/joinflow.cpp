// FORK (handheld): JOIN A FRIEND. This console as the guest in another
// console's stream: the friend's address on the keyboard, a pairing the first
// time (four digits shown here, typed on the friend's PIN pad, which opens by
// itself there: remoteplayflow.cpp), then Moonlight full screen with this
// console's controllers. The friend's game runs on the friend's console;
// this one only shows it and sends the buttons.
//
// Moonlight is handled as an emulator of its own is (standalone_play.cpp):
// this window steps aside as gamescope's overlay, each real pad's presses go
// to its player's virtual controller (vpad.h), and the console is back on
// Settings when Moonlight ends. Both stick clicks together leave, as they
// open the pause menu in a game; Moonlight's own Select+Start+L1+R1 does too.
// The friend's pause menu is the friend's: hold Select, as on any device.
//
// UNTESTED ON A DECK (2026-10-29). Moonlight must be installed: flatpaks.list
// pins it (tools/moonlight-pin.sh).

#include "app.h"

#include <signal.h>
#include <sys/wait.h>

#include <algorithm>
#include <cstdio>

#include "friends.h"
#include "moonlight.h"
#include "overlaywin.h"
#include "proc.h"
#include "rumble.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {
// A stream that ends this soon, with an error, never started: a wrong
// address, a friend's console that is off, or a pairing Moonlight no longer
// has. Measured nowhere yet; long enough for Moonlight's own connect timeout.
constexpr uint64_t kFailedStartMs = 15000;
// After asking Moonlight to close, how long before it is made to.
constexpr uint64_t kStopGraceMs = 5000;
}  // namespace

void App::pressJoin() {
    if (!moonlight::installed()) {
        menuNotice.say("Moonlight isn't on this console yet", Tone::Problem);
        sound::play(sound::Cue::Edge);
        return;
    }
    sound::play(sound::Cue::Activate);
    if (friends::list().empty()) askJoinAddress("");
    else openFriendsPanel(0);
}

void App::startJoin(const std::string& host) {
    moonlight::setLastHost(host);
    if (moonlight::isPaired(host)) startJoinStream(host);
    else startJoinPair(host);
}

void App::askJoinAddress(const std::string& why) {
    ui::Keyboard::Config cfg;
    cfg.title = "Your friend's console";
    cfg.hint = "Its address, on their Play with a friend page";
    cfg.initial = moonlight::lastHost();
    cfg.placeholder = "192.168.0.20";
    cfg.shortcuts = {".", ":"};
    keyboard.open(cfg);
    if (!why.empty()) keyboard.sayInField(why, /*problem=*/true, /*keep=*/true);
    keyboardThen = [&](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        const std::string host = moonlight::cleanHost(keyboard.value());
        if (host.empty()) {
            askJoinAddress("That isn't an address");
            return;
        }
        startJoin(host);
    };
}

// THE FIRST TIME WITH THIS FRIEND: Moonlight pairs with four digits chosen
// here, and the friend's console asks for them. Nothing is typed on this
// console, so the digits are simply shown until the friend has typed them.
void App::startJoinPair(const std::string& host) {
    const std::string pin = moonlight::newPin();
    joinHost = host;
    joinScreen.open();
    joinScreen.setText("Join a friend",
                       {"Your friend's console asks for a code.",
                        "Type these digits there:",
                        "",
                        "    " + pin,
                        "",
                        "Their console must be on Home,",
                        "not in a game."},
                       "");
    joinScreen.setBusy(false);
    if (here() != Screen::Join) stack.push_back(Screen::Join);
    joinCancel.store(false);
    const std::vector<std::string> args = moonlight::pairArgs(host, pin);
    std::fprintf(stderr, "[join] pairing with %s\n", host.c_str());
    joinPair.start([this, args](int& status, std::string& err) {
        const proc::Result r = proc::run(args, 120, &joinCancel);
        status = r.status;
        err = proc::trimmed(r.err);
        return r.ok();
    });
}

// Once a frame: the pairing's answer, and leaving its screen cancels it.
void App::pollJoin() {
    if (joinPair.busy() && here() != Screen::Join) joinCancel.store(true);
    int status = -1;
    std::string err;
    bool ok = false;
    if (!joinPair.take(&status, &err, &ok)) return;
    if (joinCancel.load()) {
        std::fprintf(stderr, "[join] pairing with %s cancelled\n", joinHost.c_str());
        return;
    }
    if (!ok) {
        std::fprintf(stderr, "[join] pairing with %s failed (%d): %s\n", joinHost.c_str(),
                     status, err.c_str());
        joinScreen.setError(status == -1 ? "Couldn't start Moonlight."
                                         : "Couldn't pair. Check the address, and that "
                                           "Streaming is on there.");
        return;
    }
    std::fprintf(stderr, "[join] paired with %s\n", joinHost.c_str());
    moonlight::rememberPaired(joinHost);
    if (here() == Screen::Join) stack.pop_back();
    startJoinStream(joinHost);
}

void App::startJoinStream(const std::string& host) {
    // ONE VIRTUAL CONTROLLER PER PLAYER HERE, fixed for the stream as for a
    // game: each arrives at the friend's console as a player of its own.
    const int pads = std::max(1, players::count());
    if (!cab::vpad::open(pads)) {
        menuNotice.say("Couldn't start Moonlight", Tone::Problem);
        return;
    }
    storage::makeDirs(storage::logsDir());
    const int pid = proc::spawn(moonlight::streamArgs(host, pads),
                                storage::logsDir() + "/moonlight.log");
    if (pid <= 0) {
        cab::vpad::close();
        menuNotice.say("Couldn't start Moonlight", Tone::Problem);
        return;
    }
    joinHost = host;
    joinPid = pid;
    joinStartMs = SDL_GetTicks();
    joinStopMs = 0;
    joinL3 = joinR3 = false;
    std::fprintf(stderr, "[join] streaming from %s (pid %d, %d pad(s))\n", host.c_str(), pid,
                 pads);
    // THIS WINDOW STEPS OUT OF THE WAY, as for an emulator of its own.
    SDL_HideWindow(window);
    cab::overlaywin::mark(window, /*takeInput=*/false);
    SDL_ShowWindow(window);
    renderer.setTransparentBackground(true);
    joinClearFrames = 30;
    letGoOfHeld();
}

// MOONLIGHT HAS THE TELEVISION AND THE CONTROLLERS: every press goes to its
// player's virtual controller and nothing here acts on it, except both stick
// clicks, which leave.
bool App::takeJoinEvent(const SDL_Event& e) {
    if (joinPid <= 0 || e.type == SDL_EVENT_QUIT) return false;
    switch (e.type) {
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            const int p = players::playerOf(e.gbutton.which);
            const auto b = static_cast<SDL_GamepadButton>(e.gbutton.button);
            const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK) joinL3 = down;
            if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK) joinR3 = down;
            if (down && joinL3 && joinR3) {
                joinL3 = joinR3 = false;
                std::fprintf(stderr, "[join] both sticks: leaving\n");
                stopJoin();
                break;
            }
            if (p >= 0 && p < cab::vpad::kMaxPlayers) cab::vpad::button(p, b, down);
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const int p = players::playerOf(e.gaxis.which);
            if (p >= 0 && p < cab::vpad::kMaxPlayers)
                cab::vpad::axis(p, static_cast<SDL_GamepadAxis>(e.gaxis.axis), e.gaxis.value);
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
            players::added(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            players::removed(e.gdevice.which);
            break;
        default:
            break;
    }
    return true;
}

// Every frame while Moonlight runs. True when the frame is done.
bool App::watchJoin(float) {
    if (joinPid <= 0) return false;
    // Rumble from the friend's game comes back through the virtual pads.
    cab::vpad::pump(static_cast<int64_t>(SDL_GetTicks()));
    rumble::update(true);
    if (joinClearFrames > 0) {
        --joinClearFrames;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
    }
    int st = 0;
    const pid_t r = ::waitpid(joinPid, &st, WNOHANG);
    if (r == joinPid || r < 0) {
        finishJoin(r == joinPid && WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        return true;
    }
    if (joinStopMs != 0 && SDL_GetTicks() - joinStopMs > kStopGraceMs) {
        ::kill(-joinPid, SIGKILL);
        ::kill(joinPid, SIGKILL);
    }
    SDL_WaitEventTimeout(nullptr, 16);
    return true;
}

void App::stopJoin() {
    if (joinPid <= 0 || joinStopMs != 0) return;
    joinStopMs = SDL_GetTicks();
    cab::vpad::releaseAll();
    // The program itself, below flatpak's two sandbox processes, which ignore
    // the signal (proc.h, findDescendant).
    const int prog = proc::findDescendant(joinPid, "moonlight");
    ::kill(prog > 0 ? prog : joinPid, SIGTERM);
}

void App::finishJoin(int status) {
    const uint64_t ran = SDL_GetTicks() - joinStartMs;
    const bool asked = joinStopMs != 0;
    std::fprintf(stderr, "[join] Moonlight ended (status %d) after %llu s\n", status,
                 static_cast<unsigned long long>(ran / 1000));
    joinPid = -1;
    joinStopMs = 0;
    cab::vpad::close();
    rumble::update(false);
    if (cab::overlaywin::active()) {
        SDL_HideWindow(window);
        cab::overlaywin::unmark(window);
        SDL_ShowWindow(window);
    }
    renderer.setTransparentBackground(false);
    dropPressesFromClosing();
    letGoOfHeld();
    idleWatch.input(clockSeconds());
    // A STREAM THAT NEVER STARTED: most likely the address, or a pairing the
    // friend removed. Forgotten here, so the next try pairs again.
    if (!asked && status != 0 && ran < kFailedStartMs) {
        moonlight::forget(joinHost);
        menuNotice.say("Couldn't join. Check the address and try again", Tone::Problem);
    } else {
        menuNotice.say("Left your friend's game", Tone::Done);
        offerSaveFriend(joinHost);
    }
}

void App::closeJoinAtExit() {
    if (joinPid <= 0) return;
    stopJoin();
    for (int i = 0; i < 60 && ::waitpid(joinPid, nullptr, WNOHANG) == 0; ++i) SDL_Delay(50);
    ::kill(joinPid, SIGKILL);
    ::waitpid(joinPid, nullptr, 0);
    joinPid = -1;
    cab::vpad::close();
}
