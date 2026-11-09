// The test hooks: --screen and --focus-bar, --nav, and the flags the frame
// loop answers at fixed points (--evict-test, --download, --switch-account,
// --unkeep, --then, --launch, --overlay, --overlay-test, --overlay-exit,
// --state-check, --load-state, --sync-test, --notice-gallery), and the
// --screenshot and SIGUSR1 captures. Each runs where it ran in main() (#398,
// restructure batch g).

#include "app.h"

#include <algorithm>
#include <cstdio>

#include "capture.h"
#include "core.h"
#include "filesave.h"
#include "noticegallery.h"
#include "saves.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// Opening straight onto a screen, for a capture. This walks the SAME route
// a person would: the Library is entered, a tile is opened, the launch
// screen is opened from a card. A capture that built a screen some other
// way would be photographing something the product cannot reach.
// SEARCH IS ITS OWN ROUTE, because walking to it is not walking to the
// Library. `--screen search --query metal` types the query the way a person
// would type it and leaves focus in the results, which is the state worth
// photographing — an empty Search is a picture of a keyboard.
// THE SWITCHER IS REACHED THE WAY A PERSON REACHES IT: focus the bar, walk
// to the chip, press it. A capture that called open() directly would be
// photographing a panel the product might not be able to get to.
void App::openScreenForCapture() {
    if (focusBarSlot >= 0) {
        barFocused = true;
        barSlot = std::clamp(focusBarSlot, 0, BarCount - 1);
    }
    if (initialScreen && (SDL_strcmp(initialScreen, "accounts") == 0 ||
                          SDL_strcmp(initialScreen, "add-account") == 0)) {
        barFocused = true;
        barSlot = BarAccount;
        barKey(screens::Nav::Activate);
        if (SDL_strcmp(initialScreen, "add-account") == 0) {
            // WALKS DOWN TO THE ADD ROW FIRST, and not doing so switched the
            // console to somebody else. This pressed Activate straight away,
            // which was right while the panel held only the Add row and wrong
            // the moment a second account existed — row 0 became a person, so
            // the capture route signed the console in as them.
            //
            // The lesson is the one this project keeps paying for: a route
            // that walks the way a person walks has to keep walking when the
            // screen gains a row. Down until the focus stops moving, then
            // press, which is what a person does.
            for (int guard = 0; guard < 16; ++guard)
                accountScreen.key(screens::Nav::Down);
            // Presses the Add row the way a person would. It starts a REAL
            // pairing against the real server — a device code that expires in
            // minutes and creates nothing unless somebody approves it.
            apply(accountScreen.key(screens::Nav::Activate));
            // AND THE CAPTURE HAS TO WAIT FOR THE SERVER. This loop is not
            // paced, so on the A9 four hundred frames go by in well under a
            // second and the shot comes out with no code on it — the same trap
            // `--launch-after` fell into and the same one that cost the setup
            // pairing capture. Gate the shot on the fact rather than on a
            // frame count.
            waitForPairCode = true;
        }
    } else if (initialScreen && SDL_strcmp(initialScreen, "howto") == 0) {
        // `--screen howto`: About's How to page over Home, for a capture or
        // the TV loop; Back goes Home.
        openHowTo();
    } else if (initialScreen && SDL_strcmp(initialScreen, "settings") == 0) {
        // `--screen settings --tile N` opens on category N; `--focus-row 1`
        // puts focus in its rows. The network is waited for here, because a
        // capture has no wall clock and would otherwise always say Checking.
        goToDestination(BarSettings);
        if (settingsNet.th.joinable()) settingsNet.th.join();
        if (settingsWifi.th.joinable()) settingsWifi.th.join();
        buildSettings();
        settingsScreen.focusCategory(initialTile, initialRow > 0);
    } else if (initialScreen && SDL_strcmp(initialScreen, "search") == 0) {
        goToDestination(2);
        if (searchQuery) {
            keyboard.typeText(searchQuery);
            searchTyped = keyboard.value();
            searchScreen.setQuery(searchTyped);
            // Asked straight away rather than waiting out the debounce: a
            // capture has no typing to wait for, and a screenshot of the
            // Searching… state is not what anybody asked for.
            runSearch(searchTyped);
            if (searchScreen.resultCount() > 0) searchScreen.setFocused(true);
        }
    } else if (initialScreen) {
        // The rest are the Library and the screens under it. A name that is
        // none of them says so rather than passing a Library capture off as
        // the screen asked for (#488, PLAY-28).
        if (SDL_strcmp(initialScreen, "library") != 0 && SDL_strcmp(initialScreen, "grid") != 0 &&
            SDL_strcmp(initialScreen, "detail") != 0)
            std::fprintf(stderr, "[screen] unknown screen '%s': showing the Library\n",
                         initialScreen);
        libraryScreen.enter();
        stack.push_back(Screen::Library);
        if (initialTab > 0) {
            // Walks the switcher the way a person would, rather than setting a
            // field: the tab change resets the grid position, and a capture
            // that skipped that would be photographing a state the product
            // cannot be in.
            libraryScreen.key(screens::Nav::Right);
            libraryScreen.key(screens::Nav::Activate);
        }
        if (SDL_strcmp(initialScreen, "library") == 0 && initialTile > 0)
            libraryScreen.focusTile(initialTile);
        if (SDL_strcmp(initialScreen, "grid") == 0 ||
            SDL_strcmp(initialScreen, "detail") == 0) {
            apply(screens::Result{screens::Action::OpenTile, initialTile});
        }
        if (SDL_strcmp(initialScreen, "detail") == 0) {
            int card = -1;
            for (size_t i = 0; i < cards.size(); ++i) {
                if (initialGame > 0 ? cards[i].id == initialGame : false) {
                    card = static_cast<int>(i);
                    break;
                }
            }
            // With no --game, the first game of the opened tile, which is what
            // somebody checking the layout wants and needs no id to hand.
            if (card < 0 && here() == Screen::Grid)
                apply(gridScreen.key(screens::Nav::Activate));
            else if (card >= 0)
                openDetailSoon(card);
        }
    }
}

// Here rather than with --screen, because Home's keys are only wired up
// just above; walking before that pressed nothing.
void App::walkNavScript() {
    if (navScript) {
        // NO START-UP SCREEN FOR A WALK, as for a capture (startCurtain):
        // navigate drops every press while it is down over Home, so a walk
        // without --screenshot pressed nothing at all (#488).
        if (arriving) {
            arriving = false;
            curtain.from = curtain.to = 0.0f;
        }
        std::string word;
        for (const char* p = navScript;; ++p) {
            if (*p && *p != ',') { word += *p; continue; }
            screens::Nav n;
            bool ok = true;
            if (word == "up") n = screens::Nav::Up;
            else if (word == "down") n = screens::Nav::Down;
            else if (word == "left") n = screens::Nav::Left;
            else if (word == "right") n = screens::Nav::Right;
            else if (word == "a") n = screens::Nav::Activate;
            else if (word == "b") n = screens::Nav::Back;
            else ok = false;
            // The shoulders, for the manual on a game's page (#364).
            if (!ok && (word == "r1" || word == "l1")) {
                const int d = word == "r1" ? 1 : -1;
                if (!shoulderOnPage(d)) switchDestination(d);
                // A capture shows the slide finished.
                if (shotMode) manualSlide.settle(manualShown ? 1.0f : 0.0f);
                word.clear();
                if (!*p) break;
                continue;
            }
            // A KEY OR THE PAD'S START, handed to handleEvent as SDL hands
            // them over, so what input.cpp does with them is walked too
            // (#484): "key:]" a press, "held:]" the system's repeat of a key
            // already down, "start" the pad's Start. A key by SDL's name for
            // it ("F5", "Return"); a comma cannot be one.
            if (!ok && (word.rfind("key:", 0) == 0 || word.rfind("held:", 0) == 0 ||
                        word == "start")) {
                SDL_Event e{};
                if (word == "start") {
                    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                    e.gbutton.button = SDL_GAMEPAD_BUTTON_START;
                    e.gbutton.down = true;
                } else {
                    const bool held = word[0] == 'h';
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDL_GetKeyFromName(word.c_str() + (held ? 5 : 4));
                    e.key.down = true;
                    e.key.repeat = held;
                }
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_UNKNOWN)
                    std::fprintf(stderr, "[nav] unknown key '%s'\n", word.c_str());
                else
                    handleEvent(e);
                word.clear();
                if (!*p) break;
                continue;
            }
            // A PAD BUTTON PRESSED AND LET GO, by SDL's name for it
            // ("pad:y", "pad:dpup"), and TEXT a physical keyboard typed
            // ("text:romm.local"), through the event handler too: the
            // on-screen keyboard is driven only there, its Y and A are a
            // pad's, and a direction pressed and never let go would go on
            // repeating (#668).
            if (!ok && word.rfind("pad:", 0) == 0) {
                const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(word.c_str() + 4);
                if (b == SDL_GAMEPAD_BUTTON_INVALID) {
                    std::fprintf(stderr, "[nav] unknown button '%s'\n", word.c_str());
                } else {
                    SDL_Event e{};
                    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                    e.gbutton.button = static_cast<Uint8>(b);
                    e.gbutton.down = true;
                    handleEvent(e);
                    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
                    e.gbutton.down = false;
                    handleEvent(e);
                }
                word.clear();
                if (!*p) break;
                continue;
            }
            if (!ok && word.rfind("text:", 0) == 0) {
                SDL_Event e{};
                e.type = SDL_EVENT_TEXT_INPUT;
                e.text.text = word.c_str() + 5;
                handleEvent(e);
                word.clear();
                if (!*p) break;
                continue;
            }
            // A digit is a PIN pad key typed on a keyboard, so a whole PIN
            // can be walked headless: "down,a,4,8,2,1,4,8,2,1".
            if (!ok && word.size() == 1 && word[0] >= '0' && word[0] <= '9') {
                pinOutcome(pinScreen.typeDigit(word[0]));
                word.clear();
                if (!*p) break;
                continue;
            }
            if (ok) navigate(n);
            else if (!word.empty()) std::fprintf(stderr, "[nav] unknown press '%s'\n", word.c_str());
            word.clear();
            if (!*p) break;
        }
        std::fprintf(stderr, "[nav] after '%s': screen %d, bar %s slot %d%s%s\n", navScript,
                     static_cast<int>(here()), barFocused ? "focused" : "not focused",
                     barSlot, pinScreen.isOpen() ? ", PIN pad open" : "",
                     overlayOpen && powerMenu ? ", Power menu open" : "");
        // A capture shows the pad at rest, not part-way through its fade.
        if (pinScreen.isOpen()) pinScreen.settle();
        if (choiceScreen.isOpen()) choiceScreen.settle();
    }
}

void App::evictTestHook() {
    if (evictTest && frame == 5) {
        // Five covers requested once and never asked for again: the shelf
        // has scrolled past them.
        for (int i = 0; i < 5; ++i) {
            images.get("covers/b-3x4.png#scrolled-past-" + std::to_string(i));
        }
    }
}

// Launch on a timer when asked to. This exists so the Home-to-game
// transition can be watched on the test machine, which has no
// controller attached — not as a product behaviour.
// The same errand as the launch above, for the action a person takes on
// the launch screen. It goes through downloadById, so the floors are
// checked exactly as they would be for a press.
// Once Home is up, as a person's press would be: a download begun
// behind the startup screen takes the curtain from it (pumpArrive
// gives the curtain to any launch job) and the screen stays black.
void App::downloadHook() {
    if (autoDownloadId > 0 && !launchJob.busy() && !arriving) {
        const int id = autoDownloadId;
        autoDownloadId = 0;
        // ASKED FOR BY ID, as --launch is (below): only Recent and
        // Favorites are in the library at boot, so any other game was not
        // found and the download never started, without a word. That is
        // how the first capture of the download ring (#145) came out as an
        // empty bar.
        if (lib.byRomId.find(id) == lib.byRomId.end()) {
            romm::Game g;
            std::string err;
            if (liveClient.fetchGame(id, &g, &err)) appendGame(lib, g);
            else std::fprintf(stderr, "[download] could not fetch rom %d: %s\n", id,
                              err.c_str());
        }
        downloadById(id);
    }
}

// --switch-account, --unkeep, --then and --launch, after the workers'
// results and before the pumps.
void App::launchHooks(float dt) {
    if (autoSwitchAccountId > 0) {
        const int id = autoSwitchAccountId;
        autoSwitchAccountId = 0;
        std::string why;
        if (!switchAccount(id, &why))
            std::fprintf(stderr, "[accounts] refused: %s\n", why.c_str());
        ra::useAccount(accounts::activeId());
    }
    if (autoUnkeepId > 0) {
        const int id = autoUnkeepId;
        autoUnkeepId = 0;
        removeDownload(id);
    }
    // ONCE THE FIRST GAME IS OVER, with nothing in flight: not while an
    // emulator of its own runs (`playing` is false all through such a game,
    // and the second launch fired over its pause menu), and not while the
    // curtain comes down on a finished job (busy() is false there, and the
    // game starts only when it is taken) (#488).
    if (thenLaunchId > 0 && !overlayExitDemo && !playing && !standaloneRun.active() &&
        autoLaunchId == 0 && !launchJob.busy() && !launchJob.waiting()) {
        std::fprintf(stderr, "[launch] then: %d\n", thenLaunchId);
        autoLaunchId = thenLaunchId;
        thenLaunchId = 0;
        autoLaunchAfter = 1.0f;
    }
    if (autoLaunchId > 0 && !playing) {
        autoLaunchAfter -= dt;
        if (autoLaunchAfter <= 0.0f) {
            const int id = autoLaunchId;
            autoLaunchId = 0;
            // NOT ON HOME, SO NOT IN THE LIBRARY, since 2026-09-22: boot
            // stopped fetching the whole catalogue, and `--launch` quietly
            // lost every game that is not Recent or a Favorite. It said
            // "no game with id" and idled, which is how the save work of
            // 2026-09-23 found it. Asked for by id instead, the way a
            // platform grid or Search would have fetched it.
            if (lib.byRomId.find(id) == lib.byRomId.end()) {
                romm::Game g;
                std::string err;
                if (liveClient.fetchGame(id, &g, &err)) {
                    appendGame(lib, g);
                    std::fprintf(stderr, "[launch] fetched %s by id\n",
                                 g.name.c_str());
                } else {
                    std::fprintf(stderr, "[launch] could not fetch rom %d: %s\n",
                                 id, err.c_str());
                }
            }
            launchById(id);
        }
    }
}

// --overlay, --overlay-test and --overlay-exit, after the pending state
// loads and before the draw.
void App::overlayHooks() {
    if (overlayDemo && playing && !overlayOpen &&
        cab::Core::shared().framesRun() >= static_cast<uint64_t>(overlayDemoAfter)) {
        overlayDemo = false;
        toggleOverlay();
    }

    // --overlay-test: hold the pause menu open over a game this console did
    // not draw. `playing` is asserted so the whole world — backdrop, hero,
    // shelves, screens — is skipped; there is no core, so no game picture is
    // drawn either, and what reaches the screen is the scrim and the panel
    // over transparency. That is the thing being looked at.
    if (overlayTest) {
        playing = true;
        if (!overlayOpen) {
            overlayOpen = true;
            overlayFade.retarget(1.0f, overlayFadeSeconds);
        }
    }
    if (overlayExitDemo && playing) {
        static int t = 0;
        // How long the game is left running before the overlay quits it.
        // Was a fixed 120 frames, which is two seconds — long enough to
        // watch the transition and far too short for anything else. A PSP
        // game is still BOOTING at that point, and a save cannot be tested
        // at all because the game has not had time to write one.
        if (++t == overlayExitAfter) {
            toggleOverlay();
            overlaySlot = static_cast<int>(pauseItems.size()) - 1;   // Exit to Home
        }
        if (t == overlayExitAfter + 60) { overlayExitDemo = false; overlayActivate(); }
    }
}

// --state-check, --load-state and --sync-test, after the disc rig.
void App::stateHooks() {
    if (stateCheckAfter > 0 && playing) {
        static std::vector<uint8_t> first;
        static uint64_t savedAt = 0;
        cab::Core& core = cab::Core::shared();
        if (first.empty() && core.framesRun() >= static_cast<uint64_t>(stateCheckAfter)) {
            if (!core.saveState(first) || first.empty()) {
                std::fprintf(stderr, "[state-check] %s cannot make a state\n",
                             launchJob.coreName.c_str());
                stateCheckAfter = 0;
            } else {
                savedAt = core.framesRun();
            }
        } else if (!first.empty() && core.framesRun() >= savedAt + 120) {
            const bool loaded = core.loadState(first);
            std::vector<uint8_t> again;
            core.saveState(again);
            size_t same = 0;
            for (size_t k = 0; k < std::min(first.size(), again.size()); ++k)
                if (first[k] == again[k]) ++same;
            std::fprintf(stderr,
                         "[state-check] %s: %zu bytes, load %s, re-save %s "
                         "(%zu of %zu bytes equal)\n",
                         launchJob.coreName.c_str(), first.size(),
                         loaded ? "ACCEPTED" : "REFUSED",
                         again == first ? "IDENTICAL" : "DIFFERS",
                         same, first.size());
            stateCheckAfter = 0;
        }
    }

    if (loadStateAfter > 0 && playing &&
        cab::Core::shared().framesRun() >= static_cast<uint64_t>(loadStateAfter)) {
        loadStateAfter = 0;
        std::fprintf(stderr, "[load-state] loading the newest state\n");
        beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
    }

    // The round trip, once, a couple of seconds into the game so there is
    // something in memory worth snapshotting.
    if (syncTest && playing) {
        static int sinceStart = 0;
        if (++sinceStart == 150) {
            std::fprintf(stderr, "[sync-test] --- saving ---\n");
            const uint64_t t0 = SDL_GetTicksNS();
            saveStateNow(session, uploader, menuNotice);
            // Forced: at a title screen with no input the battery has not
            // changed, and "nothing to send" is the correct behaviour but
            // proves nothing about whether sending works.
            session.saveAtLaunch.clear();
            syncSave(session, uploader);
            // The same forcing for the saves the core writes as files, and
            // it is the only way to see that half send anything: those are
            // captured after the unload rather than here, and a headless
            // run cannot press the buttons that would make a game save.
            // So the baseline is dropped now and the quit-time capture
            // treats whatever the core flushed as new. `hadOne` goes with
            // it, because the freshness guard is the other thing under
            // test and a forced run must not be stopped by it.
            for (cab::FileSaveState& f : session.fileSaves) {
                f.atLaunch.clear();
                f.hadOne = true;
            }
            std::fprintf(stderr,
                         "[sync-test] %zu file save(s) will be sent at the quit\n",
                         session.fileSaves.size());
            // What the game actually paid. Everything after this point is
            // on the worker, so this is the whole cost to the picture.
            std::fprintf(stderr, "[sync-test] frame thread blocked %.2f ms\n",
                         (SDL_GetTicksNS() - t0) / 1e6);
            std::fprintf(stderr, "[sync-test] --- loading back ---\n");
            beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
            std::fprintf(stderr, "[sync-test] --- done ---\n");
        }
    }
}

// --notice-gallery: every notice the console says, each for its life.
void App::noticeGalleryHook(float dt) {
    if (noticeGallery) {
        // Each for as long as it really stays up, a second apart, the
        // first after two so the screen has settled. A Busy one is held
        // 3.8 s, since nothing will answer it.
        static float galleryClock = -2.0f;
        static int galleryNext = 0;
        galleryClock += dt;
        constexpr int kN = static_cast<int>(sizeof kNoticeGallery / sizeof kNoticeGallery[0]);
        if (galleryClock >= 0.0f && galleryNext < kN * 100) {
            const GalleryNotice& g = kNoticeGallery[galleryNext % kN];
            std::fprintf(stderr, "[notice] gallery %d/%d: %s\n", galleryNext % kN + 1, kN,
                         g.text);
            menuNotice.say(g.text, g.tone);
            if (g.tone == Tone::Busy) menuNotice.life = 3.8f;
            ++galleryNext;
            galleryClock = -(menuNotice.life + 1.0f);
        }
    }
}

// Capture before the swap. After a swap the back buffer's contents are
// undefined, so a readback taken there is whatever the driver left.
// A SIGUSR1 OR DISC-SCRIPT STILL ASKED WHILE THE PICTURE IS ANOTHER
// WINDOW'S (PCSX2's, or an emulator of its own): this frame draws nothing,
// and a still taken later held whatever this window drew next, a menu over
// black, long after it was asked for (#488, PLAY-10). Said and dropped;
// gamescope's own capture takes those games (docs/lessons/testing.md).
void App::refuseCaptureOverAnotherWindow(const char* whose) {
    if (!gCaptureRequested) return;
    gCaptureRequested = 0;
    discScript.captureTo.clear();
    std::fprintf(stderr, "[capture] not taken: the picture is %s own window; use "
                         "gamescopectl screenshot\n", whose);
}

void App::captureFrame(int dw, int dh) {
    if (shotMode && frame >= shotAfterFrames &&
        (!waitForPairCode || shownPairCode) &&
        // The manual's page on screen, or its refusal (#364), however
        // long fetching and drawing take.
        (!manualShown || manualView.settled())) {
        renderer.saveFrame(shotPath, dw, dh);
        // Whether the running core can produce a state AT THIS POINT, which
        // is a different question from whether the round trip is exact and
        // is the only half of it a capture can answer.
        //
        // It is here because --state-test cannot answer it for every core.
        // Its warm-up is a tight loop of retro_run with no wall clock in
        // it, and a core that emulates on a thread of its own — PPSSPP is
        // the only one — barely advances in that loop: no sound, a static
        // picture and a zero-byte state after three thousand calls, while
        // the same core reaches its attract demo on the ordinary launch
        // path. A capture has a real frame loop under it, so the number
        // here is about the core rather than about the instrument.
        if (playing) {
            cab::Core& c = cab::Core::shared();
            std::fprintf(stderr, "[state] serialize size at capture: %zu bytes\n",
                         c.stateSize());
        }
        running = false;
    }
    if (gCaptureRequested) {
        gCaptureRequested = 0;
        renderer.saveFrame(discScript.captureTo.empty() ? "/tmp/cabinetos-frame.bmp"
                                                        : discScript.captureTo.c_str(),
                           dw, dh);
        discScript.captureTo.clear();
    }
}
