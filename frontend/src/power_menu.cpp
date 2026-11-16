// The Power menu and the power button: Sleep, Restart and Power off, the
// fade to black before the last two (#393), the game left first, the
// button borrowed at start, and logind's warnings answered every frame
// (#398, restructure batch f).

#include "app.h"

#include <cstdio>

#include "core.h"
#include "power.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::powerActivate(PowerItem item) {
    if (item == PwResume || item == PwCancel) {
        // A WII REMOTE GAME DURING A STREAM STAYS PAUSED (#473, PLAY-18;
        // MMagTech's rule of 2026-10-07, pausemenu.cpp): back to the pause
        // menu, which has no Resume, rather than into the game.
        if (remoteOnlyHere()) {
            backToPauseMenu();
            return;
        }
        closeOverlay();
        return;
    }
    if (item == PwSteam) {
        powerCue(sound::Cue::Activate);
        closeOverlay(false);
        steamPressed();
        return;
    }
    const power::Action act = item == PwRest       ? power::Action::Rest
                              : item == PwRestart  ? power::Action::Restart
                              : item == PwExitApp  ? power::Action::ExitApp
                                                   : power::Action::PowerOff;
    std::fprintf(stderr, "[power] %s chosen%s\n", power::name(act),
                 playing || standalonePaused ? " from a game; leaving it first" : "");
    if (act == power::Action::Rest) {
        // Sleep clicks like any choice.
        powerCue(sound::Cue::Activate);
        powerLeave(act);
        return;
    }
    // RESTART AND POWER OFF FADE TO BLACK FIRST, IN SILENCE (#393; no
    // sound on the way down, ever, #389). Over whatever is on the screen,
    // this menu and a paused game with it, so the game is still paused
    // under the black and is left behind it once the fade is over
    // (pollPower). Sleep does not fade: it comes back, and the picture
    // would then have to be brought back up on waking.
    poweringDown = true;
    powerFade = act;
    powerFadeAt = SDL_GetTicksNS();
    curtain.retarget(1.0f, kPowerFade);
}

// The game left, then the choice made: at once for Sleep, after the fade
// for Restart and Power off.
void App::powerLeave(power::Action act) {
    // A GAME STILL STARTING IS LEFT THE WAY EXIT TO HOME LEAVES IT (#473,
    // PLAY-12): unloaded while its core builds the machine, PPSSPP dies,
    // and the console started again instead of sleeping or powering off.
    // The choice waits for the core, at most exitToHome's wait (pumpExit).
    if (playing && !cab::Core::shared().running()) {
        powerAfterExit = act;
        closeOverlay(false);
        exitToHome();
        return;
    }
    // AN EMULATOR OF ITS OWN IS CLOSED FIRST (#473, PLAY-32), as Exit to
    // Home closes it, and the choice is made once it has gone and its
    // saves are queued (finishStandalone).
    if (standalonePaused && standaloneRun.active()) {
        powerAfterExit = act;
        closeOverlay(false);
        standaloneRun.stop();
        return;
    }
    if (playing) finishExit();
    closeOverlay(false);
    powerAct(act);
}

// The choice itself, once the game is left: Sleep waits for the uploads
// (waitForUploadsToPowerDown); Restart and Power off go now.
void App::powerAct(power::Action act) {
    if (act == power::Action::Rest) {
        restPending = true;
        restWaitStart = SDL_GetTicksNS();
    } else {
        // ON BLACK (#393): leaving a game lifts the curtain onto Home, and
        // the machine goes down on the fade's black, not on Home.
        curtain.settle(1.0f);
        // A restart or a power-off stops this process on its way down, and
        // the uploads just queued are drained then — PR #50.
        power::act(act);
    }
}

// From the Power menu back to the pause menu it was opened over: the pause
// menu's rows, built again as on the way in.
void App::backToPauseMenu() {
    overlayOpen = false;
    toggleOverlay();
}

// Pressed while it is already open, it closes, like Resume.
//
// OVER AN EMULATOR OF ITS OWN, PAUSED, IT IS THE MENU OVER A GAME (#473,
// PLAY-32): Resume first, no Steam, silent, as over any other game.
void App::openPowerMenu() {
    if (poweringDown) return;
    const bool game = playing || standalonePaused;
    if (overlayOpen && powerMenu) {
        if (remoteOnlyHere()) backToPauseMenu();
        else closeOverlay();
        return;
    }
    powerItems.clear();
    // THE FIRST ITEM DOES NOTHING, and it is the one focused. In a game
    // that is Resume; on Home it is Cancel. MMagTech, 2026-09-27: Start
    // pressed by accident opened the menu on Sleep, one A from asleep.
    // Over a Wii Remote game during a stream it is Cancel too: the game
    // stays paused (PLAY-18).
    powerItems.push_back(game && !remoteOnlyHere() ? PwResume : PwCancel);
    // STEAM'S DOOR (#223), never over a game. MMagTech, 2026-10-04: in
    // the Library it read as part of the ROM library, and it is not; here
    // it is a mode the machine switches into, the mirror of Steam's own
    // "Switch to Desktop". Straight under Cancel, his call: it is what
    // Start will most often be pressed for, and a slip still meets "Start
    // Steam?" or the PIN. Gone when somebody chose Hide Steam.
    if (!game && steam::available() && !steam::hidden()) powerItems.push_back(PwSteam);
    if (restAvailable) powerItems.push_back(PwRest);
    if (power::canRestartAndPowerOff()) {
        powerItems.push_back(PwRestart);
        powerItems.push_back(PwPowerOff);
    }
    if (power::canExitApp()) powerItems.push_back(PwExitApp);
    powerMenu = true;
    overlayOpen = true;
    overlaySlot = 0;
    overlayFade.retarget(1.0f, overlayFadeSeconds);
    overlayFocus.retarget(1.0f, kOverlayFocusDuration);
    overlayFocus.elapsed = kOverlayFocusDuration;
    std::fprintf(stderr, "[power] menu open%s\n", game ? " over a game" : "");
    powerCue(sound::Cue::Activate);
}

// The button is borrowed for the life of this process — power.h. Not for a
// capture, which is not the console and must not take its button.
void App::borrowPowerButton() {
    if (!shotMode) {
        power::takeButtons();
        power::takeShutdownDelay();
        restAvailable = power::canRest();
    }
    if (powerMenuDemo) {
        if (shotMode) restAvailable = power::canRest();
        openPowerMenu();
    }
}

// Every frame: a wake noticed, a Restart, Power off or Sleep that logind
// refused said, the end of the fade before Restart and Power off, and
// logind's warning that the machine is going down.
void App::pollPower() {
    // Kept current every frame, so it can tell a wake the moment one
    // happens rather than only when a key asks.
    power::justWoke();
    // A restart, power off or rest that logind refused (#241), said in the
    // pill rather than left looking frozen. Pressing again tries again.
    if (const std::string no = power::refusal(); !no.empty()) {
        // The fade's black goes, or nobody would see the pill (#393).
        if (poweringDown) {
            poweringDown = false;
            curtain.retarget(0.0f, kCurtainUp);
        }
        menuNotice.say(no, Tone::Problem);
    }
    // THE FADE IS OVER (#393): the game is left and the machine asked. On
    // the clock, not on the picture, so a frame that was not drawn delays
    // nothing.
    if (powerFade && (SDL_GetTicksNS() - powerFadeAt) / 1e9 >= kPowerFade) {
        const power::Action act = *powerFade;
        powerFade.reset();
        powerLeave(act);
    }

    // THE MACHINE IS ABOUT TO GO DOWN OR SLEEP, and logind is holding it
    // for us. Leave the game the one way games are left — finishExit, every
    // save class for every emulator — then let it go once the uploads are
    // through, or at 25 s, inside the image's 30 s allowance.
    switch (power::poll()) {
        case power::Event::GoingDown:
        case power::Event::GoingToSleep:
            std::fprintf(stderr, "[shutdown] the machine is going down%s\n",
                         playing ? "; leaving the game first" : "");
            if (playing && cab::Core::shared().running()) finishExit();
            // An emulator of its own is asked to close as its own window
            // would, which is when it writes what it has not.
            standaloneRun.stop();
            releasePending = true;
            releaseWaitStart = SDL_GetTicksNS();
            break;
        case power::Event::Woke:
        case power::Event::None:
            break;
    }
}

// Going down, or to Sleep, once the uploads are through or the wait is up.
void App::waitForUploadsToPowerDown() {
    if (releasePending) {
        const double waited = (SDL_GetTicksNS() - releaseWaitStart) / 1e9;
        if (uploader.pending() == 0 || waited > 25.0) {
            if (uploader.pending() > 0)
                std::fprintf(stderr, "[shutdown] going down with %d upload(s) still owed\n",
                             uploader.pending());
            releasePending = false;
            power::releaseDelay();
        }
    }
    if (restPending) {
        const double waited = (SDL_GetTicksNS() - restWaitStart) / 1e9;
        if (uploader.pending() == 0 || waited > 20.0) {
            if (uploader.pending() > 0)
                std::fprintf(stderr, "[power] resting with %d upload(s) still owed\n",
                             uploader.pending());
            restPending = false;
            power::act(power::Action::Rest);
        }
    }
}
