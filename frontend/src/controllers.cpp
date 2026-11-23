// The controllers: Add a controller and its Bluetooth window, the
// Controllers panel, the four presses taken before the event switch, and
// the low-battery notice (#65, #293) (#398, restructure batch e).

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "shortcuts.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::padWindowRows(std::vector<std::string>* names, std::vector<std::string>* values) {
    // Not the pads already playing: they are in Controllers. A pad bluez
    // calls connected but the console is not hearing (paired without its
    // keys, see bt::pair) is listed, because pairing it is the fix.
    std::vector<std::string> seated;
    for (const players::Pad& p : players::connected()) seated.push_back(p.address);
    padShown.clear();
    {
        std::lock_guard<std::mutex> lk(padWin.m);
        for (const auto& [addr, d] : padWin.heard) {
            if (std::find(seated.begin(), seated.end(), addr) != seated.end()) continue;
            if (!d.gamepad && d.name.empty()) continue;   // nobody could pick it out
            padShown.push_back(d);
        }
    }
    std::stable_sort(padShown.begin(), padShown.end(),
                     [](const bt::Device& a, const bt::Device& b) {
                         if (a.gamepad != b.gamepad) return a.gamepad;
                         return a.name < b.name;
                     });
    for (const bt::Device& d : padShown) {
        names->push_back(d.name.empty() ? d.address : d.name);
        values->push_back(d.name == padPairingName && !padPairingName.empty()
                              ? "Pairing\xE2\x80\xA6"
                              : "");
    }
}

void App::padWindowRefresh() {
    std::vector<std::string> names, values;
    padWindowRows(&names, &values);
    bool none = false;
    {
        std::lock_guard<std::mutex> lk(padWin.m);
        none = padWin.noBluetooth;
    }
    choiceScreen.replace(names, values, none ? "This console has no Bluetooth" : padDetail);
}

// NOT JOINED HERE (#481, SETTINGS-08): the worker's bluetoothctl takes a
// moment to end, and joining held the window on the screen while it did.
// The next open joins it (by then long finished), or the exit does.
void App::closePadWindow() {
    padWindowOpen = false;
    padWin.stop = true;
    padWin.cut = true;
    padPairingName.clear();
}

void App::openPadWindow() {
    padWin.quit();
    padWin.stop = false;
    padWin.cut = false;
    {
        std::lock_guard<std::mutex> lk(padWin.m);
        padWin.heard.clear();
        padWin.fresh = padWin.noBluetooth = false;
        padWin.pairAddress.clear();
        padWin.pairing = padWin.pairDone = padWin.pairOk = false;
    }
    padWindowBefore.clear();
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
        padWindowBefore.assign(ids, ids + n);
        SDL_free(ids);
    }
    padPairingName.clear();
    padDetail = "Put a controller into pairing mode";
    askChoice("Add a controller", padDetail, {}, 0, [&](int i) {
        if (i < 0 || i >= static_cast<int>(padShown.size())) return;
        if (!padPairingName.empty()) {   // one at a time
            sound::play(sound::Cue::Edge);
            return;
        }
        const bt::Device d = padShown[static_cast<size_t>(i)];
        padPairingName = d.name.empty() ? d.address : d.name;
        choiceScreen.setTitle("Add a controller");
        // The row says "Pairing…"; the line keeps what it said, so
        // nothing is said twice and the window does not change size.
        {
            std::lock_guard<std::mutex> lk(padWin.m);
            padWin.pairAddress = d.address;
        }
        padWin.cut = true;
        std::fprintf(stderr, "[players] pairing %s (%s)\n", d.address.c_str(),
                     padPairingName.c_str());
        padWindowRefresh();
    });
    choiceScreen.setStaysOpen(true);
    choiceScreen.setFixedWidth(kPadWindowWidth);
    choiceScreen.setGrows("Looking for controllers\xE2\x80\xA6");
    padWindowOpen = true;
    padWin.th = std::thread([&pw = padWin]() {
        while (!pw.stop) {
            pw.cut = false;
            // A quit landing between the test above and the line before
            // would have its cut erased and wait out a ten-second round.
            if (pw.stop) break;
            std::string address;
            {
                std::lock_guard<std::mutex> lk(pw.m);
                address = pw.pairAddress;
                pw.pairAddress.clear();
                if (!address.empty()) pw.pairing = true;
            }
            if (!address.empty()) {
                std::string err;
                const bool ok = bt::pair(address, &err, 40, &pw.stop);
                std::lock_guard<std::mutex> lk(pw.m);
                pw.pairing = false;
                pw.pairDone = true;
                pw.pairOk = ok;
                pw.pairError = err;
                pw.fresh = true;
                continue;
            }
            std::vector<bt::Device> got;
            std::string err;
            // TEN SECONDS A ROUND, as first run's scan. Classic Bluetooth
            // takes about that long to sweep once, and four-second rounds
            // missed pads in pairing mode that an eight-second scan run
            // beside them found at once (A9, 2026-09-26).
            auto heard = [&pw](const bt::Device& d) {
                std::lock_guard<std::mutex> lk(pw.m);
                pw.heard[d.address] = d;
                pw.fresh = true;
            };
            if (bt::listen(10, &got, &err, &pw.cut, heard)) {
                std::lock_guard<std::mutex> lk(pw.m);
                for (bt::Device& d : got) pw.heard[d.address] = std::move(d);
                pw.fresh = true;
            } else {
                {
                    std::lock_guard<std::mutex> lk(pw.m);
                    pw.noBluetooth = true;
                    pw.fresh = true;
                }
                for (int k = 0; k < 20 && !pw.stop; ++k) SDL_Delay(100);
            }
        }
    });
}

void App::padsPanelRows(std::vector<std::string>* names, std::vector<std::string>* values) {
    padsShown = players::connected();
    for (const players::Pad& p : padsShown) {
        names->push_back(p.name);
        values->push_back(std::string(p.id == settingsLastPad ? "\xE2\x80\xA2 " : "") +
                          "Player " + std::to_string(p.player + 1));
    }
    names->push_back("Add a controller");
    values->push_back("");
    padsPanelGen = players::generation();
    padsPanelHand = settingsLastPad;
}

void App::openPadsPanelAt(int focus) {
    std::vector<std::string> names, values;
    padsPanelRows(&names, &values);
    const int addAt = static_cast<int>(padsShown.size());
    askChoice("Controllers", "", names, std::clamp(focus, 0, addAt), [&](int i) {
        const int add = static_cast<int>(padsShown.size());
        if (i == add) {
            choiceScreen.close();
            padsPanelSeenOpen = false;
            openPadWindow();
            padWindowFromPanel = true;
            return;
        }
        if (i < 0 || i >= add) return;
        const players::Pad pad = padsShown[static_cast<size_t>(i)];
        const int me = pad.player;
        std::vector<std::string> opts;
        std::vector<int> numbers;
        for (const players::Pad& o : padsShown)
            if (o.player != me) {
                opts.push_back("Make player " + std::to_string(o.player + 1));
                numbers.push_back(o.player);
            }
        const bool canForget = !pad.address.empty();
        if (numbers.empty() && !canForget) {   // a lone wired pad
            sound::play(sound::Cue::Edge);
            return;
        }
        if (canForget) opts.push_back("Forget");
        opts.push_back("Cancel");
        sound::play(sound::Cue::Activate);
        askChoice(pad.name, "Player " + std::to_string(me + 1), opts, 0,
                  [&, me, numbers, canForget, pad, i](int k) {
            if (k >= 0 && k < static_cast<int>(numbers.size())) {
                players::swap(me, numbers[k]);
                rebuildSettingsRows();
                // ON THE SAME PAD, which the swap moved to its new
                // number's row (the list is in player order).
                int at = i;
                const std::vector<players::Pad> now = players::connected();
                for (size_t j = 0; j < now.size(); ++j)
                    if (now[j].id == pad.id) at = static_cast<int>(j);
                openPadsPanelAt(at);
                return;
            }
            if (!canForget || k != static_cast<int>(numbers.size())) {
                openPadsPanelAt(i);
                return;
            }
            // Back from a question goes back to the panel, as its own
            // Cancel does.
            askChoice("Forget " + pad.name + "?", "", {"Forget", "Cancel"}, 1,
                      [&, pad, i](int j) {
                if (j == 0) {
                    std::string err;
                    if (bt::forget(pad.address, &err))
                        std::fprintf(stderr, "[players] forgot %s\n", pad.address.c_str());
                    else {
                        std::fprintf(stderr, "[players] could not forget %s: %s\n",
                                     pad.address.c_str(), err.c_str());
                        menuNotice.say("Couldn't forget " + pad.name, Tone::Problem);
                    }
                    rebuildSettingsRows();
                }
                openPadsPanelAt(i);
            });
            choiceBack = [&, i]() { openPadsPanelAt(i); };
        });
        choiceBack = [&, i]() { openPadsPanelAt(i); };
    });
    choiceScreen.setValues(values);
    choiceScreen.setStaysOpen(true);
    padsPanelSeenOpen = true;
}

bool App::interceptControllers(const SDL_Event& e) {
    // CONTROLS IS LISTENING over the pause menu (controlsflow.cpp): the next
    // press on its pad is the answer, before anything else sees it.
    if (interceptControls(e)) return true;
    // THE NEW PAD SAYING IT WORKS: any button on a pad that was not
    // here when Add a controller opened closes it, and the press goes
    // no further.
    if (padWindowOpen && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        std::find(padWindowBefore.begin(), padWindowBefore.end(), e.gbutton.which) ==
            padWindowBefore.end()) {
        choiceScreen.close();
        closePadWindow();
        for (const players::Pad& p : players::connected())
            if (p.id == e.gbutton.which) {
                std::fprintf(stderr, "[players] %s added as player %d\n",
                             p.name.c_str(), p.player + 1);
                menuNotice.say(p.name + " is player " + std::to_string(p.player + 1),
                               Tone::Done);
            }
        settingsLastPad = e.gbutton.which;
        buildSettings();
        return true;
    }
    // SETTING THE SHORTCUT BUTTON: the row is listening, and the next
    // RAW press on its pad is the answer (shortcuts.h), read before
    // anything else sees it. B cancels; a button the game needs is
    // passed over and listening goes on. Every other press is held
    // back meanwhile, so nothing moves under the row.
    if (shortcutListen) {
        if (here() != Screen::Settings || !SDL_GetGamepadFromID(shortcutListen)) {
            shortcutListen = 0;
            buildSettings();
        } else if (e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                   e.jbutton.which == shortcutListen) {
            const shortcuts::Pick got = shortcuts::pick(
                SDL_GetGamepadFromID(shortcutListen), e.jbutton.button);
            if (got == shortcuts::Pick::Refused) {
                const std::string b = shortcuts::label(SDL_GetGamepadFromID(shortcutListen),
                                                       e.jbutton.button);
                shortcutRefused = b == "L3"   ? "L3 opens the menu with R3"
                                  : b == "R3" ? "R3 opens the menu with L3"
                                              : "Games use " + b;
                sound::play(sound::Cue::Edge);
                buildSettings();
            } else if (got != shortcuts::Pick::Ignore) {
                shortcutListen = 0;
                shortcutRefused.clear();
                shortcutTakenAt = e.jbutton.timestamp;
                sound::play(got == shortcuts::Pick::Taken ? sound::Cue::Activate
                                                          : sound::Cue::Back);
                buildSettings();
            }
            return true;
        } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            shortcutListen = 0;
            sound::play(sound::Cue::Back);
            buildSettings();
            return true;
        } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                   e.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
                   e.type == SDL_EVENT_KEY_DOWN) {
            return true;
        }
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && shortcutTakenAt &&
        e.gbutton.timestamp == shortcutTakenAt) {
        shortcutTakenAt = 0;
        return true;
    }
    // WHICH PAD IS WHICH: the pad last pressed has a dot on its row
    // in the Controllers panel (padsPanelRows). With shortcuts on, the
    // Shortcut button row follows it too.
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && here() == Screen::Settings)
        if (players::playerOf(e.gbutton.which) >= 0) {
            const bool moved = settingsLastPad != e.gbutton.which;
            settingsLastPad = e.gbutton.which;
            if (moved && shortcuts::enabled()) buildSettings();
        }
    return false;
}

// THE LOW-BATTERY NOTICE (#293, MMagTech 2026-10-08): "P2 battery low"
// in the console's one notice, only where the top bar and its
// batteries are not: in a game and on a game's page. A pad that goes
// low on a bar screen shows red there and the notice waits for the
// game. Once per pad as it reaches the last segment and is not
// charging; again only after it has been back above it. Never for a
// stream's pad (its battery is not the pad's), never for a pad set
// aside by a stream (the streamer is not holding it; it says so when
// the stream ends, if still low), and a Wii Remote on its own low
// flag, which is what a Wii warns on.
// Not while an emulator of its own has the screen: nothing is drawn
// then, so it waits for the pause menu or the game's end.
void App::noticeLowBattery(float dt) {
    {
        static float batteryTick = 0.0f;
        batteryTick -= dt;
        const bool canSay = !(standaloneRun.active() && !standalonePaused) &&
                             (playing || standalonePaused || here() == Screen::Detail);
        if (batteryTick <= 0.0f) {
            batteryTick = 0.5f;
            auto warn = [&](const std::string& key, const std::string& label, bool low,
                            bool cleared) {
                if (cleared) {
                    batteryWarned.erase(key);
                    return;
                }
                if (!low || batteryWarned.count(key) || !canSay || menuNotice.alpha() > 0.0f)
                    return;
                batteryWarned.insert(key);
                menuNotice.say(label + " battery low", Tone::Problem);
                std::fprintf(stderr, "[battery] %s battery low\n", label.c_str());
            };
            for (const players::Held& h : players::held()) {
                if (h.stream || h.key.empty()) continue;
                battery::Reading r = battery::kernel(h.dir);
                if (r.segments == 0) r = battery::sdl(h.gp);
                warn(h.key, "P" + std::to_string(h.player + 1), r.low() && !r.charging,
                     r.segments >= 2);
            }
            if (!players::streaming())
                for (const wiiremote::Battery& b : wiiremote::batteries())
                    if (b.byte >= 0)
                        warn("wii:" + b.address, "W" + std::to_string(b.light), b.low, !b.low);
        }
    }
}

// THE CONTROLLERS PANEL FOLLOWS THE PADS while it is open: one
// connecting or going, a swap, and the dot on the pad last pressed.
void App::followPads() {
    if (choiceScreen.isOpen() && choiceScreen.title() == "Controllers" && padsPanelSeenOpen &&
        here() == Screen::Settings &&
        (players::generation() != padsPanelGen || settingsLastPad != padsPanelHand)) {
        std::vector<std::string> names, values;
        padsPanelRows(&names, &values);
        choiceScreen.replace(names, values, "");
    }
}

void App::pumpPadWindow() {
    if (padWindowOpen) {
        // Add a controller: closed by Back, or a round heard or a
        // pairing ended.
        if (!choiceScreen.isOpen() || here() != Screen::Settings) {
            choiceScreen.close();
            closePadWindow();
            if (padWindowFromPanel && here() == Screen::Settings)
                openPadsPanelAt(static_cast<int>(players::connected().size()));
            padWindowFromPanel = false;
        } else {
            bool fresh = false, done = false, ok = false;
            std::string err;
            {
                std::lock_guard<std::mutex> lk(padWin.m);
                fresh = padWin.fresh;
                padWin.fresh = false;
                done = padWin.pairDone;
                padWin.pairDone = false;
                ok = padWin.pairOk;
                err = padWin.pairError;
            }
            if (done) {
                std::fprintf(stderr, "[players] pairing %s: %s\n",
                             padPairingName.c_str(),
                             ok ? "paired" : err.c_str());
                // Said in the title, as MMagTech asked (2026-09-26):
                // "Pairing…" on the row gave way to a line alone.
                choiceScreen.setTitle(ok ? "Paired successfully" : "Add a controller");
                padDetail = ok ? "Press a button on it" : "Couldn't pair";
                padPairingName.clear();
            }
            if (fresh || done) padWindowRefresh();
        }
    }
}
