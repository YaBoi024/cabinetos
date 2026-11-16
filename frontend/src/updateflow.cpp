// System update: root's answers, the row, the weekly check, Download and
// Restart, and "Updated to" once Home is up (#398, restructure batch e).

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "power.h"
#include "prefs.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

int64_t App::prefNum(const char* key) {
    return static_cast<int64_t>(std::strtoll(prefs::get(key, "0").c_str(), nullptr, 10));
}

// "1.4 GB", "312.5 MB", "under 1 MB". One decimal, so a base image in
// gigabytes and a small update in megabytes both read. Below a megabyte one
// decimal prints 0.0, which reads as nothing at all, so it says "under 1 MB".
std::string App::updBytes(int64_t b) {
    if (b < 1000000) return "under 1 MB";
    char buf[32];
    const double mb = static_cast<double>(b) / 1e6;
    if (mb >= 1000.0) std::snprintf(buf, sizeof buf, "%.1f GB", mb / 1000.0);
    else std::snprintf(buf, sizeof buf, "%.1f MB", mb);
    return std::string(buf);
}

// "0.4 of 2.1 MB": one unit, the total's. A total under a megabyte is
// "under 1 MB" alone, since both of its numbers would print 0.0.
std::string App::updOf(int64_t done, int64_t total) {
    if (total < 1000000) return updBytes(total);
    char buf[48];
    const bool gb = total >= 1000000000;
    const double d = static_cast<double>(done) / (gb ? 1e9 : 1e6);
    const double t = static_cast<double>(total) / (gb ? 1e9 : 1e6);
    std::snprintf(buf, sizeof buf, "%.1f of %.1f %s", d, t, gb ? "GB" : "MB");
    return std::string(buf);
}

// "2026.11.09.3 · 312.5 MB", or the version alone when the size is 0.
// Root writes 0 when it could not work the size out, so 0 is not known to
// be small, and no size is said rather than a wrong one.
std::string App::updVersionSize(const std::string& version, int64_t size) {
    return size > 0 ? version + " \xC2\xB7 " + updBytes(size) : version;
}

// "Checked today", by the calendar here, not by 24-hour periods.
auto App::updChecked() -> std::string {
    const int64_t at = prefNum("update_checked");
    // Never checked says nothing: the row alone, and pressing it checks.
    // MMagTech, 2026-09-25.
    if (at <= 0) return std::string();
    auto dayOf = [](std::time_t t) {
        std::tm tm{};
        localtime_r(&t, &tm);
        tm.tm_hour = 12;
        tm.tm_min = tm.tm_sec = 0;
        return static_cast<int64_t>(std::mktime(&tm) / 86400);
    };
    const int64_t days = dayOf(std::time(nullptr)) - dayOf(static_cast<std::time_t>(at));
    if (days <= 0) return "Checked today";
    if (days == 1) return "Checked yesterday";
    return "Checked " + std::to_string(days) + " days ago";
}

// What the last successful check found, when nothing has run this boot:
// still newer than what is running, or not.
bool App::updFoundNewer() {
    const std::string found = prefs::get("update_found", "");
    return !found.empty() && found != update::bootedVersion();
}

screens::SettingsRow App::updateRow() {
    using K = screens::SettingsRow::Kind;
    std::string value, detail;
    switch (upd.state) {
        case update::State::Checking:
            value = "Checking\xE2\x80\xA6";
            break;
        case update::State::UpToDate:
            value = "Up to date";
            detail = updChecked();
            break;
        case update::State::Available:
            value = "Update available";
            detail = updVersionSize(upd.version, upd.size);
            break;
        case update::State::Downloading: {
            const int pct = upd.total > 0
                ? static_cast<int>(std::min<int64_t>(100, upd.done * 100 / upd.total)) : 0;
            value = std::to_string(pct) + "%";
            // A total of 0 is a size root could not work out: the
            // percentage alone until bootc says how much it is fetching.
            detail = upd.total > 0 ? updOf(upd.done, upd.total) : std::string();
            break;
        }
        case update::State::Installing: {
            // Time and bytes, because unpacking has no honest percentage
            // and a spinner would not show it is not frozen.
            const int64_t secs = std::max<int64_t>(0, std::time(nullptr) - upd.since);
            value = "Installing\xE2\x80\xA6";
            detail = (secs >= 60 ? std::to_string(secs / 60) + "m " : std::string()) +
                     std::to_string(secs % 60) + "s, " + updBytes(upd.written) + " written";
            break;
        }
        case update::State::Ready:
            value = "Restart to update";
            detail = upd.version;
            break;
        case update::State::Failed:
            value = upd.download ? "Couldn't update" : "Couldn't check";
            detail = upd.reason;
            break;
        case update::State::None:
            if (updFoundNewer()) {
                value = "Update available";
                detail = updVersionSize(prefs::get("update_found", ""),
                                        prefNum("update_size"));
            } else if (prefNum("update_checked") > 0) {
                value = "Up to date";
                detail = updChecked();
            }
            break;
    }
    return screens::SettingsRow{K::Action, SetUpdate, "System update", detail, value};
}

// Start root's check or download, and say so at once rather than when
// root's first write lands. A refusal is said in the row: the unit is not
// in this image, or polkit said no.
void App::startUpdate(bool download, bool automatic) {
    std::string why;
    update::Status said;
    said.download = download;
    said.at = std::time(nullptr);
    if (update::start(download, &why)) {
        said.state = update::State::Checking;
    } else {
        said.state = update::State::Failed;
        updAskAfterCheck = false;   // no check is coming to answer it
        said.reason = why.find("not found") != std::string::npos ? "Not in this image" : why;
        if (said.reason.size() > 60) said.reason.resize(60);
    }
    upd = said;
    updSaidAt = said.at;
    updAuto = automatic;
    if (here() == Screen::Settings) buildSettings();
}

// "UPDATE AVAILABLE": DOWNLOAD OR LATER. A check that finds something
// used to stop at the row, and the second press that downloads was
// nowhere on screen. MMagTech on the TV, 2026-09-25: *"i have to click
// again even though it doesnt indicate it."* Now the check a person
// pressed opens this, the row opens it again, and Download goes to the
// PIN. The same shape as "Update ready", so an update is: check,
// Download, Restart now.
void App::askUpdateDownload() {
    const std::string detail = updVersionSize(upd.version, upd.size);
    askChoice("Update available", detail, {"Download", "Later"}, 0, [&](int k) {
        if (k != 0) return;
        askPin("Enter the PIN", "To update",
               [&]() { startUpdate(/*download=*/true, /*automatic=*/false); });
    });
}

// "Update ready", once per staged version. Restart now is the Power
// menu's Restart: logind holds the machine while saves upload (PR #52),
// which is the "waits for saves" Sign out has. Later leaves it staged for
// the next Restart or Power off, and the row says "Restart to update".
void App::askUpdateReady() {
    askChoice("Update ready", upd.version, {"Restart now", "Later"}, 0, [&](int k) {
        if (k != 0) return;
        std::fprintf(stderr, "[update] restart now, into %s\n", upd.version.c_str());
        power::act(power::Action::Restart);
    });
}

void App::readUpdateCheckAtStart() {
    updWeekly = prefs::get("update_check", "manual") == "weekly";
}

void App::tickUpdateTimer(float dt) {
    updAutoWait -= dt;
}

void App::pollUpdate() {
    const update::Status s = update::read();
    const bool mine = updSaidAt == 0 || s.at >= updSaidAt;
    const bool changed = s.state != upd.state || s.at != upd.at || s.done != upd.done ||
                         s.written != upd.written || s.version != upd.version ||
                         s.reason != upd.reason;
    if (mine && changed) {
        upd = s;
        updSaidAt = 0;
        std::fprintf(stderr, "[update] %s%s%s%s\n",
                     s.state == update::State::Checking      ? "checking"
                     : s.state == update::State::UpToDate    ? "up to date"
                     : s.state == update::State::Available   ? "available "
                     : s.state == update::State::Downloading ? "downloading "
                     : s.state == update::State::Installing  ? "installing "
                     : s.state == update::State::Ready       ? "ready "
                     : s.state == update::State::Failed      ? "failed: "
                                                             : "none",
                     s.version.c_str(),
                     !s.version.empty() && !s.reason.empty() ? ", " : "",
                     s.reason.c_str());
        // A check that finished: remember when, and what it found.
        // Both kinds of check write the "Checked" line.
        if ((s.state == update::State::UpToDate || s.state == update::State::Available) &&
            s.at > prefNum("update_checked")) {
            prefs::set("update_checked", std::to_string(s.at));
            prefs::set("update_found",
                       s.state == update::State::Available ? s.version : "");
            prefs::set("update_size", std::to_string(s.size));
            if (s.state == update::State::Available &&
                prefs::get("update_announced", "") != s.version) {
                // Once per new version, and only when nobody asked:
                // a check somebody pressed answers in its own row.
                if (updAuto) menuNotice.say("Update available", Tone::Info);
                prefs::set("update_announced", s.version);
            }
        }
        // Staged: ask once per version, remembering the boot it was
        // staged in so the next boot can tell whether it applied.
        if (s.state == update::State::Ready &&
            (prefs::get("update_pending", "") != s.version ||
             prefs::get("update_pending_boot", "") != update::bootId())) {
            prefs::set("update_pending", s.version);
            prefs::set("update_pending_boot", update::bootId());
            updReadyPanel = true;
            updReadyAfterGame = playing;
        }
        // The weekly check says nothing when it fails; it tries again
        // later, which is how "offline skips it" and "reconnecting
        // catches up" both happen with no separate trigger.
        // The answer to a check somebody pressed: an update opens the
        // panel, if they are still in Settings and nothing else is up.
        if (updAskAfterCheck && !s.busy()) {
            updAskAfterCheck = false;
            if (s.state == update::State::Available && here() == Screen::Settings &&
                !choiceScreen.isOpen() && !pinScreen.isOpen() && !keyboard.isOpen())
                askUpdateDownload();
        }
        if (!s.busy()) updAuto = false;
        if (here() == Screen::Settings) buildSettings();
    } else if (upd.state == update::State::Installing && here() == Screen::Settings) {
        buildSettings();   // the clock on "Installing…" moves by itself
    }
    // WEEKLY: on Home, no game, nothing else under way, more than seven
    // days since the last check that worked, and not more than once
    // every half hour while it keeps failing.
    if (updWeekly && !playing && here() == Screen::Home && !upd.busy() &&
        upd.state != update::State::Ready && updAutoWait <= 0.0f &&
        std::time(nullptr) - prefNum("update_checked") > 7 * 86400) {
        updAutoWait = 1800.0f;
        std::fprintf(stderr, "[update] weekly check\n");
        startUpdate(/*download=*/false, /*automatic=*/true);
    }
}

// NEVER DURING A GAME. Ready while playing waits for the game to be
// closed, then asks once on Home; ready anywhere else asks at once,
// unless something else is already asking.
void App::askReadyWhenFree() {
    if (updReadyPanel && !playing && !overlayOpen && !choiceScreen.isOpen() &&
        !pinScreen.isOpen() && !keyboard.isOpen() && !accountsOpen &&
        leaving == Leave::None && switchPendingId == 0 && curtain.value() < 0.01f &&
        (!updReadyAfterGame || here() == Screen::Home)) {
        updReadyPanel = false;
        askUpdateReady();
    }
}

void App::sayUpdatedTo() {
    if (!updStartNotice.empty() && !playing && here() == Screen::Home &&
        curtain.value() < 0.01f) {
        menuNotice.say(updStartNotice, updStartTone);
        updStartNotice.clear();
    }
}

void App::pressUpdate() {
    // ONE ROW, AND WHAT IT DOES IS WHAT IT SAYS: check, fetch
    // (behind the PIN: an update can be gigabytes and replaces
    // the system, so the owner decides when), restart, or
    // retry. Busy, it does nothing. docs/SETTINGS.md, System.
    // ONLY THIS BOOT'S ANSWER OPENS THE PANEL (#485, SETTINGS-19). With
    // nothing run since the start, the row's "Update available" is the
    // remembered check's, and the panel named its version and size while
    // Download fetched whatever was newest by then. That press checks
    // first, as any other, and the panel opens on the fresh answer.
    const bool available = upd.state == update::State::Available;
    const bool retryDownload = upd.state == update::State::Failed && upd.download;
    if (upd.busy()) {
        sound::play(sound::Cue::Edge);
    } else if (upd.state == update::State::Ready) {
        askUpdateReady();
        sound::play(sound::Cue::Activate);
    } else if (available) {
        askUpdateDownload();
        sound::play(sound::Cue::Activate);
    } else if (retryDownload) {
        askPin("Enter the PIN", "To update",
               [&]() { startUpdate(/*download=*/true, /*automatic=*/false); });
        sound::play(sound::Cue::Activate);
    } else {
        updAskAfterCheck = true;
        startUpdate(/*download=*/false, /*automatic=*/false);
        sound::play(sound::Cue::Activate);
    }
}
