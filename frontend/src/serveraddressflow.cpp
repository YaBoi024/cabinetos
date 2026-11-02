// Settings > RomM server: a new address checked on a worker, then the
// same server saved or a different one offered Sign out (#60, #61)
// (#398, restructure batch e). Start-up's own flow is serverwait.cpp.

#include "app.h"

#include <cstdio>

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::startServerCheck(const std::string& address) {
    if (serverJob.running.load()) return;
    if (serverJob.th.joinable()) serverJob.th.join();
    serverJob.running = true;
    {
        std::lock_guard<std::mutex> lk(serverJob.m);
        serverJob.address = address;
        serverJob.done = false;
        serverJob.startedAt = SDL_GetTicks();
    }
    serverJob.th = std::thread([&serverJob = serverJob, address]() {
        std::string detail;
        const server::Check r = server::check(address, &detail);
        std::fprintf(stderr, "[server] %s: %s%s%s\n", address.c_str(),
                     r == server::Check::Same        ? "the same server"
                     : r == server::Check::Different ? "a different server"
                     : r == server::Check::NoServer  ? "no server"
                                                     : "could not tell",
                     detail.empty() ? "" : ", ", detail.c_str());
        std::lock_guard<std::mutex> lk(serverJob.m);
        serverJob.result = r;
        serverJob.detail = detail;
        serverJob.done = true;
        serverJob.running = false;
    });
}

void App::askServerAddress(const std::string& typed, const std::string& why) {
    const ui::Keyboard::Config cfg = serverKeyboardConfig(typed);
    keyboard.open(cfg);
    if (!why.empty()) keyboard.sayInField(why, /*problem=*/true, /*keep=*/true);
    keyboardThen = [&, cfg](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        const std::string addr = trimmedAddress(keyboard.value());
        // The same address, or none: nothing to change.
        if (addr.empty() || addr == (rommAddress ? rommAddress : "")) return;
        ui::Keyboard::Config again = cfg;
        again.initial = addr;
        keyboard.open(again);
        keyboard.sayInField("Checking\xE2\x80\xA6", /*problem=*/false, /*keep=*/true);
        keyboard.setBusy(true);
        serverKeyboardFor = addr;
        keyboardThen = [&](ui::KeyboardResult) { serverKeyboardFor.clear(); };
        startServerCheck(addr);
    };
}

void App::takeServerCheck() {
    // An address check finished. Same server: saved, and the app
    // starts again on it. Anything else: said in the field, the
    // address kept behind it to correct.
    bool done = false;
    server::Check r = server::Check::Failed;
    std::string addr;
    {
        // "CHECKING…" STAYS UP 700 MS AT LEAST. A refused port
        // answers in 10 ms and the word flashed for one frame,
        // which reads as a glitch: the account switch's lesson.
        std::lock_guard<std::mutex> lk(serverJob.m);
        if (serverJob.done && SDL_GetTicks() - serverJob.startedAt >= 700) {
            done = true;
            serverJob.done = false;
            r = serverJob.result;
            addr = serverJob.address;
        }
    }
    // B while it checked means leave it alone, whatever it found.
    if (done && keyboard.isOpen() && keyboard.busy() && serverKeyboardFor == addr) {
        serverKeyboardFor.clear();
        std::string err;
        const std::string from = rommAddress ? rommAddress : "";
        if (r == server::Check::Same && server::changeAddress(from, addr, &err)) {
            keyboard.cancel();
            keyboardThen = nullptr;
            startLeaving(Leave::NewAddress, "Connecting to " + addr);
        } else if (r == server::Check::Same) {
            std::fprintf(stderr, "[server] could not save %s: %s\n",
                         addr.c_str(), err.c_str());
            askServerAddress(addr, "Couldn't save it");
        } else {
            askServerAddress(addr,
                             r == server::Check::Different ? "A different server. Sign out first"
                             : r == server::Check::NoServer
                                 ? "Can't connect"
                                 : "Couldn't check it");
        }
    }
}

void App::pressServer() {
    // PIN FIRST, then what to do: the Wi-Fi row's shape.
    // docs/SETTINGS.md, Network; issues #60 and #61.
    askPin("Enter the PIN", "To change the RomM server", [&]() {
        const std::string addr = rommAddress ? rommAddress : "";
        askChoice(addr, "", {"Change address", "Sign out", "Cancel"}, 0,
                  [&, addr](int i) {
            if (i == 0) {
                // Root's address outranks ours (serveraddress.h).
                // Only a console set up by hand has one.
                if (!server::addressIsOurs()) {
                    menuNotice.say("Set in /etc/cabinetos/session.env",
                                   Tone::Problem);
                    return;
                }
                askServerAddress(addr, "");
            } else if (i == 1) {
                // Focus on Cancel. Then everyone's unsent
                // saves are sent, and only what could not be
                // is said, not counted (MMagTech, 2026-09-25),
                // as Remove does (#194).
                askChoice("Sign out?",
                          "Removes every game and account from this console.",
                          {"Sign out", "Cancel"}, 1, [&](int k) {
                              if (k == 0) startSignOut();
                          });
            }
        });
    });
    sound::play(sound::Cue::Activate);
}
