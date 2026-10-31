// The command line: every flag the frontend takes, read into the App's
// options, then the core directory and the server address resolved from
// what was given (#398, restructure batch d). A flag that exits ends the
// parse where it stands, as it always has: the setters of the flags before
// it have run and the flags after it are never read.

#include "app.h"

#include <sys/stat.h>

#include <cstdio>
#include <optional>
#include <string>

#include "arcadematch.h"
#include "arcadesettings.h"
#include "controls.h"
#include "core.h"
#include "disc.h"
#include "discs.h"
#include "fightstick.h"
#include "files.h"
#include "firstrun.h"
#include "idle.h"
#include "launch.h"
#include "netblock.h"
#include "samenames.h"
#include "screenview.h"
#include "storage.h"
#include "update.h"
#include "xboxhdd.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

std::optional<int> App::parseCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (SDL_strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotMode = true;
            shotPath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            shotAfterFrames = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--idle-scale") == 0 && i + 1 < argc) {
            idleScale = SDL_atof(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--shift-every") == 0 && i + 1 < argc) {
            shiftEvery = SDL_atof(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--no-idle") == 0) {
            idleOff = true;
        } else if (SDL_strcmp(argv[i], "--power-menu") == 0) {
            powerMenuDemo = true;
        } else if (SDL_strcmp(argv[i], "--colour") == 0 && i + 1 < argc) {
            lookColour = argv[++i];
        } else if (SDL_strcmp(argv[i], "--dark") == 0) {
            lookDark = true;
        } else if (SDL_strcmp(argv[i], "--ra-sample") == 0) {
            raSample = true;
        } else if (SDL_strcmp(argv[i], "--notice-gallery") == 0) {
            noticeGallery = true;
        } else if (SDL_strcmp(argv[i], "--menu-rise") == 0 && i + 1 < argc) {
            overlayRise = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--menu-fade") == 0 && i + 1 < argc) {
            overlayFadeSeconds = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--integer-scale") == 0) {
            integerScaling = true;
            std::fprintf(stderr, "[picture] integer scaling, with bars\n");
        } else if (SDL_strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            searchQuery = argv[++i];
        } else if (SDL_strcmp(argv[i], "--ui-sound") == 0 && i + 1 < argc) {
            const char* v = argv[++i];
            uiSoundFlag = true;
            uiSound = !(SDL_strcmp(v, "off") == 0 || SDL_strcmp(v, "0") == 0);
            if (uiSound) uiSoundVolume = static_cast<float>(SDL_atof(v)) > 0
                                             ? static_cast<float>(SDL_atof(v))
                                             : uiSoundVolume;
            std::fprintf(stderr, "[sound] %s, volume %.2f\n",
                         uiSound ? "on" : "off", uiSoundVolume);
        } else if (SDL_strcmp(argv[i], "--ui-sound-levels") == 0 && i + 1 < argc) {
            // quiet,medium,loud, e.g. --ui-sound-levels 0.07,0.22,0.65
            uiSoundLevels = argv[++i];
        } else if (SDL_strcmp(argv[i], "--home-bar") == 0 && i + 1 < argc) {
            // top,height,gap — e.g. --home-bar 36,56,52
            float t = barTop, h = barHeight, g = barGapBelow;
            if (std::sscanf(argv[++i], "%f,%f,%f", &t, &h, &g) >= 1) {
                barTop = std::clamp(t, 0.0f, 300.0f);
                barHeight = std::clamp(h, 20.0f, 200.0f);
                barGapBelow = std::clamp(g, 0.0f, 300.0f);
            }
            std::fprintf(stderr, "[home] bar top %.0f height %.0f gap %.0f\n",
                         barTop, barHeight, barGapBelow);
        } else if (SDL_strcmp(argv[i], "--home-backdrop") == 0 && i + 1 < argc) {
            // fill,scrim,texels — e.g. --home-backdrop 0.6,0.22,28
            float f = backdropFill, s = backdropScrim, t = backdropTexels;
            float d = backdropDelay, fade = backdropFade;
            if (std::sscanf(argv[++i], "%f,%f,%f,%f,%f", &f, &s, &t, &d, &fade) >= 1) {
                backdropFill = std::clamp(f, 0.0f, 1.0f);
                backdropScrim = std::clamp(s, 0.0f, 1.0f);
                backdropTexels = std::clamp(t, 1.0f, 512.0f);
                backdropDelay = std::clamp(d, 0.0f, 3.0f);
                backdropFade = std::clamp(fade, 0.05f, 5.0f);
            }
            std::fprintf(stderr,
                         "[home] backdrop fill %.2f scrim %.2f texels %.0f "
                         "delay %.2fs fade %.2fs\n",
                         backdropFill, backdropScrim, backdropTexels,
                         backdropDelay, backdropFade);
        } else if (SDL_strcmp(argv[i], "--glow") == 0 && i + 1 < argc) {
            const char* g = argv[++i];
            glowPeak = SDL_strcmp(g, "off") == 0      ? 0.0f
                       : SDL_strcmp(g, "strong") == 0 ? 0.04f
                       : SDL_strcmp(g, "subtle") == 0 ? 0.025f
                                                      : 0.04f;
        } else if (SDL_strcmp(argv[i], "--safe-area") == 0) {
            safeGuides = true;
        } else if (SDL_strcmp(argv[i], "--keyboard") == 0) {
            keyboardDemo = true;
        } else if (SDL_strcmp(argv[i], "--audio-probe") == 0) {
            audioProbe = true;
        } else if (SDL_strcmp(argv[i], "--state-test") == 0) {
            stateTest = true;
        } else if (SDL_strcmp(argv[i], "--speed-test") == 0) {
            speedTest = true;
        } else if (SDL_strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            corePath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--romm") == 0 && i + 1 < argc) {
            rommAddress = argv[++i];
        } else if (SDL_strcmp(argv[i], "--core-dir") == 0 && i + 1 < argc) {
            coreDir = argv[++i];
        } else if (SDL_strcmp(argv[i], "--overlay-test") == 0) {
            // CAN THIS CONSOLE'S OWN MENU BE DRAWN ONTO NOTHING?
            //
            // A TEST INSTRUMENT, not a product mode, and the one thing that
            // could kill the compositing route in open question 24. Everything
            // proved there was proved with flat rectangles pushed through X11.
            // The pause menu is signed-distance fields in a GLES context that
            // has always had an opaque frame to draw onto, and a renderer that
            // gets premultiplied alpha wrong produces dark haloes around every
            // letter — invisible to any assertion, obvious on a television.
            //
            // It puts the pause menu up, over nothing, as a gamescope overlay.
            // Put a game under it with tools/gamescope-overlay-test.sh and LOOK.
            overlayTest = true;
        } else if (SDL_strcmp(argv[i], "--ps2-upscale") == 0 && i + 1 < argc) {
            // HOW MANY TIMES THE PLAYSTATION 2'S OWN RESOLUTION TO RENDER AT.
            //
            // It renders 640x448, so on a 3840x2160 panel showing a 4:3
            // picture — 2880x2160 of real screen — the arithmetic is
            // 2160/448 = 4.8. **FIVE IS THE FIRST VALUE THAT IS GENUINELY 4K
            // AND SIX CLEARS IT.** Below that the panel is stretching.
            //
            // A flag rather than a constant because this is the single largest
            // thing anybody can change about how a PlayStation 2 game looks,
            // and finding the right number is a matter of sitting in front of
            // the television rather than of reasoning. It is also the shape
            // open question 23 will want — one quality setting for the whole
            // console, not a per-emulator menu.
            setPs2Upscale(static_cast<float>(SDL_atof(argv[++i])));
        } else if (SDL_strcmp(argv[i], "--ps2-copy") == 0) {
            gPs2Copy = true;
        } else if (SDL_strcmp(argv[i], "--ps2-aniso") == 0 && i + 1 < argc) {
            // Anisotropic filtering: 0, 2, 4, 8 or 16. PCSX2 ships it OFF.
            // It sharpens surfaces seen at a steep angle, which on a
            // PlayStation 2 is the road ahead in every racing game. It cannot
            // add detail that was never rendered, so it is the smaller of the
            // two levers.
            setPs2Anisotropy(SDL_atoi(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--storage-root") == 0 && i + 1 < argc) {
            storage::setRoot(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--launch") == 0 && i + 1 < argc) {
            autoLaunchId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--download") == 0 && i + 1 < argc) {
            // The same errand as --launch and for the same reason: this machine
            // has no controller, so the only way to exercise the thing a person
            // would press is to press it from here. It calls exactly what the
            // launch screen's row calls, guards and all.
            autoDownloadId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--switch-account") == 0 && i + 1 < argc) {
            // No switcher screen yet, so this is how the teardown is exercised.
            autoSwitchAccountId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--unkeep") == 0 && i + 1 < argc) {
            autoUnkeepId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--then") == 0 && i + 1 < argc) {
            thenLaunchId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--standalone-exit") == 0 && i + 1 < argc) {
            standaloneExitAfter = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--standalone-freeze") == 0 && i + 1 < argc) {
            standaloneFreezeAfter = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--overlay-exit") == 0) {
            overlayExitDemo = true;
            // Optional frame count: --overlay-exit 2000 plays for 2000 frames
            // and then quits through the overlay, which is the only way to
            // exercise the quit-time save path on a machine with no controller.
            if (i + 1 < argc && argv[i + 1][0] != '-') overlayExitAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--overlay") == 0) {
            overlayDemo = true;
            // Optional frame count: --overlay 300 PLAYS FIRST and opens the
            // menu after 300 frames. Without it the menu opens the instant the
            // game is up, which pauses the core before it has drawn anything —
            // so the panel appears over black and looks like the game failed to
            // render. That cost a confusing ten minutes on 2026-09-21; the game
            // was fine and the flag was the fault.
            if (i + 1 < argc && argv[i + 1][0] != '-') overlayDemoAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--state-check") == 0 && i + 1 < argc) {
            stateCheckAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--disc-script") == 0 && i + 1 < argc) {
            discScriptText = argv[++i];
        } else if (SDL_strcmp(argv[i], "--disc-wait") == 0 && i + 1 < argc) {
            gDiscWaitSeconds = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--disc-boot") == 0 && i + 1 < argc) {
            const std::string b = argv[++i];
            gDiscBootOverride = b == "list"      ? static_cast<int>(disc::Boot::ListFirst)
                                : b == "swap"    ? static_cast<int>(disc::Boot::SwapFirst)
                                : b == "initial" ? static_cast<int>(disc::Boot::InitialImage)
                                                 : -1;
        } else if (SDL_strcmp(argv[i], "--disc-test") == 0) {
            discTestMode = true;
        } else if (SDL_strcmp(argv[i], "--files-test") == 0) {
            filesTestMode = true;
        } else if (SDL_strcmp(argv[i], "--vectrex-sheets-test") == 0) {
            vectrexSheetsTestMode = true;
        } else if (SDL_strcmp(argv[i], "--load-state") == 0 && i + 1 < argc) {
            loadStateAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--sync-test") == 0) {
            syncTest = true;
        } else if (SDL_strcmp(argv[i], "--launch-after") == 0 && i + 1 < argc) {
            autoLaunchAfter = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--rom-probe") == 0 && i + 1 < argc) {
            romProbeId = SDL_atoi(argv[++i]);
            rommProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--rom-exts") == 0 && i + 1 < argc) {
            romProbeExts = argv[++i];
        } else if (SDL_strcmp(argv[i], "--network") == 0) {
            networkProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--network-scan") == 0) {
            networkProbeMode = true;
            networkScan = true;
        } else if ((SDL_strcmp(argv[i], "--xbox-saves-in") == 0 ||
                    SDL_strcmp(argv[i], "--xbox-saves-out") == 0) && i + 2 < argc) {
            // THE DRIVE'S SAVES, BY HAND: onto a drive from a folder holding
            // UDATA/ and TDATA/, or off it into one. What beforeStart and
            // afterEnd do around an Xbox game (xboxhdd.h), for testing.
            std::string err;
            const bool in = SDL_strcmp(argv[i], "--xbox-saves-in") == 0;
            const bool ok = in ? cab::xboxhdd::writeSaves(argv[i + 1], argv[i + 2], &err)
                               : cab::xboxhdd::readSaves(argv[i + 1], argv[i + 2], &err);
            if (!ok) std::fprintf(stderr, "[xbox] %s\n", err.c_str());
            return ok ? 0 : 1;
        } else if (SDL_strcmp(argv[i], "--xbox-list") == 0 && i + 1 < argc) {
            std::string out, err;
            if (!cab::xboxhdd::list(argv[i + 1], &out, &err)) {
                std::fprintf(stderr, "[xbox] %s\n", err.c_str());
                return 1;
            }
            std::printf("%s", out.c_str());
            return 0;
        } else if (SDL_strcmp(argv[i], "--gpu-probe") == 0) {
            gpuProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--qr") == 0 && i + 1 < argc) {
            qrText = argv[++i];
        } else if (SDL_strcmp(argv[i], "--qr-out") == 0 && i + 1 < argc) {
            qrPbm = argv[++i];
        } else if (SDL_strcmp(argv[i], "--setup") == 0) {
            forceSetup = true;
        } else if (SDL_strcmp(argv[i], "--setup-step") == 0 && i + 1 < argc) {
            forceSetup = true;
            setupStep = argv[++i];
        } else if (SDL_strcmp(argv[i], "--no-setup") == 0) {
            noSetup = true;
        } else if (SDL_strcmp(argv[i], "--startup-screen") == 0) {
            startupShot = true;
        } else if (SDL_strcmp(argv[i], "--focus-bar") == 0) {
            // A capture of the bar's own focus, which nothing could take until
            // now (app.h's note on initialRow asked for it since the bar was
            // built). An optional slot follows:
            // 0 Library, 1 Search, 2 Settings, 3 the account chip.
            focusBarSlot = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') focusBarSlot = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--keepers") == 0 && i + 1 < argc) {
            keepersRomId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--server-check") == 0 && i + 1 < argc) {
            serverCheckAddress = argv[++i];
        } else if (SDL_strcmp(argv[i], "--accounts") == 0) {
            accountsProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--accounts-test") == 0) {
            accountsTestMode = true;
        } else if (SDL_strcmp(argv[i], "--players-test") == 0) {
            playersTestMode = true;
        } else if (SDL_strcmp(argv[i], "--fightstick-test") == 0) {
            return fightstick::test();
        } else if (SDL_strcmp(argv[i], "--arcade-settings-test") == 0) {
            return arcadesettings::test();
        } else if (SDL_strcmp(argv[i], "--arcade-match-test") == 0) {
            return arcadematch::test();
        } else if (SDL_strcmp(argv[i], "--arcade-changes") == 0 && i + 2 < argc) {
            return arcadematch::changes(argv[i + 1], argv[i + 2]);
        } else if (SDL_strcmp(argv[i], "--screen-view-test") == 0) {
            return screenview::test();
        } else if (SDL_strcmp(argv[i], "--controls-test") == 0) {
            return controls::test();
        } else if (SDL_strcmp(argv[i], "--netblock-test") == 0) {
            return netblock::test();
        } else if (SDL_strcmp(argv[i], "--idle-test") == 0) {
            return idle::test();
        } else if (SDL_strcmp(argv[i], "--battery-test") == 0) {
            batteryTestMode = true;
        } else if (SDL_strcmp(argv[i], "--volume-test") == 0) {
            volumeTestMode = true;
        } else if (SDL_strcmp(argv[i], "--playtime-test") == 0) {
            playtimeTestMode = true;
        } else if (SDL_strcmp(argv[i], "--remove-account") == 0 && i + 1 < argc) {
            removeAccountId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--remove-anyway") == 0) {
            removeAnyway = true;
        } else if (SDL_strcmp(argv[i], "--pads") == 0) {
            padsSeconds = 30;
            if (i + 1 < argc && argv[i + 1][0] != '-') padsSeconds = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--first-run-rules") == 0) {
            firstRunRulesMode = true;
        } else if (SDL_strcmp(argv[i], "--first-run-writes") == 0) {
            firstRunWriteMode = true;
        } else if (SDL_strcmp(argv[i], "--first-run") == 0) {
            firstRunProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--first-run-step") == 0 && i + 1 < argc) {
            firstRunProbeMode = true;
            firstRunStep = argv[++i];
        } else if (SDL_strcmp(argv[i], "--first-run-check-server") == 0) {
            firstRunProbeMode = true;
            firstRunServerCheck = true;
        } else if (SDL_strcmp(argv[i], "--same-names-test") == 0) {
            return samenames::test();
        } else if (SDL_strcmp(argv[i], "--arcade-tile-test") == 0) {
            return arcadeTileTest();
        } else if (SDL_strcmp(argv[i], "--same-names") == 0) {
            sameNamesMode = true;
        } else if (SDL_strcmp(argv[i], "--romm-probe") == 0) {
            rommProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--romm-pair") == 0) {
            rommPair = true;
            rommProbeMode = true;   // pairing is inherently a headless errand
        } else if (SDL_strcmp(argv[i], "--romm-race-test") == 0 && i + 2 < argc) {
            // The client's thread rule under load, against a stand-in server
            // (romm_racetest.cpp). Exits.
            return romm::raceTest(argv[i + 1], SDL_atoi(argv[i + 2]));
        } else if (SDL_strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            romPath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--evict-test") == 0) {
            evictTest = true;
        } else if (SDL_strcmp(argv[i], "--image-budget-mb") == 0 && i + 1 < argc) {
            imageBudget = static_cast<size_t>(SDL_atoi(argv[++i])) * 1024 * 1024;
        } else if (SDL_strcmp(argv[i], "--render-size") == 0 && i + 1 < argc) {
            SDL_sscanf(argv[++i], "%dx%d", &renderW, &renderH);
        } else if (SDL_strcmp(argv[i], "--focus-row") == 0 && i + 1 < argc) {
            initialRow = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--focus") == 0 && i + 1 < argc) {
            // Lets a screenshot capture a chosen card already focused, so the
            // focus treatment can be checked without a controller attached.
            initialFocus = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--core-options-off") == 0) {
            // The control. See core.h: this is the behaviour this host had
            // before it captured option tables, kept so the difference can be
            // measured rather than asserted.
            cab::Core::setAnswerOptions(false);
        } else if (SDL_strcmp(argv[i], "--core-no-hw-render") == 0) {
            // The other control. See core.h.
            cab::Core::setRefuseHWRender(true);
        } else if (SDL_strcmp(argv[i], "--core-options") == 0) {
            // Every option every built core declares, and what it is answered
            // with. This is the audit docs/PROJECT.md asked for and nobody had
            // run: an unanswered option is not the default, it is zero, and
            // until this existed there was no way to see which were which.
            coreOptionsAudit = true;
        } else if (SDL_strcmp(argv[i], "--core-options-detail") == 0) {
            coreOptionsAudit = true;
            coreOptionsDetail = true;
        } else if (SDL_strcmp(argv[i], "--core-option") == 0 && i + 1 < argc) {
            //   --core-option dolphin_shader_compilation_mode=Synchronous
            // Repeatable. See Core::setCommandLineOption.
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::fprintf(stderr, "[frontend] --core-option wants key=value, got '%s'\n",
                             kv.c_str());
                return 1;
            }
            cab::Core::setCommandLineOption(kv.substr(0, eq), kv.substr(eq + 1));
        } else if (SDL_strcmp(argv[i], "--storage") == 0) {
            storageReport = true;
        } else if (SDL_strcmp(argv[i], "--tab") == 0 && i + 1 < argc) {
            initialTab = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            // Opens straight onto a screen, so every one of them can be
            // photographed from a machine with no controller and no way to show
            // anyone a picture. "it looked right here" is not something this
            // project can say, so each screen has to be capturable by itself.
            //
            //   --screen library
            //   --screen grid --tile 3
            //   --screen detail --game 1234
            initialScreen = argv[++i];
        } else if (SDL_strcmp(argv[i], "--tile") == 0 && i + 1 < argc) {
            initialTile = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--nav") == 0 && i + 1 < argc) {
            // Presses, in order, after --screen has put the console somewhere:
            // "up,right,a". For checking a route headless, the way a person
            // would walk it, through the same door their pad goes through.
            navScript = argv[++i];
        } else if (SDL_strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            initialGame = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--update-dir") == 0 && i + 1 < argc) {
            // System update's status read from here instead of /run, so each
            // of its states can be put on the television by writing a file
            // (tools/ui-loop.sh). A `version` file here stands in for the
            // image's own. update.h.
            update::setDir(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--files-dir") == 0 && i + 1 < argc) {
            // File access's state and password read from here instead, for
            // the same reason as --update-dir. files.h.
            files::setDir(argv[++i]);
        }
    }
    return std::nullopt;
}

// --- Where the cores are, and which server this console talks to -------
//
// BOTH ARE TRIED RATHER THAN ASSUMED, because this one binary now runs in
// two places that disagree about both answers:
//
//   the image      /usr/lib/cabinetos/cores, installed by the build, and a
//                  server address in /etc/cabinetos/session.env that the
//                  session script exports before starting this;
//   a source tree  cores/build, where cores/build-core.sh leaves them, from
//                  the repository root or from frontend/ (../cores/build),
//                  and --romm on the command line.
//
// An explicit flag always wins. What is chosen is printed, for the same
// reason the storage root below is: a console reading the wrong directory
// reports every platform as "not built on this console yet", which reads
// as every core broken rather than as one wrong path.
void App::resolveCoresAndServer() {
    if (!coreDir) {
        // frontend/ too, where the stand-in library's covers are found, so
        // one folder gives both (#488, FIRSTRUN-20).
        auto isDir = [](const char* p) {
            struct stat cs;
            return ::stat(p, &cs) == 0 && S_ISDIR(cs.st_mode);
        };
        coreDir = isDir("cores/build")      ? "cores/build"
                : isDir("../cores/build")   ? "../cores/build"
                                            : "/usr/lib/cabinetos/cores";
    }
    std::fprintf(stderr, "[cores] %s\n", coreDir);
    if (!rommAddress) {
        resolvedAddress = firstrun::serverAddress();
        if (!resolvedAddress.empty()) rommAddress = resolvedAddress.c_str();
    }
}
