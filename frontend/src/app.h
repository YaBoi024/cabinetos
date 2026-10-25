// The App: everything main() held, as one struct (#398, restructure batch c).
//
// main() was one function: its locals were the console's whole state and its
// lambdas reached them through [&]. They are the members and member functions
// of this struct now, in main()'s order and unchanged, so the next batches can
// move one feature at a time into its own file. main.cpp holds only main()
// and run(); every other body is in the file named for its job (cli.cpp,
// startup.cpp, home.cpp and the rest: `grep -n 'App::name(' frontend/src/*.cpp`
// finds one). main() builds one App and calls run(), which is
// main()'s old body: the start-up in today's order, the frame loop, the
// teardown, the restart, each a call where its statements have moved.
//
// MEMBER ORDER IS DESTRUCTION ORDER, backwards. It is main()'s declaration
// order with three moves: resolvedAddress beside rommAddress, which points
// into it; liveClient before images, whose workers call it, and before every
// job whose thread uses it; kManualTemp before manualFetch. Every thread's
// stop is declared after the job it stops, so it runs first.
//
// NOT COPYABLE OR MOVABLE: members hold references and pointers to other
// members, and threads and stored callbacks hold `this`.

#pragma once

#include <SDL3/SDL.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "accountpanel.h"
#include "accounts.h"
#include "arcadesettings.h"
#include "achievements.h"
#include "achlist.h"
#include "battery.h"
#include "bluetooth.h"
#include "choice.h"
#include "design.h"
#include "detailscreen.h"
#include "disc.h"
#include "downloads.h"
#include "drives.h"
#include "files.h"
#include "grid.h"
#include "idle.h"
#include "image.h"
#include "job.h"
#include "keyboard.h"
#include "libraryscreen.h"
#include "look.h"
#include "manualview.h"
#include "net.h"
#include "panels.h"
#include "pin.h"
#include "players.h"
#include "power.h"
#include "playtime.h"
#include "qrlink.h"
#include "remoteplay.h"
#include "report.h"
#include "rewind.h"
#include "romm.h"
#include "screens.h"
#include "searchscreen.h"
#include "server.h"
#include "settings.h"
#include "setup.h"
#include "sound.h"
#include "standalone.h"
#include "steam.h"
#include "tailscale.h"
#include "text.h"
#include "ui.h"
#include "update.h"
#include "vpad.h"
#include "wiiremote.h"
#include "wikiqr.h"

// Moved out of main.cpp earlier in the restructure (#398).
#include "gamesession.h"
#include "launch.h"
#include "library.h"
#include "notice.h"
#include "states.h"
#include "settingsids.h"
#include "uploader.h"

// The startup screen's last line, which Home lifts it off (#109, see
// `arriving`). One string, so the frame the curtain starts on is the
// frame that was already on the television.
static constexpr const char* kLoadingLine = "Loading your library";

// Named by the game frame's functions (core.h).
namespace cab {
class Core;
}

// pumpLaunch's stages' shared locals (launch.cpp).
struct LaunchContext;

struct App {
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    // main()'s body (main.cpp).
    int run(int argc, char** argv);

    bool shotMode = false;
    const char* shotPath = "/tmp/cabinetos-frame.bmp";
    int shotAfterFrames = 30;
    int initialFocus = -1;
    // Which row to start on, so each of Home's rows can be photographed without
    // a controller. 0 Recent, 1 Favorites. The bar is not a row any more — see
    // the Row enum — so a capture of it wants --focus-bar, not --row 0.
    int initialRow = -1;
    // What to type into Search for a capture. See the route below.
    const char* searchQuery = nullptr;
    // Integer scaling for a game's picture: OFF by default since 2026-09-21,
    // so every system fills the height. See the draw site for what that trades.
    bool integerScaling = false;
    // INTERFACE SOUNDS, Settings' row in Display and Sound (SetInterfaceSounds).
    // MMagTech: *"a navigation sound of some sort would be nice and later we
    // have the option in settings to turn it off."*
    // --ui-sound overrides the saved level for this run only, for tests.
    bool uiSoundFlag = false;
    const char* uiSoundLevels = nullptr;
    bool uiSound = true;
    float uiSoundVolume = 0.22f;
    // HOME'S BACKDROP, TUNABLE WITHOUT A COMPILER.
    //
    // design.h is still where these live and still what ships — these start as
    // its values and are only moved by a flag. The reason the flag exists is
    // that the three of them can only be judged on a television, and the loop
    // from "try 0.55" to "see 0.55 on the panel" is otherwise a rebuild, a
    // deploy and a restarted session for one number. MMagTech on this session:
    // *"its going to be all over the place i have alot to tweak on the ui."*
    //
    // Whatever wins goes back into design.h. A number that only exists on a
    // command line is not a decision, it is a thing somebody once tried.
    float backdropFill = design::kHomeBackdropFill;
    float backdropScrim = design::kHomeBackdropScrim;
    float backdropTexels = design::kHomeBackdropTexels;
    // The two that decide how it MOVES rather than how it looks, and they are
    // the pair most worth arguing with on a panel: how long it waits before it
    // notices, and how long it takes to get there.
    float backdropDelay = design::kHomeBackdropDelay;
    float backdropFade = design::kHomeBackdropFade;
    // And the bar's own three, for the same reason. Where the room above and
    // below the top bar goes is a judgement about a television and nothing
    // else — a capture cannot show whether two rows of text are crowding each
    // other from a sofa, and this project has been wrong about the top edge of
    // a screen before.
    float barTop = design::kBarTop;
    float barHeight = design::kBarHeight;
    float barGapBelow = design::kBarGapBelow;
    // Verify the layout at a panel size this machine does not have. Most sets
    // are 4K; plenty are not; the design canvas scales to both and this is how
    // that gets checked rather than assumed.
    int renderW = 0, renderH = 0;
    // Deliberately settable, so eviction can be exercised on a machine with
    // plenty of memory. An unbounded texture cache on a 4 GB console is a real
    // failure mode and it must be testable, not merely intended.
    size_t imageBudget = 192u * 1024 * 1024;
    // Loads covers that are then never drawn again, the way scrolling a shelf
    // leaves the covers behind it. That is the only situation in which anything
    // is evictable at all, so it is the only way to test that eviction works.
    bool evictTest = false;
    // Proves a restored state is genuinely identical, not merely accepted.
    bool stateTest = false;
    // What one emulated frame costs, run back to back with no clock (#147).
    bool speedTest = false;
    bool audioProbe = false;
    // Opens the keyboard immediately, so it can be worked on without walking
    // through a first-run flow that does not exist yet.
    bool keyboardDemo = false;
    bool safeGuides = false;
    // Idle handling — idle.h. `--idle-scale 0.01` shrinks every idle timer a
    // hundredfold so a dim can be looked at in seconds; `--shift-every 5`
    // walks the pixel shift fast enough to watch; `--no-idle` turns the dim
    // and the blank off, for a capture that has to stay lit.
    double idleScale = 1.0;
    double shiftEvery = idle::kShiftEverySeconds;
    bool idleOff = false;
    // Opens the Power menu at startup, so a capture can show it.
    bool powerMenuDemo = false;
    // --colour and --dark: a look for this run only, for judging each colour
    // and dark on the television without writing anybody's setting.
    const char* lookColour = nullptr;
    bool lookDark = false;
    // Walks the notification pill through every message it can show.
    bool noticeGallery = false;
    // --ra-sample: RetroAchievements as for somebody signed in, with a made-up
    // set on every game's page and an unlock a few seconds into a game, so
    // the page and the pop-up can be captured without an account (#74).
    bool raSample = false;
    // `--menu-fade 4` stretches the menus' fade so a person can watch it in
    // slow motion and say what is wrong with it. Tuning only.
    float overlayFadeSeconds = design::kOverlayFade;
    // `--menu-rise 0` drops the slide the menus arrive with. Tuning only.
    float overlayRise = 24.0f;
    // Off / subtle / strong, the reference implementation's own three levels.
    // Always Strong, with no setting (MMagTech, 2026-10-04).
    float glowPeak = 0.04f;
    // Running a core. Both are needed: a core without a ROM has nothing to do.
    const char* corePath = nullptr;
    const char* romPath = nullptr;
    // Talks to a RomM server and prints what it found, without opening a
    // window. The same reasoning as --state-test and --audio-probe: the
    // network, the auth and the parsing are all things that can be wrong on
    // their own, and finding that out through a UI is the slow way.
    const char* rommAddress = nullptr;

    // A console has no command line. Nothing is baked into the image — this
    // repository is public and somebody's LAN address does not belong in it —
    // so the address comes from the machine, and the session script is what
    // puts it in the environment. The first-run screen writes the same file
    // when it exists; see docs/PROJECT.md, open question 15.
    //
    // TWO PLACES, AND THE ENVIRONMENT WINS. /etc/cabinetos/session.env is
    // root's and is how a console is set up by hand today; config/server.json
    // is what the first-run screen writes, because the session user cannot
    // write /etc and inventing a privileged helper for one string that is not a
    // secret would be a mechanism for nothing. firstrun::serverAddress() holds
    // that order in one place so nothing else has to know it.
    std::string resolvedAddress;
    bool rommPair = false;
    // --romm alone runs the UI against the server. --romm-probe reports and
    // exits without opening a window, which is what a headless machine and a
    // CI job can do.
    int autoDownloadId = 0;
    int autoSwitchAccountId = 0;
    int autoUnkeepId = 0;
    bool storageReport = false;
    bool coreOptionsAudit = false;
    // The same audit with the prose and the value list, which is what turns
    // "here are 78 keys" into something a person can decide from.
    bool coreOptionsDetail = false;
    const char* initialScreen = nullptr;
    int initialTile = 0;
    const char* navScript = nullptr;
    int initialTab = 0;
    int initialGame = 0;
    bool rommProbeMode = false;
    bool sameNamesMode = false;   // --same-names, for the diagnostic report (#383)
    // --- The first-run mechanisms ------------------------------------------
    //
    // Each of the four things first run needs that is NOT a picture has a way
    // to be seen working from a shell: the state machine, the QR renderer, the
    // NetworkManager plumbing, and knowing whether it is the first run at all.
    // See the probes above.
    bool networkProbeMode = false;
    bool networkScan = false;
    // What this machine's graphics hardware can do, before anything is created.
    // See gpu.h: the A9 and the test VM differ here in the one way that decides
    // whether PlayStation 2 and GameCube can be played at all.
    bool gpuProbeMode = false;
    bool firstRunProbeMode = false;
    int keepersRomId = 0;
    int focusBarSlot = -1;
    bool startupShot = false;
    bool accountsProbeMode = false;
    // --server-check <address>: would this address be accepted as the same
    // server by Change server address? Says which answer, and exits.
    const char* serverCheckAddress = nullptr;
    bool accountsTestMode = false;
    bool playersTestMode = false;
    bool batteryTestMode = false;
    bool volumeTestMode = false;
    bool playtimeTestMode = false;
    int removeAccountId = 0;     // --remove-account N (#194)
    bool removeAnyway = false;   // --remove-anyway
    int padsSeconds = 0;
    bool firstRunRulesMode = false;
    bool firstRunWriteMode = false;
    // Runs the setup flow even on a console that is already configured, so it
    // can be looked at and photographed. It never writes anything — see
    // setup::Options::dryRun — because the only machines anybody here can try
    // it on are ones that are already set up.
    bool forceSetup = false;
    // The escape hatch: start the console without setup on a machine that
    // cannot complete it. Not a product affordance, a development one.
    bool noSetup = false;
    const char* setupStep = nullptr;
    bool firstRunServerCheck = false;
    const char* firstRunStep = nullptr;
    const char* qrText = nullptr;
    const char* qrPbm = nullptr;
    // Downloads one ROM and reports what came back and what a core would be
    // handed. The formats people keep ROMs in are not uniform and this is how
    // that gets checked against a real server rather than assumed.
    int romProbeId = 0;
    const char* romProbeExts = "gb|gbc|dmg";
    // Where the built cores are. Where games and saves go is storage.h's
    // answer, not a constant here — see --storage-root.
    //
    // Left null so the two defaults below can be TRIED rather than one of them
    // assumed — see the resolution block after the argument loop. --core-dir
    // still wins outright.
    const char* coreDir = nullptr;
    // Launch this RomM id without anybody pressing anything, after a delay, so
    // the whole Home-to-game transition can be watched on a machine with no
    // controller attached to it.
    int autoLaunchId = 0;
    // A SECOND GAME IN THE SAME RUN (--then <id>): started once --overlay-exit
    // has taken the first back to Home. Several emulators have broken only
    // when they were not the first game of the run (PS2 and Dreamcast, found
    // on the TV 2026-09-27), and a single --launch never exercises that.
    int thenLaunchId = 0;
    // Runs the whole save/state round trip once the game is up: write a state,
    // upload it, then fetch the newest one back and restore it. Headless, so
    // the sync can be proved on a machine nobody is sitting at.
    bool syncTest = false;
    // See --overlay-test below. A test instrument for open question 24.
    bool overlayTest = false;
    // Opens the overlay once the game is up, so it can be looked at on a
    // machine with nothing attached to it. See --overlay for the frame count,
    // which is what makes the picture underneath it a GAME rather than black.
    bool overlayDemo = false;
    int overlayDemoAfter = 0;
    // --load-state N: after N frames of play, what the pause menu's "Load
    // latest state" does. The other half of --sync-test, which loads back the
    // state it has just made; this loads whatever is newest on the server, so
    // a state made on another device can be tried here without a controller.
    int loadStateAfter = 0;
    // --state-check N: after N frames, save a state IN MEMORY, play on, load
    // it, and save again at once. Equal bytes mean the load put the machine
    // back exactly. Nothing is written to disk or sent anywhere; it asks only
    // "does a state this console makes load on this console".
    int stateCheckAfter = 0;
    // THE DISC RIG (#162): --disc-script "<step>; <step>; ...", run against a
    // game started with --launch, through the same calls the pause menu
    // makes. See the runner in the frame loop for the steps. --disc-test
    // checks disc.h's rules with no core and no server.
    std::string discScriptText;
    bool discTestMode = false;
    // --files-test: what survives a full disk, through cab::writeBytes (#482).
    bool filesTestMode = false;
    // --vectrex-sheets-test: which Vectrex game takes which sheet (#224).
    bool vectrexSheetsTestMode = false;
    // Opens the overlay and takes Exit to Home, so the whole leave-a-game path
    // can be proved on a machine with nothing attached.
    bool overlayExitDemo = false;
    int overlayExitAfter = 120;   // frames of play before the overlay quits it
    // --standalone-exit N: an emulator that is its own application is closed
    // by the console after N seconds, as the pause menu's Exit will close it.
    // The leave-a-game path for those, with nobody at the television.
    float standaloneExitAfter = 0.0f;
    // --standalone-freeze N: frozen N seconds in, as the pause menu freezes
    // it, and left so. With --standalone-exit later, the exit-while-paused
    // path.
    float standaloneFreezeAfter = 0.0f;
    float autoLaunchAfter = 0.0f;

    // CREATED HIDDEN IN OVERLAY MODE, because gamescope classifies a window
    // when it MAPS. Set the properties afterwards and they land on a window
    // nothing re-examines, and the overlay never appears — which looks exactly
    // like a compositor that refused. Hidden, marked, then shown.
    // SEE-THROUGH CAPABLE, ALWAYS. While an emulator of its own has the screen
    // (standalone.h) this window becomes its overlay and draws the pause menu
    // over the frozen game, which needs an alpha channel the window cannot be
    // given later. Everywhere else nothing changes: every frame starts from an
    // opaque clear and the blending keeps it opaque (ui.cpp, presentScene).
    SDL_WindowFlags windowFlags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_TRANSPARENT;
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;

    ui::Renderer renderer;

    ui::TextRenderer text;
    // A key is not a path. Everything after '#' is stripped before reading, so
    // one file can stand in for many distinct entries here — and so a real RomM
    // URL with a query string is already the shape this expects.
    // The client outlives the cache deliberately: ImageCache calls the loader
    // from its worker threads, so whatever it captures must still be alive when
    // a cover arrives. romm::Client is safe to call concurrently: each request
    // builds its own CURL handle and takes the address and the token under the
    // client's lock, which an account switch, a pairing and the server coming
    // back write under (romm.h).
    romm::Client liveClient;

    // 192 MB of covers resident. A shelf holds a handful; a library grid holds
    // a screenful; anything past that is re-decoded on the way back, which is
    // cheap and bounded. Four workers, so a fast scroll keeps up without
    // starving the frame thread on a four-core box.
    ui::ImageCache images;
    // THE STORE IS ONE OBJECT AND THE OLD NAMES ARE REFERENCES INTO IT.
    //
    // These were eight independent locals until open question 28, which was
    // fine while the whole library arrived in a single call at boot and
    // nothing ever grew afterwards. Grids, collections and search now each
    // append to the store while the console is running, and `appendGame` has
    // to be handed somewhere to append TO — so the pieces live together again.
    //
    // The names below are unchanged on purpose: several hundred lines of this
    // file read `cards[i]` and `shelf`, and rewriting all of them to reach
    // through `lib.` would be a large diff that changed nothing.
    Library lib;
    std::vector<design::Card>& cards = lib.cards;
    int& heroIndex = lib.heroIndex;
    std::string& heroPlatform = lib.heroPlatform;
    // Which cards the Recent row shows, and nothing else: empty is no Recent
    // row. The stand-in library puts its six cards here (connectToServer).
    std::vector<int>& shelf = lib.shelf;
    std::vector<int>& favorites = lib.favorites;
    std::vector<romm::Game>& games = lib.games;
    std::vector<screens::Tile>& platformTiles = lib.platformTiles;
    std::vector<screens::Tile>& collectionTiles = lib.collectionTiles;

    // WHAT THE SCREEN SAYS WHILE THE CONSOLE IS BUSY.
    //
    // Everything below blocks: reaching the server, adopting the user, and
    // pulling a library that is sixteen hundred games on the reference machine.
    // Until 2026-09-20 the screen showed nothing at all for those seconds — and
    // for up to ninety of them when the server is not up yet — because the
    // frame loop does not exist until after all of it.
    //
    // MMagTech noticed it as the pause after "Start playing" in first run, but
    // it is not a first-run fault: it has happened on every boot this console
    // has ever done. Nobody watches a console boot with a stopwatch.
    // ---- Idle: pixel shift, dim, blank — idle.h, open question 10b -------
    //
    // UP HERE, BEFORE THE SERVER IS ASKED FOR, because the startup screen can
    // now wait for it without end (below), and a bright logo left all night
    // is the burn-in this exists to stop. One watch for both: a screen that
    // went dark while waiting stays dark on Home, and the same press wakes it.
    idle::Watch idleWatch;
    idle::Level idleShown = idle::Level::Awake;
    design::Animated dimLayer;
    idle::Offset shiftShown;
    static double clockSeconds();
    void idleFrame(float dt, bool playingNow, double blankAfter);

    setup::Deps waitDeps;
    bool startupShown = false;
    // Why Home started empty although the server answered: a refused
    // sign-in, or no system this console plays. Said once Home is up.
    std::string startProblem;
    // HOME IS THE DRIVE'S GAMES (#88): the console started with no server, or
    // the server went away while it was on, or a person was switched offline.
    // The frame loop loads the library properly once the server answers.
    bool libFromDrive = false;

    ui::Keyboard keyboard;

    // --- The core, if one was asked for --------------------------------------
    bool playing = false;
    SDL_AudioStream* audioStream = nullptr;
    // LOCKED TO THE SCREEN (#221), RetroArch's way: one game frame per refresh
    // (or per two on a 120 Hz screen), and the sound played that much faster
    // or slower so it still keeps time. Set at each launch. 0 is paced by the
    // clock, as every game was before: a game too far from the screen's rate
    // (a 50 Hz game on a 60 Hz TV), or no rate known.
    int paceEvery = 0;
    double paceBaseRatio = 1.0;
    int paceCount = 0;
    double paceScreenHz = 0.0;   // as SDL reports it, for the [pace] line
    float paceRatio = 1.0f;      // the sound's speed now, for the [pace] line
    // WHAT THE [pace] AND [audio] LINES COUNT, since the last line said it.
    // Set back at each launch with the lock (openSoundAndPace): kept from
    // the last game, a game's first line was that game's last seconds,
    // and its sound per game frame read +0.00%, a silent game's (#398).
    // Members, not a frame's locals: they add up across frames.
    struct PaceTally {
        uint64_t shown = 0, repeated = 0, held = 0, skipped = 0;
        double dtSum = 0.0;
        uint64_t audioLogAt = 0;              // when the next lines are due
        uint64_t lastAudio = 0, lastFrames = 0;   // the core's counts then
        uint64_t droppedBytes = 0, droppedLogAt = 0;
    };
    PaceTally pace;

    // ---- Focus -------------------------------------------------------------
    //
    // Home is rows, not one list. Focus is therefore a ROW and a SLOT within it,
    // never a single index: `focused` used to index the shelf, and that is only
    // the same number while the shelf is the only thing on screen.
    //
    // Home is two rows, Recent and Favorites (Row, below), and opens on the
    // first card of Recent, which is what "resume-first" means: the thing you
    // were playing is already under the cursor. The top bar is not one of
    // Home's rows: its focus is the app's (barFocused, barSlot, below), and
    // Home is its first item (#127).
    // THE HERO ROW IS GONE — 2026-09-21.
    //
    // Home was bar / hero / Recent / Favorites, where the hero was the most
    // recently played game lifted out of Recent and given an 1800x340 card of
    // its own. The card was mostly not artwork: a 3:4 cover fitted in the
    // middle of a 16:5 box, with the same cover blurred either side to fill the
    // space it could not. MMagTech, looking at it: *"the homepage needs some
    // work"*, and that band was the thing.
    //
    // It is now three rows, and the most recent game is simply the first card
    // on Recent — which is what it was before it was taken out. Resume-first is
    // unchanged and is now cheaper: focus opens on that card, and A launches it
    // rather than opening its launch screen. See `activateHome`.
    //
    // WHAT THE 284 POINTS BOUGHT: the shelf covers are back at the reference
    // implementation's own 260x347. They were cut to 158x210 to fit two shelves
    // under the hero — design.h records that as "about 430 points had to come
    // out of a 1080 canvas". Most of it has come back.
    // THE BAR IS NOT ONE OF HOME'S ROWS — changed 2026-09-21.
    //
    // It was, back when it was drawn on Home and nowhere else. Now it is drawn
    // over every browsing screen, and MMagTech asked the obvious question:
    // *"if the library has the top bar in view shouldnt i be able to up and
    // access it."* Yes. Chrome that is on screen and cannot be reached is worse
    // than chrome that is hidden, because it reads as the console having
    // stopped responding rather than as a thing that is not there.
    //
    // So the bar's focus belongs to the APP — `barFocused` and `barSlot` — and
    // every screen hands focus up to it the same way, through
    // screens::Action::FocusBar. Two mechanisms for one piece of chrome is how
    // a shelf and a grid end up disagreeing about what focus looks like, which
    // is the same reason design.h exists.
    enum Row { RowRecent = 0, RowFavorites = 1 };
    // HOW FAR EACH HOME ROW HAS SCROLLED, in points — 2026-09-23. There was
    // none: every card was drawn at its fixed slot, so moving right past the
    // edge of the screen put focus on a card nobody could see, and A launched
    // it. MMagTech, on the A9: *"this should scroll until the last one."* One
    // per row, and each row keeps its own when focus leaves it.
    design::Animated shelfScroll[2];
    // THE CHIP IS A BAR SLOT NOW, and it is the only one that is not a
    // capsule: it is drawn as the avatar disc at the far right, so the label
    // loop below stops at BarSettings and the chip takes its own focus rim.
    // Everything else about it — L1/R1 walking onto it, Down leaving the bar —
    // it gets for free by being in this list.
    //
    // HOME IS THE FIRST ITEM — #127, MMagTech 2026-09-27. It was left out on
    // 2026-09-21 because a bar item that does nothing where you stand teaches
    // people the bar is decorative. Since 2026-09-24 moving across the bar
    // switches the screen, so the lit item is always where you stand, and
    // Home is simply one of them. A bar slot is now its destination's number.
    enum BarItem { BarHome = 0, BarLibrary, BarSearch, BarSettings, BarAccount, BarCount };
    const char* kBarLabels[BarCount] = { "Home", "Library", "Search", "Settings", "Account" };
    // How many of those draw as labelled capsules. The chip draws itself.
    static constexpr int kBarCapsules = BarAccount;
    size_t shelfSlots();
    bool haveResume();
    bool haveFavorites();
    void playedJustNow(int romId);
    bool homeEmpty();
    size_t rowSlots(int row);
    bool rowExists(int row);
    design::Card* cardAt(int row, int slot);

    // FOCUS BELONGS TO A PLACE ON A SHELF, NOT TO A GAME — #125, 2026-09-27.
    // It lived on the Card, and a game that is both recent and a favourite is
    // one Card drawn on two shelves, so focusing it on Recent lifted its cover
    // on Favorites too. MMagTech: two covers looked focused at once. One lift
    // and one press per (row, slot), grown as a row is drawn.
    std::vector<design::Animated> slotFocus[2], slotPress[2];
    design::Animated& slotAnim(std::vector<design::Animated>* v, int row, int slot);

    // RESUME-FIRST, and it is now one rule rather than a separate object:
    // Home opens on the first card of Recent, which is the most recently played
    // game this console can play.
    int focusRow = RowRecent;
    int focusSlot = 0;
    // Home is taller than the screen once there are two shelves, so it scrolls
    // to follow focus, as Cabinet did on tvOS, where Home was a ScrollView.
    design::Animated scrollY;
    int homeTopRow();
    void homeFromTheStart();
    // What is lighting the room, and what was lighting it before. Two cache
    // keys and a mix, because a cut between two covers is the one thing this
    // must not look like. `backdropWant` is what focus is asking for, which is
    // not the same as what is on screen until it has stopped asking for long
    // enough — see kHomeBackdropDelay.
    std::string backdropKey, backdropPrevKey, backdropWant;
    // The last game art a BROWSING screen lit the room with. The plain
    // screens (Settings, adding an account) clear the room, and Search with
    // nothing found yet goes back to this rather than staying plain: MMagTech,
    // 2026-09-24, one screen switching between plain and blurred "doesn't feel
    // right".
    std::string lastLitArt;
    float backdropSettle = 0.0f;
    design::Animated backdropMix;
    void settleFocus();

    // ---- Where we are -------------------------------------------------------
    //
    // A stack, not a mode flag. Each tab owns a navigation stack and Library
    // pushes to a grid; backing out has to land where the person came from
    // rather than at a fixed home. The launch screen is a full-screen COVER
    // over whatever was behind it, and the player is a cover over that — which
    // is what makes quitting a game return to the launch screen and backing out
    // again return to the browsing.
    enum class Screen { Home, Library, Grid, Detail, Search, AddAccount, TailscaleSignIn, Report,
                        HowTo, Settings, WhatsNew };
    std::vector<Screen> stack{Screen::Home};
    // Which screen last set the room, so a change of screen can be told apart
    // from focus moving within one. See the backdrop block in the frame loop.
    Screen backdropScreen = Screen::Home;
    bool backdropForScreen = false;
    Screen here();

    screens::LibraryScreen libraryScreen;
    screens::GridScreen gridScreen;
    screens::DetailScreen detailScreen;
    // RETROACHIEVEMENTS ON THE GAME'S PAGE (#74): the list behind its row,
    // and the answer it shows, for the page that asked.
    screens::AchievementList raList;
    ra::GameList detailAch;
    int detailAchRom = 0;
    // THE GAME'S MANUAL (#364), beside its page: R1 slides it in, L1 or B
    // slides it back (manualview.h). `manualSlide` is 0 with the page in
    // front and 1 with the manual; the viewer keeps its place while the page
    // stays open. RomM's path for it, for the page that is open.
    screens::ManualView manualView;
    design::Animated manualSlide;
    bool manualShown = false;
    std::string detailManualPath;
    std::string detailManualTitle;
    screens::SearchScreen searchScreen;
    screens::AccountScreen accountScreen;
    screens::QrLinkScreen addAccountScreen;
    // Signing the console in to Tailscale (#286): the same screen, a link to
    // approve on a phone. The one line is the constraint that matters: the
    // account has to be the one the phone uses, or the two never meet.
    screens::QrLinkScreen tsSignInScreen;
    // THE DIAGNOSTIC REPORT (#195): the same screen again, the link this time
    // a download of the report. Its lines are set when it is made.
    screens::QrLinkScreen reportScreen;
    // ABOUT, WHAT'S NEW (#329): the same screen once more, its code the What's
    // new page's, made once by tools/make-wiki-qr.py like How to's Wiki.
    screens::QrLinkScreen whatsNewScreen;
    // ABOUT, HOW TO (#314): the controls, in Settings' own shape (a section
    // per row on the left, its lines on the right), so it looks like the
    // console rather than a page of text (MMagTech on the TV, 2026-10-10).
    // Its Wiki is a QR code in the pane, shown on landing.
    screens::SettingsScreen howToScreen;
    screens::SettingsScreen settingsScreen;
    // What the docked keyboard held last frame, so the query is re-run when it
    // changes and not sixty times a second when it does not.
    std::string searchTyped;
    // Typed but not yet asked, and how long until it is. SEARCH GOES TO THE
    // SERVER NOW — open question 28 — so the one thing the old in-memory
    // filter never needed is the one thing this cannot do without: somebody
    // typing "castlevania" on a pad must not fire eleven requests.
    std::string searchPending;
    float searchDebounce = 0.0f;
    // Long enough to swallow a run of keypresses, short enough that stopping
    // to look feels like the screen answering rather than catching up. The
    // request itself is 40-80 ms against the reference server.
    static constexpr float kSearchDebounce = 0.25f;
    // One page. A television search is somebody looking for a game they can
    // name, not a person paging through four hundred results with a stick —
    // and the heading says how many matched, so a query that is too broad
    // says so rather than pretending the first forty are all of it.
    static constexpr int kSearchLimit = 40;
    // THE SERVER IS ASKED ON A WORKER (#481, HOME-22). On the frame thread
    // a slow or vanishing server froze the keyboard, the sound and every
    // animation until it answered, up to 30 s. One at a time: a query typed
    // while one is out waits for it, and an answer for anything but what
    // the box says now is dropped.
    struct SearchAnswer {
        std::string query;
        std::vector<romm::Game> found;
        int total = 0;
    };
    cab::Job<SearchAnswer> searchJob;
    void runSearch(const std::string& q);
    void showSearchAnswer(const SearchAnswer& a, bool ok, const std::string& err);

    // THE TILE COVERS, FETCHED BEHIND HOME RATHER THAN BEFORE IT.
    //
    // A platform tile's artwork is the first cover among its games, and until
    // open question 28 it came for free because boot had fetched every game
    // anyway. It is one request per platform on its own: 1.55 s against the
    // reference server, measured, which is more than the whole of the rest of
    // a boot. Paying it at startup would have handed most of the win back for
    // thumbnails.
    //
    // MMagTech's call, 2026-09-22, given the choice between paying it, using
    // RomM's platform logo instead, and this: the tiles come up in their
    // colour — which the Library screen already treats as a normal state, not
    // a fault — and the covers arrive a second or so later. The finished
    // screen looks exactly as it did before.
    //
    // IT HAPPENS ON EVERY BOOT, because nothing about a library is kept
    // between them. Within a session it happens once.
    //
    // THE WORKER TOUCHES NOTHING THE FRAME THREAD OWNS. It returns cover
    // PATHS — two ints and a string — and the frame loop is what applies them.
    // Appending to the store from another thread would be a data race against
    // every screen that draws from it.
    struct CoverFill {
        std::mutex m;
        std::vector<std::pair<int, std::string>> done;   // platform id -> cover
        std::vector<int> want;
        std::atomic<size_t> next{0};
        std::atomic<bool> quit{false};
        std::vector<std::thread> workers;
    };
    CoverFill coverFill;
    void startCoverFill();
    // THE REST OF A BIG GRID, BEHIND THE FIRST PAGE.
    //
    // A grid is proportional to its own platform — about 1 ms a game — so the
    // first page is drawn at once and the remainder arrives behind it. The
    // worker returns GAMES and the frame loop is what puts them in the store
    // and on the screen, for the same reason the cover fill returns paths:
    // appending to the store from another thread would race every screen that
    // draws from it.
    //
    // ONE PER TILE, AND NEVER JOINED ON THE FRAME THREAD WHILE IT WORKS
    // (#471). A request cannot be cut half way, so a filler told to stop
    // finishes the page it is on; that page is still added to its tile, and
    // the filler is joined once it has ended (drainGridPages) or at exit.
    // Joined on the spot, opening a tile while another filled froze the
    // picture for the page in flight. `quit` and `running` change only
    // under `m`, so a filler told to stop can be told to carry on without
    // a page being fetched twice (openTile).
    struct GridFill {
        std::string tileQuery;   // the tile it fills (Tile::query)
        unsigned readId = 0;     // and which reading of it (Tile::readId)
        std::mutex m;
        std::vector<romm::Game> arrived;
        bool running = true;
        bool quit = false;
        std::thread th;
    };
    std::vector<std::unique_ptr<GridFill>> gridFills;
    // The tile the grid screen was last opened on, and which reading of it.
    std::string gridTileQuery;
    unsigned gridReadId = 0;
    struct GridFillStop {
        std::vector<std::unique_ptr<GridFill>>& fills;
        ~GridFillStop() {
            for (auto& f : fills) {
                { std::lock_guard<std::mutex> lk(f->m); f->quit = true; }
            }
            for (auto& f : fills) if (f->th.joinable()) f->th.join();
        }
    } gridFillStop{gridFills};

    // THE SWEEP AND THE EVICTION, BEHIND EVERYTHING, ONCE PER BOOT.
    //
    // MMagTech's question, and the reason there is no TTL anywhere in this
    // cache: what stops a deleted platform's art sitting on disk forever. A
    // platform that is gone stops appearing in the platform list, so it is
    // orphaned the FIRST time the console sees the server without it.
    //
    // IT IS GIVEN THE LIST THAT CAME BACK, and covercache::sweep refuses an
    // empty one, because absence is not deletion when nothing answered — a
    // sweep on a failed boot would wipe the cache that boot most wants.
    std::thread coverTidy;
    struct CoverTidyStop {
        std::thread& t;
        ~CoverTidyStop() { if (t.joinable()) t.join(); }
    } coverTidyStop{coverTidy};

    // Stops the workers before anything they write into goes out of scope. A
    // thread outliving this frame would be writing into a dead mutex.
    struct CoverFillStop {
        CoverFill& f;
        ~CoverFillStop() {
            f.quit.store(true);
            for (std::thread& t : f.workers) if (t.joinable()) t.join();
        }
    } coverFillStop{coverFill};

    // Any pad that is already plugged in. Hotplug is handled in the event loop,
    // so a controller connected later works without restarting anything.
    int padCount = 0;

    bool running = true;
    // Set when something OUTSIDE the console asked it to stop — systemd on a
    // shutdown or a restart, which SDL turns into SDL_EVENT_QUIT. Kept apart
    // from `running` because a capture ending, or Start on Home, is not a
    // shutdown and must not upload anything.
    bool askedToStop = false;
    uint64_t previous = 0;
    int frame = 0;
    bool pressing = false;
    // Whether the last press came from a Wii Remote (wiiremote::isRemote),
    // for a press that starts a game (refuseRemoteStart). Any other
    // controller's press, a key or a click clears it.
    bool lastPressFromRemote = false;


    // Starting a game from the UI. Everything it needs was decided elsewhere:
    // the catalog picks the core, the core decides whether its archive gets
    // opened, and the worker puts what comes out where the core can open it.
    LaunchJob launchJob;
    GameSession session;
    // PCSX2'S OWN WINDOW (#226). PS2 runs inside this process, as before, but
    // presents its picture to a window of its own, which gamescope shows; this
    // window becomes gamescope's overlay over it, as for the separate
    // emulators, and draws only the pause menu and what else is over a game.
    // Every frame used to be copied off the GPU for this window to draw, and
    // that copy is what held Quality at 3x (quality.cpp). Everything else
    // about a PS2 game (pads, sound, saves, the menu) is unchanged.
    //
    // ONLY UNDER GAMESCOPE ON X11, which is the console. Anywhere else (the
    // VM's cage, an offscreen run) nothing could draw the menu over another
    // window, so the copy stays; so does an older PS2 library.
    SDL_Window* ps2Window = nullptr;
    bool ps2InputOurs = false;
    int ps2ClearFrames = 0;
    void openPs2Window();
    void closePs2Window();

    // A game in an emulator that is its own application (standalone.h). While
    // it is active the emulator has the television and this loop only watches
    // it: nothing is drawn and nothing reaches the screens here.
    cab::standalone::Run standaloneRun;
    float standaloneRan = 0.0f;
    // THE PAUSE MENU OVER IT. Paused, the emulator is frozen, this window has
    // the controllers, and the ordinary loop runs and draws the ordinary menu
    // over the frozen picture.
    bool standalonePaused = false;
    // Frames still to clear to nothing after this window becomes the overlay,
    // and again when the pause menu over the game closes, or a notice still
    // showing stays frozen over the game (#398, PLAY-31). ONE WAS NOT ENOUGH:
    // gamescope kept showing the launch page on top of the game until the
    // next frame this window drew, which was the pause menu (2026-09-28,
    // Metroid Prime 4 loading behind its own launch page).
    int standaloneClearFrames = 0;
    // The shortcut button's tap, per player, as the in-process path reads it:
    // down, and whether anything else was pressed while it was, and by which
    // pad: a hold whose pad left is dropped, not released (ShortcutHold).
    struct {
        bool down = false, used = false;
        SDL_JoystickID pad = 0;
    } standaloneShortcut[cab::vpad::kMaxPlayers];
    // Back and Start as the emulator has them, per player: passed down, or
    // held back because the other was already down (Emulator::blocksBackStart).
    struct { bool passed[2] = {false, false}, held[2] = {false, false}; }
        backStart[cab::vpad::kMaxPlayers];
    // THE POINTER over a game whose emulator is touched through the console
    // (Emulator::touchPointer: the Vita, played as a PS TV, docs/PROJECT.md
    // question 49, decision 4). Two dots, white and red, drawn by this window
    // over the game: the sticks move them and L2 and R2 touch with them,
    // through the virtual touch screen (vtouch.h). A long press of L3 shows
    // and hides them. standalone_play.cpp, pointerButton and the rest.
    struct Pointer {
        bool on = false;          // the dots show
        int player = -1;          // who drives them: whoever long-pressed L3
        float x[2] = {0.5f, 0.5f}, y[2] = {0.5f, 0.5f};   // fractions of the screen
        bool second = false;      // the red dot has come, since the dots last showed
        float stick[4] = {};      // the driver's sticks, -1 to 1: left x, y, right x, y
        bool finger[2] = {};      // L2 and R2 holding a finger down
        // The driver's sticks and triggers as they are, by SDL's axis number,
        // given back to the game when the dots go.
        int16_t raw[SDL_GAMEPAD_AXIS_COUNT] = {};
        // L3's press, held back until it is a click (the game's) or a long
        // press (the dots'): whose, and since when (SDL_GetTicks).
        int l3Player = -1;
        uint64_t l3SinceMs = 0;
        // A click passed on, released this long after (the game reads L3 as
        // a held state, so a press and release in one instant is missed).
        int clickPlayer = -1;
        uint64_t clickUpAtMs = 0;
        // The touch screen made and gamescope asked to pass touch through,
        // for this game, at the first long press.
        bool madeTouch = false;
        // Frames still to draw: the dots moved, came or went.
        int dirty = 0;
    } pointer;
    // A screenshot being written by gamescope, which takes about two seconds.
    // askedMs and checkedMs are SDL_GetTicks: when it was asked for, and when
    // its size was last looked at (pumpStandaloneShot).
    struct {
        std::string path, name;
        int romId = 0;
        int64_t size = -1;
        uint64_t askedMs = 0, checkedMs = 0;
    } standaloneShot;
    // One worker for every upload. Started here and drained on the way out, so
    // quitting does not discard a save someone has already made.
    Uploader uploader;
    // TIME PLAYED (#128). A session a power cut left open, for anybody who
    // has played here, is owed now; it is sent when that person is.
    playtime::Clock playClock;
    float playCheckpointClock = 0.0f;
    bool shownAway = false;
    float onlineAgainClock = 5.0f;   // a failed reload waits before the next
    // The library being fetched for a console that started offline (#88).
    struct OnlineLoad {
        std::thread th;
        std::atomic<bool> running{false}, ready{false};
        int forAccount = 0;
        Library lib;
        bool liveOk = false;   // the live client answered at the server too
        std::string why;       // and why not
        bool failing = false;  // the last load came back empty (said once)
        ~OnlineLoad() { if (th.joinable()) th.join(); }
    };
    OnlineLoad onlineLoad;
    float owedClock = 0.0f;   // seconds since the last try at what is owed
    StateLoad stateLoad;
    MenuNotice menuNotice;
    // THE PADS WHOSE LOW-BATTERY NOTICE HAS BEEN SHOWN (#293), by the pad and
    // not its number: numbers change with a stream or a pad coming back, and a
    // pad warned before a stream must not be warned again after it. Cleared
    // for a pad once it is back above the last segment.
    std::set<std::string> batteryWarned;
    // THE BATTERY ROW'S ENTRIES AS THEY MOVE (#293, MMagTech 2026-10-08): a
    // pad switching on fades in where it goes while the others slide aside to
    // make room; one switching off fades out first, then the rest close up.
    // Each is the pad itself (its key) or the Remote (its address), so a number
    // closing up (P3 becoming P2) slides rather than swaps. The bar coming
    // back (from a game, a game's page) lays it out at once: nothing slides
    // that was not seen to change.
    struct BatteryEntry {
        std::string id;   // "pad:<key>" or "wii:<address>"
        std::string label;
        battery::Reading r;
    };
    struct BatterySlot {
        BatteryEntry e;
        design::Animated offset;   // its left edge, leftward from the row's right end
        design::Animated alpha;
        bool leaving = false;
    };
    std::vector<BatterySlot> batterySlots;
    // Whether the bar laid the row out this frame, and the frame before: a
    // row that was not on screen a frame ago arrives in place. Counted in
    // frames, not 200 ms, so a slow frame is not taken for the bar having
    // been away (#484, PLAY-07).
    bool batteryRowNow = false, batteryRowBefore = false;
    AchievementToast raToast;

    // The in-game overlay: a scrim, a panel and buttons drawn over the game
    // surface. No compositing trick — the frontend owns the frame loop, which
    // is what makes this simple and is the direct payoff of hosting cores in
    // process rather than launching them.
    bool overlayOpen = false;
    // A Wii game made for the Wii Remote is running (the launch sets it):
    // while a stream plays it, nobody holding the stream's controllers can
    // play it, so its pause menu has no Resume (#286, below).
    bool sessionNeedsRemote = false;
    bool remoteOnlyHere();
    // THE BUTTONS THE MENU WAS CLOSED WITH, per player, kept from the game
    // until they are let go (#103). The menu acts when a button goes DOWN and
    // the game reads what is held every frame, so B to go back, A on Resume,
    // or the stick clicks that closed it all reached the game a frame later
    // as a press. Filled while the menu is up; a bit clears when its button
    // is released and never comes back, so a fresh press after that is the
    // game's as usual.
    uint32_t heldThroughMenu[players::kMax] = {};
    // Whether the menu was up at the last read. The menu usually closes on
    // an event handled earlier in the SAME frame as the read, so the button
    // that closed it is only held from that frame on; the frame of the close
    // has to be captured as well as the frames it was open.
    bool menuUpAtLastRead = false;
    // THE SHORTCUT BUTTON, per player, while a game plays (shortcuts.h).
    // `before` is what was already held when it went down, so only buttons
    // pressed DURING the hold count; `used` is whether one was, which is what
    // tells a tap (the pause menu) from a hold (a shortcut) on release.
    //
    // A HOLD BELONGS TO THE PAD THAT PRESSED IT. A pad is switched off by
    // holding its Home button, so it leaves with its shortcut button down and
    // never sends the release. The hold used to stay with the player, and the
    // next pad to be that player (the same one, switched back on) read as
    // letting go: a tap, which closed the pause menu the pad's going off had
    // opened (2026-10-07, every reconnect on Genesis). `pad` is whose hold it
    // is; another pad in the seat drops it unread.
    struct ShortcutHold {
        bool down = false, used = false;
        uint32_t before = 0;
        SDL_JoystickID pad = 0;
    };
    ShortcutHold shortcutHold[players::kMax];
    // A LONE STICK CLICK WAITS kStickPairMs BEFORE THE GAME HEARS IT
    // (MMagTech, 2026-10-20). L3 + R3 is the pause menu (input.cpp), and the
    // clicks used to reach the core one by one as well, so whichever landed a
    // frame first got through as the menu opened: on the DS and the 3DS an R3
    // tapped the touch screen, on the 3DS an L3 swapped the screens. Now a
    // click the other one joins inside the window reaches nobody; one it does
    // not is delivered when the window ends, and one let go inside it is
    // delivered for a frame, then let go, so no click is lost or reordered.
    // Two frames, about 33 ms: shipped, to reopen on a report.
    static constexpr Uint64 kStickPairMs = 33;
    struct StickClick {
        bool raw = false;      // the pad's button as it is
        bool heard = false;    // delivered to the game
        bool paired = false;   // the other click joined it: the menu's
        bool tapped = false;   // let go inside the window, delivered for a frame
        Uint64 downAt = 0;
    };
    struct StickPair {
        StickClick l3, r3;
        SDL_JoystickID pad = 0;   // as ShortcutHold: another pad in the seat drops it
    };
    StickPair stickPair[players::kMax];
    // HOLDING START IN AN ARCADE GAME (#577, arcadesettings.h): player 1's
    // hold, which opens MAME 2003-Plus's own menu. FinalBurn Neo counts its
    // own.
    arcadesettings::Hold arcadeHold;
    // FAST FORWARD (#77): the shortcut button and ZR, held. Fast while held,
    // normal on release, the sound discarded meanwhile. About 4x, a fixed top
    // speed rather than a setting. ONLY WHERE STATES ARE (MMagTech,
    // 2026-09-27: fast forward and rewind go with states, on the systems kept
    // true to the console, now and later), and elsewhere it says so.
    static constexpr double kFastForward = 4.0;
    bool fastForwardSaid = false;
    // REWIND (#78): the shortcut button and ZL, held (rewind.h). A snapshot
    // every half second of play, the last 15 seconds kept in memory; held,
    // it steps back one every kRewindStep and plays on from there on release.
    // On every system with states and nowhere else (fast forward and rewind
    // go with states). Dropped when a state loads or the game ends.
    cab::Rewind rewindKeep;
    int rewindAvailable = -1;          // -1 not yet decided for this game
    uint64_t rewindLastFrame = 0;      // framesRun at the last snapshot
    float rewindStepWait = 0.0f;
    double rewindWorstMs = 0.0;        // the slowest snapshot, for the log
    std::vector<uint8_t> rewindRaw;
    static constexpr float kRewindStep = 0.20f;
    bool rewindSaid = false;
    // Both stick clicks together are the overlay hotkey — see where they are
    // read. Held state rather than a chord test at press time, because SDL
    // delivers the two presses as separate events.
    bool l3Down = false, r3Down = false;
    int overlaySlot = 0;
    design::Animated overlayFade;
    design::Animated overlayFocus;
    enum OverlayItem { OvResume = 0, OvSaveState, OvLoadState, OvScreenshot, OvChoice, OvExit,
                       OvDisc, OvCount };
    const char* kOverlayLabels[OvCount] = {
        "Resume", "Save state", "Load latest state", "Screenshot", "", "Exit to Home",
        "Change disc",
    };
    // CHOICE ROWS in a list of buttons: "Picture: Console" (#63) and a
    // system's own options, "3D glasses: Off" (#73). Built when the menu
    // opens, in the order they appear, between Screenshot and Exit to Home.
    // Left and right change one; A steps it on and wraps.
    struct PauseChoice {
        // "Picture", not "Picture quality": "Picture quality: Quality" says it
        // twice (MMagTech, 2026-10-02).
        std::string label;
        std::vector<std::string> values;
        int current = 0;
        // Says "Exit and relaunch to apply" when changed (MMagTech,
        // 2026-10-12, #368: "Exit" points at Exit to Home, the way to do it;
        // it was "From the next start"): Picture and Widescreen do, a system
        // row changes in front of the game and needs no word.
        bool nextStart = false;
        std::function<void(int)> apply;
    };
    std::vector<PauseChoice> pauseChoices;
    // Frames to run while paused so a system row's change shows behind the
    // menu (the frame loop, `redrew`).
    int pauseRedrawFrames = 0;
    // The pause menu's items for THIS game, built each time it opens: the two
    // state items only where the system has snapshots. PlayStation 2 and
    // GameCube get Resume, Screenshot and Exit to Home, and nothing to press
    // that says no.
    std::vector<OverlayItem> pauseItems{OvResume, OvSaveState, OvLoadState, OvExit};
    PauseChoice* choiceAt(int slot);

    // ---- The Power menu — docs/PROJECT.md, open question 10b ------------
    //
    // THE PAUSE MENU'S PANEL WITH A DIFFERENT LIST, not a second menu. The
    // power button opens it everywhere, and Start opens it on Home. In a game
    // the game is paused and Resume comes first and is focused, so a child
    // pressing the button costs one press of A; on Home, Cancel does the same. Rest is listed only on a
    // machine that can rest.
    //
    // Anything but Resume, from a game, leaves the game through finishExit
    // first — the one way out of a game, which uploads every kind of save — so
    // this is true for every emulator without any of them knowing.
    bool powerMenu = false;
    enum PowerItem { PwResume, PwCancel, PwRest, PwRestart, PwPowerOff, PwSteam };
    std::vector<PowerItem> powerItems;
    bool restAvailable = false;
    // Rest waits for the uploads finishExit queued: a machine that sleeps
    // mid-upload has sent nothing, and the save would sit owed until morning.
    bool restPending = false;
    uint64_t restWaitStart = 0;
    // SLEEP, RESTART OR POWER OFF CHOSEN WHILE THE GAME CANNOT BE LEFT YET
    // (#473): a core still building its machine, or an emulator of its own
    // that was asked to close. Done once the game is left (pumpExit,
    // finishStandalone); finishExit forgets it, so any other way out of the
    // game takes it with it.
    std::optional<power::Action> powerAfterExit;
    // RESTART OR POWER OFF FADING TO BLACK (#393): chosen, the picture
    // fading over design::kPowerFade, and the game left and the machine
    // asked once it is over (pollPower). Nothing takes a press meanwhile
    // (handleEvent). Cleared only by a refusal, which lifts the curtain.
    bool poweringDown = false;
    std::optional<power::Action> powerFade;
    uint64_t powerFadeAt = 0;
    void powerLeave(power::Action act);
    void powerAct(power::Action act);
    int ovCount();
    void ovStep(int delta);
    const char* ovLabel(int i);
    bool launchById(int romId);
    static std::string gigabytes(int64_t b);
    void downloadById(int romId);
    void removeDownload(int romId);

    // Opening the launch screen for a card. Everything it shows is decided
    // here, so the screen holds no opinion about where any of it came from.
    // CONTINUE FROM, fetched when a game's page opens: the newest three states
    // RomM holds for the emulator Play would run, with their pictures. Network
    // work, so on a worker, one at a time; a page opened while one is out
    // waits its turn in `wantRom` rather than blocking the frame on a join.
    struct DetailStates {
        std::mutex m;
        std::thread worker;
        std::atomic<bool> busy{false}, ready{false};
        int romId = 0;
        std::vector<screens::StateChoice> list;
        std::string saveWhen;
        screens::DetailScreen::Facts facts;
        int wantRom = 0;
        std::string wantTag, wantSaveTag, wantStateDir, wantSlug;
        // The console's own states in `list` (#88): a StateChoice id of -(i+1)
        // is localPaths[i]. Offline, they are the states; online, only the
        // ones still waiting to upload are added to RomM's.
        std::vector<std::string> localPaths;
        // Asked again once the uploads are done: a game just left may have
        // saved a state that is still on its way to RomM.
        bool stale = false;
        ~DetailStates() { if (worker.joinable()) worker.join(); }
    };
    DetailStates detailStates;
    // The open page's own states, by the negative ids its list carries.
    std::vector<std::string> detailLocalStates;
    void startDetailStates();
    void loadDetailStates(int romId);
    void pumpDetailStates();
    // A state picked on the launch screen, loaded once the game is running.
    int pendingStateId = 0;
    // ...or one of this console's own (#88), by its file.
    std::string pendingStatePath;
    // HOME'S RESUME: the newest state, if there is one, loaded the same way;
    // none, and the game simply starts (MMagTech, 2026-09-27). Only where the
    // system has states.
    bool pendingResume = false;
    // AFTER A STATE LOADS, THE GAME WAITS ON THAT FRAME for a button
    // (docs/PROJECT.md, decided 2026-09-27: "catches you off guard how quick
    // it starts"). `stateHold` is the wait, with its "Press (A) to continue";
    // `stateHoldWaiting` holds a game started from the launch screen still,
    // with no prompt, while its state is fetched, so it does not boot and
    // then jump. Only ever set by a state load, so a system without states
    // never waits. The press that ends it is kept from the game.
    bool stateHold = false, stateHoldWaiting = false, stateHoldJustEnded = false;

    // A GAME'S MANUAL, FETCHED (#364): into the `Manuals` folder beside the
    // game when the game is on a drive (cache.h), so it is there offline and
    // follows the game from then on; for a game that is not, into /tmp for as
    // long as it is being read. One at a time, on a worker. A manual that
    // will not come never stops anything: it is said in the log, and a game
    // downloading or starting beside it carries on.
    struct ManualFetch {
        struct Job {
            int romId = 0;
            std::string from, to;
            bool forViewer = false;
        };
        std::mutex m;
        std::condition_variable cv;
        std::deque<Job> jobs;
        std::thread th;
        bool stop = false;
        // The viewer's answer: which game, where, and whether it came.
        bool done = false;
        int doneRom = 0;
        std::string donePath;
        bool doneOk = false;
        ~ManualFetch() {
            {
                std::lock_guard<std::mutex> lk(m);
                stop = true;
            }
            cv.notify_all();
            if (th.joinable()) th.join();
            // A manual fetched only to be read is not kept past the app.
            ::unlink("/tmp/cabinetos-manual.pdf");
        }
    };
    const std::string kManualTemp = "/tmp/cabinetos-manual.pdf";
    ManualFetch manualFetch;
    void fetchManual(int romId, const std::string& manualPath, const std::string& to,
                     bool forViewer);
    void manualBeside(int romId, const std::string& entryPath);
    // R1 ON A GAME'S PAGE ALWAYS MEANS THE MANUAL (MMagTech, #364): the page
    // slides aside and the manual comes in, or the edge sound when the game
    // has none. Opened again on the same page, it is where it was left.
    // A starting value, to be judged on the television.
    static constexpr float kManualSlide = 0.35f;
    void openManual();
    void closeManual();
    void forgetManual();
    void openDetail(int cardIndex);

    // OPENING A GAME'S PAGE, AND LEAVING IT, DISSOLVE like a top-bar switch:
    // asked for here, done at the end of the frame, after that frame has been
    // copied to dissolve away over the new one. It was a cut, and MMagTech on
    // the TV, 2026-09-27: "the switch still happens a bit fast and then
    // everything is just thrown at you". A capture run opens it at once.
    int pendingDetail = -1;
    bool pendingDetailBack = false;
    // What that screen should say once it is open. See refuseLaunch.
    std::string pendingDetailNotice;
    void openDetailSoon(int cardIndex);
    void refreshKeeps();
    look::Appearance appearance{};
    static int savedHour(const char* key, int fallback);
    int darkFrom = 0;
    int darkUntil = 0;
    bool wantDark();
    static look::Colour accountColour(int id);
    void applyAccountColour(bool instant);
    bool switchAccount(int id, std::string* why);

    // ADDING SOMEBODY, ON A CLIENT OF ITS OWN.
    //
    // THE SEPARATE CLIENT IS THE WHOLE POINT and it is the reference
    // implementation's rule too: pairing on the live client would swap the
    // token under whoever is playing, and a half-finished pairing would leave
    // `firstrun` looking at a console it cannot classify. Nothing here touches
    // `liveClient` until `recordPairing` has succeeded — and even then it only
    // writes the account, because adding somebody must not sign anybody out.
    struct AddJob {
        std::thread th;
        std::mutex m;
        std::atomic<bool> running{false};
        romm::Pairing pairing;
        bool haveCode = false;
        bool finished = false;
        bool ok = false;
        accounts::Paired who;
        std::string err;
        // Set at exit (shutDown), under `m`: the wait between polls ends
        // at once and the worker returns, so it can be joined.
        bool stop = false;
        std::condition_variable wake;
    };
    std::shared_ptr<AddJob> addJob = std::make_shared<AddJob>();
    // The QR is encoded once, when the code first arrives, not per frame.
    bool shownPairCode = false;
    // Set by --screen add-account: hold the screenshot until there is a code.
    bool waitForPairCode = false;
    void startAddAccount();
    void refreshAccountRows();

    // WHERE THE BAR'S FOCUS LIVES. Not in Home's row model and not in any
    // screen's: the bar is drawn over all of them, so its cursor belongs to the
    // app. Every screen gets into it the same way (Action::FocusBar) and out of
    // it the same way (Down, or Back).
    // THE SWITCHER IS AN OVERLAY, NOT A DESTINATION, and that is the whole
    // point of it being a panel. It was a stack screen for one build and the
    // capture showed the fault immediately: pushing it made `here()` stop
    // being Home, so Home stopped drawing and the panel hung over an empty
    // purple field. A thing that "expands from the chip" has to have what it
    // expanded over still behind it.
    //
    // So it lives beside `barFocused` rather than in the stack: the app owns
    // it, every screen keeps drawing underneath it, and Back closes it and
    // leaves you exactly where you were.
    bool accountsOpen = false;
    bool barFocused = false;
    int barSlot = 0;
    // Trigger edge state. See the axis handler.
    bool l2Held = false, r2Held = false;
    // A HELD TRIGGER KEEPS JUMPING (MMagTech, 2026-10-10: "holding them should
    // just continuously jump to the next letter"). The d-pad's reason for one
    // meaning per hold does not apply: a held trigger meant nothing. -1, +1,
    // or 0 for neither; the frame loop repeats it.
    int letterHeld = 0;
    float letterHeldFor = 0.0f, letterNext = 0.0f;
    // THE CURTAIN. 0 is clear, 1 is black over everything. See design.h.
    design::Animated curtain;
    // An account switch waiting behind the curtain; see pumpSwitch.
    int switchPendingId = 0;
    // LEAVING: Sign out, or a new server address saved. The curtain is the
    // startup screen with this line under the name, and once it is down (and
    // uploads are through) the app starts itself again; see pumpLeave.
    enum class Leave { None, SignOut, NewAddress, Steam };
    Leave leaving = Leave::None;
    std::string leaveLabel;
    Uint64 leaveDownAt = 0;
    bool restartSelf = false;
    std::string switchLabel;
    int switchCurtainFrames = 0;
    Uint64 switchShownAt = 0;    // when the curtain was fully down
    // THE NAME HAS ITS OWN FADE, inside the curtain's. It came in and went out
    // with the curtain, so it sat over the old screen on the way down and over
    // the new Home on the way up: MMagTech saw the two overlap. Now it only
    // shows on a fully black screen: in after the curtain is down, out before
    // it lifts.
    design::Animated switchText;
    // AN ACCOUNT SWITCH'S CURTAIN IS THE CONSOLE'S PURPLE, not black: the
    // plain gradient boot and Settings stand on. Black is a game's curtain;
    // behind "Switching to vivian" it looked like a screen nobody had
    // designed. MMagTech, 2026-09-24. Set with the switch, cleared once the
    // curtain is fully up again.
    bool switchCurtain = false;
    static constexpr float kSwitchTextFade = 0.200f;   // a starting value
    bool switchDone = false;     // the switch has run; waiting out the hold
    bool switchOk = false;
    std::string switchWhy;
    // ARRIVING: THE STARTUP SCREEN LIFTS OFF HOME, #109. It used to be
    // replaced by Home between one frame and the next; MMagTech, on the A9:
    // *"the transition is extremely harsh."* Now the curtain starts down as
    // the startup screen, the frame already on the television, and lifts the
    // way it does onto a game. Sign out and a new address come down onto the
    // same screen, so leaving and coming back are one motion run both ways.
    //
    // A FIRST TRY FROZE THE TELEVISION ONCE, 2026-09-23 (1ac83e7, reverted).
    // It ran its fade in a loop of its own before Home existed, redrawing the
    // startup screen as fast as it could swap. This draws nothing of its own:
    // it is the main loop's curtain, paced like every other frame.
    //
    // Never for a capture, whose first frame must be the screen itself.
    bool arriving = false;
    bool arriveLifting = false;
    int arriveFrames = 0;
    Uint64 arriveHeldAt = 0;

    // HELD-DIRECTION REPEAT. One direction at a time, which is what a d-pad is:
    // pressing a second direction takes over rather than queueing, so a person
    // rolling their thumb around the pad never ends up with two repeats
    // fighting each other.
    bool navHeld = false;
    screens::Nav heldNav = screens::Nav::Down;
    float heldFor = 0.0f, nextRepeat = 0.0f;
    void holdNav(screens::Nav n);
    void releaseNav(screens::Nav n);
    void letGoOfHeld();

    bool navigate(screens::Nav n);
    // Home's own keys, as one function, so that everything — Home, the Library,
    // the grid — arrives through `navigate` and the bar can be offered the key
    // first in exactly one place. Assigned further down, once moveFocus and
    // moveRow exist.
    bool homeKey(screens::Nav n);

    // ---- The PIN ------------------------------------------------------------
    //
    // docs/SETTINGS.md, Accounts, issue #57. The pad is a layer over whatever
    // is showing (pin.h); these decide what an entered PIN means.
    //
    // `askPin` is the one door every protected action goes through: with no
    // PIN set it simply goes ahead, which is the rule that with no PIN every
    // account can do everything. Every protected action calls it: Wi-Fi, the
    // server address, adding, removing and signing out accounts, File and
    // Developer access, Remote Play, Downloads and updates among them
    // (`grep -n askPin frontend/src/*.cpp`).
    screens::PinScreen pinScreen;
    std::function<void()> pinThen;
    // FIVE WRONG TRIES, THEN THIRTY SECONDS. Enough to forgive fumbling with
    // a d-pad, and enough to make guessing ten thousand PINs not worth it for
    // the sibling the PIN is for. Starting values. Counted per console run,
    // not saved: a restart is slower than the wait.
    static constexpr int kPinTries = 5;
    static constexpr float kPinLockSeconds = 30.0f;
    int pinFails = 0;
    // ONCE PER VISIT TO SETTINGS. Entered once, the PIN is not asked again
    // until Settings is left; the frame loop clears this. MMagTech on the TV,
    // 2026-09-24: a wrong Wi-Fi password meant the PIN again, then the list,
    // then the keyboard. Switching into the owner's account still always asks
    // (it passes `always`), because that is not a Settings visit.
    bool pinUnlocked = false;
    // A CODE THE CONSOLE CHOSE, typed on the same pad: the last step before
    // Format, so erasing a drive is never one press too many. Not the PIN;
    // no lockout, because a wrong code costs nothing.
    std::string pinExpect;
    // A CODE ANOTHER DEVICE SHOWS, typed on the same pad and handed back
    // unchecked: Moonlight's four digits when a device pairs for Remote Play.
    // Whether they are right is the device's to say, not the console's.
    std::function<void(const std::string&)> pinCode;
    std::function<void()> pinCodeCancel;
    void askPin(const std::string& title, const std::string& detail,
                std::function<void()> then, bool always = false);
    void askCode(const std::string& code, const std::string& title,
                 const std::string& detail, std::function<void()> then);
    void choosePin(const std::string& title, const std::string& detail,
                   std::function<void()> then);
    void askPinToOpen(const std::string& detail, std::function<void()> then);
    void pinOutcome(screens::PinScreen::Outcome o);

    // ---- A question with a few answers (choice.h) -------------------------
    //
    // Same shape as the PIN: the app opens it with what to do with the
    // answer, and the answer is acted on after it closes, so an answer can
    // open another question ("who?" then "are you sure?").
    screens::ChoiceScreen choiceScreen;
    std::function<void(int)> choiceThen;
    // For a panel with a stepper row (Steam's size): Left or Right on it, and
    // Back, which on a panel opened from another goes back to that one. Both
    // cleared by every askChoice; set after it.
    std::function<void(int)> choiceStep;
    std::function<void()> choiceBack;
    void askChoice(const std::string& title, const std::string& detail,
                   std::vector<std::string> options, int focus,
                   std::function<void(int)> then);
    // ABOUT, HOW TO (#314). The words live here, next to the controls they
    // describe; docs/wiki/navigation.md says the same and changes with them.
    // THE SHORTCUTS ARE ALWAYS SHOWN: hidden while off, nobody would learn
    // they exist. Off (the default), the first line says where to turn them
    // on and the rest are greyed; both sticks in opens the pause menu either
    // way. The button named is the one set for the pad in hand.
    void openHowTo();
    static constexpr const char* kWikiUrl = wikiqr::kUrl;
    // Settings > Display and Sound > Dark hours: From and Until, and the hours
    // of each. Set where it is opened; it reopens itself after an hour is set.
    void darkHoursPanel(int focus);
    void choiceOutcome(screens::ChoiceScreen::Outcome o);
    std::vector<accounts::Account> removableAccounts();

    // ---- Settings' rows -----------------------------------------------------
    //
    // docs/PROJECT.md, open question 31. The screen is handed rows and hands
    // back an id; everything a row SHOWS is read here, fresh, each time
    // Settings opens or a row changes.
    // The row ids are settingsids.h.
    // The paired Wii Remotes as the Wii Remotes panel last listed them.
    std::vector<wiiremote::Paired> wiiRowsShown;
    // The controllers the Controllers rows were built from, to rebuild them
    // when one connects or goes (players::generation).
    int settingsPadsSeen = -1;
    // The pad last pressed in Settings, whose row has the dot. Kept as the
    // pad, not its row: a swap moves it to another row.
    SDL_JoystickID settingsLastPad = 0;
    // THE SHORTCUT BUTTON ROW IS LISTENING for this pad's next press, 0 when
    // not (shortcuts.h). And the press it took, so the gamepad event SDL sends
    // for that same press (same timestamp) goes no further: a B that cancels
    // must not also leave Settings.
    SDL_JoystickID shortcutListen = 0;
    // The last press while listening was refused: what the row says until
    // the next press (shortcuts::Pick::Refused). Empty otherwise.
    std::string shortcutRefused;
    Uint64 shortcutTakenAt = 0;
    std::vector<drives::Drive> formatable;
    std::map<int, std::string> driveNames;   // a drive row's id -> its name
    std::map<int, std::string> ejectPaths;   // an Eject row's id -> its location
    // An Eject row's id -> its drive in `formatable`, when Format is offered
    // beside Eject.
    std::map<int, size_t> ejectFormats;
    // Format, from a drive's row. Set once buildSettings exists.
    void askFormat(const drives::Drive& d);
    // WHAT IS ON THE DRIVE, counted on a worker after the PIN (#481,
    // FILES-21): a drive holding many files took the frame thread for up to
    // half a second. followDrives opens the confirm when the count arrives
    // (openFormatConfirm), for the drive it was counting.
    cab::Job<drives::Contents> formatCount;
    drives::Drive formatCounting;
    std::string formatCountingWhat;   // the confirm's first line: kind, model, size
    int formatCountFrame = 0;         // the frame the count began on, for the log
    uint64_t formatCountAt = 0;       // and the clock then
    void openFormatConfirm(const drives::Contents& c);
    std::map<std::string, std::string> locationNames(const std::vector<std::string>& locs,
                                                     const std::vector<drives::Unusable>& unusable);
    int screenOffIndex = 0;

    // ---- System update ------------------------------------------------------
    //
    // docs/SETTINGS.md, System; issue #70. Root does the work and writes one
    // status file (update.h); the frame loop reads it twice a second and this
    // decides what it means on screen. What has to outlive a boot is kept in
    // settings.json: Manual or Weekly, when the last check succeeded and what
    // it found, the version already announced, and the version waiting for a
    // restart with the boot it was staged in, which is how the first start
    // after a restart knows whether it applied.
    update::Status upd;   // the first poll reads it, so a status already there is acted on
    // A state said on screen before root has written it: "Checking…" the
    // moment the row is pressed, or why systemd refused. Root's next write
    // (at >= updSaidAt) replaces it.
    int64_t updSaidAt = 0;
    float updPoll = 0.0f;
    bool updWeekly = false;
    bool updAuto = false;            // the check running is the weekly one
    bool updAskAfterCheck = false;   // a check somebody pressed: offer the download
    float updAutoWait = 0.0f;        // before the weekly check may try again
    bool updReadyPanel = false;      // "Update ready" waits to be asked
    bool updReadyAfterGame = false;  // ...and it landed during a game
    std::string updStartNotice;      // "Updated to", once Home is up
    Tone updStartTone = Tone::Done;
    static int64_t prefNum(const char* key);
    static std::string updBytes(int64_t b);
    static std::string updOf(int64_t done, int64_t total);
    std::string updChecked();
    bool updFoundNewer();
    screens::SettingsRow updateRow();
    files::State filesState;
    int64_t filesSaidAt = 0;
    bool filesPanelWhenOn = false;   // open the panel once root says it is on
    bool filesWant = false;
    // ---- Developer access (docs/SETTINGS.md, About; issue #134) ------------
    //
    // The full command line over SSH on port 2222, off by default, and off
    // means cabinetos-developer.service is not running (MMagTech,
    // 2026-10-07: "when they turn it on, they now are a developer. Same
    // install."). There is no status file: systemd's own state for the unit
    // is the answer, read only while Settings is open or a change is waiting,
    // so a console that never turns it on asks nothing. The password is File
    // access's, one password shown in one place. On is remembered in
    // settings.json, as File access is, and started again at boot.
    static constexpr const char* kDevUnit = "cabinetos-developer.service";
    std::string devState;
    int64_t devSaidAt = 0;
    bool devPanelWhenOn = false;
    int64_t devNewPassAt = 0;   // the panel comes back once a new password is made
    std::string devOldPass;     // the one it replaces, to tell when it has been
    std::string devFailed;      // a refusal or a start that failed, said in the row
    bool devWant = false;
    bool devShown = false;
    int versionPresses = 0;
    std::chrono::steady_clock::time_point versionPressAt{};

    // ---- Remote Play (docs/SETTINGS.md, Remote Play; issue #286) ----------
    //
    // Sunshine, so Moonlight on a phone, tablet or computer can play the
    // console. Off by default, and off means cabinetos-remoteplay.service is
    // not running and remoteplay.h asks it nothing. On is remembered in
    // settings.json and started again at boot, as Developer access is.
    // Pairing is on the television: a device asking to pair brings up the
    // PIN pad for the four digits Moonlight shows.
    static constexpr const char* kRpUnit = "cabinetos-remoteplay.service";
    std::string rpState;
    int64_t rpSaidAt = 0;
    std::string rpFailed;   // a refusal or a start that failed, said in the row
    bool rpWant = false;
    int rpSeen = -1;        // remoteplay::generation() the rows were built from
    std::set<std::string> rpAsked;   // requests already put to the TV
    std::string rpAsking;            // the request the PIN pad is open for
    // Digits handed over: the name given to Sunshine, when, and the devices
    // paired before, so the new one is the one not among them.
    struct { std::string name; int64_t at = 0; std::set<std::string> before; } rpPairing;
    // TAILSCALE, for Remote Play away from home (tailscale.h says why it is
    // the one way in from outside). tailscaled.service runs only while
    // Streaming AND Tailscale are on; turning Streaming off stops it too, and
    // turning Streaming on starts it again when Tailscale was on. Signed in
    // or not is remembered as well, so a console turned off can still say
    // "Disconnected" (signed in, not running) rather than "Off" (never).
    static constexpr const char* kTsUnit = "tailscaled.service";
    bool tsWant = false;
    bool tsSignedIn = false;
    int tsSeen = -1;            // tailscale::generation() the rows were built from
    bool tsSigningIn = false;   // the sign-in screen is up, `tailscale up` running
    bool tsShownLink = false;   // its link is on the screen
    bool tsLoggingOut = false;  // `tailscale logout` running on its worker
    std::thread tsWorker;
    std::atomic<bool> tsWorkerDone{false};
    // THE DIAGNOSTIC REPORT, made on a worker (about three seconds), with the
    // console's address on the home network asked there too.
    std::thread reportWorker;
    std::atomic<bool> reportWorkerDone{false};
    report::Made reportMade;
    std::string reportAddress;
    // THE NETWORK IS ASKED OFF THE FRAME THREAD. net::status() is three or four
    // nmcli round trips, which is a visible hitch if the screen waits for it,
    // so the row says "Checking" until the answer lands.
    struct SettingsNet {
        std::mutex m;
        bool have = false;
        bool fresh = false;       // arrived and not yet shown
        net::Status st;
        std::thread th;
        ~SettingsNet() { if (th.joinable()) th.join(); }
    };
    SettingsNet settingsNet;

    // ---- Wi-Fi, in Settings > Network (issue #59) -------------------------
    //
    // WHAT IS ON THE AIR, OFF THE FRAME THREAD: what NetworkManager already
    // knows first (quick), then a real scan (seconds on the A9). Each answer
    // rebuilds the rows if Settings is still showing. A scan still running is
    // left to finish rather than waited on.
    struct SettingsWifi {
        std::mutex m;
        bool fresh = false;
        std::vector<net::Network> list;
        std::atomic<bool> running{false};
        std::thread th;
        ~SettingsWifi() { if (th.joinable()) th.join(); }
    };
    SettingsWifi settingsWifi;

    // ---- Add a controller, a window over Settings (issue #65) --------------
    //
    // A WINDOW LIKE WI-FI'S, NOT FIRST RUN'S SCREEN. MMagTech, 2026-09-26:
    // first run's full screen felt like leaving Settings. While it is open a
    // worker listens in short rounds and the list is only what it has HEARD
    // since opening, so a pad's stale identities from other modes never show.
    // Choosing one pairs it in place; the new pad's first button press closes
    // it. Pairing and listening take turns on the one worker: bluetoothctl
    // does not want two at once.
    struct PadWindow {
        std::mutex m;
        std::map<std::string, bt::Device> heard;   // by address, since opening
        bool fresh = false;
        bool noBluetooth = false;
        std::string pairAddress;                   // asked for, not started
        bool pairing = false;
        bool pairDone = false, pairOk = false;
        std::string pairError;
        std::atomic<bool> stop{false};
        std::atomic<bool> cut{false};              // ends a listening round early
        std::thread th;
        void quit() {
            stop = true;
            cut = true;
            if (th.joinable()) th.join();
        }
        ~PadWindow() { quit(); }
    };
    PadWindow padWin;
    bool padWindowOpen = false;
    // Opened from the Controllers panel: closing it goes back there, as Pair
    // a Wii Remote goes back to the Wii Remotes panel.
    bool padWindowFromPanel = false;
    std::vector<SDL_JoystickID> padWindowBefore;   // pads here when it opened
    std::vector<bt::Device> padShown;              // the rows, in order
    std::string padPairingName;                    // "" when not pairing
    std::string padDetail;
    void padWindowRows(std::vector<std::string>* names, std::vector<std::string>* values);
    void padWindowRefresh();
    void closePadWindow();
    // Wide enough for "Nintendo Switch Pro Controller" with "Pairing…"
    // beside it. A starting value, judged on the television.
    static constexpr float kPadWindowWidth = 760.0f;
    void openPadWindow();
    // ---- Pair a Wii Remote (#200) ---------------------------------------------
    //
    // The Wii's own search, over and over, until a Remote answers or the
    // window is closed; then it is paired, trusted and connected, and the
    // sensor bar is asked, as a Wii asks it. One pass of the search is about
    // 12 seconds and a Remote's red button keeps it findable for longer.
    struct WiiPairJob {
        std::mutex m;
        std::string detail;   // the line under the title, when it changes
        std::string address;  // the Remote paired, once done
        bool fresh = false, done = false;
        std::atomic<bool> stop{false};
        // Until the worker has returned. A pairing cannot be stopped half
        // way, so Back leaves it to finish behind the panel (#481,
        // SETTINGS-23), and a new window waits for it.
        std::atomic<bool> running{false};
        std::thread th;
        void quit() {
            stop = true;
            if (th.joinable()) th.join();
        }
        ~WiiPairJob() { quit(); }
    };
    WiiPairJob wiiPair;
    bool wiiPairOpen = false;
    // THE SENSOR BAR IS ASKED WITH THE FIRST REMOTE ONLY: a second Remote does
    // not move the bar (MMagTech, 2026-10-03). The panel's row changes it later.
    bool wiiPairFirst = false;
    void openWiiPairWindow();
    void closeWiiPairWindow();
    // THE WII REMOTES PANEL: each paired Remote (its value its light while
    // connected; pressed, Remove), Pair a Wii Remote, and the sensor bar,
    // which a press flips in place. Removing the last Remote greys the
    // Remote games again.
    void openWiiPanelAt(int focus);
    void rebuildSettingsRows();
    static std::vector<wiiremote::Paired> wiiPanelRemotes();
    static std::vector<std::string> wiiPanelValues(const std::vector<wiiremote::Paired>& rows);
    std::vector<std::string> wiiValuesShown;
    void openWiiPanel();

    // ---- The Controllers panel ----------------------------------------------
    //
    // A ROW PER PAD, its name and its player ("• Player 1" on the pad last
    // pressed, which is how two identical pads are told apart), then Add a
    // controller. Pressing a pad gives it another number (the two swap) or
    // forgets a Bluetooth pad; a lone wired pad has neither. After either,
    // back to this panel, on the same row (after a swap that row is the other
    // pad: the rows are in seat order).
    std::vector<players::Pad> padsShown;
    bool padsPanelSeenOpen = false;
    int padsPanelGen = -1;
    SDL_JoystickID padsPanelHand = 0;
    void padsPanelRows(std::vector<std::string>* names, std::vector<std::string>* values);
    void openPadsPanelAt(int focus);
    void askWifi();
    // JOINING BLOCKS FOR UP TO 45 SECONDS (net::join), so it is a job too. The
    // row being joined says so; the answer arrives in the notice pill.
    struct WifiJob {
        std::mutex m;
        std::thread th;
        std::atomic<bool> running{false};
        bool done = false, ok = false;
        std::string ssid, err;
        ~WifiJob() { if (th.joinable()) th.join(); }
    };
    WifiJob wifiJob;
    void startWifiJoin(const std::string& ssid, const std::string& pass,
                       bool forgetFirst);
    // ---- Change server address (issue #60) -------------------------------
    //
    // IS IT THE SAME SERVER, off the frame thread: reaching an address that
    // is not there costs curl's connect timeout. The keyboard stays up saying
    // "Checking…" while it runs, the Wi-Fi password's shape.
    struct ServerJob {
        std::mutex m;
        std::thread th;
        std::atomic<bool> running{false};
        bool done = false;
        server::Check result = server::Check::Failed;
        std::string address, detail;
        Uint64 startedAt = 0;
        ~ServerJob() { if (th.joinable()) th.join(); }
    };
    ServerJob serverJob;
    void startServerCheck(const std::string& address);

    // The networks as the rows show them: one per name, strongest signal,
    // the one in use first, then saved ones, then the rest by signal.
    std::vector<net::Network> wifiShown;
    // Whether the question panel on screen is the Wi-Fi list, so a scan that
    // lands while it is open can refresh it.
    bool wifiPanelOpen = false;
    void sortWifi();
    void wifiPanelRows(std::vector<std::string>* names, std::vector<std::string>* values);
    // THE KEYBOARD OUTSIDE SEARCH. Search owns its docked keyboard; anything
    // else that opens it (a Wi-Fi password) says here what to do with it.
    std::function<void(ui::KeyboardResult)> keyboardThen;
    // A Wi-Fi password on the on-screen keyboard, then the join on a worker.
    // `forgetFirst` is Change password; `why` is "Wrong password" when this
    // is the retry after one. Assigned once buildSettings exists.
    void askWifiPassword(const std::string& ssid, bool forgetFirst, const std::string& why);
    // The network the keyboard is waiting on, while it says "Joining…".
    std::string wifiKeyboardFor;
    // RETROACHIEVEMENTS SIGN-IN (#74): the username, then the password, on the
    // same keyboard, the password the way Wi-Fi's is. `why` is the last
    // answer's reason, said in the field. Assigned once buildSettings exists.
    void askRaUser(const std::string& initial);
    void askRaPassword(const std::string& user, const std::string& why);
    // True while the keyboard says "Signing in…" and waits on the answer.
    bool raKeyboardWaiting = false;
    // A server address on the keyboard; `why` is what went wrong with the
    // last one, said in the field with the address kept behind it. Assigned
    // once buildSettings exists.
    void askServerAddress(const std::string& typed, const std::string& why);
    // The address the keyboard is waiting on, while it says "Checking…".
    std::string serverKeyboardFor;
    void askNetwork();
    // ---- Downloads (downloads.h), #68 --------------------------------------
    //
    // Every downloaded game, for Settings' Downloads panel, named as the
    // Library names it where the library knows the game. A game still
    // downloading is left out until it has arrived (MMagTech, 2026-09-26): its
    // record is written before its first byte, so it would show half-sized.
    screens::DownloadsPanel downloadsPanel;
    // Assigned once buildSettings exists: a removal rebuilds Storage.
    void downloadsOutcome(screens::DownloadsPanel::Outcome o);
    std::vector<screens::DownloadItem> downloadItems();

    // THE TAILSCALE ROW (#286), under Streaming and greyed while it is off.
    // Its value is the address to give Moonlight; its second line is the
    // console's name there and when its sign-in ends. MMagTech, 2026-10-08.
    // "Sign in again" is offered for the last two weeks only.
    static constexpr int64_t kTsWarnDays = 14;
    bool tsEndsSoon(const tailscale::Status& st);
    screens::SettingsRow tailscaleRow(bool streaming, bool inUse);
    void buildSettings();
    void startUpdate(bool download, bool automatic);
    void askUpdateDownload();
    void askUpdateReady();
    void setFiles(bool on);
    // THE PANEL: everything a computer needs, the password in plain text
    // (anyone at the television can already turn this on or make a new
    // one), then Done, New password, Turn off. Focus on Done.
    void openFilesPanel();
    void setDeveloper(bool on);
    void setRemotePlay(bool on);
    void setTailscale(bool on);
    void openTsSignIn(bool again);
    void tsLogout();
    // NAMING A DEVICE: the keyboard, the current name in the field. Right after
    // pairing it says so; an empty name keeps the one it had.
    void openPairedPanel(int focus);
    void askDeviceName(const std::string& id, const std::string& current, bool justPaired);
    // THE PAIRED DEVICES: a list, however long, each one pressed for Remove
    // (the PIN when set). One row on the page, so a household with many
    // devices does not push Tailscale off it (MMagTech, 2026-10-07).
    std::vector<remoteplay::Device> rpRowsShown;

    // THE PANEL: File access's, with the port. Same password, same New
    // password (it changes File access's too, which is the point of one).
    void openDevPanel();

    // THE CURTAIN COMES DOWN ON THE STARTUP SCREEN, and pumpLeave does the
    // rest once it is down.
    // REMOVING A PERSON DELETES THEIR FOLDER (#194, MMagTech 2026-10-04). A
    // person deleted and made again on RomM gets a new id, so to RomM and to
    // this console they are someone new, and a folder kept "just in case" is
    // left behind for good. So Remove sends what they still owe RomM, with
    // their own login (they are never the person signed in: accounts::remove
    // refuses that), and only then deletes. Off the frame thread, because a
    // server that does not answer costs curl's whole timeout.
    // STOPPED AND JOINED, NOT DETACHED (#406): a send still running at exit
    // ran on into the program's teardown. `stop` ends it after the file on
    // the wire (sendOwedBeforeRemoving); the destructor joins, so the App
    // never ends holding it.
    struct RemoveJob {
        std::mutex m;
        std::atomic<bool> running{false};
        std::atomic<bool> stop{false};
        bool finished = false;
        int left = 0;   // still owed after trying
        accounts::Account who;
        bool everyone = false;   // Sign out: every account's, then leave
        std::thread th;
        ~RemoveJob() {
            stop = true;
            if (th.joinable()) th.join();
        }
    };
    std::shared_ptr<RemoveJob> removeJob = std::make_shared<RemoveJob>();
    void finishRemove(const accounts::Account& who);
    void startRemove(const accounts::Account& who);
    void startSignOut();
    void startLeaving(Leave what, const std::string& label);

    // ---- Steam (#223) ------------------------------------------------------
    //
    // The tile, its first-pick screen, its size and its removal. Everything
    // that runs the root helper, walks the cache or asks the network runs in
    // ONE background job at a time (steamJob), and its answer is acted on here,
    // on the frame thread, by pollSteamJob. A pill says what is happening
    // meanwhile. The handover itself is the leave curtain: "Starting Steam",
    // then the frontend quits and the session runs Steam (cabinetos-session).
    struct SteamJob {
        std::thread th;
        std::atomic<bool> done{false};
        std::function<void()> finish;
        std::string busy;        // the pill while it runs, said again until done
        Uint64 saidAt = 0;
    };
    SteamJob steamJob;
    struct SteamJobStop {
        SteamJob& j;
        ~SteamJobStop() { if (j.th.joinable()) j.th.join(); }
    } steamJobStop{steamJob};
    bool runSteamJob(const std::string& busy, std::function<void()> work,
                     std::function<void()> finish);
    // Steam's install in progress, for the panel's line (installSteam).
    struct SteamInstall {
        std::atomic<bool> cancel{false};
        std::atomic<int64_t> bytes{-1};
        bool ok = false;
        std::string why;
    };
    std::shared_ptr<SteamInstall> steamInstall;
    void pollSteamJob();

    // What the first-pick screen and the size panel show, read off the frame
    // thread: what the main drive could give Steam without touching a kept
    // game, the default, and whether the console is online (Steam downloads
    // itself the first time).
    struct SteamSizes {
        int64_t room = 0;      // the most Steam can have
        int64_t size = 0;      // what is chosen; 0 = does not fit
        int64_t drive = 0;     // the main drive, Steam's slice included
        int64_t current = 0;   // the slice now, 0 when not set up
        bool online = false;
    };
    SteamSizes steamSizes;
    static void readSteamSizes(SteamSizes* out);
    std::string steamGamesLine(int64_t size);

    void showSteamSetup();
    void installSteam(int64_t want);
    void showSteamSize(bool growing);
    void steamPressed();
    void apply(const screens::Result& res);
    void activateHome();
    void goToDestination(int d);
    int destinationHere();

    // SWITCHING TOP-BAR DESTINATIONS IS A DISSOLVE — 2026-09-24.
    //
    // MMagTech: *"the switch between top tabs is too quick and visually
    // snappy"*, most of all Search to Settings. It was a cut out and a fade in.
    // Tried the same afternoon and dropped on the television: fading the old
    // screen out and the new one in, with the background in between, then with
    // the background alongside. What he asked for in the end was the new
    // screen arriving see-through, with the old one showing through it.
    //
    // So the last frame of the old screen is copied at the moment of the
    // switch and drawn over the new one, fading away (Renderer::
    // captureSnapshot). Everything dissolves together: content, keyboard,
    // background and the bar's highlight. A press mid-dissolve copies the
    // frame as it is and starts again from there.
    //
    // THE OLD SCREEN GOES FIRST, THEN THE NEW ONE'S CONTENT ARRIVES. MMagTech,
    // same afternoon: "can the search dissolve more before it goes to
    // settings", because a solid keyboard dissolving straight into a page of
    // text read as one busy frame. So the copy fades out quickly, and the new
    // screen's content waits until it is mostly gone before fading in. The
    // new background comes with the dissolve, under the copy.
    //
    // Starting values, to be judged on the television.
    static constexpr float kTabDissolve = 0.300f;      // the old frame, away
    static constexpr float kTabContentDelay = 0.200f;  // then the new content starts
    static constexpr float kTabContentIn = 0.300f;     // and takes this long
    static constexpr float kTabArrive = kTabDissolve;  // the new background's fade
    float tabSince = -1.0f;      // seconds since the switch; negative when none
    // THE NEW CONTENT RISES THIS FAR as it fades in, and SEARCH'S KEYBOARD
    // SLIDES rather than dissolving: down and away as Search is left, up as it
    // arrives. Opacity alone read as one picture turning into another; a
    // little movement makes one screen leave and the next arrive.
    static constexpr float kTabRise = 16.0f;
    static constexpr float kKeyboardSlide = 0.300f;
    design::Animated keyboardSlide;
    bool snapTaken = false;
    int pendingDest = -1;
    design::Animated tabDissolve;
    void transitionTo(int d);
    int destinationGoing();
    void switchDestination(int delta);
    bool shoulderOnPage(int dir);
    bool barKey(screens::Nav n);
    void removeDownloads();
    void pumpSwitch();
    void pumpLeave();
    void pumpArrive();
    void refuseLaunch(const std::string& why);
    void showWhyUnplayable(int romId);
    bool refuseRemoteStart(int romId);
    void startStandalone(const cab::standalone::Emulator& emu);
    static void dropPressesFromClosing();
    void finishStandalone();
    void pumpLaunch();
    void leaveFocus();
    void enterFocus();
    void moveFocus(int delta);
    void moveRow(int delta);

    // WHO OWNS THE INPUT, asked once per event rather than decided again at
    // every call site.
    //
    // docs/PROJECT.md states the rule architecturally: while a game runs the
    // controller belongs to the core exclusively; while an overlay is open it
    // belongs to the UI; never both. It also says to implement the RULE rather
    // than the routing, because "any design where a button can mean two things
    // at once is the same bug" — the one reported from real hardware as
    // "controllers work on the homescreen but in game b exits the game".
    //
    // This was exactly that bug: arrow keys moved the Tetris piece AND shifted
    // focus on the Home screen behind it, so leaving the game landed somewhere
    // nobody chose. Found by someone actually playing it.
    enum class InputOwner { Keyboard, Overlay, Game, UI };
    InputOwner inputOwner();
    void keyboardUp();
    void keyboardResult(ui::KeyboardResult res);

    // Leaving a game. The save goes up FIRST — this is the trigger that matters
    // most, because nobody should lose progress by quitting — and only then is
    // the core torn down.
    // Quitting is DEFERRED while a core is still booting, and the waiting has
    // to happen out here in the frame loop rather than inside unloadGame.
    //
    // The first attempt did it in unloadGame, spinning on retro_run until the
    // core produced audio. It never did, and the reason is the useful part:
    // PPSSPP's emulation thread only advances when the frontend COMPLETES a
    // frame, not merely when retro_run is called. A tight loop with no present
    // in it makes no progress at all — which is the same signature that makes
    // --state-test unable to warm this core up, and now has one explanation
    // rather than two mysteries.
    //
    // So the exit sets a flag, the ordinary loop keeps running and drawing, and
    // the quit completes once the machine is up. Capped, because somebody
    // quitting must not wait on a core that is never going to boot.
    bool exitPending = false;
    uint64_t exitWaitStart = 0;
    void finishExit();
    void exitToHome();
    void pumpExit();
    void watchPs2Start();

    // A STREAM STARTING OR ENDING HANDS THE PLAYERS OVER (players.h) AND
    // PAUSES A RUNNING GAME, so nobody's car crashes in the second it takes to
    // pick up the other controller; whoever takes over presses Resume
    // (MMagTech, 2026-10-07). Every system: the built-in ones and the
    // emulators of their own. Asked from both loops (the one an emulator of
    // its own runs under skips the rest), and only a flag is read.
    void remotePlayHandover();
    void powerCue(sound::Cue c);
    void closeOverlay(bool cue = true);
    void powerActivate(PowerItem item);
    void backToPauseMenu();
    void standaloneScreenshot();
    void pumpStandaloneShot();
    void stepChoice(int slot, int delta, bool wrap);
    void beginInsert(int n);
    void pumpInsert();
    // START OVER ON DISC N: the game is left as any exit leaves it, saves
    // and all, and started again on that disc. Play carries on from it too,
    // which is the way back to disc 1 for a postgame (#162).
    int relaunchRomId = 0;
    void startOverOnDisc(int n);
    // THE DISC RIG'S STATE (#162, --disc-script). A test instrument: empty,
    // and nothing here runs.
    struct DiscScript {
        std::vector<std::string> steps;
        size_t at = 0;
        bool started = false;
        uint64_t from = 0;        // the game frame the step began on
        uint64_t wallFrom = 0;    // and the wall clock, for `sleep`
        uint32_t pad = 0;         // buttons held for the game
        uint64_t padUntil = 0;    // until this game frame
        std::string captureTo;    // the next frame capture goes here
        bool said = false;
    } discScript;
    void openDiscPanel();
    void pickDisc(int n);
    void overlayActivate();
    void toggleOverlay();
    void openStandaloneMenu();
    void openPowerMenu();
    // Set while logind is waiting on us to leave a game before it proceeds.
    bool releasePending = false;
    uint64_t releaseWaitStart = 0;

    // ---- Made in batches (d) to (g) (#398) -----------------------------------
    // Each is one stretch of main()'s old body or of apply()'s, called
    // where it ran; the file that holds it is named above its group.
    // cli.cpp
    std::optional<int> parseCommandLine(int argc, char** argv);
    void resolveCoresAndServer();
    // startup.cpp
    std::optional<int> startStorage();
    std::optional<int> startDisplay();
    std::optional<int> runFirstRun();
    std::optional<int> startStartupScreen();
    std::optional<int> connectToServer(char** argv);
    void openKeyboardDemo();
    std::optional<int> startDeveloperLaunch();
    void settleScreensAtStart();
    void startBrowsing();
    void startSync();
    bool recoverDriveSaves(const cab::standalone::Emulator& xemu, const std::string& path);
    void recoverPowerCutSaves();
    void applyStartColour();
    void noticeAppliedUpdate();
    void sayStartProblem();
    void settleTabsAtStart();
    int shutDown(char** argv);
    // serverwait.cpp
    std::optional<int> waitForServer(char** argv);
    // search.cpp
    void openSearchKeyboard();
    void searchKeyboardResult(ui::KeyboardResult res);
    void tickSearch(float dt, screens::Ctx& ctx);
    void pumpSearch(float dt);
    void sizeSearchResults();
    // detail.cpp
    void showDetailProgress();
    void tickDetail(float dt);
    // coverfill.cpp
    void drainCovers();
    // home.cpp
    void retargetHomePress();
    void drawHome(float dt, float sc);
    // nav.cpp
    void tickScreens(float dt, screens::Ctx& ctx);
    void applyTabContentFade(bool gameUp);
    void drawScreen(float sc);
    void drawScreenGlass(bool gameUp, float sc);
    void resetContentFade();
    void drawKeyboardAndDissolve();
    void takePendingSwitch();
    // batteryrow.cpp
    float settleBatteryRow(float sc, float dt);
    void drawBatteryRow(float& leftOfChip, float rowW, float sc);
    // bar.cpp
    void drawBar(bool gameUp, float sc, float dt);
    // backdrop.cpp
    void drawGround(bool gameUp, float dt);
    // settingsflow.cpp
    void readScreenOffAtStart();
    void closeDownloadsOffSettings();
    void applySetting(const screens::Result& res);
    void applySettingChoice(const screens::Result& res);
    // updateflow.cpp
    void readUpdateCheckAtStart();
    void tickUpdateTimer(float dt);
    void pollUpdate();
    void askReadyWhenFree();
    void sayUpdatedTo();
    void pressUpdate();
    // access.cpp
    void startAccessAtBoot();
    void pollAccess();
    void pressDeveloper();
    void pressVersion();
    void pressFiles();
    // remoteplayflow.cpp
    void startRemotePlayAtBoot();
    void pollRemotePlay();
    void pressPairedDevices();
    void pressRemotePlay();
    // tailscaleflow.cpp
    void startTailscaleAtBoot();
    void pollTailscale();
    void pressTailscale();
    // accountsflow.cpp
    void takeAccountResults();
    void pressRemoveAccount();
    void pressAddAccountRow();
    void pressSwitchAccount(const screens::Result& res);
    void pressAddAccount();
    // reportflow.cpp
    void pollReport();
    void stopReport();
    void pressReport();
    // controllers.cpp
    bool interceptControllers(const SDL_Event& e);
    void noticeLowBattery(float dt);
    void followPads();
    void pumpPadWindow();
    // drivesflow.cpp
    void followDrives();
    void pressEject(const screens::Result& res);
    void pressFormat(const screens::Result& res);
    // achievements_ui.cpp
    void tickRaToast(float dt);
    void drawRaToast(float sc);
    void pressRetroAchievements();
    // wifi.cpp
    void takeNetworkAnswer();
    void refreshWifiList();
    void takeWifiJoin();
    void pressWifi();
    // pinflow.cpp
    void lockPinOffSettings();
    void pressPinOff();
    void pressPinChange();
    void pressPinSet();
    // wiipanel.cpp
    void followWiiRemotes();
    void pumpWiiPairing();
    // serveraddressflow.cpp
    void takeServerCheck();
    void pressServer();
    // curtain.cpp
    void startCurtain();
    void drawCurtain(float sc);
    // gridfill.cpp
    void drainGridPages();
    bool takeGridPages(GridFill& f, bool toScreen);
    void fillGrid(screens::Tile& t);
    void openTile(const screens::Result& res);
    // about.cpp
    void pressCredits();
    void pressHowTo();
    void pressWhatsNew();
    // downloadsflow.cpp
    void pressDownloads();
    void pressRemoveDownload(const screens::Result& res);
    // steamflow.cpp
    void pressSteamShow();
    void pressSteam();
    // states.cpp
    void playFromState(const screens::Result& res);
    void loadPendingStates();
    void drawStateHold(float sc);
    void sayStateUploaded();
    // favorites.cpp
    void toggleFavorite(const screens::Result& res);
    // ps2window.cpp
    bool frameOverPs2Window();
    // discs.cpp
    void pumpDiscChange();
    // power_menu.cpp
    void borrowPowerButton();
    void pollPower();
    void waitForUploadsToPowerDown();
    // pausemenu.cpp
    void tickPauseMenu(float dt);
    void drawPauseScrim();
    void drawPauseMenu(float sc);
    // standalone_play.cpp
    bool takeStandaloneEvent(const SDL_Event& e);
    bool watchStandalone(float dt);
    void closeStandaloneAtExit();
    bool pointerButton(int player, SDL_GamepadButton b, bool down);
    bool pointerAxis(int player, SDL_GamepadAxis a, int16_t value);
    void pointerTick(float dt);
    void showPointer(bool on, int player);
    void endPointer();
    void drawPointer();
    // play.cpp
    void followRemotesInGame();
    bool readGameInput(cab::Core& core);
    void drawGamePicture(cab::Core& core, int dh);
    void leaveGameAtExit();
    void reportAndUnloadCore();
    // pace.cpp
    void runGameFrame(cab::Core& core, float dt, bool rewinding);
    // launch.cpp
    void forgetLaunchAsks();
    bool takeFinishedJob();
    void prepareSaveDirAndDiscs(LaunchContext& c);
    void restoreBeforeLoad(LaunchContext& c);
    bool loadLaunchedGame(LaunchContext& c);
    void setUpLoadedGame(LaunchContext& c);
    void reportLoadedOptions(cab::Core& core);
    void openSoundAndPace(cab::Core& core);
    void startPlaying(cab::Core& core);
    void tickLaunchBusy(float dt);
    // testhooks.cpp
    void openScreenForCapture();
    void walkNavScript();
    void evictTestHook();
    void downloadHook();
    void launchHooks(float dt);
    void overlayHooks();
    void stateHooks();
    void noticeGalleryHook(float dt);
    void captureFrame(int dw, int dh);
    void refuseCaptureOverAnotherWindow(const char* whose);
    // discrig.cpp
    void parseDiscScript();
    void runDiscScript();
    // offline_sync.cpp
    void retryOwedUploads(float dt);
    void followServer(float dt);
    // notice.cpp
    void drawNotice(float sc);
    // idle.cpp
    void drawDim();
    void wakeTvAtExit();
    // input.cpp
    void handleEvent(const SDL_Event& e);
    void repeatHeld(float dt);
    // frame.cpp
    void runFrameLoop();
};
