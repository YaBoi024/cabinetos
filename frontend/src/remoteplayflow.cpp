// Remote Play: on, off, its paired devices, the pairing on the PIN pad, and
// systemd's answers (#286) (#398, restructure batch e).

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "prefs.h"
#include "unit.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// Remote Play on or off, as Developer access: a refusal said in the row,
// otherwise "Turning on…" until systemd answers.
void App::setRemotePlay(bool on) {
    std::string why;
    const bool ok = on ? unit::start(kRpUnit, &why) : unit::stop(kRpUnit, &why);
    if (!ok) {
        rpFailed = why.find("not found") != std::string::npos ? "Not in this image" : why;
        if (rpFailed.size() > 60) rpFailed.resize(60);
        rpSaidAt = 0;
    } else {
        rpFailed.clear();
        rpSaidAt = std::time(nullptr);
    }
    rpWant = on;
    prefs::set("remote_play", on ? "on" : "off");
    remoteplay::watch(on, /*fresh=*/true);
    if (!on) rpAsked.clear();
    // TAILSCALE GOES WITH IT: never running while Streaming is off, and
    // back with Streaming when it was on.
    if (ok && tsWant) {
        std::string tsWhy;
        if (!(on ? unit::start(kTsUnit, &tsWhy) : unit::stop(kTsUnit, &tsWhy)))
            std::fprintf(stderr, "[tailscale] %s with Streaming: %s\n",
                         on ? "could not start" : "could not stop", tsWhy.c_str());
    }
    buildSettings();
}

void App::askDeviceName(const std::string& id, const std::string& current, bool justPaired) {
    ui::Keyboard::Config cfg;
    cfg.title = justPaired ? "Paired. Name this device" : "Rename " + current;
    cfg.initial = current;
    cfg.placeholder = "Name";
    keyboard.open(cfg);
    keyboardThen = [&, id, current, justPaired](ui::KeyboardResult r) {
        std::string name = r == ui::KeyboardResult::Committed ? keyboard.value() : "";
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name.empty()) name = current;
        remoteplay::later([id, name]() { remoteplay::rename(id, name); return true; },
                          [&](bool) { buildSettings(); });
        if (justPaired) menuNotice.say("Paired with " + name, Tone::Done);
    };
}

void App::openPairedPanel(int focus) {
    rpRowsShown = remoteplay::paired();
    if (rpRowsShown.empty()) {
        choiceScreen.close();
        buildSettings();
        return;
    }
    std::vector<std::string> names;
    for (const remoteplay::Device& d : rpRowsShown) names.push_back(d.name);
    const int last = static_cast<int>(names.size()) - 1;
    askChoice("Paired devices", "", names, std::clamp(focus, 0, last), [&](int i) {
        if (i < 0 || i >= static_cast<int>(rpRowsShown.size())) return;
        const remoteplay::Device d = rpRowsShown[static_cast<size_t>(i)];
        askChoice(d.name, "", {"Rename", "Remove", "Cancel"}, 0, [&, d, i](int c) {
            if (c == 0) {
                choiceScreen.close();
                askDeviceName(d.id, d.name, false);
                return;
            }
            if (c != 1) { openPairedPanel(i); return; }
            askChoice("Remove " + d.name + "?", "", {"Remove", "Cancel"}, 1, [&, d, i](int k) {
                if (k != 0) { openPairedPanel(i); return; }
                askPin("Enter the PIN", "To remove " + d.name, [&, d, i]() {
                    remoteplay::later(
                        [id = d.id]() {
                            std::string why;
                            return remoteplay::remove(id, &why);
                        },
                        [&, d, i](bool ok) {
                            if (!ok) menuNotice.say("Couldn't remove " + d.name, Tone::Problem);
                            buildSettings();
                            // Back to the list, unless the person has moved on
                            // while Sunshine answered.
                            if (here() == Screen::Settings && !choiceScreen.isOpen() &&
                                !pinScreen.isOpen() && !keyboard.isOpen())
                                openPairedPanel(i);
                        });
                });
            });
            choiceBack = [&, i]() { openPairedPanel(i); };
        });
        // B goes back to the list, as Cancel does.
        choiceBack = [&, i]() { openPairedPanel(i); };
    });
    choiceScreen.setStaysOpen(true);
}

void App::startRemotePlayAtBoot() {
    rpState = unit::state(kRpUnit);
    rpWant = prefs::get("remote_play", "off") == "on";
    if (rpWant && rpState != "active") {
        std::string why;
        if (unit::start(kRpUnit, &why)) rpSaidAt = std::time(nullptr);
        else std::fprintf(stderr, "[remoteplay] could not turn on at start: %s\n", why.c_str());
    }
    remoteplay::watch(rpWant);
}

void App::pollRemotePlay() {
    if (rpSaidAt != 0 || rpWant || here() == Screen::Settings) {
        const int64_t now = std::time(nullptr);
        // Remote Play: systemd's answer, as Developer access's. ONLY WHILE
        // SOMETHING READS IT (#481, SETTINGS-12): Settings shows it, and a
        // switch just pressed waits for it. With Streaming on it was asked
        // every half second on every screen and in games, over a new
        // system-bus connection each time; rpState is Settings' alone.
        if (rpSaidAt != 0 || here() == Screen::Settings) {
            const std::string d = unit::state(kRpUnit);
            const bool settled = d == "active" || d == "inactive" || d == "failed";
            const bool answered = rpSaidAt == 0 || now - rpSaidAt > 10 ||
                                  (settled && (d == "failed" || (d == "active") == rpWant));
            if (answered && (d != rpState || rpSaidAt != 0)) {
                if (rpSaidAt != 0 && rpWant && d != "active") rpFailed = "Couldn't start";
                rpState = d;
                rpSaidAt = 0;
                std::fprintf(stderr, "[remoteplay] %s\n", d.c_str());
                if (here() == Screen::Settings) buildSettings();
            }
        }
        if (here() == Screen::Settings && remoteplay::generation() != rpSeen) {
            buildSettings();
            if (choiceScreen.isOpen() && choiceScreen.title() == "Paired devices")
                openPairedPanel(0);
        }
        // A DEVICE ASKING TO PAIR brings up the PIN pad, wherever the
        // console is except in a game, over nothing else that is
        // open. The request lasts five minutes on Sunshine's side; one
        // put to the TV and turned down is not put again.
        const std::vector<remoteplay::Request> asking = remoteplay::waiting();
        auto stillAsking = [&](const std::string& id) {
            for (const remoteplay::Request& r : asking)
                if (r.id == id) return true;
            return false;
        };
        if (!rpAsking.empty() && !stillAsking(rpAsking)) {
            // Moonlight gave up, or the digits went through.
            if (pinScreen.isOpen() && pinCode) {
                pinCode = nullptr;
                pinCodeCancel = nullptr;
                pinScreen.close();
            }
            rpAsking.clear();
        }
        if (rpAsking.empty() && !playing && !standaloneRun.active() &&
            !pinScreen.isOpen() && !choiceScreen.isOpen() && !keyboard.isOpen()) {
            for (const remoteplay::Request& r : asking) {
                if (rpAsked.count(r.id)) continue;
                rpAsked.insert(r.id);
                rpAsking = r.id;
                const std::string id = r.id;
                std::fprintf(stderr, "[remoteplay] a device asks to pair\n");
                pinCode = [&, id](const std::string& digits) {
                    const std::string name = remoteplay::nextName();
                    std::set<std::string> before;
                    for (const remoteplay::Device& dv : remoteplay::paired())
                        before.insert(dv.id);
                    remoteplay::later(
                        [id, digits, name]() {
                            std::string why;
                            return remoteplay::pair(id, digits, name, &why);
                        },
                        [&, name, before](bool ok) {
                            if (ok)
                                rpPairing = {name, std::time(nullptr), before};
                            else
                                menuNotice.say("Couldn't pair", Tone::Problem);
                        });
                };
                pinCodeCancel = [id]() {
                    remoteplay::later([id]() { return remoteplay::decline(id); }, nullptr);
                };
                pinScreen.open(screens::PinScreen::Mode::Check, "Pair a device",
                               "Enter the code from Moonlight");
                sound::play(sound::Cue::Activate);
                break;
            }
        }
        // THE ANSWER IS THE DEVICE'S: a wrong code is only known to
        // Moonlight. Paired once it is in the list; not, if it is
        // still missing when the request has gone.
        if (rpPairing.at != 0) {
            std::string fresh;
            for (const remoteplay::Device& dv : remoteplay::paired())
                if (!rpPairing.before.count(dv.id)) fresh = dv.id;
            if (!fresh.empty() && !keyboard.isOpen() && !pinScreen.isOpen()) {
                // A DEVICE PAIRING AGAIN (its certificate already
                // paired) replaces its old entry and keeps its name:
                // two entries with one certificate lock it out
                // (remoteplay.h, samePairedDevice).
                std::string kept;
                const std::vector<std::string> olds = remoteplay::samePairedDevice(fresh);
                for (const std::string& old : olds)
                    for (const remoteplay::Device& dv : remoteplay::paired())
                        if (dv.id == old) kept = dv.name;
                remoteplay::later(
                    [olds, fresh, kept]() {
                        for (const std::string& old : olds) {
                            std::string why;
                            remoteplay::remove(old, &why);
                        }
                        if (!kept.empty()) remoteplay::rename(fresh, kept);
                        return true;
                    },
                    [&](bool) { buildSettings(); });
                if (!kept.empty()) {
                    menuNotice.say("Paired with " + kept, Tone::Done);
                } else {
                    // PAIRED: name it, with the next free "Device N"
                    // in the field, so Done alone is enough.
                    askDeviceName(fresh, rpPairing.name, true);
                }
                rpPairing = {};
            } else if (fresh.empty() && now - rpPairing.at > 10) {
                menuNotice.say("Couldn't pair", Tone::Problem);
                rpPairing = {};
            }
        }
    }
}

void App::pressPairedDevices() {
    if (players::streaming()) {
        sound::play(sound::Cue::Edge);
    } else {
        openPairedPanel(0);
        sound::play(sound::Cue::Activate);
    }
}

void App::pressRemotePlay() {
    // On behind the PIN when one is set; off at once, since
    // off only closes things. Never during a stream (the
    // row is greyed; this is for a press before it was).
    if (rpSaidAt != 0 || players::streaming()) {
        sound::play(sound::Cue::Edge);
    } else if (rpState == "active") {
        setRemotePlay(false);
        sound::play(sound::Cue::Activate);
    } else {
        askPin("Enter the PIN", "To turn on Remote Play",
               [&]() { setRemotePlay(true); });
        sound::play(sound::Cue::Activate);
    }
}
