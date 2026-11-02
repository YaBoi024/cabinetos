// Wi-Fi and the network: asked off the frame thread, the list, a join
// and its password, and the answers when they land (#398, restructure
// batch e).

#include "app.h"

#include <algorithm>
#include <cstdio>

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::askWifi() {
    if (settingsWifi.running.load()) return;
    if (settingsWifi.th.joinable()) settingsWifi.th.join();
    settingsWifi.running = true;
    settingsWifi.th = std::thread([&settingsWifi = settingsWifi]() {
        std::vector<net::Network> got;
        std::string err;
        if (net::cachedScan(&got, &err)) {
            std::lock_guard<std::mutex> lk(settingsWifi.m);
            settingsWifi.list = got;
            settingsWifi.fresh = true;
        }
        got.clear();
        if (net::scan(&got, &err)) {
            std::lock_guard<std::mutex> lk(settingsWifi.m);
            settingsWifi.list = got;
            settingsWifi.fresh = true;
        }
        settingsWifi.running = false;
    });
}

// CHANGE PASSWORD IS FORGET, THEN JOIN. The reason to change a password is
// that the router's changed, so the old one is worth nothing, and joining
// over a saved profile can leave NetworkManager with two for one network.
void App::startWifiJoin(const std::string& ssid, const std::string& pass,
                        bool forgetFirst) {
    if (wifiJob.running.load()) return;
    if (wifiJob.th.joinable()) wifiJob.th.join();
    wifiJob.running = true;
    {
        std::lock_guard<std::mutex> lk(wifiJob.m);
        wifiJob.ssid = ssid;
        wifiJob.done = false;
    }
    wifiJob.th = std::thread([&wifiJob = wifiJob, ssid, pass, forgetFirst]() {
        std::string err;
        if (forgetFirst) net::forget(ssid, &err);
        err.clear();
        const bool ok = net::join(ssid, pass, false, &err);
        std::fprintf(stderr, "[wifi] join %s: %s\n", ssid.c_str(),
                     ok ? "joined" : err.c_str());
        std::lock_guard<std::mutex> lk(wifiJob.m);
        wifiJob.ok = ok;
        wifiJob.err = err;
        wifiJob.done = true;
        wifiJob.running = false;
    });
}

void App::sortWifi() {
    std::vector<net::Network> seen;
    {
        std::lock_guard<std::mutex> lk(settingsWifi.m);
        seen = settingsWifi.list;
    }
    wifiShown.clear();
    for (const net::Network& n : seen) {
        // 802.1X needs a certificate and an identity, which nobody types
        // with a d-pad, so it is not offered at all.
        if (n.ssid.empty() || n.enterprise) continue;
        auto it = std::find_if(wifiShown.begin(), wifiShown.end(),
                               [&](const net::Network& w) { return w.ssid == n.ssid; });
        if (it == wifiShown.end()) { wifiShown.push_back(n); continue; }
        it->signal = std::max(it->signal, n.signal);
        it->active = it->active || n.active;
        it->known = it->known || n.known;
    }
    std::stable_sort(wifiShown.begin(), wifiShown.end(),
                     [](const net::Network& a, const net::Network& b) {
                         if (a.active != b.active) return a.active;
                         if (a.known != b.known) return a.known;
                         return a.signal > b.signal;
                     });
}

// The list as the panel shows it: names, and "Connected", "Saved" or
// "Joining…" on the right.
void App::wifiPanelRows(std::vector<std::string>* names, std::vector<std::string>* values) {
    std::string joining;
    if (wifiJob.running.load()) {
        std::lock_guard<std::mutex> lk(wifiJob.m);
        joining = wifiJob.ssid;
    }
    for (const net::Network& n : wifiShown) {
        names->push_back(n.ssid);
        std::string v = n.active ? "Connected" : (n.known ? "Saved" : "");
        if (n.ssid == joining) v = "Joining\xE2\x80\xA6";
        values->push_back(v);
    }
}

void App::askNetwork() {
    if (settingsNet.th.joinable()) settingsNet.th.join();
    settingsNet.th = std::thread([&settingsNet = settingsNet]() {
        net::Status st = net::status();
        std::lock_guard<std::mutex> lk(settingsNet.m);
        settingsNet.st = st;
        settingsNet.have = true;
        settingsNet.fresh = true;
    });
}

void App::askWifiPassword(const std::string& ssid, bool forgetFirst, const std::string& why) {
    ui::Keyboard::Config cfg;
    cfg.title = ssid;
    cfg.placeholder = "Password";
    cfg.conceal = true;   // masked, last character shown; keyboard.h
    cfg.required = true;  // only a secured network asks, so empty is never it
    // Opened over itself on a retry, in the same frame: no close, no cut.
    // WHY GOES IN THE FIELD ("Wrong password", shaken), not in a line
    // under the title that made the panel grow. Keyboard::sayInField.
    keyboard.open(cfg);
    if (!why.empty()) keyboard.sayInField(why, /*problem=*/true);
    keyboardThen = [&, ssid, forgetFirst, cfg](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        const std::string pass = keyboard.value();
        // THE KEYBOARD STAYS UP WHILE IT JOINS, saying so, and takes no
        // typing (Keyboard::setBusy). The job's answer closes it or turns
        // it back into a retry; B leaves and lets the join finish alone.
        keyboard.open(cfg);
        keyboard.sayInField("Joining\xE2\x80\xA6", /*problem=*/false);
        keyboard.setBusy(true);
        wifiKeyboardFor = ssid;
        keyboardThen = [&](ui::KeyboardResult) { wifiKeyboardFor.clear(); };
        startWifiJoin(ssid, pass, forgetFirst);
        buildSettings();
    };
}

// The network's answer, when it lands. Rebuilt only if Settings is
// still what is on screen; the next visit asks again anyway.
void App::takeNetworkAnswer() {
    bool fresh = false;
    {
        std::lock_guard<std::mutex> lk(settingsNet.m);
        fresh = settingsNet.fresh;
        settingsNet.fresh = false;
    }
    if (fresh && here() == Screen::Settings) buildSettings();
}

void App::refreshWifiList() {
    bool fresh = false;
    {
        std::lock_guard<std::mutex> lk(settingsWifi.m);
        fresh = settingsWifi.fresh;
        settingsWifi.fresh = false;
    }
    if (!choiceScreen.isOpen()) wifiPanelOpen = false;
    lockPinOffSettings();
    closeDownloadsOffSettings();
    if (fresh && wifiPanelOpen) {
        // The list is open: refresh it in place, focus kept.
        sortWifi();
        std::vector<std::string> names, values;
        wifiPanelRows(&names, &values);
        choiceScreen.replace(names, values,
                             names.empty() ? (settingsWifi.running.load()
                                                  ? "Looking for networks\xE2\x80\xA6"
                                                  : "No networks found")
                                           : "");
    }
    if (fresh && here() == Screen::Settings) buildSettings();
}

void App::takeWifiJoin() {
    // A join finished: say how it went, then ask again, so the
    // rows and the Status line say what is true now.
    bool done = false, ok = false;
    std::string ssid, err;
    {
        std::lock_guard<std::mutex> lk(wifiJob.m);
        if (wifiJob.done) {
            done = true;
            wifiJob.done = false;
            ok = wifiJob.ok;
            ssid = wifiJob.ssid;
            err = wifiJob.err;
        }
    }
    if (done) {
        // "Secrets were required" is nmcli's wrong password. The same three
        // words as first run's (setup.cpp), so one nmcli message never gets
        // two answers (#485, SETTINGS-17).
        const bool badPass = err.find("ecrets") != std::string::npos ||
                             err.find("password") != std::string::npos ||
                             err.find("psk") != std::string::npos;
        // Is the keyboard still up, waiting on this very join?
        const bool waiting = keyboard.isOpen() && keyboard.busy() &&
                             wifiKeyboardFor == ssid;
        if (waiting && badPass) {
            // ANOTHER GO, IN PLACE: the same panel, emptied, saying
            // why. MMagTech tested a wrong password on the TV:
            // "kicked out and hard to re-enter", then a strobe.
            askWifiPassword(ssid, false, "Wrong password");
        } else {
            if (waiting) {
                keyboard.cancel();
                keyboardThen = nullptr;
            }
            if (ok) menuNotice.say("Connected to " + ssid, Tone::Done);
            else if (badPass) menuNotice.say("Wrong password", Tone::Problem);
            else menuNotice.say("Couldn't join " + ssid, Tone::Problem);
        }
        wifiKeyboardFor.clear();
        askNetwork();
        askWifi();
        if (here() == Screen::Settings) buildSettings();
    }
}

void App::pressWifi() {
    // PIN FIRST, then either the radio on or the list of
    // networks in the question panel, scrolling past six.
    askPin("Enter the PIN", "To change Wi-Fi", [&]() {
        bool radioOn = true;
        {
            std::lock_guard<std::mutex> lk(settingsNet.m);
            if (settingsNet.have) radioOn = settingsNet.st.wifiEnabled;
        }
        if (!radioOn) {
            std::string err;
            if (!net::setRadio(true, &err))
                menuNotice.say("Couldn't turn on Wi-Fi", Tone::Problem);
            askNetwork();
            askWifi();
            buildSettings();
            return;
        }
        // A password, typed on the on-screen keyboard, then the
        // join on a worker. `forgetFirst` is Change password.
        auto typeAndJoin = [&](const net::Network& net, bool forgetFirst) {
            askWifiPassword(net.ssid, forgetFirst, "");
        };
        auto join = [&, typeAndJoin](const net::Network& net) {
            if (net.secured && !net.known) { typeAndJoin(net, false); return; }
            menuNotice.say("Joining " + net.ssid, Tone::Busy);
            startWifiJoin(net.ssid, "", false);
            buildSettings();
        };
        auto forget = [&](const net::Network& net) {
            askChoice("Forget " + net.ssid + "?", "", {"Forget", "Cancel"}, 1,
                      [&, net](int i) {
                          if (i != 0) return;
                          std::string err;
                          if (net::forget(net.ssid, &err)) {
                              menuNotice.say("Forgot " + net.ssid, Tone::Done);
                              // AT ONCE, not when the next scan
                              // lands: joining a stale "saved"
                              // network sent no password.
                              std::lock_guard<std::mutex> lk(settingsWifi.m);
                              for (net::Network& w : settingsWifi.list)
                                  if (w.ssid == net.ssid)
                                      w.known = w.active = false;
                          }
                          else
                              menuNotice.say("Couldn't forget " + net.ssid,
                                             Tone::Problem);
                          std::fprintf(stderr, "[wifi] forget %s: %s\n",
                                       net.ssid.c_str(),
                                       err.empty() ? "done" : err.c_str());
                          askNetwork();
                          askWifi();
                          buildSettings();
                      });
        };
        sortWifi();
        std::vector<std::string> names, values;
        wifiPanelRows(&names, &values);
        askChoice("Wi-Fi",
                  names.empty() ? "Looking for networks\xE2\x80\xA6" : "",
                  names, 0, [&, typeAndJoin, join, forget](int i) {
                      wifiPanelOpen = false;
                      if (i < 0 || i >= static_cast<int>(wifiShown.size()))
                          return;
                      const net::Network n = wifiShown[i];
                      if (wifiJob.running.load()) {
                          sound::play(sound::Cue::Edge);
                          return;
                      }
                      if (n.active) {
                          askChoice(n.ssid, "",
                                    {"Change password", "Forget", "Cancel"}, 0,
                                    [&, n, typeAndJoin, forget](int k) {
                                        if (k == 0) typeAndJoin(n, true);
                                        else if (k == 1) forget(n);
                                    });
                      } else if (n.known) {
                          askChoice(n.ssid, "",
                                    {"Join", "Change password", "Forget",
                                     "Cancel"},
                                    0, [&, n, typeAndJoin, join, forget](int k) {
                                        if (k == 0) join(n);
                                        else if (k == 1) typeAndJoin(n, true);
                                        else if (k == 2) forget(n);
                                    });
                      } else {
                          join(n);
                      }
                  });
        choiceScreen.setValues(values);
        wifiPanelOpen = true;
        askWifi();
    });
    sound::play(sound::Cue::Activate);
}
