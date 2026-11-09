// LinkUp's rows in Settings (#654): the switch and Server, whose panel holds Add a
// server or Change server, Run your own and Leave, with the PIN where the plan asks
// for it. Turning LinkUp on, adding or changing a server and Leave ask for it;
// playing never does.

#include "app.h"

#include <string>

#include <json-c/json.h>

#include "accounts.h"
#include "linkuprules.h"

std::vector<screens::SettingsRow> App::linkupRows() {
    using K = screens::SettingsRow::Kind;
    std::vector<screens::SettingsRow> rows;
    // Greyed with the rest of Streaming while a device streams (App::buildSettings).
    const bool streaming = linkupInUse();
    rows.push_back({streaming ? K::Disabled : K::Toggle, SetLinkUp, "LinkUp", "", linkUp.on ? "On" : "Off"});
    // A feature that is off shows only its switch.
    if (linkUp.on)
        rows.push_back({streaming ? K::Disabled : K::Action, SetLinkUpServer, "Server", "",
                        linkUp.paired ? linkUp.server : "None"});
    return rows;
}

bool App::pressLinkUpRow(int id) {
    switch (id) {
        case SetLinkUp: pressLinkUpSwitch(); return true;
        case SetLinkUpServer: pressLinkUpServer(); return true;
        default: return false;
    }
}

// The PIN guards what is new, through the one door, on any account but the
// owner's. The gallery asks every time, with a made-up PIN its caption gives.
void App::linkupAskPin(const std::string& detail, std::function<void()> then) {
    if (linkupGalleryPin) {
        askCode("0000", "Enter the PIN", detail, std::move(then));
        return;
    }
    if (accounts::activeId() == accounts::ownerId()) {
        then();
        return;
    }
    askPin("Enter the PIN", detail, std::move(then));
}

void App::pressLinkUpSwitch() {
    if (linkUp.on) {
        if (linkupGallery) linkUp.on = false;
        else linkupSetSwitch(false);
        buildSettings();
        sound::play(sound::Cue::Activate);
        return;
    }
    linkupAskPin("To turn on LinkUp", [&]() {
        if (linkupGallery) linkUp.on = true;
        else linkupSetSwitch(true);
        buildSettings();
        sound::play(sound::Cue::Activate);
    });
}

// Both are typed on the on-screen keyboard: the address, then the code.
void App::linkupAskAddress(const std::string& typed) {
    ui::Keyboard::Config cfg;
    cfg.title = "Server address";
    cfg.initial = typed;
    cfg.placeholder = "name.example.net:7845";
    cfg.required = true;
    cfg.shortcuts = {".com", ".net", ":"};
    keyboard.open(cfg);
    keyboardThen = [&](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        linkupAskCode(keyboard.value(), "");
    };
}

// The address is a copy: it comes from keyboard.value(), which the open below empties.
void App::linkupAskCode(std::string address, const std::string& typed) {
    ui::Keyboard::Config cfg;
    cfg.title = "Code";
    cfg.hint = address;
    cfg.initial = typed;
    cfg.placeholder = "XXXX-XXXX-XXXX";
    cfg.required = true;
    keyboard.open(cfg);
    keyboardThen = [&, address](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        const std::string code = linkup::tidyCode(keyboard.value());
        if (!linkup::codeComplete(code)) {
            menuNotice.say("A code has 12 characters", Tone::Problem);
            linkupAskCode(address, code);
            return;
        }
        if (!linkupGallery) {
            linkupBeginPairing(address, code);
            return;
        }
        linkupPairingAddress = address;
        linkupPairingLeft = 1.2f;
        menuNotice.say("Pairing\xE2\x80\xA6", Tone::Busy);
    };
}

// Before the frame is drawn (frame.cpp), which is what the picture rule needs (linkupsession.cpp).
void App::tickLinkUp(float dt) {
    tickLinkUpLive(dt);
    if (linkupWaitEndsIn > 0.0f) {
        linkupWaitEndsIn -= dt;
        linkupPlay.setFade(design::easeInOut((linkupWaitEndsIn - kWaitEndsGap) / kWaitEndsFade));
        if (linkupWaitEndsIn <= 0.0f && linkupWaitEndsThen) {
            const auto then = std::move(linkupWaitEndsThen);
            linkupWaitEndsThen = nullptr;
            then();
        }
    }
    if (linkupPairingLeft > 0.0f) {
        linkupPairingLeft -= dt;
        if (linkupPairingLeft <= 0.0f) {
            if (linkupGallery) {
                linkUp.paired = true;
                linkUp.server = linkupPairingAddress;
                buildSettings();
                menuNotice.text.clear();
            } else {
                // Nothing came back at all: the program never got as far as asking.
                linkupPairingCode.clear();
                menuNotice.say("Can't reach that server", Tone::Problem);
                if (!linkUp.paired) linkupWantProgram(false);
            }
        }
    }
    if (linkupNextOpen) {
        linkupNextLeft -= dt;
        if (linkupNextLeft <= 0.0f) linkupNextRanOut();
    }
    // Backed out of the Library without picking: the pick is over.
    if (linkUp.picking && here() != Screen::Library && here() != Screen::Grid) linkupEndPick();
    linkupGalleryTick(dt);
}

// The same server at a new address needs no new code: the console remembers its key. The program
// checks what answers there, and the row shows the address once the program has it (the state
// event). Another server's address opens the Code screen instead (another_server,
// linkupsession.cpp), and the old server stays until that code pairs.
void App::linkupRetypeAddress() {
    ui::Keyboard::Config cfg;
    cfg.title = "Server address";
    cfg.initial = linkUp.server;
    cfg.required = true;
    cfg.shortcuts = {".com", ".net", ":"};
    keyboard.open(cfg);
    keyboardThen = [&](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        if (linkupGallery) {
            linkUp.server = keyboard.value();
            buildSettings();
            return;
        }
        json_object* c = json_object_new_object();
        json_object_object_add(c, "cmd", json_object_new_string("retype"));
        json_object_object_add(c, "address", json_object_new_string(keyboard.value().c_str()));
        linkupSend(c);
    };
}

void App::pressRunYourOwn() {
    linkupRunOwn.open();
    linkupRunOwn.setText("Run your own", {"Scan for how to set up a server."}, "");
    linkupRunOwn.setPairing(linkup::kRepositoryUrl, linkup::kRepositoryUrl);
    stack.push_back(Screen::LinkUpRunOwn);
    sound::play(sound::Cue::Activate);
}

// One row for the server, saying which, and the rest in a panel, as Wi-Fi's are.
void App::pressLinkUpServer() {
    sound::play(sound::Cue::Activate);
    if (!linkUp.paired) {
        linkupPanel("Server", "", {"Add a server", "Run your own"}, 0, [&](int c) {
            if (c == 0) linkupAskPin("To add a server", [&]() { linkupAskAddress(""); });
            if (c == 1) pressRunYourOwn();
        });
        return;
    }
    linkupPanel(linkUp.server, "", {"Change server", "Run your own", "Leave"}, 0, [&](int c) {
        if (c == 0) linkupAskPin("To change the server", [&]() { linkupRetypeAddress(); });
        if (c == 1) pressRunYourOwn();
        if (c == 2) pressLinkUpLeave();
    });
}

void App::pressLinkUpLeave() {
    sound::play(sound::Cue::Activate);
    linkupPanel("Leave " + linkUp.server + "?", "", {"Leave", "Cancel"}, 1, [&](int c) {
        if (c != 0) return;
        linkupAskPin("To leave this server", [&]() {
            if (!linkupGallery) {
                json_object* c = json_object_new_object();
                json_object_object_add(c, "cmd", json_object_new_string("leave"));
                linkupSend(c);
                return;
            }
            linkUp.paired = false;
            linkUp.server.clear();
            buildSettings();
        });
    });
}
