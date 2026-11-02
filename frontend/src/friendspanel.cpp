// FORK (handheld): THE FRIENDS LIST ON SCREEN. Settings, Remote Play, Join a
// friend opens it when the player has friends saved (friends.h): each with
// Ready or Not reachable beside it, kept up to date while it is open, and
// Join, Rename and Remove for the one chosen. Add a friend keeps a name and an
// address; Join someone new asks for an address once and offers to keep it
// after the stream (joinflow.cpp). The same panel style as Paired devices
// (remoteplayflow.cpp).
//
// The check runs on a worker, all friends at once, and never blocks a frame.

#include "app.h"

#include <future>

#include "friends.h"
#include "moonlight.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {
constexpr const char* kPanelTitle = "Join a friend";
constexpr int kProbeTimeoutMs = 1500;
constexpr uint64_t kProbeEveryMs = 10000;

std::string hostKey(const std::vector<friends::Friend>& v) {
    std::string key;
    for (const friends::Friend& f : v) key += f.host + "\n";
    return key;
}
}  // namespace

void App::startFriendProbe() {
    if (friendProbe.busy()) {
        friendProbeMs = 0;   // pollFriends starts it when the last one is done
        return;
    }
    friendProbeMs = SDL_GetTicks();
    friendProbeKey = hostKey(friendsShown);
    std::vector<std::string> hosts;
    for (const friends::Friend& f : friendsShown) hosts.push_back(f.host);
    friendProbe.start([hosts](std::vector<int>& out, std::string&) {
        std::vector<std::future<bool>> asks;
        for (const std::string& h : hosts)
            asks.push_back(std::async(std::launch::async,
                                      [h] { return friends::reachable(h, kProbeTimeoutMs); }));
        for (std::future<bool>& a : asks) out.push_back(a.get() ? 1 : 0);
        return true;
    });
}

// The rows as they read now: the friends, then the two ways to add one.
void App::friendRows(std::vector<std::string>* options, std::vector<std::string>* values) const {
    options->clear();
    values->clear();
    for (size_t i = 0; i < friendsShown.size(); ++i) {
        options->push_back(friendsShown[i].name);
        const int r = i < friendReady.size() ? friendReady[i] : -1;
        values->push_back(r < 0 ? "Checking" : (r ? "Ready" : "Not reachable"));
    }
    options->push_back("Add a friend");
    values->push_back("");
    options->push_back("Join someone new");
    values->push_back("");
}

void App::openFriendsPanel(int focus) {
    friendsShown = friends::list();
    if (friendsShown.empty()) {
        choiceScreen.close();
        return;
    }
    friendReady.assign(friendsShown.size(), -1);
    std::vector<std::string> options, values;
    friendRows(&options, &values);
    const int last = static_cast<int>(options.size()) - 1;
    askChoice(kPanelTitle, "", options, std::clamp(focus, 0, last), [&](int i) {
        const int n = static_cast<int>(friendsShown.size());
        if (i == n) {
            choiceScreen.close();
            askFriendHost("");
            return;
        }
        if (i == n + 1) {
            choiceScreen.close();
            askJoinAddress("");
            return;
        }
        if (i < 0 || i > n) return;
        const friends::Friend f = friendsShown[static_cast<size_t>(i)];
        askChoice(f.name, f.host, {"Join", "Rename", "Remove", "Cancel"}, 0, [&, f, i](int c) {
            if (c == 0) {
                choiceScreen.close();
                startJoin(f.host);
                return;
            }
            if (c == 1) {
                choiceScreen.close();
                askFriendName(f.host, f.name, false);
                return;
            }
            if (c != 2) { openFriendsPanel(i); return; }
            askChoice("Remove " + f.name + "?", "", {"Remove", "Cancel"}, 1, [&, f, i](int k) {
                if (k == 0) {
                    friends::remove(f.host);
                    menuNotice.say("Removed " + f.name, Tone::Done);
                }
                openFriendsPanel(k == 0 ? 0 : i);
            });
            choiceBack = [&, i]() { openFriendsPanel(i); };
        });
        // B goes back to the list, as Cancel does.
        choiceBack = [&, i]() { openFriendsPanel(i); };
    });
    choiceScreen.setValues(values);
    choiceScreen.setStaysOpen(true);
    startFriendProbe();
}

// Once a frame: a finished check lands in the open list, and an open list is
// checked again every few seconds.
void App::pollFriends() {
    const bool open = choiceScreen.isOpen() && choiceScreen.title() == kPanelTitle;
    std::vector<int> got;
    std::string err;
    bool ok = false;
    if (friendProbe.take(&got, &err, &ok) && open && ok &&
        friendProbeKey == hostKey(friendsShown) && got.size() == friendsShown.size()) {
        friendReady = got;
        std::vector<std::string> options, values;
        friendRows(&options, &values);
        choiceScreen.replace(options, values, "");
    }
    if (open && !friendProbe.busy() &&
        (friendProbeMs == 0 || SDL_GetTicks() - friendProbeMs > kProbeEveryMs))
        startFriendProbe();
}

void App::askFriendHost(const std::string& why) {
    ui::Keyboard::Config cfg;
    cfg.title = "Add a friend";
    cfg.hint = "Their address, on their Play with a friend page";
    cfg.placeholder = "192.168.0.20";
    cfg.shortcuts = {".", ":"};
    keyboard.open(cfg);
    if (!why.empty()) keyboard.sayInField(why, /*problem=*/true, /*keep=*/true);
    keyboardThen = [&](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) {
            openFriendsPanel(0);
            return;
        }
        const std::string host = moonlight::cleanHost(keyboard.value());
        if (host.empty()) {
            askFriendHost("That isn't an address");
            return;
        }
        askFriendName(host, "", true);
    };
}

void App::askFriendName(const std::string& host, const std::string& current, bool isNew) {
    ui::Keyboard::Config cfg;
    cfg.title = isNew ? "Name this friend" : "Rename " + current;
    cfg.initial = current;
    cfg.placeholder = "Name";
    keyboard.open(cfg);
    keyboardThen = [&, host, current, isNew](ui::KeyboardResult r) {
        if (r == ui::KeyboardResult::Committed) {
            std::string name = friends::cleanName(keyboard.value());
            if (name.empty()) name = current.empty() ? friends::cleanName(host) : current;
            const bool saved = isNew ? friends::add({name, host}) : friends::rename(host, name);
            if (saved) menuNotice.say(isNew ? "Saved " + name : "Renamed to " + name, Tone::Done);
            else menuNotice.say("Couldn't save", Tone::Problem);
        }
        openFriendsPanel(0);
    };
}

// After a stream with someone not on the list.
void App::offerSaveFriend(const std::string& host) {
    if (friends::has(host) || friendDeclined.count(host)) return;
    askChoice("Save this friend?", host, {"Save", "Not now"}, 0, [&, host](int i) {
        if (i == 0) askFriendName(host, "", true);
        else friendDeclined.insert(host);
    });
}
