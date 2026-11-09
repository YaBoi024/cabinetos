// Tailscale, for Remote Play away from home: the row, signing in and out,
// and its answers (#286) (#398, restructure batch e).

#include "app.h"

#include <cstdio>
#include <ctime>

#include "prefs.h"
#include "unit.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

bool App::tsEndsSoon(const tailscale::Status& st) {
    return st.expires != 0 && st.expires - std::time(nullptr) < kTsWarnDays * 86400;
}

screens::SettingsRow App::tailscaleRow(bool streaming, bool inUse) {
    using K = screens::SettingsRow::Kind;
    const tailscale::Status st = tailscale::last();
    tsSeen = tailscale::generation();
    const int64_t ended = std::atoll(prefs::get("tailscale_expires", "0").c_str());
    std::string detail, value;
    auto dayOf = [](int64_t when) {
        char day[32] = "";
        const std::time_t t = static_cast<std::time_t>(when);
        std::tm tm{};
        localtime_r(&t, &tm);
        std::strftime(day, sizeof day, "%d %b %Y", &tm);
        return std::string(day[0] == '0' ? day + 1 : day);
    };
    if (!streaming) {
        value = "Off";
    } else if (tsLoggingOut) {
        value = "Logging out\xE2\x80\xA6";
    } else if (!tsWant) {
        // Signed in but not running: Connect brings it back, no sign-in.
        value = tsSignedIn ? "Disconnected" : "Off";
    } else if (unit::state(kTsUnit) == "failed") {
        value = "Couldn't start";
    } else if (st.state == "Running") {
        detail = st.name;
        value = st.address;
        // THE SIGN-IN ENDS (180 days by default), and the date is shown
        // from the moment it is signed in: somebody turns Remote Play on
        // once and rarely comes back, so that is when to see it, and set
        // a reminder (MMagTech, 2026-10-08). None when the owner turned
        // expiry off in Tailscale.
        if (st.expires != 0)
            detail += std::string(detail.empty() ? "" : " \xC2\xB7 ") + "Sign-in ends " +
                      dayOf(st.expires);
    } else if (st.state == "NeedsLogin" || st.state == "Stopped") {
        // SIGNED OUT, AND WHY when it is the date passing: the one the
        // row showed all along (kept in settings.json, since a console
        // signed out has none to read). Otherwise it was removed some
        // other way, Tailscale's own page, and "ended" would be wrong.
        // The value says what pressing does (MMagTech, 2026-10-08).
        detail = ended != 0 && ended <= std::time(nullptr) ? "Sign-in ended " + dayOf(ended)
                                                           : "Signed out";
        value = "Sign in again";
    } else if (st.state == "NeedsMachineAuth") {
        // The owner's Tailscale approves new devices by hand.
        value = "Waiting for approval";
    } else {
        value = "Turning on\xE2\x80\xA6";
    }
    return screens::SettingsRow{streaming && !inUse ? K::Action : K::Disabled, SetTailscale,
                                "Tailscale", detail, value};
}

// TAILSCALE ON OR OFF: the unit, and the choice remembered. Off keeps the
// sign-in (Disconnected); Log out is what removes it.
void App::setTailscale(bool on) {
    std::string why;
    const bool ok = on ? unit::start(kTsUnit, &why) : unit::stop(kTsUnit, &why);
    if (!ok) std::fprintf(stderr, "[tailscale] could not turn %s: %s\n", on ? "on" : "off",
                          why.c_str());
    tsWant = on;
    prefs::set("tailscale", on ? "on" : "off");
    buildSettings();
}

// SIGNING IN: the link as a QR code, approved on a phone. Leaving the
// screen before it is approved turns Tailscale off again (the frame loop).
void App::openTsSignIn(bool again) {
    if (!tsWant) setTailscale(true);
    tsSigningIn = true;
    tsShownLink = false;
    tsSignInScreen.open();
    stack.push_back(Screen::TailscaleSignIn);
    tailscale::startLogin(again);
}

// LOGGING OUT, on a worker: `tailscale logout` needs tailscaled running,
// so a console that is Disconnected starts it for the moment. The frame
// loop picks up the end and turns it off.
void App::tsLogout() {
    if (tsWorker.joinable()) return;
    tsLoggingOut = true;
    tsWorkerDone = false;
    const bool wasRunning = tsWant;
    tsWorker = std::thread([&, wasRunning]() {
        std::string why;
        if (!wasRunning) {
            unit::start(kTsUnit, &why);
            for (int i = 0; i < 20 && !tailscale::answers(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (!tailscale::logout(&why))
            std::fprintf(stderr, "[tailscale] log out: %s\n", why.c_str());
        tsWorkerDone = true;
    });
    buildSettings();
}

void App::startTailscaleAtBoot() {
    tsWant = prefs::get("tailscale", "off") == "on";
    tsSignedIn = prefs::get("tailscale_signed_in", "no") == "yes";
    if (rpWant && tsWant && unit::state(kTsUnit) != "active") {
        std::string why;
        if (!unit::start(kTsUnit, &why))
            std::fprintf(stderr, "[tailscale] could not turn on at start: %s\n", why.c_str());
    }
}

// TAILSCALE (#286): asked only while somebody can see the answer,
// the row in Settings or the sign-in screen.
void App::pollTailscale() {
    {
        const bool look = rpWant && (tsWant || tsLoggingOut) &&
                          (here() == Screen::Settings || here() == Screen::TailscaleSignIn);
        tailscale::watch(look);
        const tailscale::Status st = tailscale::last();
        if (look && st.state == "Running" && !tsSignedIn) {
            tsSignedIn = true;
            prefs::set("tailscale_signed_in", "yes");
        }
        // When the sign-in ends, kept for the row to say after it has.
        if (look && st.state == "Running" &&
            prefs::get("tailscale_expires", "") != std::to_string(st.expires))
            prefs::set("tailscale_expires", std::to_string(st.expires));
        if (tsSigningIn && here() != Screen::TailscaleSignIn) {
            // LEFT BEFORE IT WAS APPROVED: nothing half done is kept.
            // A console that was never signed in is off again.
            tsSigningIn = false;
            tailscale::cancelLogin();
            if (tailscale::last().state != "Running") setTailscale(false);
            std::fprintf(stderr, "[tailscale] sign-in left\n");
        } else if (tsSigningIn) {
            std::string why;
            // Signed in: after its link was approved, or at once
            // when it was signed in already (no link at all).
            if (st.state == "Running" && st.authUrl.empty() &&
                (tsShownLink || !tailscale::loginRunning())) {
                tsSigningIn = false;
                tailscale::cancelLogin();
                tsSignedIn = true;
                prefs::set("tailscale_signed_in", "yes");
                stack.pop_back();
                buildSettings();
                std::fprintf(stderr, "[tailscale] signed in as %s, %s\n",
                             st.name.c_str(), st.address.c_str());
            } else if (!st.authUrl.empty() && !tsShownLink) {
                tsShownLink = true;
                // The link is both halves the screen shows: the QR,
                // and the address under the title. There is no
                // separate code.
                tsSignInScreen.setPairing(st.authUrl, st.authUrl);
                std::fprintf(stderr, "[tailscale] sign in at %s\n", st.authUrl.c_str());
            } else if (tailscale::loginFailed(&why)) {
                tsSignInScreen.setError(why);
            }
        }
        if (tsWorkerDone) {
            // Logged out: off, and no longer signed in.
            tsWorker.join();
            tsWorkerDone = false;
            tsLoggingOut = false;
            tsSignedIn = false;
            prefs::set("tailscale_signed_in", "no");
            setTailscale(false);
        }
        if (here() == Screen::Settings && tailscale::generation() != tsSeen) buildSettings();
    }
}

void App::pressTailscale() {
    // Off: the PIN (when set), then sign in. On: Disconnect
    // and Log out (the PIN), and Sign in again in the last
    // two weeks. Disconnected: Connect and Log out.
    const tailscale::Status st = tailscale::last();
    auto confirmLogout = [&]() {
        askChoice("Log out of Tailscale?", "", {"Log out", "Cancel"}, 1,
                  [&](int k) {
                      if (k != 0) return;
                      askPin("Enter the PIN", "To log out of Tailscale",
                             [&]() { tsLogout(); });
                  });
    };
    if (tsLoggingOut || tsSigningIn || players::streaming()) {
        sound::play(sound::Cue::Edge);
    } else if (!tsWant && !tsSignedIn) {
        askPin("Enter the PIN", "To turn on Tailscale",
               [&]() { openTsSignIn(false); });
        sound::play(sound::Cue::Activate);
    } else if (!tsWant) {
        askChoice("Tailscale", "", {"Connect", "Log out", "Cancel"}, 0,
                  [&, confirmLogout](int c) {
                      if (c == 0)
                          askPin("Enter the PIN", "To turn on Tailscale",
                                 [&]() { setTailscale(true); });
                      else if (c == 1) confirmLogout();
                  });
        sound::play(sound::Cue::Activate);
    } else if (st.state == "Running") {
        std::vector<std::string> opts;
        const bool soon = tsEndsSoon(st);
        if (soon) opts.push_back("Sign in again");
        opts.insert(opts.end(), {"Disconnect", "Log out", "Cancel"});
        askChoice("Tailscale", "", opts, 0, [&, soon, confirmLogout](int c) {
            if (soon && c == 0) {
                askPin("Enter the PIN", "To sign in to Tailscale again",
                       [&]() { openTsSignIn(true); });
                return;
            }
            if (soon) --c;
            if (c == 0) setTailscale(false);
            else if (c == 1) confirmLogout();
        });
        sound::play(sound::Cue::Activate);
    } else if (st.answered && (st.state == "NeedsLogin" || st.state == "Stopped")) {
        // Signed out: its sign-in ended, or it was removed in
        // Tailscale's own page. The same QR code again.
        askPin("Enter the PIN", "To sign in to Tailscale",
               [&]() { openTsSignIn(false); });
        sound::play(sound::Cue::Activate);
    } else {
        sound::play(sound::Cue::Edge);
    }
}
