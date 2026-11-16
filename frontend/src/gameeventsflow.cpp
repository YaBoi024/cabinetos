// Settings > System > Game events (#716): the row, the keyboard and its
// check, the panel, in serveraddressflow.cpp's shape; and the App's side of
// each message: who it is about, which game, the pause menu followed once a
// frame, and a LinkUp friend's game. The messages, the rules and the sender
// are gameevents.cpp.

#include "app.h"

#include <cstdio>

#include "catalog.h"
#include "gameevents.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// Off (no address yet, or turned off), or its host.
screens::SettingsRow App::gameEventsRow() {
    const std::string a = gameevents::address();
    return {screens::SettingsRow::Kind::Action, SetGameEvents, "Game events", "",
            a.empty() || gameevents::turnedOff() ? "Off" : gameevents::hostOf(a)};
}

void App::startGameEventsAtBoot() { gameevents::start(); }

void App::pressGameEvents() {
    // PIN FIRST, then the keyboard until an address is typed, or the panel:
    // the RomM server row's shape. Turn off keeps the address, so Turn on
    // needs no typing.
    askPin("Enter the PIN", "To change Game events", [&]() {
        const std::string addr = gameevents::address();
        if (addr.empty()) {
            askGameEventsAddress("http://", "");
            return;
        }
        const bool off = gameevents::turnedOff();
        askChoice(addr, "", {off ? "Turn on" : "Turn off", "Change address", "Cancel"}, 0,
                  [&, addr, off](int i) {
            if (i == 0) {
                if (off) gameevents::turnOn();
                else gameevents::turnOff();
                buildSettings();
            } else if (i == 1) {
                askGameEventsAddress(addr, "");
            }
        });
    });
    sound::play(sound::Cue::Activate);
}

void App::askGameEventsAddress(const std::string& typed, const std::string& why) {
    ui::Keyboard::Config cfg;
    cfg.title = "Game events";
    cfg.initial = typed;
    cfg.placeholder = "http://n8n.local:5678/webhook/games";
    cfg.shortcuts = {".local", "/webhook/"};
    keyboard.open(cfg);
    if (!why.empty()) keyboard.sayInField(why, /*problem=*/true, /*keep=*/true);
    keyboardThen = [&, cfg](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        std::string addr = trimmedAddress(keyboard.value());
        if (!addr.empty() && addr.find("://") == std::string::npos) addr = "http://" + addr;
        // Nothing past http://: nothing to try.
        if (gameevents::hostOf(addr).empty()) return;
        ui::Keyboard::Config again = cfg;
        again.initial = addr;
        keyboard.open(again);
        keyboard.sayInField("Checking\xE2\x80\xA6", /*problem=*/false, /*keep=*/true);
        keyboard.setBusy(true);
        geKeyboardFor = addr;
        keyboardThen = [&](ui::KeyboardResult) { geKeyboardFor.clear(); };
        // One left from a B a moment ago is still going: this address is
        // tried when it ends (takeGameEventsCheck).
        if (!geCheck.busy()) startGameEventsCheck(addr);
    };
}

void App::startGameEventsCheck(const std::string& address) {
    geCheckFor = address;
    geCheckAt = SDL_GetTicks();
    geCheck.start([address](gameevents::Tried& out, std::string&) {
        out = gameevents::tryAddress(address);
        return true;
    });
}

void App::takeGameEventsCheck() {
    // "CHECKING…" STAYS UP 700 MS AT LEAST, as the server row's does.
    if (geCheck.busy() || SDL_GetTicks() - geCheckAt < 700) return;
    gameevents::Tried t;
    if (!geCheck.take(&t, nullptr, nullptr)) return;
    // B while it checked means leave it alone, whatever it found; a new
    // address typed meanwhile is tried now.
    if (!keyboard.isOpen() || !keyboard.busy() || geKeyboardFor.empty()) return;
    if (geKeyboardFor != geCheckFor) {
        startGameEventsCheck(geKeyboardFor);
        return;
    }
    const std::string addr = geKeyboardFor;
    geKeyboardFor.clear();
    if (t.answer == gameevents::Answer::Ok) {
        gameevents::setAddress(addr);
        keyboard.cancel();
        keyboardThen = nullptr;
        buildSettings();
    } else {
        askGameEventsAddress(addr, gameevents::why(t));
    }
}

// ---- The messages' side in the App ---------------------------------------------

gameevents::Who App::gameEventsWho() {
    // LinkUp as the person set it, so an app that tells friends can stay
    // quiet while they appear Offline.
    const char* linkup = !linkUp.on || !linkUp.paired ? "unused"
                         : linkUp.appearOffline       ? "offline"
                                                      : "online";
    return {storage::currentUser().name, linkup};
}

// Beside playClock.begin: a game on screen, a core's or an emulator's.
void App::gameEventStarted() {
    if (!gameevents::on()) return;
    gameevents::Game g;
    g.title = session.title;
    g.romId = session.romId;
    // A game started while this console hosts a LinkUp session is the one
    // both play (linkupStartGame).
    g.linkupHost = linkupSide == LinkupSide::Host;
    for (const romm::Game& lib : games) {
        if (lib.id != session.romId) continue;
        g.system = catalog::systemName(lib);
        g.covers = {lib.coverLargePath, lib.coverPath};
        break;
    }
    gameevents::started(g, gameEventsWho());
}

// Beside playtime::close, however the game was left: Exit to Home, Sleep,
// Restart and Power off all leave through finishExit or finishStandalone.
void App::gameEventEnded(int64_t playedMs) {
    if (gameevents::inGame(/*friendSide=*/false)) gameevents::ended(playedMs, gameEventsWho());
}

// Once a frame: the pause menu over a game, a core's or an emulator's. A
// quit waiting on a core still booting is the menu still up, so Exit to Home
// never says resumed before it says ended.
void App::followGameEventsPause() {
    if (!gameevents::on() || !gameevents::inGame(/*friendSide=*/false)) return;
    const bool menu = (playing && (overlayOpen || exitPending)) || standalonePaused;
    if (menu != gameevents::pausedNow()) gameevents::paused(menu, gameEventsWho());
}

// Each pop-up taken off ra::takePopup, shown or dropped as the game is left.
void App::gameEventUnlocked(const ra::Popup& p) {
    if (!gameevents::on()) return;
    if (p.complete) gameevents::completed(gameEventsWho());
    else gameevents::unlocked(p.title, p.points, gameEventsWho());
}

// A LINKUP FRIEND'S CONSOLE follows its own setup: the host's `starting`
// word is its game_started, to its own address under its own name, with only
// the game's name, which is all it has.
void App::gameEventFriendStarted() {
    if (!gameevents::on()) return;
    gameEventFriendEnded();
    gameevents::Game g;
    g.title = linkupGame;
    g.friendSide = true;
    gameevents::started(g, gameEventsWho());
}

// The host's `quit`, or the session ending with a game on.
void App::gameEventFriendEnded() {
    if (gameevents::inGame(/*friendSide=*/true)) gameevents::ended(0, gameEventsWho());
}
