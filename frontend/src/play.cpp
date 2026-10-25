// The game in play: the pads read into the core with the in-game
// shortcuts, fast forward and rewind, the picture, and the way out
// (finishExit: every save, before the unload), at Exit to Home and at a
// shutdown mid-game (#398, restructure batch f).

#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "arcadesettings.h"
#include "controls.h"
#include "core.h"
#include "discs.h"
#include "fightstick.h"
#include "ps2.h"
#include "saves.h"
#include "savetable.h"
#include "screenfx.h"
#include "shortcuts.h"
#include "storage.h"
#include "vectrexsheets.h"
#include "vkhost.h"
#include "wii.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// A PRESS THAT ARRIVED WHILE A GAME CLOSED IS DROPPED, from every
// controller, on both ways home (a core here, an emulator of its own).
// Closing blocks for a moment (the unload, the saves), and a press queued
// meanwhile, a second press of A or a Remote's button bouncing, reached
// Home as a fresh one and started the game it had just left: A9
// 2026-10-03 21:53:49, Geometry Wars again 34 ms after Exit. The same
// family as #103, on the way out. Releases stay, and the power and Sleep
// keys always go through.
void App::dropPressesFromClosing() {
    SDL_PumpEvents();
    SDL_FilterEvents([](void*, SDL_Event* ev) {
        if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) return false;
        if (ev->type == SDL_EVENT_KEY_DOWN)
            return ev->key.key == SDLK_POWER || ev->key.key == SDLK_SLEEP;
        return true;
    }, nullptr);
}

void App::finishExit() {
    // TIME PLAYED (#128): the session ends as the game does, and goes to
    // RomM with the save.
    if (playClock.active()) {
        playtime::close(storage::currentUser(), playClock.finish(playtime::wallMs()));
        uploader.sendPlay();
        playedJustNow(session.romId);
    }
    syncSave(session, uploader);
    // The disc in the drive, kept for this person's next Play (#162);
    // GameCube changes its own discs, so the core is asked.
    if (!session.discs.empty()) {
        // LEFT DURING INSERT'S WAIT: the disc goes in and the lid closes
        // before anything else, as it would have a moment later.
        if (session.discInserting) {
            cab::Core& c = cab::Core::shared();
            c.setDiscIndex(discIndexOf(session, session.discInserting));
            c.setDiscEjected(false);
            std::fprintf(stderr, "[disc] left during the change: disc %d in, lid closed\n",
                         session.discInserting);
            session.discInserting = 0;
        }
        syncDiscFromCore(session);
        if (session.discChange != disc::Change::Automatic)
            std::fprintf(stderr, "[disc] left on disc %d of %d\n", session.disc,
                         disc::highest(session.discs));
        // THE DRIVE BACK ON THE LIST'S FIRST ENTRY BEFORE THE UNLOAD, for
        // every core with a drive. Flycast (a172e00) keeps its disc index
        // across games: retro_unload_game never resets it, and read_m3u
        // reads no line unless it is 0, so after a change to disc 2 the
        // NEXT Dreamcast game's list read as empty and Flycast tried the
        // .m3u itself as a disc ("Unknown disk format"), refusing every
        // Dreamcast game until the app restarted. Measured 2026-10-11
        // (#162). RetroArch does not do this; it has the same exposure.
        // Reported upstream: flyinghead/flycast#2541. THE LID IS CLOSED
        // AGAIN: Flycast keeps the lid too, and a lid left open started
        // the next Dreamcast game with it open (measured 2026-10-11).
        cab::Core& core = cab::Core::shared();
        if (core.hasDiscControl() && core.discIndex() > 0) {
            const bool opened = core.setDiscEjected(true);
            const bool set = core.setDiscIndex(0);
            const bool closed = core.setDiscEjected(false);
            std::fprintf(stderr, "[disc] the drive put back on the first entry before the "
                                 "unload: open %s, set %s, close %s\n",
                         opened ? "yes" : "no", set ? "yes" : "no", closed ? "yes" : "no");
        }
    }
    // Before the unload: rcheevos reads the core's memory until told not to.
    ra::endGame();
    // And the same curtain on the way out — `unloadGame` blocks too, and a
    // game vanishing into Home mid-frame is the same cut in the other
    // direction.
    curtain.from = curtain.to = 1.0f;
    curtain.elapsed = curtain.duration;
    curtain.retarget(0.0f, kCurtainUp);
    cab::Core::shared().unloadGame();
    vectrexsheets::release();
    closePs2Window();
    // AFTER the unload, for the one platform whose save is a tree. The
    // battery above is read out of the core's own memory and is finished
    // the moment the game stopped writing to it; a directory save is files
    // on a disk, and retro_unload_game is where a core flushes them. The
    // reference implementation captures this class after shutdown for
    // exactly that reason, and it is the same rule that lost saves here
    // once already when it was ignored.
    //
    // Safe to run twice: syncDirSave compares against its own baseline and
    // sends nothing when nothing moved.
    if (!session.dirSaveRoot.empty()) syncDirSave(session, uploader);
    // The HOME Menu's rumble switch, read back from the game's SYSCONF
    // now that Dolphin has written it (#274, wii.h). Every Wii game has
    // its NAND under its save directory.
    if (session.platformSlug == "wii" && session.core == "dolphin")
        wii::keepRumbleFrom(session.saveDir + "/User/Wii/shared2/sys/SYSCONF");
    // And the same trigger for the same reason, for the class that is one
    // file rather than a tree. This is where a Dreamcast's card is read
    // back out of the system directory and taken out of `bios/`.
    syncFileSaves(session, uploader);
    // Whatever sound the game had queued goes with it, rather than playing
    // on over Home (see where the stream's rate is set, at launch).
    if (audioStream) SDL_ClearAudioStream(audioStream);
    pendingStateId = 0;
    pendingStatePath.clear();
    pendingResume = false;
    // A quit that was waiting on the core is answered by any exit, or the
    // next game's pumpExit would quit it the moment it is up (#398). So is
    // a Power menu choice waiting with it (#473): pumpExit has kept its own.
    exitPending = false;
    powerAfterExit.reset();
    stateHold = stateHoldWaiting = false;
    rewindKeep.reset();
    rewindAvailable = -1;
    rewindLastFrame = 0;
    rewindWorstMs = 0.0;
    detailStates.stale = true;   // Continue from may have changed
    playing = false;
    overlayOpen = false;
    // THE MENU GOES WITH THE GAME, as it does for an emulator of its own
    // (finishStandalone): faded out, its scrim and panel went on fading
    // over Home after the game had gone (MMagTech on the TV, 2026-10-19).
    // exitToHome's wait for a booting core still fades it, since the game
    // is on screen then.
    overlayFade.from = overlayFade.to = 0.0f;
    overlayFade.elapsed = overlayFade.duration;
    dropPressesFromClosing();
    std::fprintf(stderr, "[overlay] exited to Home\n");
}

void App::exitToHome() {
    if (!cab::Core::shared().running()) {
        // Still building the machine. Close the overlay so the quit looks
        // like it was accepted — it has been — and let the loop finish it.
        exitPending = true;
        exitWaitStart = SDL_GetTicksNS();
        overlayOpen = false;
        overlayFade.retarget(0.0f, overlayFadeSeconds);
        std::fprintf(stderr, "[overlay] quit accepted; waiting for the core to boot\n");
        return;
    }
    finishExit();
}

void App::pumpExit() {
    if (!exitPending || !playing) return;
    // WALL CLOCK, NOT FRAMES, and that distinction is the whole fix. The
    // first version capped the wait at 120 FRAMES, which crashed in one
    // configuration and not another for a reason that looked like magic: a
    // capture at 1920x1080 and one at the 1024x768 window differ threefold
    // in fill rate on a software rasteriser, so the same 120 frames are
    // seconds in one and an instant in the other. The core's boot takes the
    // time it takes.
    const double waited = (SDL_GetTicksNS() - exitWaitStart) / 1e9;
    const bool up = cab::Core::shared().running();
    if (up || waited > 15.0) {
        std::fprintf(stderr, "[overlay] quitting after %.1fs%s\n", waited,
                     up ? "" : " — the core never booted, unloading anyway");
        exitPending = false;
        // A Power menu choice that waited for the core (#473) is made now,
        // once the game is left.
        const std::optional<power::Action> then = powerAfterExit;
        finishExit();
        if (then) powerAct(*then);
    }
}

// A PS2 DISC PCSX2 CANNOT OPEN ENDS THE GAME AND SAYS SO (#506), as a
// game whose core will not load it does (loadLaunchedGame). It started as
// if it were playing, with nothing on the screen: PCSX2's start returns
// before the machine is built, and the machine that then failed was never
// asked about again (Destroy All Humans! 2, a CHD made with createcd from
// a DVD). Asked every frame until the machine runs. A game that never ran
// is not time played and does not move up Recent. The words are
// MMagTech's; a BIOS missing on the server keeps its own.
void App::watchPs2Start() {
    if (!playing) return;
    cab::Core& core = cab::Core::shared();
    if (!core.isPs2() || core.running()) return;
    const std::string why = ps2::stoppedWhy();
    if (why.empty()) return;
    std::fprintf(stderr, "[ps2] the machine stopped before it ran: %s\n", why.c_str());
    if (playClock.active()) playClock.finish(playtime::wallMs());
    finishExit();
    refuseLaunch(launchJob.biosMissing.load()
                     ? "No " + launchJob.systemName + " BIOS on your server"
                     : std::string("Disc can't be read"));
}

// REAL WII REMOTES (#200): while a Wii game runs Dolphin drives them,
// and a press of HOME is the game's HOME menu, as on a Wii. Out of the
// game each Remote gets its own light again. HOME HELD ON ONE IS THE
// CONSOLE'S OWN MENU IN EVERY GAME, an emulator of its own included, as
// both stick clicks are on a pad: in any other game the Remote plays
// nothing, and a Remote that started one with no pad on was stuck in it.
// The frame loop calls this, and watchStandalone does while an emulator
// of its own has the screen, since the loop then stops before its call.
void App::followRemotesInGame() {
    wiiremote::setGameRunning(playing && session.platformSlug == "wii");
    wiiremote::setInGame(playing || standaloneRun.active());
    if (!wiiremote::takeHomeHold()) return;
    if (standaloneRun.active() && !standalonePaused) openStandaloneMenu();
    else if (playing || overlayOpen) toggleOverlay();
}

// Buttons first, then run: a core samples input inside retro_run,
// so anything set afterwards is a frame late.
// Returns whether rewind is held this frame (pace.cpp steps the game by it).
bool App::readGameInput(cab::Core& core) {
    followControls();
    cab::PadState pad;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    auto held = [&](SDL_Scancode k) { return keys && keys[k]; };
    pad.buttons = 0;
    // The RetroPad, which every core speaks whatever the real hardware
    // had. Keyboard here; a real pad is wired the same way below.
    using cab::bit;
    if (held(SDL_SCANCODE_UP)) pad.buttons |= bit(cab::Up);
    if (held(SDL_SCANCODE_DOWN)) pad.buttons |= bit(cab::Down);
    if (held(SDL_SCANCODE_LEFT)) pad.buttons |= bit(cab::Left);
    if (held(SDL_SCANCODE_RIGHT)) pad.buttons |= bit(cab::Right);
    if (held(SDL_SCANCODE_X)) pad.buttons |= bit(cab::A);
    if (held(SDL_SCANCODE_Z)) pad.buttons |= bit(cab::B);
    if (held(SDL_SCANCODE_RETURN)) pad.buttons |= bit(cab::Start);
    if (held(SDL_SCANCODE_RSHIFT)) pad.buttons |= bit(cab::Select);
    // The disc rig's presses (#162, --disc-script `press`).
    if (discScript.pad && core.framesRun() < discScript.padUntil)
        pad.buttons |= discScript.pad;

    // A real pad, mapped by SDL's own gamepad abstraction so the
    // hundreds of controllers in its database all arrive the same way.
    // The keyboard is player one as well: both OR together, neither is
    // required.
    //
    // EVERY SEATED PAD IS ITS OWN PLAYER (players.h, issue #64). Until
    // 2026-09-26 a game read only the first pad SDL listed, so a second
    // person's pad walked the menus and did nothing in a game.
    bool fastForward = false, rewinding = false;
    const bool menuWasUp = overlayOpen || stateHold || menuUpAtLastRead;
    menuUpAtLastRead = overlayOpen || stateHold;
    for (int p = 0; p < players::kMax; ++p) {
        cab::PadState st = p == 0 ? pad : cab::PadState{};
        if (SDL_Gamepad* gp = players::gamepad(p)) {
            auto down = [&](SDL_GamepadButton b) {
                return SDL_GetGamepadButton(gp, b);
            };
            if (down(SDL_GAMEPAD_BUTTON_DPAD_UP)) st.buttons |= bit(cab::Up);
            if (down(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) st.buttons |= bit(cab::Down);
            if (down(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) st.buttons |= bit(cab::Left);
            if (down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) st.buttons |= bit(cab::Right);
            // South is the bottom face button whatever it is
            // labelled: A on Xbox, B on Nintendo, Cross on
            // PlayStation. SDL normalises by POSITION, which is the
            // only thing that is actually the same across pads.
            if (down(SDL_GAMEPAD_BUTTON_SOUTH)) st.buttons |= bit(cab::B);
            if (down(SDL_GAMEPAD_BUTTON_EAST)) st.buttons |= bit(cab::A);
            if (down(SDL_GAMEPAD_BUTTON_WEST)) st.buttons |= bit(cab::Y);
            if (down(SDL_GAMEPAD_BUTTON_NORTH)) st.buttons |= bit(cab::X);
            if (down(SDL_GAMEPAD_BUTTON_START)) st.buttons |= bit(cab::Start);
            if (down(SDL_GAMEPAD_BUTTON_BACK)) st.buttons |= bit(cab::Select);

            // THE SHOULDERS, THE TRIGGERS AND THE RIGHT STICK.
            // None of these were mapped until 2026-09-19 — the
            // pad sent a d-pad, four face buttons, Start, Select
            // and one stick, and RetroPad's other six inputs went
            // nowhere. Found by MMagTech on the A9 trying to play
            // Crazy Taxi, where the triggers ARE drive and
            // reverse, so the game could not be played at all.
            //
            // It survived because nothing headless presses a
            // button: every measurement to date drove the pad
            // from code or watched an attract demo.
            if (down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  st.buttons |= bit(cab::L);
            if (down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) st.buttons |= bit(cab::R);
            // L3/R3 also open the overlay as a pair (input.cpp), and
            // a lone click reaches the core only once kStickPairMs
            // has passed without the other (stickPair, app.h), so
            // the pair itself reaches nobody: on the DS and 3DS the
            // click a frame ahead tapped the touch screen as the
            // menu opened.
            {
                StickPair& sp = stickPair[p];
                if (sp.pad != SDL_GetGamepadID(gp)) sp = {{}, {}, SDL_GetGamepadID(gp)};
                const Uint64 now = SDL_GetTicks();
                const bool l3 = down(SDL_GAMEPAD_BUTTON_LEFT_STICK);
                const bool r3 = down(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
                auto heard = [now](StickClick& c, bool raw, bool other) {
                    if (c.tapped) c.tapped = false;   // last frame's tap, let go
                    if (raw && !c.raw) c = {true, false, false, false, now};
                    if (raw) {
                        if (other && !c.heard) c.paired = true;
                        if (!c.paired && !c.heard && now - c.downAt >= kStickPairMs)
                            c.heard = true;
                        return c.heard;
                    }
                    const bool tap = c.raw && !c.heard && !c.paired;
                    c = {};
                    c.tapped = tap;
                    return tap;
                };
                if (heard(sp.l3, l3, r3)) st.buttons |= bit(cab::L3);
                if (heard(sp.r3, r3, l3)) st.buttons |= bit(cab::R3);
            }

            // Analog triggers as digital L2/R2, which is what a
            // RetroPad's L2/R2 are for every core in this set.
            // Half travel: a hair-trigger fires on the spring's
            // own slop and a full-travel one never fires on a worn
            // pad.
            const int kTrigger = 16384;
            const int lt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            const int rt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            if (lt > kTrigger) st.buttons |= bit(cab::L2);
            if (rt > kTrigger) st.buttons |= bit(cab::R2);
            // AND HOW FAR, not just whether. A Dreamcast reads its
            // triggers as a continuous value — they are the
            // accelerator and the brake in a driving game — and
            // Flycast asks for that through the analogue channel
            // rather than the button. The digital bits above still
            // go out for every core that wants a shoulder.
            //
            // A pad with switches instead of springs, which is what
            // a Switch Pro Controller's ZL and ZR are, hands SDL a
            // clean 0 or 32767 and arrives here as 0 or 1.
            st.leftTrigger = std::clamp(lt / 32767.0f, 0.0f, 1.0f);
            st.rightTrigger = std::clamp(rt / 32767.0f, 0.0f, 1.0f);

            st.leftX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
            st.leftY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
            st.rightX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
            st.rightY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
        }
        // THE SHORTCUT BUTTON (shortcuts.h), only with the switch on.
        // While it is held the game hears nothing from this pad's
        // buttons, so neither it nor the second button of a shortcut
        // reaches the game; on release, whatever is still held is kept
        // back until let go, the way the menu's buttons are (#103).
        // A tap opens the pause menu, or closes it. Shortcuts do
        // nothing while the menu is up, but still count as a hold.
        // WHILE A DEVICE STREAMS, ITS HOME ALWAYS OPENS THE MENU,
        // switch or not: a phone's touch controls cannot click both
        // sticks, and holding Select is Home there (Sunshine's
        // back_button_timeout), so it is the one way to pause from any
        // app (MMagTech, 2026-10-08). The shortcuts themselves stay
        // the switch's. The pads seated during a stream are only the
        // stream's, so nothing changes at the TV. A FIGHT STICK'S HOME
        // OPENS IT THE SAME WAY, in every game: the stick has no stick
        // clicks for the pair (fightstick.h).
        const bool combos = shortcuts::enabled();
        if (SDL_Gamepad* gp = players::gamepad(p);
            gp && (combos || players::streaming() || players::stick(SDL_GetGamepadID(gp)))) {
            ShortcutHold& h = shortcutHold[p];
            const SDL_JoystickID who = SDL_GetGamepadID(gp);
            if (h.down && h.pad != who) h = {};
            const bool down = shortcuts::held(gp);
            // The shortcut button pressed to end a wait is that press,
            // not a tap that opens the menu on its release.
            if (down && !h.down) h = {true, stateHoldJustEnded, st.buttons, who};
            if (h.down) {
                if (combos && down && (st.buttons & bit(cab::R2)) && !overlayOpen &&
                    !stateHold && !stateHoldWaiting)
                    fastForward = true;
                if (combos && down && (st.buttons & bit(cab::L2)) && !overlayOpen &&
                    !stateHold && !stateHoldWaiting)
                    rewinding = true;
                const uint32_t fresh = st.buttons & ~h.before;
                h.before = st.buttons;
                if (fresh) {
                    h.used = true;
                    // The screenshot shortcut works in the pause menu too,
                    // on the paused frame. Nothing else does there.
                    if (combos && overlayOpen && !powerMenu && !controlsPanel.listen &&
                        (fresh & bit(cab::Y))) {
                        std::fprintf(stderr,
                                     "[shortcuts] player %d: screenshot, paused\n", p + 1);
                        screenshotNow(session, uploader, menuNotice);
                    }
                    if (combos && !overlayOpen && !stateHold && !stateHoldWaiting) {
                        const bool save = fresh & bit(cab::R);
                        const bool load = !save && (fresh & bit(cab::L));
                        if (!save && !load && (fresh & bit(cab::Y))) {
                            std::fprintf(stderr, "[shortcuts] player %d: screenshot\n",
                                         p + 1);
                            screenshotNow(session, uploader, menuNotice);
                        }
                        if ((save || load) && !session.snapshots) {
                            menuNotice.say("No save states for this system",
                                           Tone::Info);
                        } else if (save) {
                            std::fprintf(stderr, "[shortcuts] player %d: save state\n",
                                         p + 1);
                            saveStateNow(session, uploader, menuNotice);
                        } else if (load) {
                            std::fprintf(stderr, "[shortcuts] player %d: load newest\n",
                                         p + 1);
                            beginLoadLatestState(stateLoad, session, liveClient,
                                                 menuNotice);
                        }
                    }
                }
                if (!down) {
                    h.down = false;
                    heldThroughMenu[p] |= st.buttons;
                    // Not while Controls listens: the shortcut button
                    // is no control, and pressing it does nothing there.
                    if (!h.used && !stateHold && !stateHoldWaiting && !controlsPanel.listen) {
                        std::fprintf(stderr, "[shortcuts] player %d: tap, %s the menu\n",
                                     p + 1, overlayOpen ? "closing" : "opening");
                        toggleOverlay(who);
                    }
                } else {
                    st.buttons = 0;
                    st.leftTrigger = st.rightTrigger = 0;
                }
            }
        }
        // #103: what closed the menu is the menu's, not the game's.
        // The keyboard is in `st` for player one, so it is covered
        // too. A trigger that is swallowed goes to zero as well, or a
        // Dreamcast would still read the pull through its analogue
        // channel.
        if (menuWasUp) heldThroughMenu[p] = st.buttons;
        heldThroughMenu[p] &= st.buttons;
        st.buttons &= ~heldThroughMenu[p];
        if (heldThroughMenu[p] & bit(cab::L2)) st.leftTrigger = 0;
        if (heldThroughMenu[p] & bit(cab::R2)) st.rightTrigger = 0;
        // A FIGHT STICK IN AN ARCADE GAME (fightstick.h): its eight
        // buttons, read above as any pad's and kept from the game above
        // the same way, are moved last to where the game's layout puts
        // them. No other pad, and no other game, has a layout. CONTROLS
        // (controls.h): where this person moved the game's buttons for
        // this kind of pad, the same place, over the layout or the plain
        // table, in its stead.
        if (SDL_Gamepad* gp = players::gamepad(p)) {
            const fightstick::Layout* layout = fightstick::forPort(p, gp, session.core);
            if (const controls::Map* moved = controls::forPort(p, gp, layout))
                controls::apply(*moved, st);
            else if (layout)
                fightstick::apply(*layout, st);
        }
        // HOLDING START IN AN ARCADE GAME (#577, arcadesettings.h): player
        // 1's buttons as the game is about to hear them, so a hold kept from
        // the game above does not count.
        if (p == 0)
            core.setKey(arcadesettings::kMenuKey,
                        arcadesettings::step(arcadeHold, session.core, st.buttons,
                                             core.framesRun()));
        core.setPad(p, st);
    }

    // Wall-clock pacing is the product's, and it is wrong for a
    // capture. `--frames 180` asks for 180 drawn frames, and with
    // nothing to wait for offscreen those 180 take under a tenth of a
    // second — so the core is paced against a tenth of a second and
    // emulates FIVE frames, which is a black boot screen for every
    // console ever made. The first run of Mupen64Plus reported exactly
    // that and it looked like the core had failed.
    //
    // So a capture steps one emulated frame per drawn frame: `--frames`
    // then means what it says, and the same command gives the same
    // picture on a fast machine and a slow one. The instrument being
    // the thing that is wrong has cost this project a day already.
    // THE OVERLAY PAUSES THE EMULATOR. It did not until 2026-09-19,
    // so the game carried on being played behind the menu — in Crazy
    // Taxi you would still be driving while reading it. Cabinet
    // always did this (`openMenu` set `renderer.paused = true`);
    // this console simply never did, because nobody had opened the
    // overlay with a game they were actually playing.
    //
    // Not the game's own pause: the core stops being stepped, which
    // works for every system whether or not it has a pause button.
    // AND FOR PLAYSTATION 2 THAT IS NOT ENOUGH, because PCSX2 is not
    // stepped by this loop at all: it runs its own machine on a thread
    // of its own and would carry on playing behind the menu exactly as
    // every core did before 2026-09-19. It has to be TOLD. A no-op for
    // every libretro core, so it is stated unconditionally
    // rather than behind a test somebody has to remember.
    stateHoldJustEnded = false;
    if (fastForward && !session.snapshots) {
        if (!fastForwardSaid) menuNotice.say("No fast forward for this system", Tone::Info);
        fastForwardSaid = true;
        fastForward = false;
    } else if (!fastForward) {
        fastForwardSaid = false;
    }
    if (fastForward != (core.speed() > 1.0))
        std::fprintf(stderr, "[shortcuts] fast forward %s\n", fastForward ? "on" : "off");
    core.setSpeed(fastForward && !rewinding ? kFastForward : 1.0);
    // Whether this game can rewind, decided once it is running.
    // A core with no rewind is not asked its state's size: for Azahar that
    // question makes a whole state, up to 0.9 s, in the game's first frames.
    if (rewindAvailable < 0 && core.running() && core.framesRun() > 0) {
        const bool allowed = session.snapshots && catalog::rewindAllowed(session.core.c_str());
        const size_t sz = allowed ? core.stateSize() : 0;
        rewindAvailable = allowed && sz > 0 ? 1 : 0;
        rewindLastFrame = core.framesRun();
        std::fprintf(stderr, "[rewind] %s (state %zu KB)\n",
                     rewindAvailable ? "available" : "not offered", sz / 1024);
    }
    // WHAT REWIND IS HOLDING, in the log each time it starts: the
    // numbers the memory budget was argued from, measured.
    static bool wasRewinding = false;
    if (rewinding && !wasRewinding && rewindAvailable == 1) {
        const size_t n = rewindKeep.count(), b = rewindKeep.bytes();
        std::fprintf(stderr,
                     "[rewind] %zu states, %.1f s, %.1f MB in memory "
                     "(%.0f KB each, from %zu KB); slowest snapshot %.1f ms\n",
                     n, n * 0.5, b / 1048576.0, n ? b / 1024.0 / n : 0.0,
                     core.stateSize() / 1024, rewindWorstMs);
        rewindStepWait = 0.0f;   // the first step is at once
    }
    wasRewinding = rewinding;
    if (rewinding && rewindAvailable != 1) {
        if (!rewindSaid) menuNotice.say("No rewind for this system", Tone::Info);
        rewindSaid = true;
        rewinding = false;
    } else if (!rewinding) {
        rewindSaid = false;
    }
    return rewinding;
}

// OURS ONLY WHILE A BUILT-IN GAME RUNS (#212). Paused over a separate
// emulator, gameUp is true and the core still holds the LAST
// built-in game's picture: Burnout 3 was drawn behind the pause
// menu over Mario Kart in Eden and stayed after Resume. That
// emulator draws its own picture; this window draws none of it.
void App::drawGamePicture(cab::Core& core, int dh) {
    if (playing && core.texture() && core.frameWidth() > 0) {
        // Integer-scaled and centred. A Game Boy is 160x144 and its
        // pixels were each a deliberate choice; scaling by 6.4 makes
        // some of them twice the size of their neighbours, which is
        // visible from a sofa and looks like a fault. Phase 8 can offer
        // the non-integer option; the default should be honest.
        const float srcW = static_cast<float>(core.frameWidth());
        const float srcH = static_cast<float>(core.frameHeight());
        //
        // A VERTICAL ARCADE BOARD TURNS THE PICTURE AND THE LAYOUT
        // WITH IT. Its monitor was bolted into the cabinet sideways,
        // so the board renders sideways and asks for a quarter turn.
        // After the turn the picture is TALL: the rows running down
        // the screen are the source's columns and the aspect is the
        // inverse of the stored one.
        const bool quarterTurn = core.rotatedQuarterTurn();
        //
        // AND A TURNED PICTURE IGNORES THE CORE'S DECLARED ASPECT.
        // This looks like throwing away the better answer and is the
        // opposite: FBNeo reports a vertical board's aspect ALREADY
        // turned — 0.75 for DoDonPachi DaiOuJou, which is the 3:4 of
        // the cabinet's tube on its side, not the 448x224 sitting in
        // the framebuffer. Inverting that would apply the turn twice
        // and stretch the picture. Cabinet shipped it the wrong way
        // round first and the comment in `aspectFitVertices` is the
        // record: it "stretched every vertical game".
        //
        // The declared aspect is still what a picture with square
        // pixels needs and Saturn is unplayable without it, so it
        // keeps winning everywhere else. Nothing is lost by dropping
        // it here, for the reason Cabinet gave: the platforms whose
        // pixels are not square never rotate, and the boards that
        // rotate are square-pixel.
        const float declaredAspect = quarterTurn ? 0.0f : core.avInfo().aspectRatio;
        const float aspect = declaredAspect > 0 ? declaredAspect : srcW / srcH;
        //
        // EXCEPT for a hardware-rendered core, where integer scaling
        // is the wrong idea rather than a stricter one. A Dreamcast's
        // output is already a rendering of a 3D scene at whatever
        // internal resolution the core was asked for — there is no
        // grid of deliberate pixels to preserve — and at 3x internal
        // resolution the frame is 1920x1440, where flooring to an
        // integer gives zero, clamps to one, and draws 360 rows off
        // the bottom of the screen.
        //
        // Everything from here is in terms of the picture as SHOWN
        // rather than as stored, which is the only version of it the
        // screen has an opinion about.
        const float shownAspect = quarterTurn ? 1.0f / aspect : aspect;
        const float shownRows = quarterTurn ? srcW : srcH;
        //
        // AND A TURNED PICTURE FILLS THE HEIGHT RATHER THAN INTEGER
        // SCALING. MMagTech's call, 2026-09-19, asked because nothing
        // makes a vertical game fill a horizontal screen without
        // lying and the two honest answers differ. A 240x320 board
        // integer-scaled into 1080 points gives 3x, a 720-point-tall
        // window with 180 points of dead space above and below it on
        // top of the pillarboxing that is already unavoidable; filling
        // the height gives 3.375x and the largest true-shaped picture
        // the panel can show. The dot grid argument that earns integer
        // scaling a Game Boy is worth less here than the 33% of the
        // screen it costs.
        // EVERY SYSTEM FILLS THE HEIGHT NOW — changed 2026-09-21.
        //
        // MMagTech: *"all systems should go to top and bottom of the
        // screen."* Integer scaling was the default and it cost real
        // screen: a 320x240 core on a 1080 canvas scaled by 4 rather
        // than 4.5, so a 960-point picture sat inside 120 points of
        // black bar for no reason a person watching it would accept.
        //
        // WHAT IS GIVEN UP, because it is not nothing. An integer scale
        // makes every source pixel exactly the same size; 4.5 makes
        // some of them five rows tall and some four. On a Game Boy at
        // 160x144 that is a 7.5x scale and the unevenness is visible if
        // you go looking for it. The argument for integer scaling is
        // that those pixels were each a deliberate choice; the argument
        // against is that a third of the screen is a bigger price, and
        // it is the one MMagTech is paying.
        //
        // THE VERTICAL ARCADE BOARDS ALREADY WORKED THIS WAY and the
        // reasoning recorded for them is the same one, reached first:
        // "the dot grid argument that earns integer scaling a Game Boy
        // is worth less here than the 33% of the screen it costs". This
        // is that decision applied everywhere rather than in one place.
        //
        // `--integer-scale` puts it back, for comparing the two on a
        // television, which is the only place the question can be
        // settled.
        const bool integerScale =
            integerScaling && !core.hardwareRendered() && !quarterTurn;
        float scale = std::min(ui::kCanvasWidth / (shownRows * shownAspect),
                               ui::kCanvasHeight / shownRows);
        if (integerScale) {
            // COUNTED IN THE SCREEN'S OWN ROWS, not the 1080-point
            // canvas (2026-10-02). On a 2160-line TV the canvas made
            // the steps twice as coarse: 240 rows got 4x, 960 of 1080
            // points with bars, where the real panel takes 9x and
            // fills exactly. Shown to MMagTech to decide again.
            const float perPoint =
                dh > 0 ? static_cast<float>(dh) / ui::kCanvasHeight : 1.0f;
            float rows = std::floor(scale * perPoint);
            if (rows < 1.0f) rows = 1.0f;
            scale = rows / perPoint;
        }
        // Real screen pixels per canvas point, for a screen look's
        // output size: read here, before `dh` means the picture.
        const float physPerPoint =
            dh > 0 ? static_cast<float>(dh) / ui::kCanvasHeight : 1.0f;
        // ON WHOLE SCREEN PIXELS, edges and size. A screen look is
        // drawn at a whole number of pixels and put on screen nearest
        // texel to pixel; a rectangle a fraction wider or off by half
        // a pixel repeated one column to make up the difference. On
        // the TV that is invisible, but it moved a CRT mask by one
        // pixel from there on, and Remote Play's shrink of the 4K
        // picture turned the shift into a green left half and a
        // purple right one, split at column 1924 (Double Dragon,
        // crt-easymode, 2026-10-07, #286).
        auto snap = [physPerPoint](float v) {
            return std::round(v * physPerPoint) / physPerPoint;
        };
        const float dh = snap(shownRows * scale);
        const float dw = snap(shownRows * scale * shownAspect);
        const float px = snap((ui::kCanvasWidth - dw) * 0.5f);
        const float py = snap((ui::kCanvasHeight - dh) * 0.5f);
        // Where the picture actually sits in that texture. A
        // software core answers "all of it, the right way up"; a
        // hardware core answers a corner of a larger target with its
        // rows the other way round, and neither the layout above nor
        // the draw below has to know which.
        float u0, v0, u1, v1;
        core.frameUV(u0, v0, u1, v1);
        // Said once per game. The picture's geometry is four numbers
        // that have to agree — what the core hands back, what it says
        // the shape is, the quad the layout builds, and the corner of
        // the texture that is sampled — and when the picture comes out
        // the wrong shape there is no way from outside to tell which
        // of the four is lying.
        {
            static int saidFor = -1;
            if (saidFor != session.romId) {
                saidFor = session.romId;
                std::fprintf(stderr,
                             "[picture] core %gx%g aspect %.4f%s -> quad %.0fx%.0f "
                             "(%.4f) at %.0f,%.0f  uv %.4f,%.4f..%.4f,%.4f\n",
                             srcW, srcH, core.avInfo().aspectRatio,
                             quarterTurn ? " TURNED" : "", dw, dh, dw / dh, px, py,
                             u0, v0, u1, v1);
            }
        }
        // Opaque, always. A game's frame is a picture, and whatever is
        // in its alpha channel is the emulated machine's own state
        // rather than a compositing instruction. Found on PPSSPP:
        // Lumines leaves the PSP framebuffer's alpha at nearly zero,
        // the blend took it literally, and the whole 1920x1080 capture
        // peaked at RGB (4,4,4) — a picture that was there all along
        // and read as a core that renders black.
        //
        // The turn goes to the DRAW and the shape goes to the layout,
        // and they are two different things. frameUV has already said
        // where the picture is in the texture and which way up its
        // rows are; rotation says how the picture it found is turned,
        // and the two compose — which is what a hardware-rendered
        // vertical board needs.
        // THE SCREEN LOOK (#122): RetroArch's shader files over the
        // picture, sized to the real pixels it covers on the screen,
        // in the game's own orientation and turned afterwards, as
        // RetroArch does. None chosen, or one that failed to load,
        // draws the picture as before.
        screenfx::Chain& look = screenfx::shared();
        look.use(screenfx::presetFor(session.platformSlug));
        GLuint looked = 0;
        if (look.active()) {
            const int rot = static_cast<int>(core.rotation());
            const int qw = static_cast<int>(std::lround(dw * physPerPoint));
            const int qh = static_cast<int>(std::lround(dh * physPerPoint));
            const bool turned = rot == 1 || rot == 3;
            looked = look.run(core.texture(), u0, v0, u1, v1, static_cast<int>(srcW),
                              static_cast<int>(srcH), turned ? qh : qw,
                              turned ? qw : qh);
        }
        if (looked)
            ui::drawImageTexture(renderer, looked, px, py, dw, dh, 0, 0, 1, 1, true,
                                 static_cast<int>(core.rotation()));
        else
            ui::drawImageTexture(renderer, core.texture(), px, py, dw, dh, u0, v0, u1,
                                 v1, true, static_cast<int>(core.rotation()));
        // A VECTREX GAME'S SCREEN SHEET (#224), over exactly the picture, as
        // the printed sheet sat over the tube: stretched with it, and drawn
        // only for a Vectrex game with a sheet and Overlays on.
        vectrexsheets::draw(renderer, px, py, dw, dh);

        // The glow goes over the bars, not under the picture: it is
        // drawn after, and its shader discards inside the picture rect,
        // so no game pixel is ever covered. An integer-scaled handheld
        // on a 4K set is mostly dead space, which is exactly the case
        // this exists for.
        //
        // It reshapes itself for a vertical board for free, and that
        // is worth saying because it looks like it should need work.
        // The shader ramps from the picture's edge to the screen's in
        // each direction separately, so handing it a tall rect lights
        // two wide bars at the sides and nothing above or below, where
        // a picture that fills the height leaves no room to ramp
        // across. The rect is the whole interface.
        renderer.drawBiasGlow(px, py, dw, dh, glowPeak);
    }
}

// A SHUTDOWN MID-GAME LEAVES THE WAY EXIT TO HOME DOES — docs/PROJECT.md,
// open question 10b. Until 2026-09-22 it did not: the loop ended, the code
// below unloaded the core, and nothing uploaded, so pressing the power
// button during a game threw away whatever the game had saved since it
// started. finishExit is the one way out of a game and it carries every
// save class there is — battery, directory, file — so going through it is
// what makes this true for every emulator, present and future, rather
// than for the ones somebody remembered. The uploads it queues are
// drained below, before the process ends.
//
// Not while the machine is still being built: nothing has been played, and
// finishExit's own caller waits for exactly that case rather than calling
// it. And never for a capture, which is not a person's evening.
void App::leaveGameAtExit() {
    if (playing && askedToStop && !shotMode && cab::Core::shared().running()) {
        std::fprintf(stderr, "[shutdown] leaving the game the way Exit to Home does\n");
        finishExit();
    }
}

// After the loop, while a game is still up: the core's statistics, then the
// unload. leaveGameAtExit has already ended a game whose core was running
// when the console was asked to stop.
void App::reportAndUnloadCore() {
    if (playing) {
        cab::Core& core = cab::Core::shared();
        const double realtime = core.audioFramesTotal() / core.avInfo().sampleRate;
        std::fprintf(stderr,
                     "[core] %llu frames, %llu audio frames = %.2fs of emulated time\n",
                     static_cast<unsigned long long>(core.framesRun()),
                     static_cast<unsigned long long>(core.audioFramesTotal()), realtime);
        // What the bridge between Vulkan and GL cost, when there was one. The
        // whole question "does handing a Vulkan picture to a GLES UI hurt"
        // gets an answer here rather than an opinion. See vkhost.h.
        {
            uint64_t vframes = 0;
            double vseconds = 0.0;
            cab::vk::presentCost(vframes, vseconds);
            if (vframes > 0)
                std::fprintf(stderr,
                             "[vulkan] %llu pictures crossed into GL in %.3fs "
                             "= %.3f ms each\n",
                             static_cast<unsigned long long>(vframes), vseconds,
                             1000.0 * vseconds / static_cast<double>(vframes));
            if (vframes > 0) {
                double rec = 0.0, wait = 0.0;
                cab::vk::presentCostSplit(rec, wait);
                std::fprintf(stderr,
                             "[vulkan]   of which %.3f ms recording the copy and "
                             "%.3f ms waiting for the GPU\n",
                             1000.0 * rec / static_cast<double>(vframes),
                             1000.0 * wait / static_cast<double>(vframes));
            }
        }
        // NOT UNLOADED WHILE THE MACHINE IS STILL BEING BUILT (#473), the
        // rule exitToHome keeps (core.h, running): asked to stop in the
        // first seconds of a PSP game, the unload killed PPSSPP's boot
        // thread. The core is stepped until it runs, at most five seconds,
        // and unloaded then whatever it says.
        if (!core.running()) {
            const Uint64 since = SDL_GetTicksNS();
            while (!core.running() && (SDL_GetTicksNS() - since) / 1e9 < 5.0)
                core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
            std::fprintf(stderr, "[shutdown] the core was still starting; %s after %.1fs\n",
                         core.running() ? "running" : "unloading anyway",
                         (SDL_GetTicksNS() - since) / 1e9);
        }
        core.unload();
    }
}
