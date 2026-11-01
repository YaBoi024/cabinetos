// Input: every event the frame loop takes, in the order that decides who
// it belongs to (the gate, then the controllers' four interceptions, then
// the keyboard's and the pad's ladders), the held direction and letter,
// and what a keyboard's commit or cancel means (#398, restructure batch g).

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "power.h"
#include "saves.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::holdNav(screens::Nav n) {
    navHeld = true;
    heldNav = n;
    heldFor = 0.0f;
    nextRepeat = kRepeatDelay;
}

void App::releaseNav(screens::Nav n) {
    // Only the direction that is actually held releases it. Letting go of
    // Left while holding Down must not stop the Down.
    if (navHeld && heldNav == n) navHeld = false;
}

// EVERYTHING HELD IS LET GO when presses stop reaching this loop and when
// they start again: a game in an emulator of its own takes every event,
// releases included, so A held into one left Home's cover pressed in after
// it, a held direction ran on by itself and a held trigger's next pull did
// nothing (#398, PLAY-14).
void App::letGoOfHeld() {
    pressing = false;
    navHeld = false;
    letterHeld = 0;
    l2Held = r2Held = false;
}

// What a commit or a cancel MEANS, which is not the same on every screen.
// On Search, done goes up into the results and cancel leaves the screen —
// and cancel closing the keyboard while leaving an empty Search behind it
// would be a dead end with no way out but the bar.
void App::keyboardResult(ui::KeyboardResult res) {
    // Done is a choice and Cancel a step back (#389); Search sounds its
    // own Cancel below, where it goes Home.
    if (res == ui::KeyboardResult::Committed) sound::play(sound::Cue::Activate);
    if (res == ui::KeyboardResult::Cancelled && here() != Screen::Search)
        sound::play(sound::Cue::Back);
    if (here() != Screen::Search) {
        if (res == ui::KeyboardResult::Typing || !keyboardThen) return;
        auto then = std::move(keyboardThen);
        keyboardThen = nullptr;
        then(res);
        return;
    }
    searchKeyboardResult(res);
}

// One event, after an emulator of its own has had its look at it
// (takeStandaloneEvent). A return ends the event, as `continue` did in the
// frame loop's poll.
void App::handleEvent(const SDL_Event& e) {
    const InputOwner owner = inputOwner();
    // ANY TOUCH WAKES THE SCREEN, AND ON A DARK SCREEN THAT IS ALL IT
    // DOES. The press that lights a dimmed or blank Home is swallowed,
    // so it cannot also launch whatever had focus, which nobody could
    // see. A WAKE IS NOT A CHOICE (idle.h, #411): after the screen
    // comes back from off, the machine from Rest, or a device starts
    // streaming, every press is swallowed for the first seconds too. A
    // running game is the exception: its pad is read by state rather
    // than by these events, so nothing is lost from play.
    bool touched = false;
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            touched = true;
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            touched = std::abs(static_cast<int>(e.gaxis.value)) > idle::kAxisDeadzone;
            break;
        default:
            break;
    }
    // WHO PRESSED, for a press that starts a game (refuseStartWithoutController).
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        lastPressFromRemote = wiiremote::isRemote(e.gbutton.which);
    else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        lastPressFromRemote = false;
    // The power and Sleep keys are never swallowed: on a dark screen
    // they light it AND open the Power menu, which is what the person
    // reaching for that button wants.
    const bool powerKey = e.type == SDL_EVENT_KEY_DOWN &&
                          (e.key.key == SDLK_POWER || e.key.key == SDLK_SLEEP);
    if (touched) playClock.touch();
    // ON THE WAY DOWN NOTHING TAKES A PRESS (#393): the picture is fading
    // to black over Restart or Power off, and the machine goes when it has.
    if (poweringDown && (touched || powerKey)) return;
    // Rest is asked here, at the press, not by the frame: the press that
    // woke the machine is handled before the frame notices the wake.
    if (touched && (idleWatch.input(clockSeconds()) || power::justWoke()) &&
        owner != InputOwner::Game && !powerKey)
        return;
    // THE PRESS THAT ENDS THE WAIT after a state loads: A, as the
    // prompt says (Return on a keyboard). It was any button, and
    // MMagTech: "it says press A to continue and any button works".
    // Every other press does nothing while it waits. None goes further;
    // the frame read keeps them from the game until let go, as it does
    // the pause menu's. L3 and R3 still reach the pause menu below.
    if (stateHold && playing && !overlayOpen &&
        (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
         (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !powerKey))) {
        const bool stick = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                           (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK ||
                            e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        const bool a = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) ||
                       (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN);
        if (a) {
            stateHold = false;
            stateHoldJustEnded = true;
            std::fprintf(stderr, "[state] continuing\n");
        }
        if (!stick) return;
    }
    if (powerKey) {
        // The press that woke the machine from Rest arrives here too,
        // and must not wake it straight into this menu.
        if (power::justWoke())
            std::fprintf(stderr, "[power] ignoring the press that woke the machine\n");
        else
            openPowerMenu();
        return;
    }
    if (interceptControllers(e)) return;
    switch (e.type) {
        case SDL_EVENT_QUIT:
            running = false;
            askedToStop = true;
            std::fprintf(stderr, "[shutdown] asked to stop%s\n",
                         playing ? " with a game running" : "");
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            players::added(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED: {
            // A PLAYING PAD DROPPING OUT PAUSES THE GAME, as on a
            // Switch or an Apple TV (MMagTech, 2026-09-26): its
            // player would otherwise be run over while they find the
            // charger. Any pad can resume.
            const int was = players::playerOf(e.gdevice.which);
            players::removed(e.gdevice.which);
            if (playing && !overlayOpen && was >= 0) {
                std::fprintf(stderr, "[players] player %d's pad went off: pausing\n",
                             was + 1);
                toggleOverlay();
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            // A physical keyboard types into the same field. Not a
            // separate path — the same string and the same commit.
            if (keyboard.isOpen()) keyboard.typeText(e.text.text);
            break;
        case SDL_EVENT_KEY_DOWN:
            // A HELD KEY CHOOSES ONCE, as a pad's A and B do (#398,
            // PLAY-04). The system's key repeat sent a held Return,
            // Space, Escape or Backspace again and again, and each
            // repeat reached whatever the first press had opened:
            // Return held on Exit to Home quit the game and started it
            // again from Home. Backspace still repeats where it
            // deletes, on the on-screen keyboard and the PIN pad. The
            // arrows hold the console's own way, below.
            if (e.key.repeat) {
                const SDL_Keycode k = e.key.key;
                const bool deletes = owner == InputOwner::Keyboard ||
                                     (pinScreen.isOpen() && owner == InputOwner::UI);
                // And `[` and `]`, which moved along the bar again and
                // again, and F5, which wrote a save state at every
                // repeat (#484).
                if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE ||
                    k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET || k == SDLK_F5 ||
                    k == SDLK_F8 || (k == SDLK_BACKSPACE && !deletes))
                    break;
            }
            // A physical keyboard's digits go into the PIN pad, the
            // same field the controller fills.
            if (pinScreen.isOpen() && owner == InputOwner::UI) {
                const SDL_Keycode k = e.key.key;
                char d = 0;
                if (k >= SDLK_0 && k <= SDLK_9) d = static_cast<char>('0' + (k - SDLK_0));
                else if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
                    d = static_cast<char>('1' + (k - SDLK_KP_1));
                else if (k == SDLK_KP_0) d = '0';
                if (d) { pinOutcome(pinScreen.typeDigit(d)); break; }
                if (k == SDLK_BACKSPACE) { pinScreen.deleteDigit(); break; }
                if (k == SDLK_ESCAPE) { navigate(screens::Nav::Back); break; }
            }
            if (owner == InputOwner::Keyboard) {
                // While it owns input it takes every key, the same way
                // the core owns the pad while a game runs. A control
                // that means two things at once is the bug.
                switch (e.key.key) {
                    case SDLK_LEFT: keyboard.moveFocus(-1, 0); break;
                    case SDLK_RIGHT: keyboard.moveFocus(+1, 0); break;
                    case SDLK_UP: keyboardUp(); break;
                    case SDLK_DOWN: keyboard.moveFocus(0, +1); break;
                    case SDLK_BACKSPACE: keyboard.backspace(); break;
                    case SDLK_RETURN:
                        // ITS LENGTH, NEVER THE TEXT: a Wi-Fi or
                        // RetroAchievements password reached the log, and a
                        // diagnostic report on a public issue with it.
                        std::fprintf(stderr, "[keyboard] committed %zu characters\n",
                                     keyboard.value().size());
                        keyboardResult(keyboard.commit());
                        break;
                    case SDLK_ESCAPE: keyboardResult(keyboard.cancel()); break;
                    default: break;
                }
                break;
            }
            if (e.key.key == SDLK_ESCAPE) {
                // In a game, Escape is the overlay — not a quit. A
                // download is the one thing it should interrupt.
                if (launchJob.busy()) launchJob.cancel = true;
                // A question over the pause menu (Change disc) goes
                // back first, as B does.
                else if (playing && overlayOpen && choiceScreen.isOpen())
                    navigate(screens::Nav::Back);
                else if (playing) toggleOverlay();
                // Back, while there is anywhere to go back to. Quitting
                // from the middle of the Library would throw away the
                // whole stack the person had walked down.
                else if (overlayOpen) closeOverlay();
                // A question or Downloads open over any screen, Home
                // included, takes Escape as B does: Back. On Home it
                // opened the Power menu over the panel instead, so a
                // keyboard could not cancel Steam's install (#223).
                else if (choiceScreen.isOpen() || downloadsPanel.isOpen())
                    navigate(screens::Nav::Back);
                else if (stack.size() > 1) navigate(screens::Nav::Back);
                // At the root, Escape is Start: the Power menu.
                else openPowerMenu();
            }
            // The shoulders, for a machine with no pad attached. `[`
            // and `]` sit where L1 and R1 do on a controller and the
            // capture tooling can drive them.
            if (owner == InputOwner::UI) {
                if (e.key.key == SDLK_LEFTBRACKET && !shoulderOnPage(-1))
                    switchDestination(-1);
                if (e.key.key == SDLK_RIGHTBRACKET && !shoulderOnPage(+1))
                    switchDestination(+1);
                // The triggers' equivalent, where a pad is not attached.
                if (here() == Screen::Grid) {
                    if (e.key.key == SDLK_COMMA) gridScreen.jumpLetter(-1);
                    if (e.key.key == SDLK_PERIOD) gridScreen.jumpLetter(+1);
                }
                if (here() == Screen::Library) {
                    if (e.key.key == SDLK_COMMA) libraryScreen.jumpGroup(-1);
                    if (e.key.key == SDLK_PERIOD) libraryScreen.jumpGroup(+1);
                }
            }
            if (owner == InputOwner::Overlay && choiceScreen.isOpen()) {
                switch (e.key.key) {
                    case SDLK_UP: navigate(screens::Nav::Up); break;
                    case SDLK_DOWN: navigate(screens::Nav::Down); break;
                    case SDLK_LEFT: navigate(screens::Nav::Left); break;
                    case SDLK_RIGHT: navigate(screens::Nav::Right); break;
                    case SDLK_RETURN:
                    case SDLK_SPACE: navigate(screens::Nav::Activate); break;
                    default: break;
                }
                break;
            }
            if (owner == InputOwner::Overlay) {
                if (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN) {
                    ovStep(e.key.key == SDLK_DOWN ? 1 : -1);
                    overlayFocus.retarget(0.0f, 0.0f);
                    overlayFocus.elapsed = 0.0f;
                    overlayFocus.retarget(1.0f, kOverlayFocusDuration);
                }
                if ((e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT) && !powerMenu &&
                    choiceAt(overlaySlot))
                    stepChoice(overlaySlot, e.key.key == SDLK_RIGHT ? 1 : -1, false);
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)
                    overlayActivate();
                break;
            }
            // F5 writes a state, F8 restores the newest one THIS build can
            // load, F6 pushes the game's own save. Quitting syncs the
            // save by itself; F6 is for testing without quitting.
            // Not while a state picked on the game's page is still on its way
            // (#484, PLAY-33): a save then is of the game's first frames, and
            // the next Resume would start there. The shortcut path refuses
            // the same way (play.cpp).
            if (playing && session.snapshots && !stateHoldWaiting && e.key.key == SDLK_F5)
                saveStateNow(session, uploader, menuNotice);
            if (playing && session.snapshots && !stateHoldWaiting && e.key.key == SDLK_F8)
                beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
            if (playing && e.key.key == SDLK_F6) syncSave(session, uploader);
            if (owner != InputOwner::UI) break;
            // THROUGH `navigate`, NOT STRAIGHT AT A SCREEN. Every key on
            // every screen arrives at one place, so the bar can be
            // offered it first wherever it holds focus.
            // `e.key.repeat` is the system's own key repeat, at the
            // system's own rate. Ignore it and run the same accelerating
            // repeat the pad gets, on every screen as the pad does, so
            // the two input devices behave identically: the rule the
            // whole input model is built on. Until #398 (PLAY-03) only
            // Home's arrows held this way.
            if (!e.key.repeat) {
                if (e.key.key == SDLK_LEFT) {
                    navigate(screens::Nav::Left); holdNav(screens::Nav::Left);
                }
                if (e.key.key == SDLK_RIGHT) {
                    navigate(screens::Nav::Right); holdNav(screens::Nav::Right);
                }
                if (e.key.key == SDLK_UP) {
                    navigate(screens::Nav::Up); holdNav(screens::Nav::Up);
                }
                if (e.key.key == SDLK_DOWN) {
                    navigate(screens::Nav::Down); holdNav(screens::Nav::Down);
                }
            }
            // Anything pushed on top of Home owns its own focus model,
            // so the press goes there and this function does not get an
            // opinion about it. Home is handled below because it is the
            // one screen whose focus lives here.
            if (here() != Screen::Home) {
                switch (e.key.key) {
                    case SDLK_RETURN:
                    case SDLK_SPACE: navigate(screens::Nav::Activate); break;
                    case SDLK_BACKSPACE: navigate(screens::Nav::Back); break;
                    default: break;
                }
                break;
            }
            if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
                pressing = true;
                navigate(screens::Nav::Activate);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (e.key.key == SDLK_LEFT) releaseNav(screens::Nav::Left);
            if (e.key.key == SDLK_RIGHT) releaseNav(screens::Nav::Right);
            if (e.key.key == SDLK_UP) releaseNav(screens::Nav::Up);
            if (e.key.key == SDLK_DOWN) releaseNav(screens::Nav::Down);
            if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) pressing = false;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            // THE SHOULDERS LEAVE SEARCH, keyboard or not. MMagTech,
            // 2026-09-24: R1 landed on Search and then neither shoulder
            // got you off it, because the docked keyboard took every
            // button. Only Search's docked keyboard: a keyboard asking
            // a question (a Wi-Fi password) is modal and keeps them.
            // Leaving clears the search, as entering any destination
            // starts it fresh.
            if (owner == InputOwner::Keyboard && here() == Screen::Search &&
                (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ||
                 e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                switchDestination(
                    e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ? -1 : +1);
                break;
            }
            // X TAKES A DIGIT OFF THE PIN, as it takes a letter off
            // the on-screen keyboard. B leaves the pad.
            if (pinScreen.isOpen() && owner == InputOwner::UI &&
                e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                pinScreen.deleteDigit();
                break;
            }
            if (owner == InputOwner::Keyboard) {
                switch (e.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: keyboard.moveFocus(-1, 0); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: keyboard.moveFocus(+1, 0); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_UP: keyboardUp(); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: keyboard.moveFocus(0, +1); break;
                    case SDL_GAMEPAD_BUTTON_SOUTH: keyboardResult(keyboard.pressKey()); break;
                    case SDL_GAMEPAD_BUTTON_WEST: keyboard.backspace(); break;
                    case SDL_GAMEPAD_BUTTON_NORTH: keyboard.toggleShift(); break;
                    case SDL_GAMEPAD_BUTTON_START:
                        // ITS LENGTH, NEVER THE TEXT: a Wi-Fi or
                        // RetroAchievements password reached the log, and a
                        // diagnostic report on a public issue with it.
                        std::fprintf(stderr, "[keyboard] committed %zu characters\n",
                                     keyboard.value().size());
                        keyboardResult(keyboard.commit());
                        break;
                    case SDL_GAMEPAD_BUTTON_EAST:
                        keyboardResult(keyboard.cancel()); break;
                    default: break;
                }
                break;
            }
            // BOTH STICK CLICKS, NOT START. Start is the pause button
            // on nearly every system this console emulates, and taking
            // it meant a game could never be paused — MMagTech, on the
            // A9, 2026-09-19, before playing the first real game on it.
            //
            // L3+R3 was Cabinet's default and its reasoning carries:
            // RetroArch's alternative to Select+Start, chosen because
            // analog trigger pairs collide with real gameplay —
            // braking and accelerating together in a racing game is
            // the obvious case — "while clicking both sticks at once
            // has no gameplay meaning in anything this app runs".
            //
            // Still hardcoded here. Cabinet made it remappable and
            // GLOBAL rather than per-controller, on the grounds that
            // the risk comes from the game and not the pad model, and
            // lets the second button be cleared for single-button
            // mode. That belongs with the remapping screen.
            //
            // The clicks themselves wait a moment before a game hears
            // one alone (play.cpp, App::kStickPairMs), so the pair
            // reaches only the menu.
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK)
                l3Down = true;
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                r3Down = true;
            if (l3Down && r3Down && (playing || overlayOpen)) {
                l3Down = r3Down = false;   // one toggle per pair, not per frame
                toggleOverlay(e.gbutton.which);
                break;
            }
            if (owner == InputOwner::Overlay && choiceScreen.isOpen()) {
                switch (e.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_UP: navigate(screens::Nav::Up); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: navigate(screens::Nav::Down); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: navigate(screens::Nav::Left); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: navigate(screens::Nav::Right); break;
                    case SDL_GAMEPAD_BUTTON_SOUTH: navigate(screens::Nav::Activate); break;
                    case SDL_GAMEPAD_BUTTON_EAST: navigate(screens::Nav::Back); break;
                    default: break;
                }
                break;
            }
            if (owner == InputOwner::Overlay) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) ovStep(-1);
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) ovStep(+1);
                if ((e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                     e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) &&
                    !powerMenu && choiceAt(overlaySlot))
                    stepChoice(overlaySlot,
                               e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 1 : -1,
                               false);
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) overlayActivate();
                // East is Back, and Back from the overlay is Resume —
                // or, from the Power menu on Home, just closing it.
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) closeOverlay();
                // Start closes the Power menu it opened on Home.
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START && powerMenu &&
                    !playing)
                    closeOverlay();
                break;
            }
            if (owner != InputOwner::UI) break;
            // The shoulders first, because they are the one input that
            // means the same thing on every screen that has them.
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
                if (!shoulderOnPage(-1)) switchDestination(-1);
                break;
            }
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                if (!shoulderOnPage(+1)) switchDestination(+1);
                break;
            }
            // ONE DIRECTION HANDLER FOR EVERY SCREEN — 2026-09-21.
            //
            // There were three: Left and Right were handled early and
            // split by screen, Up and Down were handled once for the
            // pushed screens and again for Home, and only Home's copy
            // ever started the hold. So `holdNav` existed and MMagTech
            // was right that *"holding down isnt working"* — it worked
            // on Home and nowhere else, which is the worst kind of
            // working. Everything below is one switch, and there is now
            // no way to add a direction to one screen and forget it on
            // another.
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    navigate(screens::Nav::Left);
                    holdNav(screens::Nav::Left);
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    navigate(screens::Nav::Right);
                    holdNav(screens::Nav::Right);
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    navigate(screens::Nav::Up);
                    holdNav(screens::Nav::Up);
                    break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    navigate(screens::Nav::Down);
                    holdNav(screens::Nav::Down);
                    break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    navigate(screens::Nav::Activate);
                    pressing = true;
                    break;
                // East is Back everywhere in this product, which is the
                // rule the in-game overlay already follows. On Home it
                // is the root and `homeKey` answers with the edge cue.
                case SDL_GAMEPAD_BUTTON_EAST:
                    navigate(screens::Nav::Back);
                    break;
                case SDL_GAMEPAD_BUTTON_START:
                    // START ON HOME IS THE POWER MENU — decided
                    // 2026-09-22. It used to quit the frontend, a
                    // development leftover that just got the session
                    // restarted under whoever was watching.
                    // AND ON EVERY SCREEN OUTSIDE A GAME since #223
                    // (MMagTech, 2026-10-04): it only did on Home,
                    // which nothing explained, and the menu is now
                    // Steam's door too. Not over the PIN pad, a
                    // question or the Downloads panel. NOT ON SEARCH:
                    // its keyboard is always up and is drawn over
                    // everything, so the menu opened under it (on the
                    // TV, 2026-10-04), and there Start is the
                    // keyboard's "done", as its legend says. The rule
                    // is: Start opens the menu except while typing.
                    // On Search with the keyboard put away (the results
                    // focused), nobody is typing, so it is the menu too
                    // (#484). With the results or the bar focused and the
                    // keyboard still showing (it takes the keys only while
                    // focus is in it, inputOwner), Start is the keyboard's
                    // "done", as its legend still says: it goes, and the
                    // next Start is the menu. It did nothing (#484).
                    if (pinScreen.isOpen() || choiceScreen.isOpen() ||
                        downloadsPanel.isOpen())
                        break;
                    if (here() == Screen::Search && keyboard.isOpen())
                        keyboardResult(keyboard.commit());
                    else
                        openPowerMenu();
                    break;
                default: break;
            }
            break;
        // THE TRIGGERS ARE FAST NAVIGATION BY LETTER, and they are an
        // AXIS rather than a button: a trigger reports how far it is
        // pulled, from 0 to 32767, so it has to be edge-detected here
        // or one pull would fire a hundred jumps on its way down.
        //
        // The threshold is deliberately high and the release threshold
        // deliberately lower. A trigger at rest on a worn pad does not
        // read zero, and a single threshold turns that into a jump
        // every time the pad is picked up.
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const bool left = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            const bool right = e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            if (!left && !right) break;
            bool& held = left ? l2Held : r2Held;
            const int dir = left ? -1 : +1;
            const int v = e.gaxis.value;
            // A RELEASE COUNTS WHOEVER OWNS INPUT. A trigger let go
            // during a game used to stay "held", and the next pull
            // back on the grid did nothing.
            if (held && v < 8000) {
                held = false;
                if (letterHeld == dir) letterHeld = 0;
            } else if (!held && v > 20000 && owner == InputOwner::UI) {
                held = true;
                if (here() == Screen::Grid) {
                    gridScreen.jumpLetter(dir);
                    letterHeld = dir;
                    letterHeldFor = 0.0f;
                    letterNext = kRepeatDelay;
                }
                // Person to person in Collections (#352); one jump a
                // pull, since a household has a handful of people.
                if (here() == Screen::Library) libraryScreen.jumpGroup(dir);
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                    releaseNav(screens::Nav::Left); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                    releaseNav(screens::Nav::Right); break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    releaseNav(screens::Nav::Up); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    releaseNav(screens::Nav::Down); break;
                default: break;
            }
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK)
                l3Down = false;
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                r3Down = false;
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) pressing = false;
            break;
        default:
            break;
    }
}

// Every frame: a held direction, and a held trigger on a grid, repeat.
void App::repeatHeld(float dt) {
    // The held direction, repeating and speeding up. Driven from the frame
    // loop rather than from the event queue, because the pad sends nothing
    // at all while a button is held down.
    if (navHeld && inputOwner() == InputOwner::UI && !keyboard.isOpen()) {
        heldFor += dt;
        if (heldFor >= nextRepeat) {
            navigate(heldNav);
            const float t = std::clamp(heldFor / kRepeatRamp, 0.0f, 1.0f);
            nextRepeat = heldFor + (kRepeatStart + (kRepeatFast - kRepeatStart) * t);
        }
    } else {
        navHeld = false;
    }
    // The held trigger, a letter at a time at a steady pace: each jump
    // can cross dozens of games, so it does not speed up the way a held
    // direction does.
    if (letterHeld != 0 && inputOwner() == InputOwner::UI && here() == Screen::Grid) {
        letterHeldFor += dt;
        if (letterHeldFor >= letterNext) {
            if (gridScreen.jumpLetter(letterHeld)) letterNext = letterHeldFor + kLetterRepeat;
            else letterHeld = 0;
        }
    } else {
        letterHeld = 0;
    }
}
