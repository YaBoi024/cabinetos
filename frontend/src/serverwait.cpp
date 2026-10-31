// The screen a console shows while its server does not answer at start
// (#60, #88): the wait, a new address, Sign out, and going offline after
// fifteen seconds when a game on the drive can be played (#398,
// restructure batch d). Its PIN and its address check follow Settings'
// rules (pinflow.cpp, serveraddressflow.cpp; #485); Sign out is offered
// here because Settings is out of reach (startup.cpp, above the call).

#include "app.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cache.h"
#include "catalog.h"
#include "firstrun.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

std::optional<int> App::waitForServer(char** argv) {
    std::string err;
    constexpr double kOfferAfter = 10.0;
    // OFFLINE AFTER FIFTEEN SECONDS (#88, MMagTech 2026-10-05), when
    // there is a game on the drive to play: the screen lifts onto
    // Home with the games on the drive, and the server is tried in
    // the background from then on. With none, this waits as before.
    constexpr double kOfflineAfter = 15.0;
    // THE TRIES ARE ON A CLIENT OF THEIR OWN, so going offline can
    // walk away from one still in flight (a server that does not
    // answer at all holds a try for its whole connect timeout) and
    // leave it to finish alone. The live client is set on this thread
    // once a try has answered.
    struct Probe {
        std::atomic<bool> stop{false}, answered{false};
        std::mutex m;
        std::string err;
    };
    auto probe = std::make_shared<Probe>();
    const std::string firstAddress = rommAddress;
    std::thread probeThread([probe, firstAddress]() {
        romm::Client scratch;
        std::string e;
        while (!probe->stop.load()) {
            if (scratch.setAddress(firstAddress, &e)) { probe->answered = true; return; }
            {
                std::lock_guard<std::mutex> lk(probe->m);
                probe->err = e;
            }
            for (int i = 0; i < 20 && !probe->stop.load(); ++i) SDL_Delay(100);
        }
    });
    bool goOffline = false;
    double driveCheckedAt = -1.0;
    // ONE CHECK AT A TIME, AS IN SETTINGS (#485, FIRSTRUN-09): B closes the
    // keyboard while one runs, and a second commit joined it on this thread,
    // freezing the screen for as long as the address took to fail. Shared
    // with its thread, so a check still running when the loop ends is left
    // to finish alone, not waited for on the last frame.
    struct CheckJob {
        std::thread th;
        std::atomic<bool> running{false}, done{false};
        server::Check result = server::Check::Failed;
        std::string address;
        Uint64 at = 0;
    };
    auto check = std::make_shared<CheckJob>();
    struct CheckEnd {
        std::shared_ptr<CheckJob> job;
        ~CheckEnd() {
            if (!job->th.joinable()) return;
            if (job->running.load()) job->th.detach();
            else job->th.join();
        }
    } checkEnd{check};
    // Root's address outranks ours (serveraddress.h): a console set up by
    // hand cannot change it here, as in Settings (#485, FIRSTRUN-10).
    const bool addressOurs = server::addressIsOurs();
    const std::string setElsewhere = "Set in " + firstrun::serverAddressSource();

    // The pads, which main opens only later: this screen needs them.
    if (SDL_JoystickID* ids = SDL_GetGamepads(nullptr)) {
        for (int i = 0; ids[i]; ++i) players::added(ids[i]);
        SDL_free(ids);
    }

    ui::Keyboard addrKeyboard;
    screens::ChoiceScreen question;
    screens::PinScreen pad;
    int pinFails = 0;
    std::string newAddress;
    bool signOutNow = false, quit = false, said = false, swallowText = false;
    const double blankAfter = kScreenOff[savedScreenOff()].seconds;
    const Uint64 start = SDL_GetTicks();
    Uint64 prevNs = SDL_GetTicksNS();

    auto openKeyboard = [&](const std::string& typed, const std::string& why) {
        addrKeyboard.open(serverKeyboardConfig(typed));
        if (!why.empty()) addrKeyboard.sayInField(why, /*problem=*/true, /*keep=*/true);
    };
    auto keyboardResult = [&](ui::KeyboardResult r) {
        if (r == ui::KeyboardResult::Cancelled) sound::play(sound::Cue::Back);   // #389
        if (r != ui::KeyboardResult::Committed) return;
        if (check->running.load()) {
            sound::play(sound::Cue::Edge);
            return;
        }
        sound::play(sound::Cue::Activate);
        const std::string addr = trimmedAddress(addrKeyboard.value());
        if (addr.empty()) return;
        addrKeyboard.open(serverKeyboardConfig(addr));
        addrKeyboard.sayInField("Checking\xE2\x80\xA6", /*problem=*/false, /*keep=*/true);
        addrKeyboard.setBusy(true);
        if (check->th.joinable()) check->th.join();   // finished: running is false
        check->done = false;
        check->running = true;
        check->address = addr;
        check->at = SDL_GetTicks();
        check->th = std::thread([job = check, addr]() {
            std::string detail;
            job->result = server::check(addr, &detail);
            std::fprintf(stderr, "[server] %s at startup: %d%s%s\n", addr.c_str(),
                         static_cast<int>(job->result), detail.empty() ? "" : ", ",
                         detail.c_str());
            job->done = true;
            job->running = false;
        });
    };
    // What the pad's answer means, from a press or from a typed digit.
    auto pinOutcome = [&](screens::PinScreen::Outcome o) {
        if (o == screens::PinScreen::Outcome::Cancelled) pad.close();
        if (o != screens::PinScreen::Outcome::Entered) return;
        // SETTINGS' RULE (pinflow.cpp; #485, FIRSTRUN-08): the edge sound on
        // a wrong PIN, the count reset by a right one, then the activate sound.
        if (!accounts::checkPin(pad.pin())) {
            ++pinFails;
            sound::play(sound::Cue::Edge);
            pad.reject("Wrong PIN");
            if (pinFails >= kPinTries) {
                pinFails = 0;
                pad.lockFor(kPinLockSeconds);
            }
            return;
        }
        pinFails = 0;
        pad.close();
        sound::play(sound::Cue::Activate);
        openKeyboard(rommAddress, "");
    };
    auto press = [&](screens::Nav n) {
        if (pad.isOpen()) {
            pinOutcome(pad.key(n));
            return;
        }
        if (question.isOpen()) {
            const auto o = question.key(n);
            if (o == screens::ChoiceScreen::Outcome::None) return;
            question.close();
            if (o == screens::ChoiceScreen::Outcome::Chosen && question.chosen() == 0)
                signOutNow = true;
            return;
        }
        if (addrKeyboard.isOpen()) {
            switch (n) {
                case screens::Nav::Left: addrKeyboard.moveFocus(-1, 0); break;
                case screens::Nav::Right: addrKeyboard.moveFocus(+1, 0); break;
                case screens::Nav::Up: addrKeyboard.moveFocus(0, -1); break;
                case screens::Nav::Down: addrKeyboard.moveFocus(0, +1); break;
                case screens::Nav::Activate: keyboardResult(addrKeyboard.pressKey()); break;
                case screens::Nav::Back: keyboardResult(addrKeyboard.cancel()); break;
            }
            return;
        }
        if (n != screens::Nav::Activate) return;
        if ((SDL_GetTicks() - start) / 1000.0 < kOfferAfter) return;
        // Not over a check still running, nor for an address set by hand.
        if (check->running.load() || !addressOurs) {
            sound::play(sound::Cue::Edge);
            return;
        }
        sound::play(sound::Cue::Activate);   // #389
        if (accounts::pinIsSet())
            pad.open(screens::PinScreen::Mode::Check, "Enter the PIN",
                     "To change the RomM server");
        else
            openKeyboard(rommAddress, "");
    };

    while (!probe->answered.load() && newAddress.empty() && !signOutNow && !quit &&
           !goOffline) {
        const Uint64 now = SDL_GetTicks();
        // Not while somebody is typing a new address or a PIN: the
        // screen does not leave from under them.
        {
            const double waited = (now - start) / 1000.0;
            if (waited >= kOfflineAfter && waited - driveCheckedAt >= 5.0 &&
                !addrKeyboard.isOpen() && !pad.isOpen() && !question.isOpen()) {
                driveCheckedAt = waited;
                for (const cache::OnDrive& d : cache::onDrive()) {
                    romm::Game g;
                    if (!d.record.empty() && romm::gameFromJson(d.record, &g) &&
                        catalog::playable(g)) {
                        goOffline = true;
                        break;
                    }
                }
            }
        }
        // Once, not once per attempt: a line a second for a minute and
        // a half buries whatever else the boot had to say.
        if (!said && now - start > 2500) {
            std::lock_guard<std::mutex> lk(probe->m);
            if (!probe->err.empty()) {
                said = true;
                std::fprintf(stderr, "[romm] %s, waiting for it\n", probe->err.c_str());
            }
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // A PRESS ON A DARK SCREEN ONLY WAKES IT, as on Home; the
            // text a swallowed key would have typed goes with it.
            const bool touched =
                e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                 std::abs(static_cast<int>(e.gaxis.value)) > idle::kAxisDeadzone);
            if (touched) {
                swallowText = false;
                if (idleWatch.input(clockSeconds())) {
                    swallowText = e.type == SDL_EVENT_KEY_DOWN;
                    continue;
                }
            }
            if (e.type == SDL_EVENT_TEXT_INPUT && swallowText) {
                swallowText = false;
                continue;
            }
            switch (e.type) {
                case SDL_EVENT_QUIT: quit = true; break;
                case SDL_EVENT_GAMEPAD_ADDED: players::added(e.gdevice.which); break;
                case SDL_EVENT_GAMEPAD_REMOVED: players::removed(e.gdevice.which); break;
                case SDL_EVENT_TEXT_INPUT:
                    if (pad.isOpen()) {
                        for (const char* c = e.text.text; *c && pad.isOpen(); ++c)
                            pinOutcome(pad.typeDigit(*c));
                    } else if (addrKeyboard.isOpen()) {
                        addrKeyboard.typeText(e.text.text);
                    }
                    break;
                case SDL_EVENT_KEY_DOWN:
                    // THE SYSTEM'S KEY REPEAT, dropped as first run and the
                    // console's own input drop it (#443, #485): a held Return
                    // committed again and again. Backspace still repeats: here
                    // it only ever deletes.
                    if (e.key.repeat && (e.key.key == SDLK_RETURN ||
                                         e.key.key == SDLK_KP_ENTER ||
                                         e.key.key == SDLK_ESCAPE))
                        break;
                    switch (e.key.key) {
                        case SDLK_UP: press(screens::Nav::Up); break;
                        case SDLK_DOWN: press(screens::Nav::Down); break;
                        case SDLK_LEFT: press(screens::Nav::Left); break;
                        case SDLK_RIGHT: press(screens::Nav::Right); break;
                        case SDLK_ESCAPE: press(screens::Nav::Back); break;
                        case SDLK_BACKSPACE:
                            if (pad.isOpen()) pad.deleteDigit();
                            else if (addrKeyboard.isOpen()) addrKeyboard.backspace();
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            if (addrKeyboard.isOpen() && !pad.isOpen() && !question.isOpen())
                                keyboardResult(addrKeyboard.commit());
                            else
                                press(screens::Nav::Activate);
                            break;
                        default: break;
                    }
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_UP: press(screens::Nav::Up); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: press(screens::Nav::Down); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: press(screens::Nav::Left); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: press(screens::Nav::Right); break;
                        case SDL_GAMEPAD_BUTTON_SOUTH: press(screens::Nav::Activate); break;
                        case SDL_GAMEPAD_BUTTON_EAST: press(screens::Nav::Back); break;
                        case SDL_GAMEPAD_BUTTON_WEST:
                            if (pad.isOpen()) pad.deleteDigit();
                            else if (addrKeyboard.isOpen()) addrKeyboard.backspace();
                            break;
                        case SDL_GAMEPAD_BUTTON_NORTH:
                            if (addrKeyboard.isOpen()) addrKeyboard.toggleShift();
                            break;
                        case SDL_GAMEPAD_BUTTON_START:
                            if (addrKeyboard.isOpen() && !pad.isOpen() && !question.isOpen())
                                keyboardResult(addrKeyboard.commit());
                            break;
                        default: break;
                    }
                    break;
                default: break;
            }
        }

        // An address check finished ("Checking…" up 700 ms at least,
        // as in Settings). B while it ran means leave it alone.
        if (check->done.load() && SDL_GetTicks() - check->at >= 700) {
            check->done = false;
            if (addrKeyboard.isOpen() && addrKeyboard.busy()) {
                const std::string addr = check->address;
                switch (check->result) {
                    // SAVED HERE, AS IN SETTINGS (#485, FIRSTRUN-10): one that
                    // cannot be saved says so in the field, rather than being
                    // used for this start only and forgotten at the next.
                    case server::Check::Same: {
                        std::string cerr;
                        if (server::changeAddress(rommAddress, addr, &cerr)) {
                            newAddress = addr;
                        } else {
                            std::fprintf(stderr, "[server] could not save %s: %s\n",
                                         addr.c_str(), cerr.c_str());
                            openKeyboard(addr, "Couldn't save it");
                        }
                        break;
                    }
                    case server::Check::Different: {
                        addrKeyboard.cancel();
                        const bool unsent = server::unsentSaves() > 0;
                        question.open("A different server",
                                      std::string("Removes every game and account from "
                                                  "this console.\n") +
                                          (unsent ? "Saves waiting to upload will be lost."
                                                  : "Saves stay on the server."),
                                      {"Sign out", "Cancel"}, 1);
                        break;
                    }
                    case server::Check::NoServer:
                        openKeyboard(addr, "No RomM server there");
                        break;
                    case server::Check::Failed:
                        openKeyboard(addr, "Couldn't check it");
                        break;
                }
            }
        }

        const Uint64 ns = SDL_GetTicksNS();
        const float dt = static_cast<float>(ns - prevNs) / 1e9f;
        prevNs = ns;
        question.tick(dt);
        pad.tick(dt);
        idleFrame(dt, /*playingNow=*/false, blankAfter);

        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        if (dw > 0 && dh > 0) {
            renderer.beginFrame(dw, dh);
            const double waited = (SDL_GetTicks() - start) / 1000.0;
            // A NUMBER THAT CHANGES. One unchanging sentence is what a
            // hung console looks like.
            const int secs = static_cast<int>(waited);
            char line[96];
            if (secs < 60)
                std::snprintf(line, sizeof line, "Waiting for the server, %ds", secs);
            else
                std::snprintf(line, sizeof line, "Waiting for the server, %dm %ds",
                              secs / 60, secs % 60);
            setup::drawStartup(renderer, text,
                               newAddress.empty() ? line : "Connecting", 1.0f,
                               waited < kOfferAfter ? nullptr
                               : addressOurs        ? "Press (A) to change the server address"
                                                    : setElsewhere.c_str());
            renderer.presentScene();
            screens::Ctx ctx{renderer, text, images, renderer.scale()};
            if (addrKeyboard.isOpen()) addrKeyboard.draw(renderer, text, renderer.scale());
            question.draw(ctx);
            pad.draw(ctx);
            if (const float d = dimLayer.value(); d > 0.001f)
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(d)});
            SDL_GL_SwapWindow(window);
        }
        // Dark: ten pictures a second is plenty, as on Home.
        if (idleShown == idle::Level::Blank && dimLayer.value() >= 0.999f) SDL_Delay(100);
    }

    probe->stop = true;
    if (!probe->answered.load() && (!newAddress.empty() || signOutNow)) {
        // Said while the old address's last try runs out.
        setup::showWaiting(waitDeps, signOutNow ? "Signing out"
                                                : ("Connecting to " + newAddress).c_str());
    }
    // Offline does not wait for the last try: it finishes alone.
    if (goOffline && !probe->answered.load()) probeThread.detach();
    else probeThread.join();
    if (probe->answered.load()) {
        // The live client, now that a try has answered: one quick
        // call. Should the server go in between, start offline.
        goOffline = !liveClient.setAddress(rommAddress, &err);
    }
    if (goOffline) {
        romm::setServerAway(true);
        libFromDrive = true;
        std::fprintf(stderr,
                     "[offline] no server after %.0f s; starting with the games on "
                     "the drive\n", kOfflineAfter);
    }
    if (quit) return 0;
    if (signOutNow) {
        std::string serr;
        if (!server::signOut(&serr)) {
            std::fprintf(stderr, "[sign out] could not: %s\n", serr.c_str());
            return 1;
        }
        // The clearing and first run are the next start's, as from
        // Settings. In place, so gamescope stays up.
        std::fprintf(stderr, "[frontend] starting again\n");
        std::fflush(stderr);
        ::close_range(3, ~0U, 0);
        ::execv("/proc/self/exe", argv);
        std::fprintf(stderr, "[frontend] could not start again: %s\n",
                     std::strerror(errno));
        return 1;
    }
    if (!probe->answered.load() && !newAddress.empty()) {
        resolvedAddress = newAddress;
        rommAddress = resolvedAddress.c_str();
        if (!liveClient.setAddress(rommAddress, &err)) {
            std::fprintf(stderr, "[romm] %s at the new address\n", err.c_str());
            return 1;
        }
    }
    if (said && !goOffline) std::fprintf(stderr, "[romm] the server answered\n");
    return std::nullopt;
}
