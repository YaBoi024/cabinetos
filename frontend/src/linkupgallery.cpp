// --linkup-gallery: every LinkUp screen with made-up friends, walked with the
// triggers (R2 the next, L2 the last) and tried with the pad like any other
// screen. It plays the other console and the server, so nothing is contacted and
// nothing is saved. `--linkup-scene <n>` opens at a scene, for a --screenshot.

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "linkuprules.h"
#include "screenparts.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {

using linkup::Friend;
using linkup::Status;

constexpr const char* kMadeUpServer = "linkup.example.net:7845";

std::vector<Friend> madeUpFriends() {
    return {
        {"Riley", "", Status::InvitedYou, "Burnout 3", false},
        {"Sam", "", Status::InGame, "", false},
        {"Alex", "", Status::Online, "", false},
        {"Jordan", "", Status::Online, "", false},
        {"admin", "Morgan's server", Status::Online, "", false},
        {"admin", "Casey's server", Status::Online, "", false},
        {"Maya", "", Status::Offline, "", false},
        {"Theo", "", Status::Offline, "", false},
    };
}

}  // namespace

// ---- What the gallery plays the other side of ---------------------------

// The made-up Library, so the Host flow needs no server: Wii's first game needs
// a Wii Remote, and greys. No covers: the stand-in library's are other games' art.
void App::linkupGalleryLibrary() {
    if (!linkupGallery) return;
    auto add = [&](const char* title, uint32_t art, const char* platform, bool wii, bool remote) {
        design::Card c;
        c.title = title;
        c.sortKey = title;
        c.art = ui::Color::rgb(art);
        c.platform = platform;
        c.wii = wii;
        c.needsRemote = remote;
        cards.push_back(std::move(c));
        return static_cast<int>(cards.size()) - 1;
    };
    screens::Tile snes, wii, ps2, arcade;
    snes.id = 1; snes.query = "linkup-gallery-1"; snes.title = "Super Nintendo"; snes.detail = "3 games";
    snes.cards = {add("Contra III", 0x2484D6, "Super Nintendo", false, false),
                  add("Super Metroid", 0xEC405C, "Super Nintendo", false, false),
                  add("Chrono Trigger", 0x7A6BC4, "Super Nintendo", false, false)};
    wii.id = 2; wii.query = "linkup-gallery-2"; wii.title = "Wii"; wii.detail = "2 games";
    wii.cards = {add("Wii Sports", 0x58E8F6, "Wii", true, true),
                 add("Super Smash Bros. Brawl", 0xFFC457, "Wii", true, false)};
    ps2.id = 3; ps2.query = "linkup-gallery-3"; ps2.title = "PlayStation 2"; ps2.detail = "1 game";
    ps2.cards = {add("Burnout 3", 0xFF7AC7, "PlayStation 2", false, false)};
    arcade.id = 4; arcade.query = "linkup-gallery-4"; arcade.title = "Arcade"; arcade.detail = "1 game";
    arcade.cards = {add("DoDonPachi DaiOuJou", 0xFFC457, "Arcade", false, false)};
    libraryScreen.build({snes, wii, ps2, arcade}, {});
}

void App::linkupGalleryLibraryBack() {
    if (linkupGallery) libraryScreen.build(platformTiles, collectionTiles);
}

// A press on one of the made-up tiles above: they live in no server's list.
bool App::linkupGalleryOpenTile(const screens::Result& res) {
    if (!linkupGallery || !linkUp.picking) return false;
    const auto& tiles = libraryScreen.visible();
    if (res.value < 0 || res.value >= static_cast<int>(tiles.size())) return true;
    const screens::Tile& t = tiles[static_cast<size_t>(res.value)];
    gridScreen.open(t.title, screens::LineNames::Nothing, t.cards, cards);
    stack.push_back(Screen::Grid);
    sound::play(sound::Cue::Activate);
    return true;
}

// The host's game comes up a moment after a friend joins.
void App::linkupGalleryJoined(const std::string& game) {
    if (linkupGallery) {
        linkupGalleryGame = game;
        linkupGalleryPictureIn = 3.0f;
    }
}

void App::linkupGalleryTick(float dt) {
    if (!linkupGallery) return;
    if (linkupGalleryAt < 0) {
        linkupGalleryShow(std::clamp(linkupGalleryStart, 0, linkupGalleryScenes() - 1));
    }
    if (linkupGalleryPictureIn > 0.0f) {
        linkupGalleryPictureIn -= dt;
        if (linkupGalleryPictureIn <= 0.0f && here() == Screen::LinkUpPlay &&
            linkupPlay.shows() == screens::LinkUpPlayScreen::Shows::Waiting) {
            linkupPlay.open(screens::LinkUpPlayScreen::Shows::Picture, linkupGalleryGame, "", true);
            menuNotice.say(linkup::connection(true, 28), Tone::Info);
        }
    }
}

// R2 and L2, from whatever is on screen.
void App::linkupGalleryStep(int dir) {
    linkupGalleryShow(std::clamp(linkupGalleryAt + dir, 0, linkupGalleryScenes() - 1));
    sound::play(sound::Cue::Move);
}

// ---- The scenes ---------------------------------------------------------

namespace {

using App_Scene = void (*)(App&);
struct Scene {
    const char* name;
    App_Scene show;
};

using Play = screens::LinkUpPlayScreen::Shows;

// Alex has started a game and picked you; Riley's invite makes way for his.
void alexInvites(App& a) {
    a.linkUp.friends[2].status = Status::InvitedYou;
    a.linkUp.friends[2].game = "Contra III";
    a.linkUp.friends.erase(a.linkUp.friends.begin());
}

// The game, as a host's TV would show it, for what goes over it.
void hostGame(App& a) {
    a.linkupPlay.open(Play::Picture, "Contra III", "", /*friendSide=*/false);
    a.stack.push_back(App::Screen::LinkUpPlay);
    a.linkupGallerySession = true;
}
// The picture, as the friend's console would show it.
void friendGame(App& a) {
    a.linkupHost = "Alex";
    a.linkupPlay.open(Play::Picture, "Contra III", "", /*friendSide=*/true);
    a.stack.push_back(App::Screen::LinkUpPlay);
    a.linkupGallerySession = true;
}
void openSettingsOnLinkUp(App& a, bool intoRows) {
    a.goToDestination(App::BarSettings);
    a.settingsScreen.settleArrival();
    a.linkupFocusSettings(intoRows);
}

const Scene kScenes[] = {
    {"LinkUp in the top bar", [](App& a) { a.goToDestination(App::BarHome); }},
    {"LinkUp's screen", [](App& a) { a.goToDestination(App::BarLinkUp); }},
    {"Focus on the friends", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupScreen.focusFriends();
     }},
    {"Appear Offline on", [](App& a) {
         a.linkUp.appearOffline = true;
         a.goToDestination(App::BarLinkUp);
     }},
    {"No friends yet", [](App& a) {
         a.linkUp.friends.clear();
         a.goToDestination(App::BarLinkUp);
     }},
    {"Nudge offered", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupOfferNudge(2);
     }},
    {"Nudge sent, then greyed for 5 minutes", [](App& a) {
         a.linkUp.friends[2].nudged = true;
         a.goToDestination(App::BarLinkUp);
         a.linkupOfferNudge(2);
         a.menuNotice.say("Nudged Alex", Tone::Done);
     }},
    {"Their Nudge arrives", [](App& a) {
         a.goToDestination(App::BarHome);
         a.menuNotice.say(linkup::wantsToPlay("Alex"), Tone::Info);
     }},
    {"An invite arrives", [](App& a) {
         a.goToDestination(App::BarHome);
         a.menuNotice.say(linkup::invitedYou("Alex", "Contra III"), Tone::Info);
     }},
    {"The Invited line, with Join", [](App& a) {
         alexInvites(a);
         a.goToDestination(App::BarLinkUp);
         a.linkupScreen.focusFriends();
     }},
    {"Host: pick a game", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupPickGame();
         a.libraryScreen.settleArrival();
     }},
    {"Host: pick a game from a system", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupPickGame();
         a.libraryScreen.settleArrival();
         a.apply({screens::Action::OpenTile, 0});
     }},
    {"Host: Needs a Wii Remote, greyed", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupPickGame();
         a.libraryScreen.settleArrival();
         a.apply({screens::Action::OpenTile, 1});
     }},
    {"Host: pick a friend", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupPickGame();
         a.libraryScreen.settleArrival();
         a.linkupPickFriend("Contra III");
     }},
    {"Host: the invite goes out", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupStartHosting("Contra III", "Alex");
     }},
    {"Host: Alex joined, Direct", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.menuNotice.say(linkup::joined("Alex") + ". " + linkup::connection(true, 28), Tone::Done);
     }},
    {"Host: Alex joined, Relay", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.menuNotice.say(linkup::joined("Alex") + ". " + linkup::connection(false, 64), Tone::Done);
     }},
    {"Host: a second controller sits out", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.menuNotice.say(linkup::sittingOut("Alex"), Tone::Info);
     }},
    {"Host: the friend's connection dropped", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.linkupPausePanel(linkup::waitingFor("Alex"), "");
     }},
    {"Host: the friend is back", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.linkupPausePanel(linkup::isBack("Alex"), "");
     }},
    {"Host: the friend left", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.linkupPausePanel(linkup::left("Alex"), "");
     }},
    {"Idle: paused after 10 minutes, on both consoles", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.linkupPausePanel("Still playing?", "");
     }},
    {"Too slow", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         hostGame(a);
         a.menuNotice.say(linkup::kTooSlow, Tone::Problem);
     }},
    {"Remote Play's Streaming row, In use during a session", [](App& a) {
         a.linkupGallerySession = true;
         a.goToDestination(App::BarSettings);
         a.settingsScreen.settleArrival();
         a.linkupFocusSettingsNamed("Remote Play");
     }},
    {"Friend: Waiting for Alex", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupHost = "Alex";
         a.linkupPlay.open(Play::Waiting, linkup::waitingFor("Alex"), "Contra III", true);
         a.stack.push_back(App::Screen::LinkUpPlay);
     }},
    {"Friend: the game, Direct", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         friendGame(a);
         a.menuNotice.say(linkup::connection(true, 28), Tone::Info);
     }},
    {"Friend: Leave Alex's game?", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         friendGame(a);
         a.linkupAskLeave();
     }},
    {"Friend: Reconnecting", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupHost = "Alex";
         a.linkupPlay.open(Play::Reconnecting, "Reconnecting", "", true);
         a.stack.push_back(App::Screen::LinkUpPlay);
     }},
    {"Friend: back on the screen, offered a rejoin", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.askChoice(linkup::rejoinQuestion("Alex"), "Contra III", {"Rejoin", "Back"}, 0, [](int) {});
     }},
    {"Friend: the host ended the session", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.menuNotice.say(linkup::endedSession("Alex"), Tone::Info);
     }},
    {"Update: the friend's console is behind", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.menuNotice.say(linkup::needsUpdate("Alex"), Tone::Problem);
     }},
    {"Update: this console is behind", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.menuNotice.say(linkup::kThisNeedsUpdate, Tone::Problem);
     }},
    {"Update: the server is behind", [](App& a) {
         a.linkUp.instead = linkup::kServerNeedsUpdate;
         a.goToDestination(App::BarLinkUp);
     }},
    {"The server can't be reached", [](App& a) {
         a.linkUp.instead = linkup::kCantConnect;
         a.goToDestination(App::BarLinkUp);
     }},
    {"Taken off the server", [](App& a) {
         a.linkUp.instead = linkup::kConsoleRemoved;
         a.goToDestination(App::BarLinkUp);
     }},
    {"Host and Join greyed, In use: a device is streaming", [](App& a) {
         a.linkupGalleryStreaming = true;
         a.goToDestination(App::BarLinkUp);
     }},
    {"LinkUp greyed: Offline", [](App& a) {
         a.linkupGalleryOffline = true;
         a.goToDestination(App::BarHome);
     }},
    {"Settings: off", [](App& a) {
         a.linkUp.on = false;
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         openSettingsOnLinkUp(a, true);
     }},
    {"Settings: turning it on asks for the PIN (0000 here)", [](App& a) {
         a.linkUp.on = false;
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         a.linkupGalleryPin = true;
         openSettingsOnLinkUp(a, true);
         a.pressLinkUpSwitch();
     }},
    {"Settings: on, with no server yet", [](App& a) {
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         openSettingsOnLinkUp(a, true);
     }},
    {"Settings: Add a server, the address", [](App& a) {
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         openSettingsOnLinkUp(a, true);
         a.linkupAskAddress(kMadeUpServer);
     }},
    {"Settings: Add a server, the code", [](App& a) {
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         openSettingsOnLinkUp(a, true);
         a.linkupAskCode(kMadeUpServer, "K7MR-4QXT-9HPV");
     }},
    {"Settings: pairing", [](App& a) {
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         openSettingsOnLinkUp(a, true);
         a.menuNotice.say("Pairing\xE2\x80\xA6", Tone::Busy);
     }},
    {"Settings: paired, and LinkUp joins the bar", [](App& a) {
         openSettingsOnLinkUp(a, true);
         a.menuNotice.say(std::string("Paired with ") + kMadeUpServer, Tone::Done);
     }},
    {"Settings: a code that no longer works", [](App& a) {
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         openSettingsOnLinkUp(a, true);
         a.menuNotice.say("That code has expired or was already used", Tone::Problem);
     }},
    {"Settings: a server that can't be reached", [](App& a) {
         a.linkUp.paired = false;
         a.linkUp.server.clear();
         openSettingsOnLinkUp(a, true);
         a.menuNotice.say("Can't reach that server", Tone::Problem);
     }},
    {"Settings: Run your own", [](App& a) {
         openSettingsOnLinkUp(a, true);
         a.pressRunYourOwn();
     }},
    {"Settings: Leave", [](App& a) {
         openSettingsOnLinkUp(a, true);
         a.pressLinkUpLeave();
     }},
};

constexpr int kSceneCount = static_cast<int>(sizeof kScenes / sizeof kScenes[0]);

}  // namespace

int App::linkupGalleryScenes() const { return kSceneCount; }

void App::linkupGalleryShow(int index) {
    linkupGalleryAt = index;
    linkupPairingLeft = 0.0f;
    linkupGalleryPictureIn = 0.0f;

    // Everything put back, so a scene owes nothing to the one before it.
    if (keyboard.isOpen()) keyboard.cancel();
    if (pinScreen.isOpen()) pinScreen.close();
    choiceScreen.forget();
    choiceThen = nullptr;
    choiceBack = nullptr;
    menuNotice.text.clear();
    accountsOpen = false;
    barFocused = false;
    linkupEndPick();
    linkupGalleryPin = false;
    linkupGalleryOffline = false;
    linkupGalleryStreaming = false;
    linkupGallerySession = false;
    linkUp = linkup::State{};
    linkUp.on = true;
    linkUp.paired = true;
    linkUp.server = kMadeUpServer;
    linkUp.friends = madeUpFriends();

    kScenes[index].show(*this);
    refreshLinkUpScreen();
    linkupScreen.settleArrival();
    if (shotMode) {
        choiceScreen.settle();
    }
    std::fprintf(stderr, "[linkup] gallery %d/%d: %s\n", index + 1, kSceneCount, kScenes[index].name);
}

// Where it stands, under everything and clear of the pill.
void App::drawLinkUpGalleryCaption(float sc) {
    if (!linkupGallery || linkupGalleryAt < 0) return;
    const std::string line = std::to_string(linkupGalleryAt + 1) + " of " +
                             std::to_string(kSceneCount) + "   \xC2\xB7   " +
                             kScenes[linkupGalleryAt].name + "   \xC2\xB7   L2 back, R2 next";
    const ui::TextStyle st = ui::TextStyle::Caption1;
    const float w = text.measure(line, st, sc);
    constexpr float kH = 38.0f, kPad = 18.0f;
    const float x = (ui::kCanvasWidth - w) * 0.5f;
    const float y = ui::kCanvasHeight - kH - 6.0f;
    renderer.draw(ui::Rect{x - kPad, y, w + kPad * 2.0f, kH, kH * 0.5f, ui::Color::black(0.72f)});
    text.draw(renderer, line, x, y + (kH - text.lineHeight(st, sc)) * 0.5f + text.ascent(st, sc), st,
              ui::Color::white(0.80f), sc);
}

// For a scene: Settings open on LinkUp's category.
void App::linkupFocusSettings(bool intoRows) { linkupFocusSettingsNamed("LinkUp", intoRows); }

void App::linkupFocusSettingsNamed(const std::string& name, bool intoRows) {
    settingsScreen.focusCategory(settingsScreen.categoryNamed(name), intoRows);
}
