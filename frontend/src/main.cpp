// CabinetOS frontend — the program, and the screens it moves between.
//
// main() builds the App (app.h) and runs it; run(), below, is the console's
// life in order: the command line, the start-up, the frame loop (frame.cpp)
// and the teardown. The App owns everything a screen is not allowed to: the
// RomM client, the core, the download worker, the upload worker, the cache,
// and the navigation between them, each in the file named for it. The
// screens themselves are in their own files (what they share is screens.h)
// and return an Action rather than doing anything; what an Action costs is
// decided in actions.cpp, which is why a screen can never start a download by
// accident.
//
//   Home       resume-first: the Recent and Favorites shelves. The first Recent
//              card is what you were playing, focused on arrival, and the one
//              cover that goes straight into the game ("Resume" in the header);
//              every other cover opens the launch screen. No hero since
//              2026-09-21.
//   Library    every system and every collection on the server, as a tile grid.
//   Grid       one system's or one collection's games.
//   Launch     a full-screen cover over whatever was behind it: play, or
//              download and keep.
//   The player a full-screen cover over THAT, so quitting a game returns to the
//              launch screen and backing out again returns to the browsing.
//
// See docs/PROJECT.md, "The design system" and "Navigation model".

#include <SDL3/SDL.h>

#include <csignal>
#include <cstdio>
#include <optional>
#include <string>

#include "app.h"
#include "capture.h"
#include "covercache.h"
#include "optcheck.h"
#include "probes.h"
#include "steam.h"

// The SIGUSR1 capture (capture.h).
volatile std::sig_atomic_t gCaptureRequested = 0;
void requestCapture(int) { gCaptureRequested = 1; }

int main(int argc, char** argv) {
    // The version-bump check's frontend half (optcheck.h): before anything is
    // opened, so CI runs it with no display, no GPU and no core.
    if (argc == 3 && std::string(argv[1]) == "--check-option-tables")
        return optcheck::run(argv[2]);
    App app;
    return app.run(argc, argv);
}

// main()'s body: the start-up, the frame loop, the teardown and the restart,
// each step a call, in the old order, into startup.cpp, frame.cpp and the
// feature files. Its locals and lambdas are the App's members (app.h).
int App::run(int argc, char** argv) {
    if (const std::optional<int> rc = parseCommandLine(argc, argv)) return *rc;

    resolveCoresAndServer();

    // The probes that need no storage tree, no window, GL or controller,
    // before anything is created on disk (probes.cpp).
    if (const std::optional<int> rc = earlyProbe({.sameNamesMode = sameNamesMode,
                                                  .coreDir = coreDir,
                                                  .rommAddress = rommAddress,
                                                  .accountsTestMode = accountsTestMode,
                                                  .playersTestMode = playersTestMode,
                                                  .batteryTestMode = batteryTestMode,
                                                  .volumeTestMode = volumeTestMode,
                                                  .playtimeTestMode = playtimeTestMode,
                                                  .discTestMode = discTestMode,
                                                  .filesTestMode = filesTestMode,
                                                  .vectrexSheetsTestMode = vectrexSheetsTestMode,
                                                  .padsSeconds = padsSeconds,
                                                  .firstRunRulesMode = firstRunRulesMode,
                                                  .firstRunWriteMode = firstRunWriteMode,
                                                  .qrText = qrText,
                                                  .qrPbm = qrPbm,
                                                  .networkProbeMode = networkProbeMode,
                                                  .networkScan = networkScan,
                                                  .gpuProbeMode = gpuProbeMode}))
        return *rc;

    if (const std::optional<int> rc = startStorage()) return *rc;

    // The probes that need the storage tree and the core directory, before
    // SDL (probes.cpp).
    if (const std::optional<int> rc =
            storageProbe({.firstRunProbeMode = firstRunProbeMode,
                          .firstRunStep = firstRunStep,
                          .firstRunServerCheck = firstRunServerCheck,
                          .rommAddress = rommAddress,
                          .romProbeId = romProbeId,
                          .romProbeExts = romProbeExts,
                          .rommProbeMode = rommProbeMode,
                          .rommPair = rommPair,
                          .coreOptionsAudit = coreOptionsAudit,
                          .coreOptionsDetail = coreOptionsDetail,
                          .corePath = corePath,
                          .romPath = romPath,
                          .autoLaunchId = autoLaunchId,
                          .coreDir = coreDir,
                          .accountsProbeMode = accountsProbeMode,
                          .serverCheckAddress = serverCheckAddress,
                          .keepersRomId = keepersRomId,
                          .storageReport = storageReport,
                          .removeAccountId = removeAccountId,
                          .removeAnyway = removeAnyway}))
        return *rc;

    if (const std::optional<int> rc = startDisplay()) return *rc;

    if (const std::optional<int> rc = runFirstRun()) return *rc;

    if (const std::optional<int> rc = startStartupScreen()) return *rc;

    if (const std::optional<int> rc = connectToServer(argv)) return *rc;
    openKeyboardDemo();
    if (const std::optional<int> rc = startDeveloperLaunch()) return *rc;
    settleScreensAtStart();
    tsSignInScreen.setText("Sign in to Tailscale",
                           {"Use the Tailscale account", "your phone is signed in to."},
                           "Getting a link\xE2\x80\xA6");
    startBrowsing();
    previous = SDL_GetTicksNS();
    startSync();
    recoverPowerCutSaves();
    overlayFade.smooth = true;    // 350 ms ease-in-out, per the design system

    applyStartColour();
    startCurtain();

    readScreenOffAtStart();
    readUpdateCheckAtStart();
    startAccessAtBoot();
    startRemotePlayAtBoot();
    startTailscaleAtBoot();
    // NOTHING HERE for a Remote Play session already open at start (back
    // from Steam, a crash): the session script starts gamescope composed
    // (cabinetos-session, #304).

    noticeAppliedUpdate();
    settleTabsAtStart();

    // The first answer, before anything is drawn. Everything after this is a
    // refresh triggered by the thing that changed it.
    refreshKeeps();

    openScreenForCapture();

    // BACK FROM STEAM (#223): Home, where the console starts anyway; the
    // file is only taken so it is not read again.
    if (!initialScreen) steam::takeReturned();
    parseDiscScript();

    borrowPowerButton();

    walkNavScript();


    runFrameLoop();

    wakeTvAtExit();

    leaveGameAtExit();

    closeStandaloneAtExit();
    reportAndUnloadCore();
    if (audioStream) SDL_DestroyAudioStream(audioStream);
    {
        const covercache::Stats cs = covercache::stats();
        if (cs.fromDisk || cs.fetched)
            std::fprintf(stderr, "[covers] %d from disk, %d fetched, %d stored\n",
                         cs.fromDisk, cs.fetched, cs.stored);
    }
    std::fprintf(stderr, "[image] resident %.1f MB, %d still pending\n",
                 images.bytesResident() / (1024.0 * 1024.0), images.pendingCount());
    return shutDown(argv);
}
