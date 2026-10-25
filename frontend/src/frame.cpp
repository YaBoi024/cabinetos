// The frame loop: the events, the frame clock, every per-frame step in its
// order, the ticks, the drawing in its order (the world, presentScene, the
// glass and the panels, the dim), the captures and the present. Each step
// is a call into the file that owns it; THE ORDER IS THE BEHAVIOUR, and the
// comments in place say why each is where it is (#398, restructure batch g).

#include "app.h"

#include <algorithm>

#include "core.h"
#include "powerprofile.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// The function statics in this loop stay function statics (#398, batch c):
// run() runs once, so each is one object for the life of the process, as
// in main(). The [pace] counters among them are never reset at a launch,
// so a new game's first [pace] line carries the last game's tail; that is
// recorded in the audit (Part 2, 3.14 item 12), not changed here.
void App::runFrameLoop() {
    while (running) {
        batteryRowBefore = batteryRowNow;   // settleBatteryRow (PLAY-07)
        batteryRowNow = false;
        // Full speed while any game runs, balanced on Home (powerprofile.h).
        powerprofile::update(playing || standaloneRun.active());
        // RetroAchievements' network answers, on this thread (achievements.h).
        ra::pump();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (takeStandaloneEvent(e)) continue;
            handleEvent(e);
        }

        uint64_t now = SDL_GetTicksNS();
        float dt = static_cast<float>(now - previous) / 1e9f;
        previous = now;
        // A stall must not teleport an animation; the reference implementation
        // caps its own accumulator for the same reason.
        dt = std::min(dt, 0.1f);

        pollPower();
        followDrives();
        waitForUploadsToPowerDown();

        // THE SCREENSHOT gamescope is writing, whenever there is one.
        pumpStandaloneShot();
        if (watchStandalone(dt)) {
            refuseCaptureOverAnotherWindow("the emulator's");
            continue;
        }

        // Idle, once a frame. The timers only ever deepen here; input() is
        // what lifts them, above.
        // A GAME ON SCREEN is one running: not under the menu, not held on
        // its state's "Press A to continue", and not frozen waiting for a
        // state picked on its page to arrive (#484, PLAY-16), which is when
        // the game itself stands still (pace.cpp).
        const bool gameOnScreen = playing && !overlayOpen && !stateHold && !stateHoldWaiting;
        idleFrame(dt, gameOnScreen, kScreenOff[screenOffIndex].seconds);
        // TIME PLAYED (#128): the same test the dim uses for "a game is on
        // screen". Written down once a minute, so a power cut loses at most
        // one.
        playClock.tick(dt, gameOnScreen);
        if (playClock.active() && (playCheckpointClock += dt) >= 60.0f) {
            playCheckpointClock = 0.0f;
            playtime::checkpoint(storage::currentUser(), playClock.now(playtime::wallMs()));
        }
        // Held seats for pads that went off last only as long as the game
        // (players.h). The pause menu is still the game, over an emulator of
        // its own too.
        players::setInGame(playing || standaloneRun.active());
        if (playing) cab::Core::shared().setPlayers(players::count());
        if (here() == Screen::Settings && players::generation() != settingsPadsSeen)
            buildSettings();

        repeatHeld(dt);

        evictTestHook();
        followRemotesInGame();
        if (playing) {
            cab::Core& core = cab::Core::shared();
            const bool rewinding = readGameInput(core);
            runGameFrame(core, dt, rewinding);
            // A CORE THAT MOVED ITS SOUND RATE MID-GAME (#220): the speaker
            // is set to it again, as at the launch (openSoundAndPace), or
            // the sound would play at the old rate's speed.
            static unsigned soundRateSeen = 0;
            if (core.soundRateChanges() != soundRateSeen) {
                soundRateSeen = core.soundRateChanges();
                SDL_AudioSpec src{};
                src.format = SDL_AUDIO_S16;
                src.channels = 2;
                src.freq = static_cast<int>(core.avInfo().sampleRate);
                if (audioStream && SDL_SetAudioStreamFormat(audioStream, &src, nullptr))
                    std::fprintf(stderr, "[frontend] audio in at %d Hz\n", src.freq);
            }
        }

        downloadHook();
        takeAccountResults();
        launchHooks(dt);

        pumpLaunch();
        pumpSwitch();
        pumpLeave();
        pollSteamJob();
        pumpArrive();
        pumpExit();
        watchPs2Start();
        pumpStateLoad(stateLoad, session, menuNotice);
        pumpDetailStates();
        loadPendingStates();
        overlayHooks();

        pumpDiscChange();

        runDiscScript();

        stateHooks();
        images.pump(dt);
        look::tick(dt);
        // Scheduled turns dark at its hour, asked about once a minute.
        {
            static float sinceDarkCheck = 0.0f;
            sinceDarkCheck += dt;
            if (sinceDarkCheck >= 60.0f) {
                sinceDarkCheck = 0.0f;
                look::setDark(wantDark());
            }
        }
        for (auto& c : cards) {
            c.focus.tick(dt);
            c.press.tick(dt);
        }
        for (int r = 0; r < 2; ++r) {
            for (auto& a : slotFocus[r]) a.tick(dt);
            for (auto& a : slotPress[r]) a.tick(dt);
        }
        // These two are not Cards and so are not in the loop above. Forgetting
        // them cost a debugging pass: the scroll target was computed correctly
        // every frame and then discarded, because an Animated whose elapsed
        // never advances returns its start value forever.
        scrollY.tick(dt);
        backdropMix.tick(dt);
        curtain.tick(dt);
        switchText.tick(dt);
        menuNotice.tick(dt);
        tickRaToast(dt);
        remoteplay::answered();
        // ---- Root's answers, every half second: access, Remote Play, the
        // report, Tailscale and the update (and its weekly check) ----
        updPoll -= dt;
        tickUpdateTimer(dt);
        if (updPoll <= 0.0f) {
            updPoll = 0.5f;
            pollAccess();
            remotePlayHandover();
            pollReport();
            pollTailscale();
            pollRemotePlay();
            pollUpdate();
        }
        askReadyWhenFree();
        noticeLowBattery(dt);
        sayUpdatedTo();
        sayStartProblem();
        noticeGalleryHook(dt);
        retryOwedUploads(dt);
        followServer(dt);
        sayStateUploaded();
        tickLaunchBusy(dt);
        tickPauseMenu(dt);
        {
            screens::Ctx ctx{renderer, text, images, renderer.scale(), &cards};
            tickScreens(dt, ctx);
            takeNetworkAnswer();
            refreshWifiList();
            followWiiRemotes();
            followPads();
            pumpWiiPairing();
            pumpPadWindow();
            takeWifiJoin();
            takeServerCheck();
            tickSearch(dt, ctx);

            showDetailProgress();

            pumpSearch(dt);
            drainGridPages();

            drainCovers();

            sizeSearchResults();
            tickDetail(dt);
        }
        retargetHomePress();

        if (frameOverPs2Window()) {
            refuseCaptureOverAnotherWindow("PCSX2's");
            continue;
        }

        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        bool offscreen = false;
        if (renderW > 0 && renderH > 0) {
            offscreen = renderer.beginOffscreen(renderW, renderH);
            if (offscreen) {
                dw = renderW;
                dh = renderH;
            }
        }
        renderer.beginFrame(dw, dh);
        // Declared here rather than inside a screen's branch: the glass, the
        // pause menu and the bar are drawn after presentScene, outside them.
        const float sc = renderer.scale();
        // A game is on the screen: ours, or one this window is paused over
        // (standalone.h), where nothing of Home may be drawn under the menu.
        const bool gameUp = playing || standalonePaused;
        drawGround(gameUp, dt);

        applyTabContentFade(gameUp);

        if (gameUp) {
            cab::Core& core = cab::Core::shared();
            drawGamePicture(core, dh);
        } else if (here() != Screen::Home) {
            drawScreen(sc);
        } else {

            drawHome(dt, sc);
        }  // end of the shelf branch

        drawStateHold(sc);

        drawPauseScrim();

        renderer.presentScene();

        drawScreenGlass(gameUp, sc);

        drawPauseMenu(sc);

        resetContentFade();

        drawBar(gameUp, sc, dt);

        // DOWNLOADS (Settings), over the top bar: it is a panel over the whole
        // screen, and the bar drawn on top of it showed "Settings" through
        // it (MMagTech on the TV, 2026-09-26). Under the pill, which says
        // what a removal did where the panel's foot is, and under the
        // question it asks ("Remove 3 games?").
        if (downloadsPanel.isOpen() && !playing) {
            screens::Ctx dctx{renderer, text, images, sc, &cards};
            downloadsPanel.draw(dctx);
            renderer.setContentAlpha(1.0f);
        }

        drawNotice(sc);

        drawRaToast(sc);

        // ---- The account switcher, over the screen and over the bar -------
        //
        // AFTER THE BAR, because it hangs from the chip the bar draws and has
        // to sit on top of it rather than under. Before the curtain, because a
        // curtain covers everything including this.
        if (accountsOpen) {
            screens::Ctx actx{renderer, text, images, sc, &cards};
            accountScreen.draw(actx);
        }
        // The question panel, over the account switcher. A level of the pause
        // menu also while it fades out, and with the menu as that closes.
        if (choiceScreen.visible() &&
            (!playing || overlayOpen || overlayFade.value() > 0.001f)) {
            screens::Ctx cctx{renderer, text, images, sc, &cards};
            choiceScreen.draw(cctx);
            renderer.setContentAlpha(1.0f);
        }
        if (raList.isOpen() && !playing) {
            screens::Ctx lctx{renderer, text, images, sc, &cards};
            raList.draw(lctx);
            renderer.setContentAlpha(1.0f);
        }
        // The PIN pad over all of it, the panel included: it can be opened
        // from the panel, and it covers the screen.
        if (pinScreen.isOpen() && !playing) {
            screens::Ctx pctx{renderer, text, images, sc, &cards};
            pinScreen.draw(pctx);
            renderer.setContentAlpha(1.0f);
        }

        drawCurtain(sc);

        drawKeyboardAndDissolve();

        drawDim();

        ++frame;
        captureFrame(dw, dh);

        takePendingSwitch();

        if (offscreen) {
            renderer.endOffscreen();
        } else {
            SDL_GL_SwapWindow(window);
        }

        // A BLANK SCREEN DOES NOT NEED SIXTY PICTURES A SECOND. Ten keeps the
        // background jobs polled and a wake within a tenth of a second, and
        // lets the GPU stop redrawing 4K for nobody.
        if (idleShown == idle::Level::Blank && dimLayer.value() >= 0.999f) SDL_Delay(100);
    }
}
