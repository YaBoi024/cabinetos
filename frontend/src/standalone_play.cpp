// A game whose emulator is its own application (#169): started with the
// console's virtual pads, its presses passed on, watched while it has the
// television, paused under the same menu, and its saves sent when it
// ends (#398, restructure batch f). And the pointer over a game that is
// touched through the console, the Vita's (#198, pointerButton).

#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "cache.h"
#include "catalog.h"
#include "dirsave.h"
#include "overlaywin.h"
#include "quality.h"
#include "rumble.h"
#include "saves.h"
#include "shortcuts.h"
#include "storage.h"
#include "vtouch.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// THE GAME IS ON THE DISK AND ITS EMULATOR IS ITS OWN APPLICATION: start
// it and let it have the television. The curtain is already down, so the
// moment between here and the emulator's first picture is black, which is
// what every launch looks like. gamescope gives the new window the screen
// by itself (measured 2026-09-28), so this window stays as it is.
void App::startStandalone(const cab::standalone::Emulator& emu) {
    const storage::User& user = storage::currentUser();
    const std::string saveDir = storage::savesDir(
        user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
    // ONE VIRTUAL CONTROLLER PER PLAYER, and at least one: the emulator
    // is given these and never the real pads (vpad.h). Without them the
    // game would start and nothing would move it, so that refuses here.
    const int pads = std::max(1, players::count());
    if (!cab::vpad::open(pads)) {
        refuseLaunch("Couldn't start this game");
        return;
    }
    bool missingKeys = false;
    std::string err;
    // THE GAME'S PICTURE QUALITY (#63), its own choice or the console's.
    const quality::Level level = quality::forGame(launchJob.romId);
    quality::logApplied(launchJob.coreName, launchJob.romId, level);
    if (!cab::standalone::prepare(emu, launchJob.entryPath, saveDir, user.name, pads, level,
                                  &missingKeys, &err)) {
        cab::vpad::close();
        if (!err.empty()) std::fprintf(stderr, "[standalone] %s\n", err.c_str());
        refuseLaunch(missingKeys ? "No " + launchJob.systemName + " " + emu.needs +
                                       " on your server"
                                 : std::string("Couldn't start this game"));
        return;
    }
    // THE SAVE COMES DOWN FIRST, as PSP's does: a zip of the game's save
    // folder, unpacked where the emulator will look. The folder inside is
    // named by the game's title ID, so the console never has to know it.
    const std::string root = cab::standalone::saveRoot(emu, saveDir);
    const char* tag = catalog::saveTag(launchJob.coreName.c_str());
    // A LAST GAME HERE THAT NEVER GOT ITS SAVES ZIPPED goes up first, so
    // the restore below keeps it (finishInterruptedSave); for an Xbox game
    // that is saves another person's start brought off the drive for this
    // person (recoverDriveSaves).
    if (cab::standalone::hasPlayingNote(saveDir))
        finishInterruptedSave(emu, saveDir, uploader);
    // AN XBOX GAME WHOSE LAST SAVES NEVER CAME OFF ITS DRIVE (the note in
    // the game's own folder): off now, as a start-up would take them, to this
    // person's folder and the server or to the folder of whoever they belong
    // to (recoverDriveSaves), since beforeStart writes this person's older
    // copy over the drive's. Only a drive that will not give them up stops
    // the game.
    if (!recoverDriveSaves(emu, launchJob.entryPath)) {
        cab::vpad::close();
        std::fprintf(stderr, "[standalone] %s still holds saves from its last game; "
                     "not started\n", launchJob.entryPath.c_str());
        refuseLaunch("Couldn't start this game");
        return;
    }
    if (!root.empty() && tag && launchJob.saves.asked)
        restoreDirSave(launchJob.saves, tag, root, launchJob.title);
    launchJob.saves = SaveFetch{};
    // AND ONTO WHERE THE EMULATOR READS THEM, when that is not a folder
    // (Xbox: its hard drive). The note says whose they are, should the
    // console go down before the game ends.
    {
        const std::string note = std::to_string(launchJob.romId) + "\n" + saveDir + "\n" +
                                 launchJob.title + "\n" + (tag ? tag : "") + "\n";
        if (!cab::standalone::beforeStart(emu, launchJob.entryPath, saveDir, user.name, note,
                                          &err)) {
            cab::vpad::close();
            std::fprintf(stderr, "[standalone] %s\n", err.c_str());
            refuseLaunch("Couldn't start this game");
            return;
        }
    }
    if (!standaloneRun.start(emu, launchJob.romPath, launchJob.entryPath, saveDir, level,
                             &err)) {
        std::string aerr;
        cab::standalone::afterEnd(emu, launchJob.entryPath, saveDir, &aerr);
        cab::standalone::finished(emu, saveDir);
        cab::vpad::close();
        std::fprintf(stderr, "[standalone] %s\n", err.c_str());
        refuseLaunch("Couldn't start this game");
        return;
    }
    // Just played, for the cache's eviction order, now that it has started
    // (#484, LAUNCH-05): a game refused above, a Switch game with no keys on
    // the server, is not, as a built-in game is touched only once it loads.
    cache::touch(launchJob.entryPath);
    // A GAME FOR THE SEATS, as one of ours is (players.h): a pad that goes off
    // keeps its player, so the next pad does not take over its controller.
    // The frame loop says so while the menu is up; while the emulator has the
    // screen that loop skips it, so it is said here.
    players::setInGame(true);
    session = GameSession{};
    session.romId = launchJob.romId;
    session.core = launchJob.coreName;
    session.platformSlug = launchJob.platformSlug;
    session.title = launchJob.title;
    session.fsStem = launchJob.fsStem;
    session.saveDir = saveDir;
    session.standaloneCore = emu.core;
    session.entryPath = launchJob.entryPath;
    if (tag) session.saveTag = tag;
    // No save states: the pause menu is Resume, Screenshot and Exit to
    // Home (open question 25; Switch fails its rule).
    session.snapshots = catalog::snapshotsAllowed(launchJob.coreName.c_str());
    // The baseline: what changes between here and the end is what the
    // game saved. See GameSession.
    if (!root.empty()) {
        session.dirSaveRoot = root;
        session.dirAtLaunch = cab::listTree(root);
        std::fprintf(stderr, "[save] %s holds %zu file(s) at launch\n", root.c_str(),
                     session.dirAtLaunch.size());
    }
    standaloneRan = 0.0f;
    if (session.romId > 0) {
        playClock.begin(session.romId, playtime::wallMs());
        playCheckpointClock = 0.0f;
    }
    // THIS WINDOW STEPS OUT OF THE WAY AND WAITS ON TOP. It becomes
    // gamescope's overlay (overlaywin.h), draws nothing, and leaves the
    // controllers with the game, so the emulator has the screen; the pause
    // menu is this same window drawing again. gamescope classifies a window
    // when it maps, hence hidden, marked, shown.
    SDL_HideWindow(window);
    cab::overlaywin::mark(window, /*takeInput=*/false);
    SDL_ShowWindow(window);
    renderer.setTransparentBackground(true);
    // The launch curtain is down, and nothing lifts it while the game runs
    // because nothing is drawn. Up at once, or the first paused frame is
    // the curtain's black rather than the frozen game.
    curtain.from = curtain.to = 0.0f;
    curtain.elapsed = curtain.duration;
    standaloneClearFrames = 30;
    standalonePaused = false;
    // No dots until somebody asks for them, and then in the middle.
    pointer = Pointer{};
    // From here every press and release is the emulator's (takeStandaloneEvent),
    // so nothing still held may stay held for this loop.
    letGoOfHeld();
}

// AND WHEN IT ENDS, however it ends, the console is where it was: Home,
// or the game's screen, under the curtain lifting. A game that could not
// load says so where the press came from; one that fell over says so in
// the pill, because the person was playing, not choosing.
void App::finishStandalone() {
    // AFTER THE EMULATOR HAS GONE, which is when everything it wrote is
    // on the disk. Whatever changed is zipped and sent, crash or not: a
    // game that saved and then fell over still saved.
    if (const cab::standalone::Emulator* ran =
            cab::standalone::find(session.standaloneCore)) {
        std::string aerr;
        if (!cab::standalone::afterEnd(*ran, session.entryPath, session.saveDir, &aerr))
            std::fprintf(stderr, "[standalone] saves stay on the drive: %s\n", aerr.c_str());
    }
    if (!session.dirSaveRoot.empty()) syncDirSave(session, uploader);
    session.dirSaveRoot.clear();
    // The zip is on the disk and owed to the server: nothing left for a
    // restart to finish.
    if (const cab::standalone::Emulator* ran = cab::standalone::find(session.standaloneCore))
        cab::standalone::finished(*ran, session.saveDir);
    endPointer();
    cab::vpad::close();
    players::setInGame(false);
    if (playClock.active()) {
        playtime::close(storage::currentUser(), playClock.finish(playtime::wallMs()));
        uploader.sendPlay();
        playedJustNow(session.romId);
    }
    curtain.from = curtain.to = 1.0f;
    curtain.elapsed = curtain.duration;
    curtain.retarget(0.0f, kCurtainUp);
    // Nothing reached this loop while the emulator had the screen, so the
    // idle timers think nobody has touched anything since the launch.
    idleWatch.input(clockSeconds());
    switch (standaloneRun.ended()) {
        case cab::standalone::Run::End::CouldNotLoad:
            refuseLaunch("Couldn't start this game");
            break;
        case cab::standalone::Run::End::KeysTooOld:
            refuseLaunch("Needs newer " + launchJob.systemName + " keys on your server");
            break;
        case cab::standalone::Run::End::NoLicence:
            refuseLaunch("No license for this game on your server");
            break;
        case cab::standalone::Run::End::Crashed:
            menuNotice.say("The game closed unexpectedly", Tone::Problem);
            break;
        default:
            break;
    }
    detailStates.stale = true;
    // And Home again: an ordinary window, opaque, with the controllers.
    if (cab::overlaywin::active()) {
        SDL_HideWindow(window);
        cab::overlaywin::unmark(window);
        SDL_ShowWindow(window);
    }
    renderer.setTransparentBackground(false);
    standalonePaused = false;
    overlayOpen = false;
    overlayFade.from = overlayFade.to = 0.0f;
    overlayFade.elapsed = overlayFade.duration;
    dropPressesFromClosing();
    letGoOfHeld();
    std::fprintf(stderr, "[standalone] back to Home\n");
    // A Power menu choice made over its pause menu (#473), now that it
    // has gone and its saves are queued.
    if (powerAfterExit) {
        const power::Action act = *powerAfterExit;
        powerAfterExit.reset();
        powerAct(act);
    }
}

// THE PAUSE MENU OVER A GAME THIS WINDOW DID NOT DRAW (#169). The same
// menu as every other system, from the same code; what differs is only
// what "pause" means. The emulator is frozen where it is, its controllers
// are let go, and this window, already the overlay, takes the controllers
// and draws the menu over the frozen picture. Resume hands them back.
void App::openStandaloneMenu() {
    if (!standaloneRun.active() || standaloneRun.stopping() || standalonePaused) return;
    standaloneRun.freeze();
    cab::vpad::releaseAll();
    // The pointer's fingers come off with everything else, and an L3 held
    // back is neither a click nor a long press now.
    cab::vtouch::liftAll();
    for (bool& f : pointer.finger) f = false;
    pointer.l3Player = pointer.clickPlayer = -1;
    rumble::update(false);
    cab::overlaywin::mark(window, /*takeInput=*/true);
    standalonePaused = true;
    for (auto& sh : standaloneShortcut) sh = {};
    for (auto& bs : backStart) bs = {};
    toggleOverlay();
    std::fprintf(stderr, "[standalone] paused\n");
}

// THE EMULATOR HAS THE TELEVISION AND THE CONTROLLERS. Nothing here
// may act on a press it also sees, or Home would be navigated
// blind behind the game; a press only says somebody is playing.
// A request to quit still goes through.
//
// EACH REAL PAD'S PRESSES GO TO ITS PLAYER'S VIRTUAL CONTROLLER,
// the moment they arrive: this loop waits on events rather than
// sleeping while the emulator runs, so nothing is added to the
// time between a press and the game. A pad arriving or leaving
// still reaches players.h, so the order stays the console's, and
// a playing pad leaving opens the pause menu.
//
// AND THE PAUSE MENU OPENS THE WAY IT DOES FOR EVERY SYSTEM: both
// stick clicks, always, or a tap of the shortcut button with the
// in-game shortcuts on, or HOME held on a Wii Remote (watchStandalone). The shortcut button (Home, unless set) is
// never passed to the emulator at all: its Home is its own menu
// and a set of Home+button shortcuts, one of which leaves
// fullscreen, and none of them belong on a console.
// True when the event was the emulator's: nothing else may act on it.
bool App::takeStandaloneEvent(const SDL_Event& e) {
    if (standaloneRun.active() && !standalonePaused && e.type != SDL_EVENT_QUIT) {
        switch (e.type) {
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                playClock.touch();
                const int p = players::playerOf(e.gbutton.which);
                const auto b = static_cast<SDL_GamepadButton>(e.gbutton.button);
                const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK) l3Down = down;
                if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK) r3Down = down;
                if (down && l3Down && r3Down) {
                    l3Down = r3Down = false;
                    openStandaloneMenu();
                    break;
                }
                SDL_Gamepad* gp = players::gamepad(p);
                const bool shortcut = gp && shortcuts::held(gp);
                // A streaming device's Home always opens the menu, as
                // in the built-in path (players::streaming()).
                if (p >= 0 && p < cab::vpad::kMaxPlayers &&
                    (shortcuts::enabled() || players::streaming())) {
                    auto& sh = standaloneShortcut[p];
                    if (sh.down && sh.pad != e.gbutton.which) sh = {};
                    // THE SHORTCUT BUTTON'S OWN PRESS IS NOT A
                    // SECOND BUTTON. Sunshine's hold-Select-for-Home
                    // lets go of Select and presses Home in the same
                    // instant, so the hold began on Select's release
                    // (Home already down) and Home's own press then
                    // counted as a combination: no menu (MMagTech's
                    // phone in a PS3 game, 2026-10-08).
                    if (shortcut && !sh.down) sh = {true, false, e.gbutton.which};
                    else if (shortcut && down && !shortcuts::isShortcut(gp, b))
                        sh.used = true;
                    if (!shortcut && sh.down) {
                        const bool tap = !sh.used;
                        sh = {};
                        if (tap) {
                            std::fprintf(stderr,
                                         "[shortcuts] player %d: tap, opening the menu\n",
                                         p + 1);
                            openStandaloneMenu();
                            break;
                        }
                    }
                    if (shortcut) break;   // held: the game hears none of it
                }
                if (b == SDL_GAMEPAD_BUTTON_GUIDE) break;
                // L3, held back for the pointer's long press (pointerButton).
                if (pointerButton(p, b, down)) break;
                // BACK AND START TOGETHER never reach an emulator that
                // opens its own menu on them (xemu): the second one
                // down is held back, press and release both.
                const cab::standalone::Emulator* running = standaloneRun.emulator();
                if (running && running->blocksBackStart && p >= 0 &&
                    p < cab::vpad::kMaxPlayers &&
                    (b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_START)) {
                    auto& bs = backStart[p];
                    const int me = b == SDL_GAMEPAD_BUTTON_BACK ? 0 : 1;
                    if (down && bs.passed[1 - me]) {
                        bs.held[me] = true;
                        break;
                    }
                    if (!down && bs.held[me]) {
                        bs.held[me] = false;
                        break;
                    }
                    bs.passed[me] = down;
                }
                cab::vpad::button(p, b, down);
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                const int p = players::playerOf(e.gaxis.which);
                const auto a = static_cast<SDL_GamepadAxis>(e.gaxis.axis);
                // The dots' driver's sticks and triggers are the dots'.
                if (pointerAxis(p, a, e.gaxis.value)) break;
                cab::vpad::axis(p, a, e.gaxis.value);
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
                players::added(e.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED: {
                // A PLAYING PAD DROPPING OUT PAUSES THE GAME, as one of ours
                // does (handleEvent): its player would otherwise be run over
                // while they find the charger. Any pad resumes.
                const int was = players::playerOf(e.gdevice.which);
                players::removed(e.gdevice.which);
                if (was >= 0) {
                    std::fprintf(stderr, "[players] player %d's pad went off: pausing\n",
                                 was + 1);
                    openStandaloneMenu();
                }
                break;
            }
            case SDL_EVENT_KEY_DOWN:
                playClock.touch();
                break;
            default:
                break;
        }
        return true;
    }
    return false;
}

// Every frame while an emulator of its own runs. True when this frame is
// done: while the emulator has the screen the frame loop does nothing else.
bool App::watchStandalone(float dt) {
    // HOME HELD ON A WII REMOTE opens the menu here as in every game
    // (followRemotesInGame). While the emulator has the screen the frame
    // loop stops before its own call, so it is asked here.
    if (standaloneRun.active() && !standalonePaused) followRemotesInGame();

    // PAUSED OVER AN EMULATOR OF ITS OWN, the ordinary loop below runs and
    // draws the menu; here it only keeps watch. The emulator can still end
    // (Exit to Home asked it to, or it fell over), and when the menu has
    // closed and faded right out, the controllers go back and it thaws.
    if (standaloneRun.active() && standalonePaused) {
        // THE VIRTUAL CONTROLLERS ARE STILL ANSWERED, rumble held off. An
        // emulator asks its controller to stop rumbling as it shuts down,
        // and the kernel holds that request until this process answers or
        // 30 s pass. Exit to Home is chosen from here, so unanswered, God
        // of War III took a minute to close with the console sitting on
        // it, SIGKILL included (2026-09-28: its Pad Thread in
        // uinput_request_submit).
        cab::vpad::pump(static_cast<int64_t>(SDL_GetTicks()));
        rumble::update(false);
        if (!standaloneRun.poll()) {
            finishStandalone();
        } else if (!overlayOpen && overlayFade.value() <= 0.001f &&
                   !standaloneRun.stopping()) {
            cab::overlaywin::mark(window, /*takeInput=*/false);
            standalonePaused = false;
            for (auto& sh : standaloneShortcut) sh = {};
            standaloneRun.thaw();
            // The menu is gone but a notice may still show (Screenshot
            // saved): cleared, or its last frame stays over the game.
            standaloneClearFrames = 30;
            std::fprintf(stderr, "[standalone] resumed\n");
        }
    }

    // WHILE AN EMULATOR OF ITS OWN HAS THE SCREEN, this loop watches it and
    // does nothing else: no idle dimming over a game this process cannot
    // see, nothing drawn, and a short sleep so a watcher does not take a
    // core from the game. Play time counts as it does for any game.
    if (standaloneRun.active() && !standalonePaused) {
        // THE POINTER: its long press, its dots moving, and drawn while they
        // change. Over the dots, the frames that clear this window draw them
        // instead, so they stay over the game.
        pointerTick(dt);
        if (pointer.on && standaloneClearFrames > 0) {
            pointer.dirty = std::max(pointer.dirty, standaloneClearFrames);
            standaloneClearFrames = 0;
        }
        if (pointer.dirty > 0) {
            --pointer.dirty;
            drawPointer();
        } else if (standaloneClearFrames > 0) {
            --standaloneClearFrames;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);
        }
        playClock.tick(dt, true);
        if (playClock.active() && (playCheckpointClock += dt) >= 60.0f) {
            playCheckpointClock = 0.0f;
            playtime::checkpoint(storage::currentUser(), playClock.now(playtime::wallMs()));
        }
        standaloneRan += dt;
        if (standaloneFreezeAfter > 0.0f && standaloneRan >= standaloneFreezeAfter &&
            !standaloneRun.frozen() && !standaloneRun.stopping()) {
            standaloneFreezeAfter = 0.0f;
            standaloneRun.freeze();
        }
        if (standaloneExitAfter > 0.0f && !standaloneRun.stopping() &&
            standaloneRan >= standaloneExitAfter) {
            std::fprintf(stderr, "[standalone] --standalone-exit: closing it\n");
            standaloneRun.stop();
        }
        // The game's rumble, to the pads in people's hands, and never
        // while it is frozen under the pause menu.
        cab::vpad::pump(static_cast<int64_t>(SDL_GetTicks()));
        rumble::update(!standaloneRun.frozen());
        if (!standaloneRun.poll()) finishStandalone();
        // This loop skips everything below, so the handover is asked here
        // too: a PS3 game did not hand over until its menu was opened
        // (2026-10-07).
        remotePlayHandover();
        // Until the next press or a sixtieth of a second, whichever is
        // first: presses pass straight on, and a watcher costs nothing.
        SDL_WaitEventTimeout(nullptr, 16);
        return true;
    }
    return false;
}

// An emulator of its own outlives this process unless it is closed: it has
// a session of its own. Asked, then made, inside its grace period.
void App::closeStandaloneAtExit() {
    if (standaloneRun.active()) {
        standaloneRun.stop();
        while (standaloneRun.poll()) SDL_Delay(50);
        finishStandalone();
    }
}

// --- The pointer (#198) -------------------------------------------------------
//
// SONY'S PS TV POINTER, WITH TWO CHANGES (docs/PROJECT.md question 49,
// decision 4, MMagTech 2026-10-20). The left stick moves a white dot and L2
// touches with it; held, it is a finger kept down, so holding and moving
// drags. Moving the right stick brings a second, red dot, which R2 touches
// with. While the dots show, the sticks, L2 and R2 of whoever brought them
// work the dots and every other button goes to the game. The changes: a long
// press of L3 (half a second) shows and hides them, where the PS TV used a
// click of L3 behind a switch in its own menu, so a click of L3 stays the
// game's; and no rear pointer, since a touch screen reaches only the front.
//
// WHO DRIVES THEM: whichever player held L3. A dot starts in the middle of
// the screen and stays where it was left, for the rest of the game. THE RED
// DOT GOES WHEN THE DOTS ARE HIDDEN: each long press shows the white dot
// alone, and the red one comes again only when the right stick moves or R2
// is pulled, since most games want one finger (MMagTech, 2026-10-21, after
// it kept coming back in WipEout 2048). The long press counts only while R3
// stays up, so L3 and R3 together still open the pause menu and never show a
// dot.

namespace {
// The long press, as the plan says: about half a second.
constexpr uint64_t kPointerHoldMs = 500;
// A click of L3 passed on is held this long: the game reads the button's
// state about once a frame.
constexpr uint64_t kClickMs = 60;
// The sticks: nothing inside the dead zone, and past it the dot speeds up
// with the square of the push, so a small push places it finely. Full push
// crosses the screen's height in this many seconds' worth: 1.4 heights a
// second.
constexpr float kDeadZone = 0.15f;
constexpr float kHeightsPerSecond = 1.4f;
// L2 and R2 touch past half way and let go under a third, so a finger
// resting on a trigger does not flicker.
constexpr int16_t kTriggerDown = 16384, kTriggerUp = 10000;
// The dots, in design points (ui.h's 1920 by 1080): 15 across a radius, a
// little smaller while touching.
constexpr float kDotRadius = 15.0f, kDotPressed = 12.0f;
}  // namespace

bool App::pointerButton(int p, SDL_GamepadButton b, bool down) {
    const cab::standalone::Emulator* running = standaloneRun.emulator();
    if (!running || !running->touchPointer || b != SDL_GAMEPAD_BUTTON_LEFT_STICK || p < 0 ||
        p >= cab::vpad::kMaxPlayers)
        return false;
    if (down) {
        pointer.l3Player = p;
        pointer.l3SinceMs = SDL_GetTicks();
    } else if (pointer.l3Player == p) {
        // Let go before the long press: a click, and the game's.
        pointer.l3Player = -1;
        cab::vpad::button(p, b, true);
        pointer.clickPlayer = p;
        pointer.clickUpAtMs = SDL_GetTicks() + kClickMs;
    }
    // A release after a long press, or after L3 and R3 opened the menu, goes
    // nowhere: the game never had the press.
    return true;
}

bool App::pointerAxis(int p, SDL_GamepadAxis a, int16_t v) {
    if (!pointer.on || p != pointer.player || a < 0 || a >= SDL_GAMEPAD_AXIS_COUNT) return false;
    pointer.raw[a] = v;
    const float f = std::max(-1.0f, v / 32767.0f);
    switch (a) {
        case SDL_GAMEPAD_AXIS_LEFTX: pointer.stick[0] = f; break;
        case SDL_GAMEPAD_AXIS_LEFTY: pointer.stick[1] = f; break;
        case SDL_GAMEPAD_AXIS_RIGHTX: pointer.stick[2] = f; break;
        case SDL_GAMEPAD_AXIS_RIGHTY: pointer.stick[3] = f; break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: {
            const int which = a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : 1;
            const bool held = pointer.finger[which] ? v > kTriggerUp : v > kTriggerDown;
            if (held == pointer.finger[which]) break;
            pointer.finger[which] = held;
            // R2 brings the red dot too, where it was left.
            if (which == 1) pointer.second = true;
            if (held) cab::vtouch::touch(which, pointer.x[which], pointer.y[which]);
            else cab::vtouch::lift(which);
            pointer.dirty = std::max(pointer.dirty, 3);
            break;
        }
        default: break;
    }
    if (std::abs(pointer.stick[2]) > kDeadZone || std::abs(pointer.stick[3]) > kDeadZone)
        pointer.second = true;
    return true;
}

void App::pointerTick(float dt) {
    const uint64_t now = SDL_GetTicks();
    if (pointer.clickPlayer >= 0 && now >= pointer.clickUpAtMs) {
        cab::vpad::button(pointer.clickPlayer, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
        pointer.clickPlayer = -1;
    }
    // THE LONG PRESS: the dots come for this player, go if they were theirs,
    // or pass to them from whoever had them.
    if (pointer.l3Player >= 0 && !r3Down && now - pointer.l3SinceMs >= kPointerHoldMs) {
        const int who = pointer.l3Player;
        pointer.l3Player = -1;
        showPointer(!(pointer.on && pointer.player == who), who);
    }
    if (!pointer.on) return;
    // Each stick moves its dot at the same speed across as down: a height of
    // the screen is a smaller fraction of its width.
    const float aspect = ui::kCanvasWidth / ui::kCanvasHeight;
    for (int d = 0; d < 2; ++d) {
        if (d == 1 && !pointer.second) break;
        bool moved = false;
        for (int axis = 0; axis < 2; ++axis) {
            const float s = pointer.stick[d * 2 + axis];
            const float m = std::abs(s);
            if (m <= kDeadZone) continue;
            const float push = (m - kDeadZone) / (1.0f - kDeadZone);
            float step = (s < 0 ? -1.0f : 1.0f) * push * push * kHeightsPerSecond * dt;
            if (axis == 0) step /= aspect;
            float& v = axis == 0 ? pointer.x[d] : pointer.y[d];
            v = std::clamp(v + step, 0.0f, 1.0f);
            moved = true;
        }
        if (!moved) continue;
        if (pointer.finger[d]) cab::vtouch::touch(d, pointer.x[d], pointer.y[d]);
        pointer.dirty = std::max(pointer.dirty, 3);
    }
}

void App::showPointer(bool on, int p) {
    if (on && !pointer.madeTouch) {
        // THE TOUCH SCREEN, at the first long press of this game rather than
        // at its start, so a game nobody points at never has one; gamescope
        // has it registered well before the first touch, which takes at
        // least a trigger's pull.
        if (!cab::vtouch::open()) {
            std::fprintf(stderr, "[pointer] no touch screen; no dots\n");
            return;
        }
        cab::vtouch::passThrough(true);
        pointer.madeTouch = true;
    }
    // Whoever had them gives the game back their sticks and triggers as
    // they are now.
    if (pointer.on) {
        cab::vtouch::liftAll();
        for (bool& f : pointer.finger) f = false;
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            cab::vpad::axis(pointer.player, static_cast<SDL_GamepadAxis>(a), pointer.raw[a]);
    }
    if (on) {
        // And the new driver's go still in the game, until the dots go.
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            cab::vpad::axis(p, static_cast<SDL_GamepadAxis>(a), 0);
        for (int16_t& r : pointer.raw) r = 0;
        for (float& s : pointer.stick) s = 0.0f;
        pointer.player = p;
        pointer.dirty = 3;
    } else {
        // This window clears, which takes the dots off the game, and the red
        // dot is forgotten until the right stick or R2 brings it again.
        standaloneClearFrames = 30;
        pointer.dirty = 0;
        pointer.second = false;
    }
    pointer.on = on;
    std::fprintf(stderr, "[pointer] dots %s for player %d\n", on ? "on" : "off", p + 1);
}

void App::endPointer() {
    if (pointer.clickPlayer >= 0)
        cab::vpad::button(pointer.clickPlayer, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
    if (pointer.madeTouch) {
        cab::vtouch::close();
        cab::vtouch::passThrough(false);
    }
    pointer = Pointer{};
}

// THE DOTS, ON THIS WINDOW OVER THE GAME, and nothing else: it is gamescope's
// overlay while the emulator has the screen (startStandalone), drawn
// transparent wherever nothing is. White for the left stick's, red for the
// right's, as Sony's two pointers are; each with a dark rim so it shows on
// any picture, and a little smaller while it touches.
void App::drawPointer() {
    int dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(window, &dw, &dh);
    renderer.beginFrame(dw, dh);
    renderer.presentScene();
    for (int d = 0; d < 2; ++d) {
        if (d == 1 && !pointer.second) break;
        const float r = pointer.finger[d] ? kDotPressed : kDotRadius;
        ui::Rect dot;
        dot.x = pointer.x[d] * ui::kCanvasWidth - r;
        dot.y = pointer.y[d] * ui::kCanvasHeight - r;
        dot.w = dot.h = 2.0f * r;
        dot.radius = r;
        dot.fill = d == 0 ? ui::Color::white(0.92f) : ui::Color::rgb(0xE8343A, 0.92f);
        dot.border = 3.0f;
        dot.borderColor = ui::Color::black(0.55f);
        dot.shadowBlur = 8.0f;
        dot.shadowColor = ui::Color::black(0.4f);
        renderer.draw(dot);
    }
    SDL_GL_SwapWindow(window);
}
