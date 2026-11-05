// LinkUp, the App's side (#654): whether it is in the top bar, what a press on its
// screen means, inviting (a friend, then a game, then the wait), answering an invite
// and the panels a session puts over the game. It holds no internet code at all: the
// real answers come over the local socket from the LinkUp program, which is step 3 of
// the plan. Until then linkupgallery.cpp plays the other side.

#include "app.h"

#include "linkuprules.h"
#include "players.h"
#include "screenparts.h"

// ---- The top bar --------------------------------------------------------

bool App::linkupInBar() const { return linkUp.on && linkUp.paired; }

bool App::linkupOffline() const { return romm::serverAway() || linkupGalleryOffline; }

// Focus and the shoulders skip LinkUp where it is hidden, and where it is greyed
// because the console cannot reach its RomM server.
bool App::barItemReachable(int item) const {
    return item != BarLinkUp || (linkupInBar() && !linkupOffline());
}

// ---- Its screen ---------------------------------------------------------

bool App::linkupInUse() const { return players::streaming() || linkupGalleryStreaming; }

bool App::linkupSessionOpen() const { return linkupGallerySession; }

void App::refreshLinkUpScreen() {
    const bool inUse = linkupInUse();
    linkupScreen.setState(linkUp, inUse);
    linkupGrid.setState(linkUp, inUse);

    std::vector<screens::Tile> tiles;
    if (linkUp.instead.empty()) {
        for (const linkup::Group& g : linkup::groups(linkUp.friends)) {
            for (int i : g.friends) {
                const linkup::Friend& f = linkUp.friends[static_cast<size_t>(i)];
                const bool canPress = g.status == linkup::Status::InvitedYou || g.status == linkup::Status::Online;
                screens::Tile t;
                t.id = i;
                t.query = "friend-" + std::to_string(i);
                t.title = f.name;
                t.enterable = canPress && !inUse;
                // One grid in the list's order, with no headings: a heading per
                // status gave each a row of its own, and six friends ran off the
                // screen. So each tile says its own status.
                const bool sameName = linkup::nameOnList(linkUp.friends, i) != f.name;
                t.detail = linkup::heading(g.status);
                if (canPress && inUse) t.detail = linkup::kInUse;
                else if (g.status == linkup::Status::InvitedYou) t.detail += ": " + f.game;
                else if (sameName) t.detail += ", " + f.from;
                tiles.push_back(std::move(t));
            }
        }
    }
    linkupTiles.build(std::move(tiles), {});
    linkupTiles.setOnePill(linkup::statusLine(linkUp.appearOffline),
                           screens::LinkUpScreen::kAppearOffline,
                           linkUp.instead.empty() ? "No friends yet" : linkUp.instead);
    linkupTiles.settleFocus();
}

screens::Result App::linkupKey(screens::Nav n) {
    if (linkupLayout == LinkUpLayout::Rows) return linkupScreen.key(n);
    if (linkupLayout == LinkUpLayout::Grid) return linkupGrid.key(n);
    screens::Result r = linkupTiles.key(n);
    if (r.action == screens::Action::OpenTile) {
        const auto& tiles = linkupTiles.visible();
        r = {screens::Action::LinkUp, tiles[static_cast<size_t>(r.value)].id};
    }
    return r;
}

void App::applyLinkUp(const screens::Result& res) {
    using Play = screens::LinkUpPlayScreen;
    if (res.value == screens::LinkUpScreen::kAppearOffline) {
        // Belongs to this account, and asks for no PIN.
        linkUp.appearOffline = !linkUp.appearOffline;
        refreshLinkUpScreen();
        sound::play(sound::Cue::Activate);
    } else if (res.value == Play::kCancelInvite) {
        linkupCancelInvite();
    } else if (res.value == Play::kLeave) {
        linkupAskLeave();
    } else if (res.value >= 0 && res.value < static_cast<int>(linkUp.friends.size())) {
        const linkup::Friend f = linkUp.friends[static_cast<size_t>(res.value)];
        if (f.status == linkup::Status::InvitedYou) linkupAnswerInvite(f);
        else if (f.status == linkup::Status::Online) linkupPickGame(f.name);
    }
}

// ---- Inviting: a friend, then a game, then the wait ----------------------

// The Library chooses the game, greyed as Remote Play greys one player two could
// not hold (App::controllersOn). A press on a game comes back to linkupPickedGame.
void App::linkupPickGame(const std::string& who) {
    linkupGuest = who;
    linkUp.picking = true;
    libraryScreen.setPlatformsOnly(true);
    linkupGalleryLibrary();
    libraryScreen.enter();
    stack.push_back(Screen::Library);
    sound::play(sound::Cue::Activate);
    menuNotice.say("Pick a game to play with " + who, Tone::Info);
}

void App::linkupEndPick() {
    if (!linkUp.picking) return;
    linkUp.picking = false;
    libraryScreen.setPlatformsOnly(false);
    linkupGalleryLibraryBack();
}

bool App::linkupPickedGame(int cardIndex) {
    if (!linkUp.picking) return false;
    if (cardIndex < 0 || cardIndex >= static_cast<int>(cards.size())) return true;
    const design::Card& card = cards[static_cast<size_t>(cardIndex)];
    if (card.needsRemote) {
        menuNotice.say(screens::kNeedsRemote, Tone::Problem);
        sound::play(sound::Cue::Edge);
        return true;
    }
    sound::play(sound::Cue::Activate);
    linkupInvite(card.title);
    return true;
}

// The invite goes out at once and the person inviting waits here. The game starts
// when the friend joins, so both begin at its title screen: many games read how many
// are playing there, and an emulator run as a program gets its controllers only once.
void App::linkupInvite(const std::string& game) {
    linkupEndPick();
    while (stack.size() > 1 && here() != Screen::LinkUp) stack.pop_back();
    linkupGame = game;
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Inviting, linkup::waitingFor(linkupGuest), game,
                    /*friendSide=*/false);
    stack.push_back(Screen::LinkUpPlay);
    linkupGallerySession = true;
    linkupGalleryInvited();
    menuNotice.say("Invited " + linkupGuest + " to " + game, Tone::Done);
}

void App::linkupGameStarts() {
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Picture, linkupGame, "", /*friendSide=*/false);
}

void App::linkupCancelInvite() {
    linkupGallerySession = false;
    if (here() == Screen::LinkUpPlay && stack.size() > 1) stack.pop_back();
    refreshLinkUpScreen();
    sound::play(sound::Cue::Back);
}

void App::linkupDeclinedBy(const std::string& who) {
    linkupCancelInvite();
    menuNotice.say(linkup::declined(who), Tone::Info);
}

// Nobody may be watching by now, so it stays until a press rather than as a pill.
void App::linkupNoAnswerFrom(const std::string& who) {
    linkupCancelInvite();
    linkupPanel(linkup::noAnswer(who), "", {}, 0, [](int) {});
    choiceScreen.setPrompt("Press", "A", "to continue");
}

// While an invite waits, the screen neither dims nor turns off: the person may have
// walked away, and is coming back to a game.
bool App::linkupHoldsScreen() {
    return here() == Screen::LinkUpPlay &&
           linkupPlay.shows() == screens::LinkUpPlayScreen::Shows::Inviting;
}

// ---- The panels ----------------------------------------------------------

// EVERY LINKUP QUESTION IS ONE WIDTH. A panel follows its longest line, so a game's
// name made each invite a different size. Too long a line is cut short rather than
// the panel growing, the same reason as Add a controller's (choice.h).
void App::linkupPanel(const std::string& title, const std::string& detail,
                      std::vector<std::string> options, int focus, std::function<void(int)> then) {
    constexpr float kWidth = 780.0f;
    askChoice(title, detail, std::move(options), focus, std::move(then));
    choiceScreen.setFixedWidth(kWidth);
}

// ---- Answering an invite -------------------------------------------------

// Longer than a pill's reading time: a person may be looking at their own game. Who
// it is decides whether to stop now; the game waits for the panel.
void App::linkupSayInvited(const std::string& who) {
    menuNotice.say(linkup::invitedYou(who), Tone::Info);
    menuNotice.life = 6.0f;
}

void App::linkupAnswerInvite(const linkup::Friend& f) {
    sound::play(sound::Cue::Activate);
    linkupPanel(linkup::joinQuestion(f.name), f.game, {"Join", "Decline"}, 0, [&, f](int c) {
        if (c == 0) {
            linkupJoin(f);
            return;
        }
        for (linkup::Friend& other : linkUp.friends) {
            if (other.name == f.name && other.status == linkup::Status::InvitedYou) {
                other.status = linkup::Status::Online;
                other.game.clear();
            }
        }
        refreshLinkUpScreen();
    });
}

void App::linkupJoin(const linkup::Friend& f) {
    sound::play(sound::Cue::Activate);
    linkupHost = f.name;
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Waiting, linkup::waitingFor(f.name), f.game,
                    /*friendSide=*/true);
    stack.push_back(Screen::LinkUpPlay);
    linkupGallerySession = true;
    linkupGalleryJoined(f.game);
}

// The friend's only menu: Leave and Back, starting on the harmless one.
void App::linkupAskLeave() {
    sound::play(sound::Cue::Activate);
    if (!linkupPlay.friendSide()) {
        // The host leaves through the pause menu; here it stands in for a game.
        linkupGallerySession = false;
        if (stack.size() > 1) stack.pop_back();
        refreshLinkUpScreen();
        return;
    }
    linkupPanel(linkup::leaveQuestion(linkupHost), "", {"Leave", "Back"}, 1, [&](int c) {
        if (c != 0) return;
        linkupGallerySession = false;
        if (here() == Screen::LinkUpPlay && stack.size() > 1) stack.pop_back();
        refreshLinkUpScreen();
    });
}

// One panel, whichever way the game stopped: the title says why, and A resumes.
// No Resume to focus: nobody moved focus there, so it is said, "Press (A)".
void App::linkupPausePanel(const std::string& title) {
    linkupPanel(title, "", {}, 0, [](int) {});
    choiceScreen.setPrompt("Press", "A", "to resume");
}
