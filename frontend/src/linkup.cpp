// LinkUp, the App's side (#654): whether it is in the top bar, what a press on its
// screen means, the Host flow (a game, then a friend), Join, Nudge, Appear Offline
// and the panels a session puts over the game. It holds no internet code at all:
// the real answers come over the local socket from the LinkUp program, which is
// step 3 of the plan. Until then linkupgallery.cpp plays the other side.

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

void App::refreshLinkUpScreen() { linkupScreen.setState(linkUp, linkupInUse()); }

void App::applyLinkUp(const screens::Result& res) {
    using S = screens::LinkUpScreen;
    if (res.value == S::kHost) {
        linkupPickGame();
    } else if (res.value == S::kAppearOffline) {
        // Belongs to this account, and asks for no PIN.
        linkUp.appearOffline = !linkUp.appearOffline;
        refreshLinkUpScreen();
        sound::play(sound::Cue::Activate);
    } else if (res.value == screens::LinkUpPlayScreen::kLeave) {
        linkupAskLeave();
    } else if (res.value >= 0 && res.value < static_cast<int>(linkUp.friends.size())) {
        const linkup::Friend f = linkUp.friends[static_cast<size_t>(res.value)];
        if (f.status == linkup::Status::InvitedYou) linkupJoin(f);
        else if (f.status == linkup::Status::Online) linkupOfferNudge(res.value);
    }
}

// Pressing an Online friend offers Nudge. Once per 5 minutes per friend, so it
// greys while it waits.
void App::linkupOfferNudge(int index) {
    const linkup::Friend f = linkUp.friends[static_cast<size_t>(index)];
    sound::play(sound::Cue::Activate);
    askChoice(f.name, "", {"Nudge", "Back"}, f.nudged ? 1 : 0, [&, index](int c) {
        if (c != 0) return;
        linkup::Friend& who = linkUp.friends[static_cast<size_t>(index)];
        who.nudged = true;
        refreshLinkUpScreen();
        menuNotice.say("Nudged " + who.name, Tone::Done);
    });
    choiceScreen.setDisabled({f.nudged, false});
}

// ---- Hosting: a game, then a friend -------------------------------------

// The Library chooses the game, greyed as Remote Play greys one player two could
// not hold (App::controllersOn). A press on a game comes back to linkupPickedGame.
void App::linkupPickGame() {
    linkUp.picking = true;
    libraryScreen.setPlatformsOnly(true);
    linkupGalleryLibrary();
    libraryScreen.enter();
    stack.push_back(Screen::Library);
    sound::play(sound::Cue::Activate);
    menuNotice.say("Pick a game to host", Tone::Info);
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
    linkupPickFriend(card.title);
    return true;
}

// Everyone, Online first, then Offline, whose invite waits for them. Somebody
// already in a LinkUp game, or hosting the one they invited you to, is greyed: a
// game has one seat for a friend, and they are not free to take it.
void App::linkupPickFriend(const std::string& game) {
    std::vector<std::string> names, values;
    std::vector<bool> greyed;
    std::vector<int> who;
    using S = linkup::Status;
    for (S status : {S::Online, S::Offline, S::InGame, S::InvitedYou}) {
        for (int i = 0; i < static_cast<int>(linkUp.friends.size()); ++i) {
            if (linkUp.friends[static_cast<size_t>(i)].status != status) continue;
            names.push_back(linkup::nameOnList(linkUp.friends, i));
            values.push_back(linkup::heading(status));
            greyed.push_back(status == S::InGame || status == S::InvitedYou);
            who.push_back(i);
        }
    }
    askChoice("Invite to " + game, "", names, 0, [&, game, who](int c) {
        if (c < 0 || c >= static_cast<int>(who.size())) return;
        const linkup::Friend f = linkUp.friends[static_cast<size_t>(who[static_cast<size_t>(c)])];
        linkupStartHosting(game, f.name);
    });
    choiceScreen.setValues(values);
    choiceScreen.setDisabled(greyed);
}

void App::linkupStartHosting(const std::string& game, const std::string& who) {
    linkupEndPick();
    while (stack.size() > 1 && here() != Screen::LinkUp) stack.pop_back();
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Picture, game, "", /*friendSide=*/false);
    stack.push_back(Screen::LinkUpPlay);
    linkupGallerySession = true;
    menuNotice.say("Invited " + who + " to " + game, Tone::Done);
}

// ---- Joining, and leaving -----------------------------------------------

void App::linkupJoin(const linkup::Friend& f) {
    sound::play(sound::Cue::Activate);
    linkupHost = f.name;
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Waiting, linkup::waitingFor(f.name), f.game,
                    /*friendSide=*/true);
    stack.push_back(Screen::LinkUpPlay);
    linkupGallerySession = true;
    linkupGalleryJoined(f.game);
}

// "That is the friend's only menu": Leave and Back, starting on the harmless one.
void App::linkupAskLeave() {
    sound::play(sound::Cue::Activate);
    if (!linkupPlay.friendSide()) {
        // The host leaves through the pause menu; here it stands in for a game.
        linkupGallerySession = false;
        if (stack.size() > 1) stack.pop_back();
        refreshLinkUpScreen();
        return;
    }
    askChoice(linkup::leaveQuestion(linkupHost), "", {"Leave", "Back"}, 1, [&](int c) {
        if (c != 0) return;
        linkupGallerySession = false;
        if (here() == Screen::LinkUpPlay && stack.size() > 1) stack.pop_back();
        refreshLinkUpScreen();
    });
}

// One panel, whichever way the game stopped: the title says why, and A resumes.
// No Resume to focus: nobody moved focus there, so it is said, "Press (A)".
void App::linkupPausePanel(const std::string& title, const std::string& detail) {
    askChoice(title, detail, {}, 0, [](int) {});
    choiceScreen.setPrompt("Press", "A", "to resume");
}
