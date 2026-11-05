// LinkUp's rows in Settings (#654): the switch, Add a server, the server's address,
// Run your own and Leave, with the PIN where the plan asks for it. Turning LinkUp
// on, adding or changing a server and Leave ask for it; playing never does.

#include "app.h"

#include <string>

#include "accounts.h"
#include "linkuprules.h"

std::vector<screens::SettingsRow> App::linkupRows() {
    using K = screens::SettingsRow::Kind;
    std::vector<screens::SettingsRow> rows;
    rows.push_back({K::Toggle, SetLinkUp, "LinkUp", "", linkUp.on ? "On" : "Off"});
    if (linkupSettingsShape != 2) {
        if (linkUp.on)
            rows.push_back({K::Action, SetLinkUpServer, "Server", "", linkUp.paired ? linkUp.server : "None"});
        return rows;
    }
    if (linkUp.paired) {
        rows.push_back(
            {linkUp.on ? K::Action : K::Disabled, SetLinkUpServer, "Server", "", linkUp.server});
    } else {
        rows.push_back({linkUp.on ? K::Action : K::Disabled, SetLinkUpAdd, "Add a server", "", ""});
    }
    rows.push_back({K::Action, SetLinkUpRunOwn, "Run your own", "", ""});
    if (linkUp.paired) rows.push_back({K::Action, SetLinkUpLeave, "Leave", "", ""});
    return rows;
}

bool App::pressLinkUpRow(int id) {
    switch (id) {
        case SetLinkUp: pressLinkUpSwitch(); return true;
        case SetLinkUpAdd: linkupAskPin("To add a server", [&]() { linkupAskAddress(""); }); return true;
        case SetLinkUpServer:
            if (linkupSettingsShape != 2) pressLinkUpServer();
            else linkupAskPin("To change the server", [&]() { linkupRetypeAddress(); });
            return true;
        case SetLinkUpRunOwn: pressRunYourOwn(); return true;
        case SetLinkUpLeave: pressLinkUpLeave(); return true;
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
        linkUp.on = false;
        linkupGallerySession = false;
        buildSettings();
        sound::play(sound::Cue::Activate);
        return;
    }
    linkupAskPin("To turn on LinkUp", [&]() {
        linkUp.on = true;
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

void App::linkupAskCode(const std::string& address, const std::string& typed) {
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
        linkupPairingAddress = address;
        linkupPairingLeft = 1.2f;
        menuNotice.say("Pairing\xE2\x80\xA6", Tone::Busy);
    };
}

// The answer to a pairing. The real one comes from the LinkUp program.
void App::tickLinkUp(float dt) {
    if (linkupPairingLeft > 0.0f) {
        linkupPairingLeft -= dt;
        if (linkupPairingLeft <= 0.0f) {
            if (linkupGallery) {
                linkUp.paired = true;
                linkUp.server = linkupPairingAddress;
                buildSettings();
                menuNotice.text.clear();
            } else {
                menuNotice.say("Can't reach that server", Tone::Problem);
            }
        }
    }
    // Backed out of the Library without picking: the pick is over.
    if (linkUp.picking && here() != Screen::Library && here() != Screen::Grid) linkupEndPick();
    linkupGalleryTick(dt);
}

// Retyping needs no new code: the console remembers the server's key.
void App::linkupRetypeAddress() {
    ui::Keyboard::Config cfg;
    cfg.title = "Server address";
    cfg.initial = linkUp.server;
    cfg.required = true;
    cfg.shortcuts = {".com", ".net", ":"};
    keyboard.open(cfg);
    keyboardThen = [&](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        linkUp.server = keyboard.value();
        buildSettings();
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
            linkUp.paired = false;
            linkUp.server.clear();
            linkupGallerySession = false;
            buildSettings();
        });
    });
}
