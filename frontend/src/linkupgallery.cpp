// --linkup-gallery: LinkUp's screens with made-up friends, one section at a time
// (`--linkup-section <n>`), walked with the triggers (R2 the next, L2 the last) and
// tried with the pad like any other screen. It plays the other console and the
// server, so nothing is contacted and nothing is saved. `--linkup-scene <n>` opens
// at a section's scene, for a --screenshot.

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "linkuprules.h"
#include "screenparts.h"
#include "settingsids.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {

using linkup::Friend;
using linkup::Status;

constexpr const char* kMadeUpServer = "linkup.example.net:7845";

std::vector<Friend> madeUpFriends() {
    return {
        {"Riley", "", Status::InvitedYou, "Burnout 3"},
        {"Sam", "", Status::InGame, ""},
        {"Alex", "", Status::Online, ""},
        {"Jordan", "", Status::Online, ""},
        {"Morgan", "", Status::Online, ""},
        {"Casey", "", Status::Online, ""},
        {"Maya", "", Status::Offline, ""},
        {"Theo", "", Status::Offline, ""},
    };
}

}  // namespace

// ---- What the gallery plays the other side of ---------------------------

// The made-up Library, so inviting needs no server: Wii's first game needs a Wii
// Remote, and greys. No covers: the stand-in library's are other games' art.
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

// The friend joins a while after the invite, so the wait can be seen and pressed.
void App::linkupGalleryInvited() {
    if (linkupGallery) linkupGalleryJoinIn = 8.0f;
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
        linkupGalleryShow(std::clamp(linkupGalleryFirst() + linkupGalleryStart, linkupGalleryFirst(),
                                     linkupGalleryLast()));
    }
    if (linkupGalleryJoinIn > 0.0f) {
        linkupGalleryJoinIn -= dt;
        if (linkupGalleryJoinIn <= 0.0f && linkupHoldsScreen()) {
            linkupGameStarts();
        }
    }
    if (linkupGalleryPictureIn > 0.0f) {
        linkupGalleryPictureIn -= dt;
        if (linkupGalleryPictureIn <= 0.0f && here() == Screen::LinkUpPlay &&
            linkupPlay.shows() == screens::LinkUpPlayScreen::Shows::Waiting) {
            linkupPlay.open(screens::LinkUpPlayScreen::Shows::Picture, linkupGalleryGame, "", true);
        }
    }
}

// R2 and L2, from whatever is on screen.
void App::linkupGalleryStep(int dir) {
    linkupGalleryShow(std::clamp(linkupGalleryAt + dir, linkupGalleryFirst(), linkupGalleryLast()));
    sound::play(sound::Cue::Move);
}

// Y, to compare: LinkUp's screen with no friends, and how Settings' sections are cut.
bool App::linkupGalleryFlip() {
    if (keyboard.isOpen() || pinScreen.isOpen() || choiceScreen.isOpen()) return false;
    if (here() == Screen::LinkUp && linkUp.friends.empty()) {
        linkupScreen.setNoFriendsLook((linkupScreen.noFriendsLook() + 1) % 3);
        std::fprintf(stderr, "[linkup] gallery no-friends look: %d\n", linkupScreen.noFriendsLook());
    } else if (here() == Screen::Settings) {
        linkupSettingsShape = (linkupSettingsShape + 1) % 3;
        buildSettings();
        std::fprintf(stderr, "[linkup] gallery Settings shape: %d\n", linkupSettingsShape);
    } else {
        return false;
    }
    sound::play(sound::Cue::Move);
    return true;
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

// A game of your own, nobody else in it, for what arrives over it.
void soloGame(App& a) {
    a.linkupPlay.open(Play::Picture, "Super Metroid", "", /*friendSide=*/false);
    a.stack.push_back(App::Screen::LinkUpPlay);
}

// The game, on the console of whoever invited, for what goes over it.
void inviterGame(App& a) {
    a.goToDestination(App::BarLinkUp);
    a.linkupGuest = "Alex";
    a.linkupPlay.open(Play::Picture, "Contra III", "", /*friendSide=*/false);
    a.stack.push_back(App::Screen::LinkUpPlay);
    a.linkupGallerySession = true;
}

// The picture, as the friend's console would show it.
void friendGame(App& a) {
    a.goToDestination(App::BarLinkUp);
    a.linkupHost = "Alex";
    a.linkupPlay.open(Play::Picture, "Contra III", "", /*friendSide=*/true);
    a.stack.push_back(App::Screen::LinkUpPlay);
    a.linkupGallerySession = true;
}

void pickingForAlex(App& a) {
    a.goToDestination(App::BarLinkUp);
    a.linkupPickGame("Alex");
    a.libraryScreen.settleArrival();
}

void inviteOut(App& a) {
    pickingForAlex(a);
    a.linkupInvite("Contra III");
}

void settingsOnLinkUp(App& a) {
    a.goToDestination(App::BarSettings);
    a.settingsScreen.settleArrival();
    a.settingsScreen.focusSetting(SetLinkUp, true);
}

void noServerYet(App& a) {
    a.linkUp.paired = false;
    a.linkUp.server.clear();
    settingsOnLinkUp(a);
}

// Each "#" line heads a section, and only one section is on the screen at a time,
// so there is one thing to judge (--linkup-section).
const Scene kScenes[] = {
    {"#Your friends", nullptr},
    {"LinkUp in the top bar", [](App& a) { a.goToDestination(App::BarHome); }},
    {"The list", [](App& a) { a.goToDestination(App::BarLinkUp); }},
    {"Two friends with the same name", [](App& a) {
         a.linkUp.friends[4] = {"admin", "Morgan's server", Status::Online, ""};
         a.linkUp.friends[5] = {"admin", "Casey's server", Status::Online, ""};
         a.goToDestination(App::BarLinkUp);
     }},
    {"The same name, with long server names", [](App& a) {
         a.linkUp.friends[4] = {"admin", "The Extremely Serious Retro Gaming Basement of Morgan", Status::Online, ""};
         a.linkUp.friends[5] = {"admin", "xX_CaseysUltimateSuperServerOfDoom2026_Xx", Status::Online, ""};
         a.goToDestination(App::BarLinkUp);
     }},
    {"Appear Offline on", [](App& a) {
         a.linkUp.appearOffline = true;
         a.goToDestination(App::BarLinkUp);
     }},
    {"No friends", [](App& a) {
         a.linkUp.friends.clear();
         a.goToDestination(App::BarLinkUp);
     }},
    {"#Inviting a friend", nullptr},
    {"Inviting Alex: pick a system", pickingForAlex},
    {"Inviting Alex: pick a game", [](App& a) {
         pickingForAlex(a);
         a.apply({screens::Action::OpenTile, 0});
     }},
    {"Inviting Alex: Needs a Wii Remote, greyed", [](App& a) {
         pickingForAlex(a);
         a.apply({screens::Action::OpenTile, 1});
     }},
    {"The invite goes out and you wait (Alex joins in 8 seconds, B cancels)", inviteOut},
    {"Alex joined: the game", inviterGame},
    {"Alex declined", [](App& a) {
         inviteOut(a);
         a.linkupGalleryJoinIn = 0.0f;
         a.linkupDeclinedBy("Alex");
     }},
    {"No answer from Alex, after 30 minutes", [](App& a) {
         inviteOut(a);
         a.linkupGalleryJoinIn = 0.0f;
         a.menuNotice.text.clear();
         a.linkupNoAnswerFrom("Alex");
     }},
    {"#In the game", nullptr},
    {"A second controller sits out", [](App& a) {
         inviterGame(a);
         a.menuNotice.say(linkup::sittingOut("Alex"), Tone::Info);
     }},
    {"The friend's connection dropped", [](App& a) {
         inviterGame(a);
         a.linkupPausePanel(linkup::waitingFor("Alex"));
     }},
    {"The friend is back", [](App& a) {
         inviterGame(a);
         a.linkupPausePanel(linkup::isBack("Alex"));
     }},
    {"The friend left", [](App& a) {
         inviterGame(a);
         a.linkupPausePanel(linkup::left("Alex"));
     }},
    {"Idle: paused after 10 minutes, on both consoles", [](App& a) {
         inviterGame(a);
         a.linkupPausePanel("Still playing?");
     }},
    {"Too slow", [](App& a) {
         inviterGame(a);
         a.menuNotice.say(linkup::kTooSlow, Tone::Problem);
     }},
    {"Remote Play's row, In use during a session", [](App& a) {
         a.linkupGallerySession = true;
         a.goToDestination(App::BarSettings);
         a.settingsScreen.settleArrival();
         a.settingsScreen.focusSetting(SetRemotePlay, false);
     }},
    {"#An invite to you", nullptr},
    {"Friend: an invite arrives while you play, and nothing stops", [](App& a) {
         alexInvites(a);
         a.goToDestination(App::BarHome);
         soloGame(a);
         a.linkupSayInvited("Alex");
     }},
    {"Friend: an invite arrives at Home", [](App& a) {
         alexInvites(a);
         a.goToDestination(App::BarHome);
         a.linkupSayInvited("Alex");
     }},
    {"Friend: the Invited you line", [](App& a) {
         alexInvites(a);
         a.goToDestination(App::BarLinkUp);
     }},
    {"Friend: Join or Decline", [](App& a) {
         alexInvites(a);
         a.goToDestination(App::BarLinkUp);
         a.linkupAnswerInvite(a.linkUp.friends[1]);
     }},
    {"Friend: Join or Decline, a long name", [](App& a) {
         alexInvites(a);
         a.linkUp.friends[1].game = "Castlevania: Symphony of the Night (Director's Cut, Special Edition)";
         a.goToDestination(App::BarLinkUp);
         a.linkupAnswerInvite(a.linkUp.friends[1]);
     }},
    {"#In a friend's game", nullptr},
    {"Friend: Waiting for Alex", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.linkupHost = "Alex";
         a.linkupPlay.open(Play::Waiting, linkup::waitingFor("Alex"), "Contra III", true);
         a.stack.push_back(App::Screen::LinkUpPlay);
     }},
    {"Friend: the game", friendGame},
    {"Friend: Leave Alex's game?", [](App& a) {
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
         a.linkupPanel(linkup::rejoinQuestion("Alex"), "Contra III", {"Rejoin", "Back"}, 0, [](int) {});
     }},
    {"Friend: the host ended the session", [](App& a) {
         a.goToDestination(App::BarLinkUp);
         a.menuNotice.say(linkup::endedSession("Alex"), Tone::Info);
     }},
    {"#When something is wrong", nullptr},
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
    {"Friends greyed, In use: a device is streaming", [](App& a) {
         a.linkupGalleryStreaming = true;
         a.goToDestination(App::BarLinkUp);
     }},
    {"LinkUp greyed: Offline", [](App& a) {
         a.linkupGalleryOffline = true;
         a.goToDestination(App::BarHome);
     }},
    {"#Settings: the side list", nullptr},
    {"Streaming, with Y to compare", settingsOnLinkUp},
    {"#Settings: adding a server", nullptr},
    {"Settings: off", [](App& a) {
         a.linkUp.on = false;
         noServerYet(a);
     }},
    {"Settings: turning it on asks for the PIN (0000 here)", [](App& a) {
         a.linkUp.on = false;
         a.linkupGalleryPin = true;
         noServerYet(a);
         a.pressLinkUpSwitch();
     }},
    {"Settings: on, with no server yet", noServerYet},
    {"Settings: Add a server, the address", [](App& a) {
         noServerYet(a);
         a.linkupAskAddress(kMadeUpServer);
     }},
    {"Settings: Add a server, the code", [](App& a) {
         noServerYet(a);
         a.linkupAskCode(kMadeUpServer, "K7MR-4QXT-9HPV");
     }},
    {"Settings: pairing", [](App& a) {
         noServerYet(a);
         a.menuNotice.say("Pairing\xE2\x80\xA6", Tone::Busy);
     }},
    {"Settings: paired, and LinkUp joins the bar", [](App& a) {
         settingsOnLinkUp(a);
     }},
    {"Settings: a code that no longer works", [](App& a) {
         noServerYet(a);
         a.menuNotice.say("That code has expired or was already used", Tone::Problem);
     }},
    {"Settings: a server that can't be reached", [](App& a) {
         noServerYet(a);
         a.menuNotice.say("Can't reach that server", Tone::Problem);
     }},
    {"Settings: Server, with no server yet", [](App& a) {
         noServerYet(a);
         a.pressLinkUpServer();
     }},
    {"#Settings: a server you have", nullptr},
    {"Settings: Server, paired", [](App& a) {
         settingsOnLinkUp(a);
         a.pressLinkUpServer();
     }},
    {"Settings: Run your own", [](App& a) {
         settingsOnLinkUp(a);
         a.pressRunYourOwn();
     }},
    {"Settings: Leave", [](App& a) {
         settingsOnLinkUp(a);
         a.pressLinkUpLeave();
     }},
};

constexpr int kSceneCount = static_cast<int>(sizeof kScenes / sizeof kScenes[0]);

}  // namespace

int App::linkupGalleryFirst() const {
    int seen = -1;
    for (int i = 0; i < kSceneCount; ++i)
        if (!kScenes[i].show && ++seen == linkupGallerySection) return i + 1;
    return 1;
}

int App::linkupGalleryLast() const {
    int last = linkupGalleryFirst();
    while (last + 1 < kSceneCount && kScenes[last + 1].show) ++last;
    return last;
}

void App::linkupGalleryShow(int index) {
    linkupGalleryAt = index;
    linkupPairingLeft = 0.0f;
    linkupGalleryPictureIn = 0.0f;
    linkupGalleryJoinIn = 0.0f;

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
    const int first = linkupGalleryFirst();
    std::string line = std::string(kScenes[first - 1].name + 1) + "   \xC2\xB7   " +
                       std::to_string(linkupGalleryAt - first + 1) + " of " +
                       std::to_string(linkupGalleryLast() - first + 1) + "   \xC2\xB7   " +
                       kScenes[linkupGalleryAt].name + "   \xC2\xB7   ";
    if (here() == Screen::LinkUp && linkUp.friends.empty()) {
        static constexpr const char* kNames[] = {"11 Standing behind", "5 Only its screen on", "17 Same game on both"};
        line += std::string(kNames[linkupScreen.noFriendsLook()]) + "   \xC2\xB7   Y for the next   \xC2\xB7   ";
    }
    if (here() == Screen::Settings) {
        static constexpr const char* kNext[] = {"Y for 7 sections", "Y for 9 sections", "Y for 8 sections"};
        line += std::string(kNext[linkupSettingsShape]) + "   \xC2\xB7   ";
    }
    line += "L2 back, R2 next";
    const ui::TextStyle st = ui::TextStyle::Caption1;
    const float w = text.measure(line, st, sc);
    constexpr float kH = 38.0f, kPad = 18.0f;
    const float x = (ui::kCanvasWidth - w) * 0.5f;
    const float y = ui::kCanvasHeight - kH - 6.0f;
    renderer.draw(ui::Rect{x - kPad, y, w + kPad * 2.0f, kH, kH * 0.5f, ui::Color::black(0.72f)});
    text.draw(renderer, line, x, y + (kH - text.lineHeight(st, sc)) * 0.5f + text.ascent(st, sc), st,
              ui::Color::white(0.80f), sc);
}

