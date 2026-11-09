// LinkUp, the App's side (#654): whether it is in the top bar, what a press on its
// screen means, inviting (a friend, then a game, then the wait), answering an invite
// and the panels a session puts over the game. It holds no internet code at all: the
// real answers come over the local socket from the LinkUp program (linkupsession.cpp),
// and in --linkup-gallery that file is quiet and linkupgallery.cpp plays the other side.

#include "app.h"

#include <algorithm>

#include <json-c/json.h>

#include "fileio.h"
#include "linkuprules.h"
#include "players.h"
#include "screenparts.h"
#include "storage.h"

// ---- The top bar --------------------------------------------------------

bool App::linkupInBar() const { return linkUp.on && linkUp.paired; }

bool App::linkupOffline() const { return romm::serverAway() || linkupGalleryOffline; }

// LinkUp joins the bar only when paired, while the person stands on Settings, so
// Settings glides along to make room and the word fades in once there is room: faded
// in at the start it would show through Settings passing over it. Nothing moves on the
// first frame. How much of its place LinkUp has, 0 to 1.
float App::linkupBarRoomNow(float dt) {
    const float want = linkupInBar() ? 1.0f : 0.0f;
    if (!linkupBarRoomSet) {
        linkupBarRoom.settle(want);
        linkupBarRoom.smooth = true;
        linkupBarRoomSet = true;
    }
    constexpr float kSeconds = 0.5f;
    linkupBarRoom.retarget(want, kSeconds);
    linkupBarRoom.tick(dt);
    return linkupBarRoom.value();
}

float App::linkupBarFade(float room) { return std::clamp((room - 0.6f) / 0.4f, 0.0f, 1.0f); }

// Focus and the shoulders skip LinkUp where it is hidden, and where it is greyed
// because the console cannot reach its RomM server.
bool App::barItemReachable(int item) const {
    return item != BarLinkUp || (linkupInBar() && !linkupOffline());
}

// ---- Its screen ---------------------------------------------------------

bool App::linkupInUse() const { return players::streaming() || linkupGalleryStreaming; }

bool App::linkupSessionOpen() const { return linkupSide != LinkupSide::None || linkupGallerySession; }

void App::refreshLinkUpScreen() { linkupScreen.setState(linkUp, linkupInUse()); }

void App::applyLinkUp(const screens::Result& res) {
    using Play = screens::LinkUpPlayScreen;
    if (res.value == screens::LinkUpScreen::kAppearOffline) {
        // Belongs to this account, and asks for no PIN.
        linkUp.appearOffline = !linkUp.appearOffline;
        const std::string mark = storage::userDir(storage::currentUser()) + "/linkup-offline";
        if (linkUp.appearOffline) cab::writeBytes(mark, std::string("offline"));
        else cab::removeFile(mark);
        refreshLinkUpScreen();
        sound::play(sound::Cue::Activate);
    } else if (res.value == Play::kCancelInvite) {
        linkupCancelInvite();
    } else if (res.value == Play::kLeave) {
        linkupAskLeave();
    } else if (res.value == Play::kResumeBoth) {
        sound::play(sound::Cue::Activate);
        linkupTellPeer("resume");
    } else if (res.value >= 0 && res.value < static_cast<int>(linkUp.friends.size())) {
        const linkup::Friend f = linkUp.friends[static_cast<size_t>(res.value)];
        if (f.status == linkup::Status::InvitedYou) linkupAnswerInvite(f);
        else if (f.status == linkup::Status::Online) {
            linkupGuestId = f.id;
            linkupPickGame(f.name);
        }
    }
}

// ---- Inviting: a friend, then a game, then the wait ----------------------

// What a friend cannot play is not on the list: a Wii game that needs a Remote, a light-gun game,
// a handheld and Steam. A virtual controller cannot carry a Remote's motion or hold a gun, and
// nothing of Steam runs while the console does. A game that is greyed here for its own reason is
// left out with them.
bool App::linkupFriendCanPlay(int cardIndex) const {
    if (cardIndex < 0 || cardIndex >= static_cast<int>(cards.size())) return false;
    const design::Card& c = cards[static_cast<size_t>(cardIndex)];
    return c.unavailable.empty() && !c.needsRemote;
}

// The Library chooses the game. A press on a game comes back to linkupPickedGame.
void App::linkupPickGame(const std::string& who) {
    linkupGuest = who;
    linkUp.picking = true;
    libraryScreen.setPlatformsOnly(true);
    if (linkupGallery) {
        linkupGalleryLibrary();
    } else {
        // A system left with nothing a friend can play has no tile (inviteHasSystem).
        takeInviteReadAhead(/*wait=*/true);
        std::vector<screens::Tile> tiles;
        std::string none;
        for (screens::Tile& t : platformTiles) {
            if (!t.enterable || linkup::handheldSystem(t.slug)) continue;
            if (inviteHasSystem(t)) tiles.push_back(t);
            else none += (none.empty() ? "" : ", ") + t.title;
        }
        if (!none.empty()) std::fprintf(stderr, "[linkup] inviting: nothing a friend can play in %s\n", none.c_str());
        libraryScreen.build(tiles, {});
    }
    libraryScreen.enter();
    stack.push_back(Screen::Library);
    sound::play(sound::Cue::Activate);
}

void App::linkupEndPick() {
    if (!linkUp.picking) return;
    linkUp.picking = false;
    libraryScreen.setPlatformsOnly(false);
    libraryScreen.build(platformTiles, collectionTiles);
}

bool App::linkupPickedGame(int cardIndex) {
    if (!linkUp.picking) return false;
    if (cardIndex < 0 || cardIndex >= static_cast<int>(cards.size())) return true;
    const design::Card& card = cards[static_cast<size_t>(cardIndex)];
    sound::play(sound::Cue::Activate);
    linkupGameId = card.id;
    if (linkupNextOpen) linkupNextStarts(card.title);
    else linkupInvite(card.title);
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
    if (linkupGallery) {
        linkupGallerySession = true;
        linkupGalleryInvited();
    } else {
        linkupSendInvite();
    }
}

void App::linkupGameStarts() {
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Picture, linkupGame, "", /*friendSide=*/false);
}

void App::linkupCancelInvite() {
    linkupWaitEndsIn = 0.0f;
    linkupWaitEndsThen = nullptr;
    linkupGallerySession = false;
    if (!linkupGallery && linkupSide == LinkupSide::Host) {
        // Past the join, B ends the session; before it, it takes the invite back.
        if (linkupJoined) {
            linkupTellPeer("ended");
            linkupSend(json_tokener_parse("{\"cmd\":\"end\"}"));
            linkupSessionClosed();
        } else {
            linkupSendCancel();
        }
    } else if (here() == Screen::LinkUpPlay && stack.size() > 1) {
        stack.pop_back();
    }
    refreshLinkUpScreen();
    sound::play(sound::Cue::Back);
}

// ---- The next game --------------------------------------------------------

// Quitting a game keeps the session: the friend waits, and the person hosting lands on
// the game list, where the next one starts on both with no new invite. A game's end comes
// here, however it ended (linkupHostGameEnded), and a crash counts as quitting.
void App::linkupGameQuit() {
    if (here() == Screen::LinkUpPlay && stack.size() > 1) stack.pop_back();
    linkupPickGame(linkupGuest);
    linkupNextOpen = true;
    linkupNextLeft = linkup::kNextGameSeconds;
    libraryScreen.setSessionLine(linkup::playingWith(linkupGuest));
}

void App::linkupNextClose() {
    linkupNextOpen = false;
    linkupNextLeft = 0.0f;
    libraryScreen.setSessionLine("");
}

void App::linkupNextStarts(const std::string& game) {
    linkupNextClose();
    linkupEndPick();
    while (stack.size() > 1 && here() != Screen::LinkUp) stack.pop_back();
    linkupGame = game;
    if (linkupGallery) {
        linkupPlay.open(screens::LinkUpPlayScreen::Shows::Picture, game, "", /*friendSide=*/false);
        stack.push_back(Screen::LinkUpPlay);
        return;
    }
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Waiting, game, "", /*friendSide=*/false);
    stack.push_back(Screen::LinkUpPlay);
    linkupStartGame();
}

// The session is over: back to LinkUp's list.
void App::linkupNextDone() {
    linkupNextClose();
    linkupEndPick();
    linkupGallerySession = false;
    if (!linkupGallery && linkupSide == LinkupSide::Host) {
        linkupTellPeer("ended");
        linkupSend(json_tokener_parse("{\"cmd\":\"end\"}"));
        linkupSessionClosed();
        return;
    }
    while (stack.size() > 1 && here() != Screen::LinkUp) stack.pop_back();
    refreshLinkUpScreen();
}

// Starting on the harmless one. The friend's console then says the session ended.
void App::linkupAskEndSession() {
    sound::play(sound::Cue::Activate);
    linkupPanel(linkup::endSessionQuestion(linkupGuest), "", {"End", "Cancel"}, 1, [&](int c) {
        if (c == 0) linkupNextDone();
    });
}

// Nobody may be watching by now, so this stays until a press, over the list it ended on.
void App::linkupNextRanOut() {
    linkupNextOpen = false;
    linkupPanel(linkup::sessionEndedWith(linkupGuest), "", {}, 0, [&](int) { linkupNextDone(); });
    choiceScreen.setPrompt("Press", "A", "to continue");
    choiceScreen.setOnlyA(true);
}

// However long the wait has been, it ends gently: the screen goes, a moment passes, and
// what comes next arrives. A press to cancel is answered at once instead.
void App::linkupWaitEnds(std::function<void()> then) {
    linkupWaitEndsIn = kWaitEndsFade + kWaitEndsGap;
    linkupWaitEndsThen = [&, then = std::move(then)]() {
        linkupCancelInvite();
        linkupScreen.enter();
        then();
    };
}

// Nobody may be watching by now, so these stay until a press rather than as a pill.
void App::linkupWaitEndsWith(const std::string& words) {
    linkupWaitEnds([&, words]() {
        linkupPanel(words, "", {}, 0, [](int) {});
        choiceScreen.setPrompt("Press", "A", "to continue");
        choiceScreen.setOnlyA(true);
    });
}

void App::linkupDeclinedBy(const std::string& who) { linkupWaitEndsWith(linkup::declined(who)); }

void App::linkupNoAnswerFrom(const std::string& who) { linkupWaitEndsWith(linkup::noAnswer(who)); }

// The server refused the invite: they stopped being Online while it was picked, it
// crossed with theirs, or they joined someone else first.
void App::linkupNotAvailable(const std::string& who) { linkupWaitEndsWith(linkup::notAvailable(who)); }

// While an invite waits, the screen dims as it does over a game and never turns off: the
// person may have walked away, and is coming back to a game.
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
    menuNotice.ownDot = true;
    menuNotice.dot = screens::LinkUpScreen::dotColour(linkUp.friends, who);
    sound::playInvite();
    menuNotice.life = 6.0f;
    menuNotice.holdUntilPress = true;
}

void App::linkupAnswerInvite(const linkup::Friend& f) {
    sound::play(sound::Cue::Activate);
    linkupAnswering = f.name;
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
        if (!linkupGallery) linkupSendDecline(f);
        refreshLinkUpScreen();
    });
}

// The inviter cancelled, or the wait ran out. Their Invited line goes, and on the Join
// or Decline panel the box fades out and another says so until A: the person may have
// walked off, and a press meant for Join must not land on the list under it. The list
// waits for that press, so nothing changes behind the panel while it swaps.
void App::linkupInviteEnded(const std::string& who) {
    auto lineGoes = [&, who]() {
        for (linkup::Friend& f : linkUp.friends) {
            if (f.name == who && f.status == linkup::Status::InvitedYou) {
                f.status = linkup::Status::Online;
                f.game.clear();
            }
        }
        refreshLinkUpScreen();
    };
    if (linkupAnswering != who || !choiceScreen.isOpen()) {
        lineGoes();
        return;
    }
    linkupAnswering.clear();
    constexpr float kBoxLeaves = 0.28f;
    choiceThen = nullptr;
    choiceScreen.fadeAway(kBoxLeaves);
    linkupWaitEndsIn = kBoxLeaves + kWaitEndsGap;
    linkupWaitEndsThen = [&, who, lineGoes]() {
        linkupPanel(linkup::inviteEnded(who), "", {}, 0, [lineGoes](int) { lineGoes(); });
        choiceScreen.holdDim();
        choiceScreen.setPrompt("Press", "A", "to continue");
        choiceScreen.setOnlyA(true);
    };
}

void App::linkupJoin(const linkup::Friend& f) {
    sound::play(sound::Cue::Activate);
    // The pill has said what it came to say.
    if (menuNotice.text == linkup::invitedYou(f.name)) menuNotice.text.clear();
    linkupHost = f.name;
    linkupGame = f.game;
    linkupPlay.open(screens::LinkUpPlayScreen::Shows::Waiting, linkup::waitingFor(f.name), f.game,
                    /*friendSide=*/true);
    stack.push_back(Screen::LinkUpPlay);
    if (linkupGallery) {
        linkupGallerySession = true;
        linkupGalleryJoined(f.game);
    } else {
        linkupSendJoin(f);
    }
}

// The friend's pause button, and B on a waiting screen: over the game, Paused, which
// stops it for both; with no game to pause, only the leaving, starting on the harmless one.
void App::linkupAskLeave() {
    sound::play(sound::Cue::Activate);
    if (!linkupPlay.friendSide()) {
        // In the gallery B stands in for the pause menu's Quit. In a session the person hosting
        // reaches this only past the join, waiting on a download: B there ends the session.
        if (!linkupGallery && linkupSide == LinkupSide::Host) {
            linkupCancelInvite();
            return;
        }
        if (linkupGallerySession) {
            linkupGameQuit();
            return;
        }
        if (stack.size() > 1) stack.pop_back();
        refreshLinkUpScreen();
        return;
    }
    // On a waiting screen there is no game to pause, only the leaving its prompt offers.
    if (linkupPlay.shows() != screens::LinkUpPlayScreen::Shows::Picture) {
        linkupPanel(linkup::leaveQuestion(linkupHost), "", {"Leave", "Cancel"}, 1, [&](int c) {
            if (c != 0) return;
            if (!linkupGallery) {
                linkupFriendLeaves();
                return;
            }
            linkupGallerySession = false;
            if (here() == Screen::LinkUpPlay && stack.size() > 1) stack.pop_back();
            refreshLinkUpScreen();
        });
        return;
    }
    if (!linkupGallery) {
        linkupFriendAsksPause();
        return;
    }
    // The game stops for both of you while this is up; the other console says so.
    linkupPanel("Paused", "", {"Resume", "Leave"}, 0, [&](int c) {
        if (c != 1) return;
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
    choiceScreen.setOnlyA(true);
}
