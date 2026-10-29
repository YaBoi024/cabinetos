// main()'s start-up sequence and its shutdown (#398, restructure batch d).
//
// Each function is one stretch of main()'s old body, unchanged, and run()
// calls them in the old order. The steps other batches move are still in
// run() between the calls, at their points. THE ORDER IS THE BEHAVIOUR:
// storage before sound (the levels are in settings.json), sound::init
// before volume::start (the silent day, 2026-10-12), the library before
// Home's focus, the uploader before the power-cut recovery; the comments
// in place say why each one is where it is.

#include "app.h"

#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <utility>

#include "capture.h"
#include "catalog.h"
#include "core.h"
#include "coretests.h"
#include "covercache.h"
#include "dirsave.h"
#include "dolphinprep.h"
#include "fileio.h"
#include "firstrun.h"
#include "idlesleep.h"
#include "offline_sync.h"
#include "overlaywin.h"
#include "prefs.h"
#include "quality.h"
#include "samplelibrary.h"
#include "saves.h"
#include "storage.h"
#include "volume.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// The names in a folder, without . and .., or none.
static std::vector<std::string> listNames(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d))
            if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
                out.push_back(e->d_name);
        ::closedir(d);
    }
    return out;
}

// --- Where everything lives, decided before anything writes a byte ------
//
// Printed rather than assumed. A console quietly writing somewhere nobody
// expected is the kind of bug that costs an afternoon, and the one line it
// takes to prevent that is this one.
//
// USB drives are mounted first, so the tree below and everything after it
// see a drive that was plugged in before the console started. drives.h.
std::optional<int> App::startStorage() {
    if (!shotMode) drives::start();
    {
        std::string serr;
        if (!storage::ensureTree(&serr)) {
            std::fprintf(stderr, "[storage] %s\n", serr.c_str());
            return 1;
        }
        std::fprintf(stderr, "[storage] root %s\n", storage::root().c_str());
        clearDolphinCheats();
        const std::vector<std::string> locs = storage::locations();
        for (size_t i = 1; i < locs.size(); ++i)
            std::fprintf(stderr, "[storage] games drive %s\n", locs[i].c_str());
        // SAID ONCE AND THEN NEVER AGAIN. A console that complains every boot
        // about a drive somebody removed on purpose is worse than one that says
        // nothing, so having reported it the console forgets that drive. The
        // screen version of this waits with the rest of the UI; the games on it
        // already behave correctly, because nothing is found and not found
        // means fetch it.
        if (const std::string gone = storage::missingDriveToReport(); !gone.empty())
            std::fprintf(stderr,
                         "[storage] the games drive at %s is not connected — games "
                         "kept on it will be fetched from RomM again\n", gone.c_str());
    }

    // THE MACHINE CLASS, worked out at every start (#63 phase 2), so a new
    // graphics card is noticed the next time the console comes up. It only
    // decides anything while nobody has moved the dial. quality.h.
    if (!shotMode) quality::machineClass();

    // Runs before SDL, deliberately. This needs no window, no GL and no
    // controller, and on a headless machine it must work anyway — the whole
    // point is to test the server conversation on its own.
    // The probe reports, per game, whether this console can play it — and it
    // asked that question WITHOUT being told where the cores are, so on a real
    // console it answered "not playable here" for every platform while all
    // twenty-one cores sat installed and working. Found on the A9 Pro,
    // 2026-09-19. Harmless to the product and corrosive to the diagnosis: a
    // tool that lies is worse than one that says nothing.
    catalog::setCoreDirectory(coreDir);
    return std::nullopt;
}

std::optional<int> App::startDisplay() {
    std::signal(SIGUSR1, requestCapture);

    // THE OVERLAY SLOT IS AN X11 IDEA, so the test mode has to be an X11
    // client. gamescope's STEAM_OVERLAY and STEAM_INPUT_FOCUS are window
    // properties its Xwayland half reads; a native Wayland client can reach the
    // external-overlay plane but not the one that takes input. Under gamescope
    // Xwayland is always there, so this costs nothing — but it is a real
    // constraint on the design and not an artefact of the test.
    if (overlayTest) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");

    // CONTROLLERS REACH THIS PROCESS EVEN WHEN ANOTHER WINDOW IS IN FRONT. An
    // emulator that is its own application has the television and the focus,
    // and this process still has to see the pad: to count time played, and to
    // open the pause menu over the game. SDL drops those events by default
    // once the window loses focus. Nothing else changes, because this window
    // has the focus whenever anything of the console's is on the screen.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // AND NEVER ITS OWN VIRTUAL CONTROLLERS (vpad.h). They are real devices to
    // every SDL on the machine, this one included, and a console that adopted
    // them as players would pass each press back to itself.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, "0x1209/0xCAB0");
    // A WII REMOTE IS NOT ONE OF SDL'S PADS HERE (#200): wiiremote.h drives
    // them, and the bridge's stand-ins carry Nintendo's id, which SDL's own Wii
    // driver would otherwise take as a second controller. The kernel's Wii
    // driver's controllers are kept from SDL by udev (72-cabinetos-wii-remote.rules).
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "[frontend] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    players::loadMappings();
    // A SET-ASIDE PAD IS NOT HEARD AT ALL (players.h, Remote Play): the
    // console's own pads while a stream plays, a stream's pads when it has
    // ended. Dropped as SDL queues them, so no screen, menu or game sees them.
    SDL_SetEventFilter(
        [](void*, SDL_Event* e) -> bool {
            switch (e->type) {
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    players::pressed(e->gbutton.which);   // FORK (handheld): players.h
                    return !players::setAside(e->gbutton.which);
                case SDL_EVENT_GAMEPAD_BUTTON_UP: return !players::setAside(e->gbutton.which);
                case SDL_EVENT_GAMEPAD_AXIS_MOTION: return !players::setAside(e->gaxis.which);
                case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
                case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
                case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: return !players::setAside(e->gtouchpad.which);
                case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: return !players::setAside(e->gsensor.which);
                case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                case SDL_EVENT_JOYSTICK_BUTTON_UP: return !players::setAside(e->jbutton.which);
                case SDL_EVENT_JOYSTICK_AXIS_MOTION: return !players::setAside(e->jaxis.which);
                case SDL_EVENT_JOYSTICK_HAT_MOTION: return !players::setAside(e->jhat.which);
                default: return true;
            }
        },
        nullptr);
    // Real Wii Remotes (#200): whether one is paired, before the library is
    // drawn, and the thread that sets each up as it connects.
    wiiremote::start();
    // Each controller's battery, from the kernel, for Home's row (#293).
    battery::start();

    // The interface's own sounds. Opened here rather than lazily, because the
    // first click a person hears should not be the second one they asked for —
    // and a console with no audio device says so once and stays silent.
    sound::init();
    if (uiSoundLevels) {
        float q = 0, m = 0, l = 0;
        if (std::sscanf(uiSoundLevels, "%f,%f,%f", &q, &m, &l) == 3) {
            sound::setLevelVolumes(q, m, l);
            std::fprintf(stderr, "[sound] levels %.2f, %.2f, %.2f\n", q, m, l);
        }
    }
    if (uiSoundFlag) {
        sound::setEnabled(uiSound);
        sound::setVolume(uiSoundVolume);
    } else {
        // The level Settings saved. Nothing saved, or a word this build does
        // not know, is Medium: how the console always sounded.
        sound::Level lv = sound::Level::Medium;
        sound::levelFromWord(prefs::get("interface_sounds", ""), &lv);
        sound::setLevel(lv);
        std::fprintf(stderr, "[sound] interface sounds %s\n", sound::levelName(lv));
    }
    {
        sound::Level chime = sound::Level::Medium;
        sound::levelFromWord(prefs::get("achievement_sound", ""), &chime);
        sound::setChimeLevel(chime);
    }
    // The volume keys, from the kernel, and full volume at start on a TV
    // (#351). AFTER THE SOUND IS OPEN: started before sound::init, the app's
    // sound thread never ran and every sound was silent (A9, 2026-10-12).
    // Never offscreen: a headless copy would answer every key again and reset
    // the television's volume.
    if (const char* vd = SDL_GetCurrentVideoDriver();
        vd && std::strcmp(vd, "offscreen") != 0 && std::strcmp(vd, "dummy") != 0)
        volume::start();

    // GLES 3.0, which is what the libretro hardware-rendered cores ask for via
    // RETRO_ENVIRONMENT_SET_HW_RENDER. The UI and the cores share one context
    // on purpose: that is what removes the readback Cabinet's Apple build could
    // not avoid. See docs/PROJECT.md, "Video: two paths".
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    if (overlayTest) windowFlags |= SDL_WINDOW_HIDDEN;

    window = SDL_CreateWindow("CabinetOS", 1920, 1080, windowFlags);
    if (!window) {
        std::fprintf(stderr, "[frontend] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    // NO POINTER, EVER (docs/PROJECT.md, question 16): a mouse moves focus and
    // clicks the focused thing, nothing else. A PS5 pad's touchpad is a mouse
    // to Linux, so a thumb brushing it drew gamescope's arrow over Home and
    // over every game drawn in this window (#509). With the cursor hidden on
    // the console's windows, gamescope draws none while one of them has the
    // pointer; the emulators that run as programs of their own have windows
    // of their own.
    SDL_HideCursor();

    if (overlayTest) {
        if (!cab::overlaywin::mark(window, /*takeInput=*/true))
            std::fprintf(stderr, "[overlay] could not mark the window — it will "
                                 "come up as an ordinary one\n");
        SDL_ShowWindow(window);
    }

    gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "[frontend] SDL_GL_CreateContext failed: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Under cage on software rendering vsync throttles to whatever llvmpipe can
    // manage, which is fine — the frame loop is paced by wall clock, not by the
    // swap, exactly as the core pacing will be.
    SDL_GL_SetSwapInterval(1);

    std::fprintf(stderr, "[frontend] GL_VENDOR   %s\n", glGetString(GL_VENDOR));
    std::fprintf(stderr, "[frontend] GL_RENDERER %s\n", glGetString(GL_RENDERER));
    std::fprintf(stderr, "[frontend] GL_VERSION  %s\n", glGetString(GL_VERSION));
    if (!renderer.init()) {
        std::fprintf(stderr, "[frontend] renderer init failed\n");
        return 1;
    }

    // Draw onto nothing rather than onto black. Only the two clears change —
    // the blend function was already correct for it.
    if (overlayTest) renderer.setTransparentBackground(true);
    // Regular, SemiBold, Bold, then the CJK fallback. All four are already
    // in the Bazzite base, so the type costs the image nothing.
    if (!text.init({"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
                    "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc"})) {
        std::fprintf(stderr, "[frontend] text init failed\n");
        return 1;
    }
    return std::nullopt;
}

// --- FIRST RUN -----------------------------------------------------------
//
// Before anything asks a server for anything, because on a console that has
// never been paired there is no server to ask. Setup runs a loop of its own
// and returns once the machine is configured; see setup.h for why it is not
// a mode inside the loop below.
//
// A MACHINE THAT IS ALREADY CONFIGURED IS NEVER SHOWN THIS. The reference
// console was set up by hand over SSH and has been playing games for a day;
// presenting it with a welcome screen would be a far worse failure than
// skipping a wizard nobody needed. firstrun::completion() answers "is this
// machine configured", not "has this flow been run" — and where the answer
// came from being configured rather than from a marker, the marker is
// back-filled here so the judgement is made once and recorded.
std::optional<int> App::runFirstRun() {
    if (!noSetup) {
        firstrun::Completion done = firstrun::completion();
        // SIGNED OUT AND NOT YET CLEARED: the last run signed out and started
        // this one. Cleared here, before anything else runs, so nothing can
        // write into a folder being emptied; server.h. Under the startup
        // screen the last run left on.
        if (done.why == firstrun::Why::SignedOut && done.clearing && !shotMode) {
            setup::Deps deps;
            deps.window = window;
            deps.renderer = &renderer;
            deps.text = &text;
            setup::showWaiting(deps, "Signing out");
            server::finishSignOut();
            done = firstrun::completion();
        }
        if (forceSetup || !done.done) {
            setup::Deps deps;
            deps.window = window;
            deps.renderer = &renderer;
            deps.text = &text;
            setup::Options opts;
            // Back from Sign out: the network is set up and stays, so the
            // server step (docs/SETTINGS.md, Network).
            opts.startStep = setupStep ? setupStep
                             : done.why == firstrun::Why::SignedOut ||
                                       done.why == firstrun::Why::NoServer
                                 ? "server"
                                 : nullptr;
            opts.signedOut = done.why == firstrun::Why::SignedOut;
            // A capture of a setup screen must never write a marker or an
            // address: it is a photograph, and the machine it is taken on is
            // usually one that is already set up.
            opts.dryRun = forceSetup || shotMode;
            opts.screenshotPath = shotMode ? shotPath : nullptr;
            opts.frames = shotAfterFrames;
            opts.renderWidth = renderW;
            opts.renderHeight = renderH;
            if (done.why == firstrun::Why::NoServer)
                std::fprintf(stderr, "[first-run] set up, but no server address is set: "
                                     "asking for one\n");
            for (;;) {
                const setup::Outcome outcome = setup::run(deps, opts);
                if (outcome != setup::Outcome::Completed) {
                    renderer.shutdown();
                    SDL_Quit();
                    return 0;
                }
                // Setup writes the address; pick it up, because the resolution
                // done before SDL started ran on a console that had none.
                resolvedAddress = firstrun::serverAddress();
                rommAddress = resolvedAddress.empty() ? nullptr : resolvedAddress.c_str();
                // NOT LEFT WITH NONE (#485, FIRSTRUN-06): an address that
                // could not be saved at the server step is asked for again,
                // rather than starting on the stand-in library. A capture
                // writes nothing, so it is let through.
                if (rommAddress || opts.dryRun) break;
                std::fprintf(stderr, "[first-run] finished with no server address saved: "
                                     "asking again\n");
                opts.startStep = "server";
            }
        } else if (done.why == firstrun::Why::AdoptedExisting && done.when.empty()) {
            // Adopted, and no marker on disk yet — so write one. An empty
            // `when` is exactly the case where the answer came from looking at
            // the machine rather than from a file.
            std::string merr;
            if (firstrun::markCompleted(/*adopted=*/true, &merr))
                std::fprintf(stderr,
                             "[first-run] this console was already configured; "
                             "recorded that rather than asking again\n");
        }
    }
    return std::nullopt;
}

std::optional<int> App::startStartupScreen() {
    // TEXT INPUT IS OFF BY DEFAULT IN SDL3, and the handler for it below has
    // therefore never once fired. Without this a physical keyboard cannot type
    // a character into the on-screen keyboard — which is not a small gap, it is
    // the input the whole first-run design is built on being guaranteed.
    SDL_StartTextInput(window);
    idleWatch.setTimeScale(idleScale);
    idleWatch.setEnabled(!idleOff);
    dimLayer.smooth = true;
    waitDeps.window = window;
    waitDeps.renderer = &renderer;
    waitDeps.text = &text;

    // A capture of the startup screen, which otherwise exists only for the few
    // seconds between the window appearing and the library arriving — and on a
    // fast server that is too short to photograph by hand. It draws the real
    // thing through the real path rather than reconstructing it.
    if (startupShot) {
        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        // A representative line rather than the bare one: what this screen
        // actually shows during a boot is a count that climbs, and a capture
        // of it saying nothing would be a picture of a state that lasts a
        // fraction of a second.
        setup::showWaiting(waitDeps, "Loading your library. 640 games");
        renderer.saveFrame(shotPath, dw, dh);
        renderer.shutdown();
        SDL_Quit();
        return 0;
    }
    return std::nullopt;
}

double App::clockSeconds() { return SDL_GetTicksNS() / 1e9; }

// Once a frame. The timers only ever deepen here; idleWatch.input() is
// what lifts them.
void App::idleFrame(float dt, bool playingNow, double blankAfter) {
    const double t = clockSeconds();
    idleWatch.setBlankAfter(blankAfter);
    idleWatch.setDimAfter(idle::dimSeconds());   // FORK (handheld)
    const idle::Level lvl = idleWatch.update(t, playingNow);
    if (lvl != idleShown) {
        std::fprintf(stderr, "[idle] %s -> %s after %.0fs without input\n",
                     idle::name(idleShown), idle::name(lvl), idleWatch.idleFor(t));
        // A REMOTE PLAY SESSION ENDS BEFORE THE SCREEN GOES OUT (#294).
        // Sunshine set up against a sleeping screen streams black and
        // never looks again: going out mid-stream breaks the stream at
        // once, and a phone that left without quitting would rejoin
        // black, since a rejoin runs nothing that could wake it. So the
        // session is closed first, connected or not, as Quit in
        // Moonlight closes it; joining again starts a new one, and its
        // start step lights the screen. MMagTech chose this over
        // dimming without end while a device is connected: Moonlight
        // keeps a phone awake, so a phone left on a table would hold an
        // OLED lit all night. The timer is the setting's, so a stream
        // nobody touches ends at 10, 15 or 30 minutes; a game in play
        // never blanks, so is never cut off.
        if (lvl == idle::Level::Blank) {
            if (remoteplay::sessionOpen())
                idle::setDisplayAsleep(true, false, [] { remoteplay::endSession(); });
            else
                idle::setDisplayAsleep(true);
        } else if (idleShown == idle::Level::Blank) {
            idle::setDisplayAsleep(false);
        }
        const float depth = lvl == idle::Level::Awake ? 0.0f
                            : lvl == idle::Level::Dim ? idle::kDimDepth
                                                      : 1.0f;
        dimLayer.retarget(depth, lvl == idle::Level::Awake ? idle::kWakeFadeSeconds
                                                           : idle::kDimFadeSeconds);
        idleShown = lvl;
    }
    dimLayer.tick(dt);
    // FORK (handheld): sleep when idle on battery (idlesleep.h). Only with no game
    // loaded, only when the screen has already had its turn to go out, and asked
    // once per stretch of idleness: a press (idleFor back under 5 s) arms it again.
    {
        static bool sleepAsked = false;
        static double nextLook = 0.0;
        const double after = idlesleep::seconds();
        const double quiet = idleWatch.idleFor(t);
        if (quiet < 5.0) sleepAsked = false;
        if (after > 0.0 && !sleepAsked && !playing && !standaloneRun.active() &&
            quiet >= std::max(after, blankAfter) && t >= nextLook) {
            nextLook = t + 5.0;   // the supply files are not read every frame
            if (idlesleep::onBattery() && power::canRest()) {
                sleepAsked = true;
                std::fprintf(stderr, "[idle] sleeping after %.0fs without input, on battery\n",
                             quiet);
                power::act(power::Action::Rest);
            }
        }
    }
    const idle::Offset o = idle::pixelShift(t, renderer.scale(), shiftEvery);
    if (o.dx != shiftShown.dx || o.dy != shiftShown.dy) {
        std::fprintf(stderr, "[idle] pixel shift %+d,%+d px\n", o.dx, o.dy);
        renderer.setPixelShift(o.dx, o.dy);
        shiftShown = o;
    }
}

std::optional<int> App::connectToServer(char** argv) {
    if (rommAddress) {
        // Where the cores are, so the catalog can tell "the manifest has a core
        // for this" apart from "this console has it built".
        catalog::setCoreDirectory(coreDir);
        setup::showWaiting(waitDeps, std::string("Looking for your server at ")
                                         .append(rommAddress)
                                         .append("…")
                                         .c_str());
        // WAIT FOR THE NETWORK RATHER THAN GIVING UP ON IT.
        //
        // This used to try once and exit 1, and on a console that is a fault
        // rather than tidiness: the session starts within a couple of seconds
        // of boot and the network is routinely not up yet, so a cold boot
        // reached RomM before the machine had an address and the frontend
        // quit. gamescope exits when its child exits, and the compositor
        // ladder then read that as ITS OWN failure and demoted the machine to
        // software rendering for the rest of the session — see
        // docs/PROJECT.md, open question 22. The ladder no longer draws that
        // conclusion; this is the other half, which is not having the race.
        //
        // IT WAS A BOUNDED WAIT until 2026-09-25 (below), and it is still not
        // the offline console. Ninety seconds covered a boot race and a router
        // coming back after a power cut. It is deliberately NOT the answer to
        // "there is no server"
        // — a console that keeps its library, plays its kept games and fills
        // in when the server returns is open question 22's design and a
        // different piece of work. This is the difference between a machine
        // that recovers from a power cut on its own and one that does not.
        //
        // AND IT OFFERS CHANGE SERVER ADDRESS (issue #60), because this is the
        // one screen a console whose server moved ever shows: it never reaches
        // Settings. After ten seconds, when a boot race is over, a second line
        // says "Press (A) to change the server address"; the same keyboard and
        // the same check as Settings follow, PIN first if one is set. The same
        // server at the new address: the start carries on there. A different
        // server: Sign out is offered here, since Settings is out of reach.
        // MMagTech, 2026-09-25. So the server is tried on a worker and this
        // loop keeps drawing and listening.
        //
        // NO LONGER BOUNDED, 2026-09-25. It gave up at ninety seconds and the
        // session started it again, which bought nothing once this screen
        // retried by itself and cost a black flash every minute and a half
        // while a server was off. MMagTech: wait for as long as it takes. The
        // (A) line is the cue to go and look at the server.
        if (const std::optional<int> rc = waitForServer(argv)) return rc;
        // WHOEVER PLAYED LAST. The active account's token is what this console
        // starts as — open question 26, decision 2. No account means a machine
        // that has not been paired, which is first run's job; one that got
        // past it (its accounts emptied by hand, a token file lost) STARTS TO
        // AN EMPTY HOME AND SAYS SO (#485), as a refused sign-in does
        // (FIRSTRUN-23, below). It exited, and the session started it again
        // two seconds later, for ever, with nothing on the screen; Settings'
        // Add an account, the way back, was never reached.
        const bool noAccount = !accounts::loadActiveToken(liveClient);
        if (noAccount) {
            std::fprintf(stderr, "[romm] no account on this console; starting to an empty Home\n");
            startProblem = "No account on this console";
        }
        // THE LIST IS HELD IN A NAMED LOCAL AND IT HAS TO BE. `accounts::find`
        // returns a pointer INTO the vector it is given — which is why it takes
        // one rather than hiding a static — so passing `accounts::all()` inline
        // leaves the pointer dangling at the end of the condition. It segfaulted
        // on the first run, in the first place the function was ever called.
        const std::vector<accounts::Account> known = accounts::all();
        if (const accounts::Account* who = accounts::find(known, accounts::activeId()))
            std::fprintf(stderr, "[accounts] acting as %d - %s\n", who->id,
                         who->name.c_str());
        if (libFromDrive) adoptAccountOffline(accounts::activeId());
        else adoptUser(liveClient);
        // RetroAchievements: started for everyone, signed in only for a
        // person who chose to (achievements.h).
        ra::init(update::bootedVersion());
        ra::useAccount(accounts::activeId());
        if (raSample) ra::useSample();
        // FOUR SMALL CALLS NOW, not the catalogue — open question 28. This
        // used to count games onto the screen as they arrived, because it took
        // several seconds and a still sentence is indistinguishable from a
        // hang. It no longer takes several seconds, so there is no longer a
        // number worth putting on a screen nobody has time to read.
        //
        // THE COUNTING LINE IS GONE AND THAT IS THE POINT, not a loss. It
        // existed to make a slow thing bearable; the slow thing was removed.
        // The server-wait countdown above survives untouched, because a router
        // coming back after a power cut is not something this can make faster.
        setup::showWaiting(waitDeps, kLoadingLine);
        startupShown = true;
        std::string platformsErr;
        lib = libFromDrive ? offlineLibrary()
              : noAccount  ? Library{}
                           : loadLibrary(liveClient, &platformsErr);
        // The references above name lib's own members, so there is nothing to
        // copy out any more.

        // EMPTY CARDS IS NO LONGER AN EMPTY LIBRARY, and this check had to
        // change with the rest. `cards` now holds recents and favourites
        // rather than the catalogue, so a console whose server is fine but
        // which has never played anything and has no favourites would have
        // been told its library came back empty and refused to start. The
        // question worth asking is whether the SERVER answered, and the
        // platform list is what answers it.
        //
        // AND A SERVER THAT ANSWERS "NO" STARTS TO HOME (#398, FIRSTRUN-23).
        // This exited for a refused sign-in (a token that expired, say) and
        // for a RomM with no system this console plays, and the session
        // started it again two seconds later, for ever, with nothing on the
        // screen: Settings, where Sign out and Add account are, was never
        // reached. Home starts empty instead and says which. Anything else
        // (the server gone again, an HTTP error) still exits, so the next
        // start waits for the server.
        if (platformTiles.empty() && !libFromDrive && !noAccount) {
            const bool refused = platformsErr.rfind("not authorised", 0) == 0;
            if (!refused && !platformsErr.empty()) {
                std::fprintf(stderr, "[romm] the library did not load: %s\n",
                             platformsErr.c_str());
                return 1;
            }
            std::fprintf(stderr, "[romm] %s; starting to an empty Home\n",
                         refused ? "the server refused this console's sign-in"
                                 : "the server has no system this console plays");
            startProblem = refused ? "RomM refused this console's sign-in"
                                   : "RomM has no systems this console plays";
        }
        // Covers come from the server, authenticated. The cache never learns
        // what a server is — it was built to take exactly this.
        //
        // AND IT ASKS THE DISK FIRST. Nothing about a library survives a boot,
        // so every start used to re-fetch every cover it drew. The path RomM
        // hands out carries the art's own timestamp, so a cached file is only
        // ever returned for the exact version that was asked for — see
        // covercache.h. Art this console has already seen costs no network at
        // all on the next boot, which matters most for the hosted server of
        // open question 29.
        covercache::setServer(rommAddress);
        images.init(imageBudget, 4, [this](const std::string& key) {
            // A PERSON'S PICTURE is asked for fresh (it can change), and a
            // copy kept so the chip and the switcher show faces offline (#88).
            // A STATE'S PICTURE ON THIS CONSOLE (#88): a file, read as one.
            if (key.rfind(storage::root() + "/", 0) == 0) return cab::readBytes(key);
            // A RETROACHIEVEMENTS BADGE (#74): from their server, and a copy
            // kept so a game's page shows its badges offline.
            if (ra::isBadgeUrl(key)) {
                // The whole path after the host: a badge and a game's icon
                // can share a file name in different folders.
                const size_t host = key.find('/', key.find("//") + 2);
                const std::string kept = covercache::dir() + "/achievements/badges/" +
                                         storage::safeSegment(key.substr(host + 1));
                if (std::vector<uint8_t> have = cab::readBytes(kept); !have.empty())
                    return have;
                std::vector<uint8_t> got = ra::fetchBytes(key);
                if (got.empty()) return got;
                storage::makeDirs(covercache::dir() + "/achievements/badges");
                cab::writeBytes(kept, got);
                return got;
            }
            if (key.rfind("/api/users/", 0) == 0) {
                const std::string kept =
                    covercache::dir() + "/avatars/" + storage::safeSegment(key);
                std::vector<uint8_t> got =
                    romm::serverAway() ? std::vector<uint8_t>() : liveClient.fetchBytes(key);
                if (got.empty()) return cab::readBytes(kept);
                storage::makeDirs(covercache::dir() + "/avatars");
                cab::writeBytes(kept, got);
                return got;
            }
            if (std::vector<uint8_t> have = covercache::read(key); !have.empty())
                return have;
            // Not asked while the server is away: each would wait out a
            // timeout. Asked again when it answers (forgetFailed).
            if (romm::serverAway()) return std::vector<uint8_t>();
            std::vector<uint8_t> got = liveClient.fetchBytes(key);
            covercache::write(key, got);
            return got;
        });
    } else {
        // A key is not a path. Everything after '#' is stripped before reading,
        // so one file can stand in for many distinct entries here.
        images.init(imageBudget, 4, [](const std::string& key) {
            const size_t hash = key.find('#');
            return ui::ImageCache::readFile(hash == std::string::npos ? key
                                                                     : key.substr(0, hash));
        });
        for (const SampleEntry& e : kSampleLibrary) {
            Card c;
            c.art = ui::Color::rgb(e.art);
            c.title = e.title;
            c.cover = e.cover ? e.cover : "";
            // The sample library is files on disk at one size, so the large
            // cover IS the cover. Keeping the field filled rather than empty
            // means the drawing code never has to ask which library it is in.
            c.coverLarge = c.cover;
            // On Recent, which shows the shelf and nothing else.
            shelf.push_back(static_cast<int>(cards.size()));
            cards.push_back(std::move(c));
        }
    }
    return std::nullopt;
}

void App::openKeyboardDemo() {
    if (keyboardDemo) {
        // The real first field: a RomM address.
        //
        // NO PREFILLED SCHEME, deliberately. A self-hosted RomM on a home LAN
        // is very often plain HTTP on a port, and prefilling "https://" pushes
        // people toward a scheme their server does not speak — which is the
        // shape of the problem tvOS has, where App Transport Security refuses
        // plain HTTP outright. That is an Apple constraint and CabinetOS does
        // not inherit it, but only if it is not designed back in. The
        // placeholder shows the common case instead: a host and a port.
        ui::Keyboard::Config cfg;
        cfg.title = "Connect to RomM";
        cfg.hint = "The same address you open in a browser.";
        cfg.placeholder = "romm.local:8080";
        cfg.shortcuts = {".local", ".com"};
        keyboard.open(cfg);
    }
}

std::optional<int> App::startDeveloperLaunch() {
    if (corePath && romPath) {
        cab::Core& core = cab::Core::shared();
        // THE DEVELOPER PATH USES THE SAME TREE AS THE PRODUCT, under a user
        // id of 0 that cannot be mistaken for a person. It used to have a
        // `saves/` directory of its own, which is half of the fault open
        // question 18 opened with: a PSP save written by `--core` was not the
        // one the library path could see.
        //
        // Set before load(), not before loadGame(): retro_init happens inside
        // load(), and a core is allowed to read the directories there.
        const std::string saveDir = storage::scratchSavesDir(corePath);
        storage::makeDirs(saveDir);
        core.setDirectories(storage::biosDir(), saveDir);
        // The same overrides the library path applies, so --core plays the
        // core the same way the product does. See beginLaunch. --core-option
        // goes on top inside the call (Core::setCommandLineOption).
        core.setOptionOverrides(catalog::optionOverrides(corePath));
        if (!core.load(corePath)) {
            std::fprintf(stderr, "[frontend] core: %s\n", core.error().c_str());
            return 1;
        }
        core.setPlayers(players::count());
        if (!core.loadGame(romPath, storage::biosDir(), saveDir)) {
            std::fprintf(stderr, "[frontend] %s\n", core.error().c_str());
            return 1;
        }
        playing = true;

        // --audio-probe, --speed-test and --state-test, with the game loaded
        // (coretests.cpp). --speed-test leaves the process from inside.
        if (const std::optional<int> rc = coreTest(core, {.audioProbe = audioProbe,
                                                          .speedTest = speedTest,
                                                          .stateTest = stateTest}))
            return *rc;

        // Audio: SDL owns the device thread and we push from the frame loop.
        // The rule from the reference implementation is that the callback must
        // never block — so there is no callback, and nothing to block.
        SDL_AudioSpec src{};
        src.format = SDL_AUDIO_S16;
        src.channels = 2;
        src.freq = static_cast<int>(core.avInfo().sampleRate);
        audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src,
                                                nullptr, nullptr);
        if (audioStream) {
            SDL_ResumeAudioStreamDevice(audioStream);
            std::fprintf(stderr, "[frontend] audio out at %d Hz\n", src.freq);
        } else {
            // A console with no sound card is still a console. Say so and play on.
            std::fprintf(stderr, "[frontend] no audio device: %s\n", SDL_GetError());
        }
    }
    return std::nullopt;
}

void App::settleScreensAtStart() {
    // Favorites when nothing has been played, as homeFromTheStart.
    focusRow = homeTopRow();
    // --focus N still means "start on card N of Recent", which is what every
    // existing capture script passes it for. On an empty row the high bound
    // is held at 0, not -1, which std::clamp leaves undefined (#488,
    // HOME-21); slot 0 of an empty row is no card, as -1 was.
    if (initialFocus >= 0) {
        focusRow = RowRecent;
        focusSlot = std::clamp(initialFocus, 0, std::max(0, static_cast<int>(shelfSlots()) - 1));
    }
    if (initialRow >= 0) {
        focusRow = std::clamp(initialRow, 0, static_cast<int>(RowFavorites));
        if (!rowExists(focusRow)) focusRow = homeTopRow();
        focusSlot =
            std::clamp(focusSlot, 0, std::max(0, static_cast<int>(rowSlots(focusRow)) - 1));
    }
    scrollY.from = scrollY.to = 0.0f;
    backdropMix.from = backdropMix.to = 1.0f;
    // EASE-IN-OUT, the design system's curve for a state change worth watching.
    // Every other animation on these screens is ease-out because it is focus
    // arriving; this one is the room itself changing and it has to leave as
    // deliberately as it arrives.
    backdropMix.smooth = true;
    settleFocus();
    manualSlide.smooth = true;
}

void App::startBrowsing() {
    libraryScreen.build(platformTiles, collectionTiles);
    if (rommAddress) startCoverFill();
    if (rommAddress && !platformTiles.empty()) {
        std::vector<int> live;
        for (const screens::Tile& t : platformTiles) live.push_back(t.id);
        coverTidy = std::thread([live]() {
            covercache::sweep(live);
            covercache::evict(covercache::kBudgetBytes);
        });
    }
    if (SDL_JoystickID* ids = SDL_GetGamepads(&padCount)) {
        for (int i = 0; i < padCount; ++i) players::added(ids[i]);
        SDL_free(ids);
    }
    std::fprintf(stderr, "[frontend] gamepads at startup: %d\n", padCount);
}

void App::startSync() {
    uploader.start(&liveClient);
    for (const storage::User& u : storage::knownUsers()) playtime::recover(u);
    // Whatever an earlier run could not send goes first.
    uploader.resendOwed();
    // OFFLINE PLAY (#88): the server tried in the background whenever it is
    // away, and what offline needs fetched while it is not.
    if (rommAddress) {
        startReconnect(rommAddress);
        if (!libFromDrive) backfillDrive(rommAddress);
    }
    shownAway = romm::serverAway();
}

// SAVES A POWER CUT LEFT ON A DRIVE: an Xbox game's folder still holding
// the note beforeStart wrote. Its saves come off now and go up as if the
// game had just ended; the note names the person's folder and the game.
// Nothing evicts such a folder in the meantime (cache.cpp, candidates).
void App::recoverPowerCutSaves() {
    if (const cab::standalone::Emulator* xemu = cab::standalone::find("xemu")) {
        for (const std::string& loc : storage::locations()) {
            for (const std::string& half : {storage::cacheDir(loc), storage::romsDir(loc)}) {
                for (const std::string& platform : listNames(half)) {
                    for (const std::string& entry : listNames(half + "/" + platform))
                        recoverDriveSaves(*xemu, half + "/" + platform + "/" + entry);
                }
            }
        }
    }
    // AND THE NOTES IN THIS PERSON'S OWN FOLDERS, `saves/<platform>/<rom
    // id>/<core>/`: an Xbox 360, Wii U, Switch or PS3 game's (`notesPlaying`,
    // the emulator writes there), and an Xbox game's whose saves another
    // person's start brought off the drive for them (recoverDriveSaves,
    // below). Only this person's, since the upload goes as them; anyone
    // else's is finished when they next start a game of that kind here, or
    // the console starts as them.
    const std::string saves = storage::userDir(storage::currentUser()) + "/saves";
    for (const std::string& platform : listNames(saves))
        for (const std::string& rom : listNames(saves + "/" + platform))
            for (const cab::standalone::Emulator* emu : cab::standalone::all()) {
                const std::string dir = saves + "/" + platform + "/" + rom + "/" + emu->core;
                if (cab::standalone::hasPlayingNote(dir))
                    finishInterruptedSave(*emu, dir, uploader);
            }
}

// ONE XBOX GAME'S FOLDER, as above: true when it holds no note, or its
// saves came off. False only while they stay on the drive because it would
// not give them up: nothing may write that drive until they are off. A
// launch of the game runs it too, before beforeStart would put this
// person's older copy over them (#398, EMU-12).
//
// WHOSE THEY ARE DECIDES WHERE THEY GO (#484, MMagTech, 2026-10-19). This
// person's go up now, as if their game had just ended. Another person's are
// copied into that person's folder, exactly as their own end would do, and
// the note is left there rather than on the drive: the upload goes only as
// them, through what Xbox 360 and Wii U use (finishInterruptedSave, at their
// next launch of the game or the next start of the console as them,
// recoverPowerCutSaves), and the game starts for whoever is here. Until then
// another person's stayed on the drive and the game was refused to everyone
// else until that person played it. Saves whose folder names nobody on this
// console (the person was removed, and their folder with them) are treated
// as removed with them: one line here, and the game starts.
bool App::recoverDriveSaves(const cab::standalone::Emulator& xemu, const std::string& path) {
    if (!cab::standalone::hasPlayingNote(path)) return true;
    const std::string text = cab::standalone::playingNote(path);
    std::istringstream note(text);
    std::string id, dir, title, tag;
    std::getline(note, id);
    std::getline(note, dir);
    std::getline(note, title);
    std::getline(note, tag);
    const std::string mine = storage::userDir(storage::currentUser()) + "/";
    const bool theirs = dir.compare(0, mine.size(), mine) != 0;
    if (theirs) {
        bool known = false;
        for (const storage::User& u : storage::knownUsers()) {
            const std::string own = storage::userDir(u) + "/";
            if (dir.compare(0, own.size(), own) == 0) known = true;
        }
        if (!known) {
            std::fprintf(stderr, "[xbox] %s holds saves of nobody on this console any more "
                         "(%s); removed with them\n", path.c_str(), dir.c_str());
            cab::standalone::removePlayingNote(path);
            return true;
        }
        // THEIR NOTE FIRST, THEN THEIR SAVES. A stop between the two leaves
        // both notes, and the next start does this again. The other order
        // would leave their saves in their folder with nothing to say they
        // are owed, and their next launch would unpack the server's older
        // zip over them (restoreDirSave).
        storage::makeDirs(dir);
        if (!cab::standalone::writePlayingNote(dir, text)) {
            std::fprintf(stderr, "[xbox] saves left on %s: could not mark %s\n", path.c_str(),
                         dir.c_str());
            return false;
        }
    }
    GameSession owed;
    owed.romId = std::atoi(id.c_str());
    owed.title = title;
    owed.saveDir = dir;
    owed.saveTag = tag;
    owed.dirSaveRoot = cab::standalone::saveRoot(xemu, dir);
    owed.dirAtLaunch = cab::listTree(owed.dirSaveRoot);
    std::string aerr;
    if (!cab::standalone::afterEnd(xemu, path, dir, &aerr)) {
        std::fprintf(stderr, "[xbox] saves left on %s: %s\n", path.c_str(), aerr.c_str());
        return false;
    }
    if (theirs) {
        std::fprintf(stderr, "[xbox] %s held another person's saves; copied into %s, to go "
                     "up when the console next acts as them\n", path.c_str(), dir.c_str());
        return true;
    }
    std::fprintf(stderr, "[xbox] finished what a stop left on %s\n", path.c_str());
    syncDirSave(owed, uploader);
    return true;
}

// WHAT THE MENUS ARE DRAWN IN — #75 and #129, 2026-09-27. The colour is
// the signed-in person's (accounts.json); Appearance and its hours are the
// console's (settings.json). See look.h.
void App::applyStartColour() {
    appearance = look::appearanceFromWord(prefs::get("appearance", "standard"));
    // Scheduled's starting hours: dark from 8 PM until 7 AM.
    darkFrom = savedHour("dark_from", 20);
    darkUntil = savedHour("dark_until", 7);
    applyAccountColour(true);
    if (lookColour) look::setColour(look::colourFromWord(lookColour), true);
    if (lookDark) appearance = look::Appearance::Dark;
    look::setDark(wantDark(), true);
}

// WHY HOME IS EMPTY (connectToServer), said once Home is up and after
// "Updated to", since one notice shows at a time.
void App::sayStartProblem() {
    if (!startProblem.empty() && updStartNotice.empty() && menuNotice.life <= 0.0f &&
        !playing && here() == Screen::Home && curtain.value() < 0.01f) {
        menuNotice.say(startProblem, Tone::Problem);
        startProblem.clear();
    }
}

// AFTERWARDS: the first start after a restart compares the version the
// machine booted with the one that was staged. Said once Home is up.
void App::noticeAppliedUpdate() {
    const std::string pending = prefs::get("update_pending", "");
    if (!pending.empty() && prefs::get("update_pending_boot", "") != update::bootId()) {
        const std::string booted = update::bootedVersion();
        const bool applied = booted == pending;
        std::fprintf(stderr, "[update] staged %s, booted %s: %s\n", pending.c_str(),
                     booted.empty() ? "(no version)" : booted.c_str(),
                     applied ? "applied" : "did not apply");
        updStartNotice = applied ? "Updated to " + pending : "Update didn't apply";
        updStartTone = applied ? Tone::Done : Tone::Problem;
        prefs::set("update_pending", "");
        prefs::set("update_pending_boot", "");
        if (applied) prefs::set("update_found", "");
    }
}

void App::settleTabsAtStart() {
    keyboardSlide.smooth = true;
    tabDissolve.smooth = true;
    tabDissolve.from = tabDissolve.to = 0.0f;
}

// THE REST OF AN EXIT IS GIVEN TWO SECONDS (#305), once the saves are sent
// and the unlocks are on disk. Everything after that is a watcher's look, a
// cache, or a request somebody can make again, and every one of them can
// wait on something outside: a RomM call 30 s, a look at Sunshine 8 s, a
// Steam slice job minutes. Nothing bounded them, and the session waits for
// this process before Steam starts. Measured on the A9, 2026-10-19: the
// rest takes 0.2 to 0.6 s; the journal's 21 hand-overs since 2026-10-04
// had it gone in 0.06 to 0.28 s but once in 2.4 s, with no line to say
// where. Past the two seconds the process ends here, the way Restart and
// Power off end it every day (gamescope's reaper), and says what it was
// waiting on. Not for a restart in place: that one must reach its exec.
namespace {
constexpr int kExitRestSeconds = 2;
std::atomic<const char*> gExitStep{""};

void boundTheRestOfTheExit() {
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(kExitRestSeconds));
        std::fprintf(stderr, "[shutdown] %d s after the saves, still waiting on %s; ending now\n",
                     kExitRestSeconds, gExitStep.load());
        ::_exit(0);
    }).detach();
}
}  // namespace

int App::shutDown(char** argv) {
    // Drained rather than abandoned: anything still queued is a save somebody
    // has already made, and quitting is not a reason to throw it away. This is
    // the one place a wait for the network is correct, because there is no
    // picture left to stop.
    if (uploader.pending() > 0)
        std::fprintf(stderr, "[sync] finishing %d upload(s)\n", uploader.pending());
    uploader.shutdown();
    // And an unlock still waiting to go out is written to its queue on disk.
    ra::shutdown();
    if (!restartSelf) boundTheRestOfTheExit();
    // A STEAM DOWNLOAD IS CANCELLED, not waited for (#485, SETTINGS-04):
    // SteamJobStop joins the Steam job's thread as the App goes, and the
    // download script ends only on a cancel or its parent gone. Cancelled,
    // proc::run stops it within about two seconds. Only the download has a
    // cancel: making, growing or removing Steam's space (steam.cpp) clears
    // cached games and then waits on the root helper, up to 15 minutes, and
    // an install cancelled meanwhile stops once its space is made. A quit
    // ends two seconds after the saves anyway (the bound above); a restart
    // in place execs before the App goes, so it joins nothing.
    if (steamInstall) steamInstall->cancel = true;
    // THE WATCHING THREADS JOINED BEFORE THEY ARE DESTROYED: a std::thread
    // still running at exit ends the process with abort().
    gExitStep = "Remote Play's watcher";
    remoteplay::watch(false);
    remoteplay::join();
    gExitStep = "Tailscale's watcher";
    tailscale::watch(false);
    tailscale::cancelLogin();
    tailscale::join();
    gExitStep = "Tailscale's sign-out";
    if (tsWorker.joinable()) tsWorker.join();
    gExitStep = "the report";
    stopReport();
    // AND ADD AN ACCOUNT'S PAIRING (#398, SETTINGS-03; part of #406). It was
    // joined only before the next pairing, so after any visit to Add an
    // account the App was destroyed holding a joinable thread and Switch to
    // Steam ended in std::terminate. A poll still waiting stops at once; one
    // in flight is the server's answer away.
    gExitStep = "an account's pairing";
    {
        std::lock_guard<std::mutex> lk(addJob->m);
        addJob->stop = true;
    }
    addJob->wake.notify_all();
    if (addJob->th.joinable()) addJob->th.join();
    // AND THE REST OF #406: a Remove or Sign-out send, stopped after the file
    // on the wire (what is left stays owed), the reconnect watcher and the
    // drive backfill, each at its next step.
    gExitStep = "a Remove or Sign out";
    removeJob->stop = true;
    if (removeJob->th.joinable()) removeJob->th.join();
    gExitStep = "offline sync";
    stopOfflineSync();

    gExitStep = "the covers";
    images.shutdown();
    gExitStep = "the window";
    text.shutdown();
    renderer.shutdown();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    gExitStep = "the App's workers or the end of the process";

    // STARTING AGAIN IN PLACE, for Sign out and a new server address. Not by
    // exiting: gamescope ends with its child, and the session coming back up
    // is seconds of black. exec keeps the process, so gamescope stays and the
    // startup screen follows the one this left on. Every descriptor is closed
    // first so nothing this run held (the power delay lock above all) is
    // carried into the next one.
    if (restartSelf) {
        std::fprintf(stderr, "[frontend] starting again\n");
        std::fflush(stderr);
        ::close_range(3, ~0U, 0);
        ::execv("/proc/self/exe", argv);
        std::fprintf(stderr, "[frontend] could not start again: %s\n", std::strerror(errno));
        return 1;
    }
    return 0;
}
