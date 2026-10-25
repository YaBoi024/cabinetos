#include "launch.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>

#include "app.h"
#include "cache.h"
#include "catalog.h"
#include "core.h"
#include "disc.h"
#include "discs.h"
#include "dolphinprep.h"
#include "filesave.h"
#include "library.h"
#include "ps2.h"
#include "quality.h"
#include "romm.h"
#include "saves.h"
#include "standalone.h"
#include "storage.h"
#include "sysopts.h"
#include "vectrexsheets.h"
#include "wii.h"
#include "wiimii.h"
#include "wiiremote.h"

// PlayStation 2 picture quality.
//
// **THESE TWO FLAGS ARE A TEST INSTRUMENT AND NOT THE PRODUCT.** MMagTech,
// 2026-09-21: they exist so a number can be tried in front of the television,
// and how this is really exposed is open question 23 — ONE quality setting for
// the whole console, not a per-emulator menu and not a command line. Nobody
// should build a settings screen on top of these, and nobody should read a
// value chosen here as a decision: the point of them is to find out what the
// decision should be.
//
// Both default to what PCSX2 itself ships — native resolution, no anisotropic
// filtering — so a console that is given neither flag behaves exactly as it
// did. See --ps2-upscale in the argument parser for what the multiplier means
// on a 4K panel.
//
// SINCE #63 NEITHER IS SET UNLESS A FLAG SAYS SO, and the picture quality level
// decides (quality.h). -1 means "not given".
static float gPs2Upscale = -1.0f;
static int gPs2Anisotropy = -1;

// Set by the command line (cli.cpp). ONE DEFINITION OF EACH, HERE: a second
// copy of the two statics anywhere else would compile, the parser would write
// that copy, and --ps2-upscale and --ps2-aniso would stop working without a
// word.
void setPs2Upscale(float upscale) { gPs2Upscale = upscale; }
void setPs2Anisotropy(int anisotropy) { gPs2Anisotropy = anisotropy; }

// Starts one. Returns false if the game cannot be played here at all, which is
// worth saying immediately rather than after a download.
//
// WHERE THE BYTES GO is decided here and nowhere else. A game somebody keeps is
// written straight into `roms/` rather than downloaded into `cache/` and then
// moved, because the move would be a second pass over gigabytes for no reason.
// A game already on the disk is left exactly where it is — including when
// somebody else keeps it, which is the case that makes "one kept game, two
// people" true on disk rather than only in a comment.
bool beginLaunch(LaunchJob& job, romm::Client& client, const romm::Game& game,
                 const std::string& coreDir, std::string* err,
                 bool playWhenReady, bool keepWhenReady) {
    const catalog::Coverage cov = catalog::coverageFor(game);
    if (cov.support != catalog::Support::Playable || !cov.core) {
        *err = game.platformName + ": " + (cov.reason ? cov.reason : "not playable here");
        return false;
    }

    // REMEMBERED, so it can be shown and played with no server (#88): the
    // game is on its way onto the drive, or already there.
    cache::rememberGame(game.id, gameRecordJson(game));

    job.stop();
    job.cancel = false;
    job.got = 0;
    job.total = 0;
    job.title = game.name.empty() ? game.fsName : game.name;
    job.romId = game.id;
    job.platformFsSlug = game.platformFsSlug;
    job.systemName = game.platformName;
    job.biosMissing = false;
    job.platformSlug = game.platformSlug;
    job.titleId = game.titleId;
    job.platformId = game.platformId;
    job.raHash = game.raHash;
    job.md5 = game.md5;
    job.fsStem = game.fsName;
    if (const size_t dot = job.fsStem.find_last_of('.'); dot != std::string::npos)
        job.fsStem.erase(dot);
    job.coreName = cov.core;
    // ONE RULE, ONE PLACE. This used to append `_libretro.so` to whatever the
    // manifest called the core, which is right for twenty of the twenty-one
    // and wrong for `fbneo_libretro`, whose name already ends in it — so the
    // launch looked for `fbneo_libretro_libretro.so`, a file nothing builds,
    // while coverageFor looked for the right one and reported the game
    // playable. Every FBNeo game on this console failed at the moment somebody
    // pressed Play. See catalog::coreFileName.
    job.corePath = coreDir + "/" + catalog::coreFileName(cov.core);
    job.romPath.clear();
    job.discs.clear();
    job.message.clear();
    job.playWhenReady = playWhenReady;
    job.keepWhenReady = keepWhenReady;
    job.loweredCurtain = false;
    job.stage = LaunchJob::Stage::Downloading;

    const storage::User& user = storage::currentUser();

    // AN EMULATOR THAT IS ITS OWN APPLICATION LOADS NOTHING HERE. There is no
    // core to open and nothing in this process to hand directories to; the
    // game file is fetched exactly as for any other system, and pumpLaunch
    // starts the program once it is on the disk. docs/PROJECT.md, open
    // question 32.
    const cab::standalone::Emulator* emu = cab::standalone::find(cov.core);
    std::string validExts;
    bool blockExtract = false;
    if (emu) {
        // The emulator opens the file as it came, and a Switch NSP is not an
        // archive the console would open, so nothing is unpacked. NOT
        // `blockExtract`: that also stops a one-file game collapsing into one
        // file (`852 - Aqua Kitty UDX.nsp`), which is how every other system's
        // one-file game is kept, and it left each Switch game a folder.
        validExts = emu->extensions;
    } else {
        // The core is loaded here, on the frame thread, before the worker starts:
        // it is cheap, it is the thing that decides whether the archive gets
        // opened, and a core that will not load should fail now rather than after
        // three gigabytes have been fetched.
        cab::Core& core = cab::Core::shared();
        // Before load(), because retro_init is inside it and a core may read the
        // directories there and never ask again. See Core::setDirectories.
        //
        // THE SAVE DIRECTORY IS THIS PERSON'S AND THIS GAME'S. Every core on the
        // machine used to share one, which is why attributing a PSP save folder to
        // the game that wrote it needed a timestamp comparison rather than a path.
        const std::string saveDir =
            storage::savesDir(user, game.platformFsSlug, game.id, cov.core);
        storage::makeDirs(saveDir);
        core.setDirectories(storage::biosDir(), saveDir);
        // Also before load(), and for the same reason. This was reaching only the
        // --core-options audit until PPSSPP needed the first real override, which
        // meant the override table was being PRINTED rather than applied: every
        // core played on its declared defaults and the audit agreed with itself.
        // Invisible while the table was empty, wrong the moment it was not.
        //
        // PICTURE QUALITY ON TOP (#63): the level this game runs at decides
        // its resolution and shader building; quality.h is the one place that
        // knows what each level means.
        {
            const quality::Level level = quality::forGame(game.id);
            std::map<std::string, std::string> opts = catalog::optionOverrides(job.coreName);
            for (const auto& kv : quality::coreOptions(job.coreName, game.platformSlug, level))
                opts[kv.first] = kv.second;
            // And the system's own pause menu rows (#73).
            for (const auto& kv : sysopts::overrides(game.platformSlug))
                opts[kv.first] = kv.second;
            // The Wii's rumble switch as the last Wii game's HOME Menu left
            // it (#274, wii.h).
            for (const auto& kv : wii::coreOptions(job.coreName, game.platformSlug))
                opts[kv.first] = kv.second;
            // A Vectrex game under its sheet, fitted inside the sheet's
            // window (#224, vectrexsheets.h).
            for (const auto& kv : vectrexsheets::coreOptions(job.coreName, game.platformSlug,
                                                             game.md5, job.title, game.fsName))
                opts[kv.first] = kv.second;
            core.setOptionOverrides(opts);
            quality::logApplied(job.coreName, game.id, level);
        }

        // PLAYSTATION 2 NEEDS THREE THINGS NO LIBRETRO CORE DOES, and the second
        // is the one that matters most.
        //
        // PCSX2 refuses to start without its own resources folder — its game
        // database, fonts and GS shaders — rather than degrading, so it ships with
        // the emulator the way PPSSPP's system files already do.
        //
        // The memory card is NOT named here. Core::loadGame names it from the rom
        // by the same rule catalog::saveFiles uses, so the existing save machinery
        // (restore before launch, capture after, refuse an unformatted card, file
        // it on the server under the name Cabinet's Mac used) works on it
        // unchanged.
        if (cov.core && std::string(cov.core) == "pcsx2") {
            // The level's values, unless a --ps2-upscale or --ps2-anisotropy
            // given for a measurement says otherwise.
            const quality::Ps2 q = quality::ps2(quality::forGame(game.id));
            core.setPs2(storage::imageAssetsDir() + "/pcsx2/resources",
                        gPs2Upscale > 0.0f ? gPs2Upscale : q.upscale,
                        gPs2Anisotropy >= 0 ? gPs2Anisotropy : q.anisotropy,
                        ps2::widescreenOn(game.id), game.titleId);
        }

        if (!core.load(job.corePath)) {
            std::fprintf(stderr, "[launch] core %s: %s\n", job.coreName.c_str(),
                         core.error().c_str());
            *err = "Couldn't start this game";
            job.stage = LaunchJob::Stage::Idle;
            return false;
        }
        validExts = core.validExtensions();
        blockExtract = core.blockExtract();
    }

    // THE KEEP IS RECORDED BEFORE THE FIRST BYTE MOVES, for two reasons that
    // both matter. It makes the in-flight download safe from the eviction its
    // own size may trigger — a game in `roms/` is not a candidate, because
    // eviction only walks `cache/` — and it means the console has already
    // decided it can afford the promise rather than discovering it cannot after
    // fetching two gigabytes. A job that fails clears the record again, in
    // pumpLaunch.
    if (keepWhenReady) {
        std::string kerr;
        if (!cache::keep(user, game.id, gameRecordJson(game), &kerr)) {
            std::fprintf(stderr, "[keep] %s\n", kerr.c_str());
            *err = "Couldn't download this game";
            job.stage = LaunchJob::Stage::Idle;
            return false;
        }
    }

    // ONE GAME, ONE COPY, and this is where that is enforced. A game kept while
    // the drive was plugged in, then played while it was not, is on the machine
    // twice — see cache::dedupe. Checked at the moment somebody presses Play
    // rather than when a drive appears, so there is no plug-in event to miss.
    const cache::Placement placed = cache::dedupe(game.id, game.sizeBytes);

    // Already here? Then that is where it stays.
    //
    // OTHERWISE, AND THIS IS THE RULE THAT CAUGHT ME OUT: a fetch only writes
    // into `roms/` when the person is keeping the game with THIS press. Pressing
    // Play is not a storage act — that is the product's own rule, the one that
    // makes Download the single deliberate one — so a game fetched by playing it
    // goes into the cache even when somebody keeps it.
    //
    // The case that proves it: keep a game on the drive, unplug the drive, press
    // Play. The game is still kept and the keep record is still on the internal
    // disk, so asking "is this kept" put the fetched copy in `roms/` on the
    // INTERNAL disk, where nothing is allowed to evict it. Do that twenty times
    // with the drive in a drawer and the console has filled its own disk with
    // games it may not delete — which is the exact failure the system reserve
    // exists to prevent, arriving by a door nothing was watching.
    //
    // As a cached copy it is a stand-in: evictable, costing a download at worst,
    // and deleted outright the moment the drive comes back and dedupe sees the
    // real one. Which is what a copy of a file you already own should be.
    const bool kept = placed.present ? placed.kept : keepWhenReady;
    const std::string location = placed.present
                                     ? placed.location
                                     : (keepWhenReady ? cache::keepLocation(game.sizeBytes)
                                                      : storage::primaryLocation());
    job.entryPath = placed.present
                        ? placed.entryPath
                        : cache::entryPathFor(location, game.platformFsSlug, game.id,
                                              job.title, kept);
    job.location = location;

    const int id = game.id;
    const int platformId = game.platformId;
    const std::string slug = game.platformSlug;
    const std::string fsSlug = game.platformFsSlug;
    const std::string fsName = game.fsName;
    const int64_t expectedSize = game.sizeBytes;
    const bool multiFile = game.multiFile;
    const std::string entryPath = job.entryPath;
    job.worker = std::thread([&job, &client, id, platformId, slug, fsSlug, fsName, emu,
                              entryPath, location, validExts, blockExtract, expectedSize,
                              multiFile, user]() {
        launchWorker(job, client, id, platformId, slug, fsSlug, fsName, emu, entryPath,
                     location, validExts, blockExtract, expectedSize, multiFile, user);
    });
    return true;
}

// ---- The launch on the frame thread (#398) --------------------------------
// Play, a refusal, the pickup of a finished job and its stages, and the busy
// clock: members of the App (app.h), moved out of main.cpp in batch (f).

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

auto App::launchById(int romId) -> bool {
    // PRESSING PLAY ON SOMETHING ALREADY COMING JOINS IT — 2026-09-21.
    //
    // A download started by "Download and keep" runs in the background and
    // leaves the person free to browse. Pressing Play on that same game
    // while it is in flight means "and I want to play it when it lands",
    // which is one flag on the job that is already fetching it — not a
    // second fetch of the same bytes, and not a refusal.
    //
    // Any OTHER game is still refused while one is in flight: the fetch is
    // one worker and one entry path, and a second would need both.
    if (launchJob.busy()) {
        if (launchJob.romId == romId && !launchJob.playWhenReady) {
            launchJob.playWhenReady = true;
            launchJob.startedOn = static_cast<int>(here());
            std::fprintf(stderr, "[launch] joining the download of %s\n",
                         launchJob.title.c_str());
            return true;
        }
        // SAID, NOT SILENT (#184): a press that did nothing looked like a
        // broken button. One download at a time until there is a queue.
        menuNotice.say(launchJob.title + " is still downloading", Tone::Info);
        // THE SAME GAME, ALREADY JOINED, is a press that counts (#484), so
        // the state a second Play from a state picked goes with the launch.
        return launchJob.romId == romId;
    }
    // A JOB THE FRAME LOOP HAS NOT TAKEN YET is not replaced (#472): a
    // second press while the curtain falls on a Ready game replaced it, and
    // a replacement that ended in no game left the screen black. The press
    // can only be for the same game, from its own page; it counts as taken,
    // so a state or a Resume it asked for stays with the launch.
    if (launchJob.waiting()) return launchJob.romId == romId;
    const romm::Game* g = nullptr;
    for (const auto& x : games) if (x.id == romId) { g = &x; break; }
    if (!g) { std::fprintf(stderr, "[launch] no game with id %d\n", romId); return false; }

    std::string lerr;
    if (!beginLaunch(launchJob, liveClient, *g, coreDir, &lerr)) {
        std::fprintf(stderr, "[launch] %s\n", lerr.c_str());
        // A CORE THAT WILL NOT LOAD AT THE PRESS SAYS SO, where every other
        // launch refusal does (#398); it said nothing anywhere. Not a game
        // this console cannot play: beginLaunch refuses that before it
        // touches the job, so refuseLaunch would name the last game.
        const catalog::Coverage cov = catalog::coverageFor(*g);
        if (cov.support == catalog::Support::Playable && cov.core) refuseLaunch(lerr);
        else showWhyUnplayable(romId);
        return false;
    }
    launchJob.startedOn = static_cast<int>(here());
    std::fprintf(stderr, "[launch] %s (%s) via %s\n", g->name.c_str(),
                 g->platformName.c_str(), launchJob.coreName.c_str());
    return true;
}

// A LAUNCH THAT CANNOT GO AHEAD says why on the game's own screen, the
// way a core that will not load does below: opened if the press came from
// somewhere with nowhere to put a message, and the curtain lifted onto it.
//
// THE MESSAGE WAITS FOR THE SCREEN when the screen is opened a frame later:
// DetailScreen::open clears its notice, so a notice set before the open
// was wiped by it, and a refusal from Home arrived on the game's screen
// saying nothing. Found 2026-09-28 by a Switch game that would not load,
// launched from Home; the core-failure path had the same order.
void App::refuseLaunch(const std::string& why) {
    bool deferred = false;
    if (here() != Screen::Detail || detailScreen.game().romId != launchJob.romId) {
        for (size_t i = 0; i < cards.size(); ++i) {
            if (cards[i].id == launchJob.romId) {
                openDetailSoon(static_cast<int>(i));
                deferred = pendingDetail >= 0;
                break;
            }
        }
    }
    if (deferred) pendingDetailNotice = why;
    else detailScreen.setNotice(why);
    sound::play(sound::Cue::Edge);
    curtain.retarget(0.0f, kCurtainUp);
}

// A GAME THIS CONSOLE CANNOT PLAY, pressed where Play is not greyed (Home's
// Resume on a Vita game in RomM's history, or a Wii game that needs a Remote
// with none paired): its page, which says why, and the edge sound (#484).
// It did nothing on screen. Not refuseLaunch: the job was never touched, so
// it names the last game launched.
void App::showWhyUnplayable(int romId) {
    sound::play(sound::Cue::Edge);
    if (here() == Screen::Detail && detailScreen.game().romId == romId) return;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].id == romId) {
            openDetailSoon(static_cast<int>(i));
            return;
        }
    }
}

// A WII REMOTE STARTS ONLY WII GAMES (MMagTech, 2026-10-24;
// docs/decisions/2026-10-24-wii-remote-outside-wii-games.md). Its buttons
// drive the menus, so with only a Remote on, a player started a game it
// cannot play and could not leave it (a Discord report, 2026-10-23). Asked
// first by each press that starts a game (Play, Play from a state, Home's
// Resume), never by launchById, so nothing that starts a game without a
// press is touched. True when it refused: a toast where the press was made.
bool App::refuseRemoteStart(int romId) {
    if (!lastPressFromRemote) return false;
    for (const auto& g : games) {
        if (g.id != romId) continue;
        if (g.platformSlug == "wii") return false;
        std::fprintf(stderr, "[launch] a Wii Remote cannot start %s (%s)\n", g.name.c_str(),
                     g.platformName.c_str());
        menuNotice.say("This Remote only plays Wii games", Tone::Info);
        sound::play(sound::Cue::Edge);
        return true;
    }
    return false;
}

// A LAUNCH THAT DOES NOT START A GAME TAKES WHAT ITS PRESS ASKED FOR WITH
// IT: the state picked on the game's page, Home's Resume and the disc Start
// over asked for belong to this launch, and left set they were loaded into
// the next game of any title on its first frame (#398). Called on every
// way out of takeFinishedJob and loadLaunchedGame that loads no game here,
// and by a press that counts before it sets its own (playFromState,
// activateHome; #484).
// NOT IN finishExit, which keeps its own three lines: Start over sets the
// disc just before it leaves the game (startOverOnDisc).
void App::forgetLaunchAsks() {
    pendingStateId = 0;
    pendingStatePath.clear();
    pendingResume = false;
    gNextStartDisc = 0;
}

// What pumpLaunch's stages hand on: the locals of the one function it was
// that more than one stage reads (#398, restructure batch f).
struct LaunchContext {
    cab::Core& core;
    const storage::User& user;
    std::string saveDir{};
    const char* dirSub = nullptr;
    const char* launchTag = nullptr;
    disc::System discSys{};
    disc::Boot discBoot{};
    int startDisc = 0;
    std::vector<int> discOrder{};
    std::string romForCore{};
    std::vector<cab::FileSaveState> restored{};
};

// Picks up a finished job. Loading the game happens HERE, on the frame
// thread, because the core is not thread-safe and the worker only ever
// moved bytes.
//
// IN STAGES, IN THE OLD ORDER (#398, restructure batch f). A stage that
// returns false ends the pump for this frame where the one function
// returned; a stage after it does not run. What the stages hand on is
// the LaunchContext.
//
// THE RESTORE ORDER, decided here and nowhere else (#481, LAUNCH-17): the
// folder save and the file saves are written BEFORE the core loads the game,
// because a core reads them once while the machine is built; the battery
// save AFTER it, because the core is handed the bytes; and in every one a
// copy this console still owes the server wins over the server's. The
// server's rows and bytes were fetched by the worker before the job was
// Ready (launch_routes.cpp, fetchLaunchSaves; launchJob.saves), so no stage
// here waits on the network.
void App::pumpLaunch() {
    if (!takeFinishedJob()) return;
    LaunchContext c{.core = cab::Core::shared(), .user = storage::currentUser()};
    prepareSaveDirAndDiscs(c);
    restoreBeforeLoad(c);
    if (!loadLaunchedGame(c)) return;
    setUpLoadedGame(c);
    restoreBatterySave(c.core, session, c.saveDir, c.user, launchJob.saves);
    // The fetched saves are written; a card can be megabytes, so not kept.
    launchJob.saves = SaveFetch{};
    launchJob.miiSaves = SaveFetch{};
    reportLoadedOptions(c.core);
    openSoundAndPace(c.core);
    startPlaying(c.core);
}

// Stages 1 to 3: a failed job is undone and a Ready one taken (a launch once
// the curtain is down); a download, a launch the person walked away from
// and an emulator of its own end here. True when a game is to be loaded now.
bool App::takeFinishedJob() {
    const LaunchJob::Stage st = launchJob.stage.load();
    if (st == LaunchJob::Stage::Failed) {
        std::fprintf(stderr, "[launch] failed: %s\n", launchJob.message.c_str());
        // A promise the console could not deliver is not a promise. The
        // record went in before the download so the download would be safe
        // from eviction; it comes out again if the download never finished,
        // or the disk fills with reserved space holding nothing.
        // UNDOING A PROMISE, NOT RECLAIMING SPACE, so the bytes stay and
        // become evictable. The file may be a game that was already on the
        // disk before anybody pressed Download, and throwing that away
        // because a firmware fetch failed would be its own small disaster.
        if (launchJob.keepWhenReady)
            cache::unkeep(storage::currentUser(), launchJob.romId,
                          /*keepTheBytes=*/true);
        // A PLAY THAT FAILED SAYS WHY WHERE THE PERSON IS, as a core that
        // will not load the game does (refuseLaunch): pressed on Home's
        // Resume, it said nothing anywhere (#398). A Download, a Play the
        // person walked away from and a cancel keep the page's notice.
        // A CANCEL SAYS NOTHING (#472): the person asked for it, and a
        // transfer cut short by it fails in the same words as a real fault.
        const bool cancelled = launchJob.cancel.load();
        const bool sayIt = launchJob.playWhenReady && !cancelled &&
                           launchJob.startedOn == static_cast<int>(here());
        if (here() == Screen::Detail &&
            detailScreen.game().romId == launchJob.romId) {
            detailScreen.setKept(
                cache::isKeptBy(storage::currentUser(), launchJob.romId));
            if (!sayIt) detailScreen.setNotice(cancelled ? std::string() : launchJob.message);
        }
        refreshKeeps();
        forgetLaunchAsks();
        launchJob.stop();
        launchJob.stage = LaunchJob::Stage::Idle;
        if (sayIt) refuseLaunch(launchJob.message);
        return false;
    }
    if (st != LaunchJob::Stage::Ready) return false;

    // WHETHER THE PERSON MOVED ON is decided BEFORE the curtain, not after
    // it — 2026-09-27. It used to be checked below, after the curtain had
    // come all the way down, and that branch returned without lifting it:
    // the console sat on a black screen with no way out but restarting
    // the app (#85, Dynamite Cop). Asked every frame, so a screen change
    // while the curtain is on its way down lifts it again as well.
    const bool movedOn = launchJob.playWhenReady && launchJob.startedOn >= 0 &&
                         launchJob.startedOn != static_cast<int>(here());

    // THE CURTAIN COMES DOWN BEFORE THE BLOCKING WORK, not after it. See
    // design::kCurtainDown. The stage is deliberately NOT cleared here, so
    // this runs again next frame and the frames in between are spent
    // animating something a person can see rather than waiting inside
    // Core::loadGame with nothing on the screen.
    //
    // Only for a launch. A download that was merely asked for must not
    // black the screen out — the person is still browsing, and its progress
    // is on the row they pressed.
    if (launchJob.playWhenReady && !movedOn) {
        curtain.retarget(1.0f, kCurtainDown);
        launchJob.loweredCurtain = true;
        if (curtain.value() < 0.995f) return false;
    }

    launchJob.stop();
    launchJob.stage = LaunchJob::Stage::Idle;
    // THE GAME'S MANUAL COMES WITH IT (#364), beside it on the drive, on
    // a worker of its own: a manual that does not come never holds up the
    // game.
    manualBeside(launchJob.romId, launchJob.entryPath);

    // A download that was asked for rather than needed stops here. The game
    // is on the disk, prepared exactly as a launch would have prepared it,
    // so playing it later costs nothing — and nothing about it is guessed
    // at a second time.
    if (!launchJob.playWhenReady) {
        std::fprintf(stderr, "[download] %s is here%s\n", launchJob.title.c_str(),
                     launchJob.keepWhenReady ? " and kept" : "");
        if (here() == Screen::Detail &&
            detailScreen.game().romId == launchJob.romId)
            detailScreen.setKept(
                cache::isKeptBy(storage::currentUser(), launchJob.romId));
        refreshKeeps();
        // THE ONE EARLY WARNING. Only a Download can fill the drives (the
        // cache clears itself, saves are small), so this is the one moment
        // to say it: kept games are close to leaving no room to play
        // something new. MMagTech, 2026-09-25.
        if (launchJob.keepWhenReady && cache::almostFull())
            menuNotice.say("Storage almost full", Tone::Info);
        forgetLaunchAsks();
        return false;
    }

    // AND IT ONLY LAUNCHES ITSELF IF NOBODY WALKED AWAY — 2026-09-21.
    //
    // MMagTech, on the download no longer blocking the screen: *"wait
    // should it auto launch after complete."* It is a question that could
    // not be asked while a modal panel was up, because there was nowhere to
    // walk to. Now there is: press Play on a 1.78 GB arcade set, get bored,
    // go and look at something else, and four minutes later the console
    // would drop you into a game you had stopped waiting for.
    //
    // So: still on the screen you pressed it from, you are visibly waiting
    // and it launches — "one action from cold to playing" holds. Moved on,
    // and it finishes quietly. The game is on the disk, prepared exactly as
    // a launch would have prepared it, and its row says Play.
    if (movedOn) {
        // Which screens, because the one time this was seen it fired two
        // seconds after the press, too soon to be somebody walking away,
        // and the log could not say what had moved.
        std::fprintf(stderr,
                     "[launch] %s is ready, and not starting: the screen moved on "
                     "(pressed on screen %d, now on %d)\n",
                     launchJob.title.c_str(), launchJob.startedOn,
                     static_cast<int>(here()));
        if (launchJob.loweredCurtain) curtain.retarget(0.0f, kCurtainUp);
        if (here() == Screen::Detail &&
            detailScreen.game().romId == launchJob.romId)
            detailScreen.setKept(
                cache::isKeptBy(storage::currentUser(), launchJob.romId));
        refreshKeeps();
        forgetLaunchAsks();
        return false;
    }

    // No states and no discs in an emulator run as a program, so Home's
    // Resume on one asked for nothing it can give, even when it starts.
    if (const cab::standalone::Emulator* emu =
            cab::standalone::find(launchJob.coreName)) {
        forgetLaunchAsks();
        startStandalone(*emu);
        return false;
    }
    return true;
}

// Stage 4: this person's save directory for this game, its directory save
// restored before the load, and the disc it starts on.
void App::prepareSaveDirAndDiscs(LaunchContext& c) {
    // THIS PERSON'S SAVES, FOR THIS GAME, FROM THIS CORE. It is also the
    // directory the core itself is handed, so a Sega CD's .brm and a PSP's
    // memory stick land in the same place as the battery snapshot this
    // frontend takes — which is the whole of what "two save directories"
    // and "one flat pile" cost.
    c.saveDir = storage::savesDir(
        c.user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
    storage::makeDirs(c.saveDir);

    // A DIRECTORY save comes down BEFORE the game is loaded, unlike the
    // battery below, which the core can be handed afterwards. PPSSPP mounts
    // the memory stick while the game boots, so a folder that arrives later
    // is a folder the game has already decided is not there.
    c.dirSub = catalog::directorySaveRoot(launchJob.coreName.c_str(),
                                          launchJob.platformSlug,
                                          launchJob.titleId);
    c.launchTag = catalog::saveTag(launchJob.coreName.c_str());
    // GameCube's raw card from before, which Dolphin reads into the folder
    // (writeDolphinConfig): `card.raw` with the region Dolphin stamps in.
    std::string oldGameCubeCard;
    if (launchJob.platformSlug == "ngc") {
        if (const char* region = catalog::gameCubeRegion(launchJob.titleId))
            oldGameCubeCard = c.saveDir + "/card." + region + ".raw";
        else
            std::fprintf(stderr, "[save] no game code for this GameCube game, from RomM or "
                         "off its file, so its region and save folder are unknown; saves "
                         "stay on this console\n");
    }
    if (c.dirSub && c.launchTag && launchJob.saves.asked)
        restoreDirSave(launchJob.saves, c.launchTag, c.saveDir + "/" + c.dirSub,
                       launchJob.title, oldGameCubeCard);

    // AND THE SAVES THE CORE WRITES AS A FILE, which is most of the ones
    // on a real server and none of the ones this console used to handle.
    // Before the load for the same reason the directory save is: these
    // cores read their file once while the machine is being built. See
    // filesave.h.
    //
    // The stem is the basename of what the core is handed, without its
    // extension, because that is what these cores name the save after —
    // `lethalen.nv`, `Lunar - The Silver Star.brm`.
    // A GAME ON SEVERAL DISCS (#162): the disc it starts on, the last one
    // this person was on (disc 1 the first time) or the one Start over
    // asked for, and how that disc is put first on this system.
    c.discSys = disc::systemFor(launchJob.platformSlug);
    c.discBoot = c.discSys.boot;
    if (gDiscBootOverride >= 0 && c.discSys.change == disc::Change::Insert)
        c.discBoot = static_cast<disc::Boot>(gDiscBootOverride);
    // Taken whatever this game is, so a single-disc launch never leaves it set.
    const int askedDisc = gNextStartDisc;
    gNextStartDisc = 0;
    if (!launchJob.discs.empty()) {
        const std::vector<disc::Disc>& discs = launchJob.discs;
        const int count = disc::highest(discs);
        c.startDisc = askedDisc > 0 ? askedDisc : disc::lastDisc(c.user, launchJob.romId);
        if (!disc::byNumber(discs, c.startDisc)) c.startDisc = discs.front().number;
        // GAMECUBE ALWAYS STARTS AT THE TOP OF THE LIST: the game asks for
        // the disc it needs and Dolphin puts it in; which one was in last
        // time is not known (syncDiscFromCore).
        if (c.discSys.change == disc::Change::Automatic) c.startDisc = discs.front().number;
        const std::string dir = launchJob.entryPath;
        const std::string first = dir + "/" + disc::byNumber(discs, c.startDisc)->file;
        auto writeList = [&](int top) {
            const std::string t = disc::listText(discs, top);
            if (!cab::writeBytes(launchJob.romPath, std::vector<uint8_t>(t.begin(), t.end())))
                std::fprintf(stderr, "[disc] could not write %s\n", launchJob.romPath.c_str());
            c.discOrder = disc::listOrder(discs, top);
        };
        const char* how = "";
        switch (c.discBoot) {
            case disc::Boot::File:
                // No list for this core: the disc's own file.
                launchJob.romPath = first;
                c.discOrder = {c.startDisc};
                how = "its own file";
                break;
            case disc::Boot::ListFirst:
                writeList(c.startDisc);
                how = "first in the list";
                break;
            case disc::Boot::InitialImage:
                writeList(0);
                // Disc 1 as well: Flycast keeps the last start disc it
                // was told across games, and would start on it.
                c.core.setInitialDisc(c.startDisc - 1, first);
                how = "the core's start disc";
                break;
            case disc::Boot::SwapFirst:
                writeList(0);
                how = "put in before the first frame";
                break;
        }
        std::fprintf(stderr, "[disc] starts on disc %d of %d (%s)\n", c.startDisc, count, how);
    }
}

// Stage 5: what the core reads while the machine is built, put in place
// before the load: the file saves, Dolphin's files, the Miis, the Wii's
// port devices.
void App::restoreBeforeLoad(LaunchContext& c) {
    std::vector<catalog::SaveFile> saveSpecs;
    {
        std::string stem = launchJob.romPath;
        if (const size_t slash = stem.find_last_of('/'); slash != std::string::npos)
            stem.erase(0, slash + 1);
        if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos)
            stem.erase(dot);
        saveSpecs = catalog::saveFiles(launchJob.platformSlug,
                                       launchJob.coreName, stem);
    }
    // Before the core loads: it reads Dolphin.ini on startup.
    if (launchJob.platformSlug == "ngc") writeDolphinConfig(c.saveDir);
    // This person's Miis into every Wii game but the Mii Channel, whose
    // own are the master copy (#275).
    if (launchJob.platformSlug == "wii" && launchJob.coreName == "dolphin" &&
        !wii::isMiiChannel(launchJob.titleId))
        copyMiisInto(launchJob.miiSaves, launchJob.platformFsSlug, launchJob.coreName,
                     c.saveDir);
    if (launchJob.coreName == "dolphin") shareDolphinCache(c.saveDir);
    // What each player's port holds. A joypad, but on two systems. Wii: a
    // Classic Controller, or a GameCube pad for a game that takes only
    // that, in the pads' ports; and, while a Wii Remote is paired, a real
    // Remote in every port after them (#200). A game that takes neither
    // pad gets Remotes only: pads have no player in it, and it is greyed
    // out while no Remote is paired (coverageFor).
    //
    // PlayStation: a DualShock in every player's port, for every game
    // (#345), so the sticks and rumble work. It starts in digital mode, as
    // the real pad powers on, and answers as the original pad until a game
    // asks for the DualShock's commands, so a game made for the old pad
    // plays as before; PCSX ReARMed's L1+R1+Select is its ANALOG button.
    // Any pad with two sticks drives it; one without rumble just does not
    // rumble. RetroArch and Batocera plug in the old pad and leave the
    // DualShock to the player; DuckStation plugs in a DualShock.
    sessionNeedsRemote = false;
    if (launchJob.coreName == "pcsx_rearmed") c.core.setPadDevice(cab::Core::kPs1DualShock);
    if (launchJob.platformSlug == "wii") {
        const unsigned device = wii::padDevice(wii::codeFromTitleId(launchJob.titleId));
        sessionNeedsRemote = device == 0;
        c.core.setPadDevice(device);
        if (wiiremote::anyPaired())
            c.core.setRealRemotesFrom(device == 0 ? 0 : static_cast<int>(players::connected().size()));
    }
    c.romForCore = launchJob.coreName == "dolphin"
                       ? dolphinReadablePath(launchJob.romPath, launchJob.romId)
                       : launchJob.romPath;

    c.restored = restoreFileSaves(saveSpecs, launchJob.fsStem, c.saveDir, c.launchTag,
                                  launchJob.saves);
}

// Stage 6: the load. False when the core refused the game, which is said
// on the game's screen.
bool App::loadLaunchedGame(LaunchContext& c) {
    c.core.setPlayers(players::count());
    if (c.core.isPs2()) openPs2Window();
    if (!c.core.loadGame(c.romForCore, storage::biosDir(), c.saveDir)) {
        closePs2Window();
        // AND SAY IT ON THE SCREEN, NOT ONLY TO STDERR — 2026-09-21.
        //
        // MMagTech: *"dreamcast game downloaded and didnt auto launch and
        // clicking play didnt launch it either."* The console knew exactly
        // why and had said so, to a log nobody on a sofa can read:
        //
        //   [launch] this core wants Vulkan and this device will not export
        //   memory as a file descriptor, so nothing it draws could reach
        //   the screen
        //
        // A refusal the person cannot see is indistinguishable from a
        // console that has stopped responding — and it is worse than a
        // crash, because pressing the button again does the same nothing
        // forever. The launch screen already has somewhere to put this:
        // `setNotice` is the same place a full disk reports itself.
        std::fprintf(stderr, "[launch] %s\n", c.core.error().c_str());
        forgetLaunchAsks();
        // If the press came from somewhere with nowhere to put a message —
        // Home's Resume card, for instance — the console goes to the
        // game's own screen and says it there, rather than inventing a
        // second place for refusals to live. See refuseLaunch.
        refuseLaunch(launchJob.biosMissing.load()
                         ? "No " + launchJob.systemName + " BIOS on your server"
                         : std::string("Couldn't start this game"));
        return false;
    }
    return true;
}

// Stage 7: after a good load, before the first frame: the session, the
// discs checked against the core, the state tag and the directory save's
// baseline.
void App::setUpLoadedGame(LaunchContext& c) {
    // Played now, so it is the LAST thing eviction should take rather than
    // whatever its download time says. The ENTRY's own mtime is the record —
    // see cache.h — so a game downloaded and never started stays oldest, and
    // it works the same whether the entry is one file or a folder.
    cache::touch(launchJob.entryPath);
    // The session, and the game's own save restored into it BEFORE the
    // first frame. A battery save is the game's progress; it has to be in
    // place when the game boots, not offered as a choice afterwards.
    // The game is loaded and the next frame is its first. Up it goes.
    curtain.retarget(0.0f, kCurtainUp);

    session = GameSession{};
    session.romId = launchJob.romId;
    session.raHash = launchJob.raHash;
    session.core = launchJob.coreName;
    session.platformSlug = launchJob.platformSlug;
    session.title = launchJob.title;
    session.saveDir = c.saveDir;
    session.fsStem = launchJob.fsStem;
    session.stateDir = storage::statesDir(
        c.user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
    session.fileSaves = std::move(c.restored);
    if (c.launchTag) session.saveTag = c.launchTag;
    session.snapshots = catalog::snapshotsAllowed(launchJob.coreName.c_str());
    // A Vectrex game's screen sheet (#224), read and uploaded here behind the
    // curtain; any other game drops the last one's.
    vectrexsheets::prepare(launchJob.platformSlug, launchJob.md5, launchJob.title,
                           launchJob.fsStem);
    if (!session.snapshots)
        std::fprintf(stderr, "[state] no save states on this system, true to the console\n");
    // THE DISCS, and the start disc checked against what the core says:
    // a core that took no start disc, or one put in before the first
    // frame by design (Saturn), gets it now, before any frame has run.
    if (!launchJob.discs.empty()) {
        session.discs = launchJob.discs;
        session.discOrder = c.discOrder;
        session.discChange = c.discSys.change;
        session.disc = c.startDisc;
        const int want = discIndexOf(session, c.startDisc);
        const int idx = c.core.discIndex();
        std::fprintf(stderr, "[disc] the core has %d disc(s), index %d (%s)\n",
                     c.core.discCount(), idx, c.core.discPath(idx).c_str());
        if (c.discSys.change == disc::Change::Automatic) {
            // Nothing to check or remember: see syncDiscFromCore.
        } else if (c.discSys.change == disc::Change::Insert && c.core.hasDiscControl() && idx != want) {
            if (c.discBoot == disc::Boot::InitialImage)
                std::fprintf(stderr, "[disc] the core started on index %d, not %d\n", idx,
                             want);
            discSwapNow(session, c.startDisc, "before the first frame");
        } else if (c.core.hasDiscControl()) {
            syncDiscFromCore(session);
        } else {
            disc::remember(c.user, session.romId, session.disc, disc::highest(session.discs));
        }
    }
    if (const char* tag = catalog::emulatorTag(launchJob.coreName.c_str())) {
        session.stateTag = tag;
    } else {
        std::fprintf(stderr,
                     "[sync] no settled tag for %s — states stay local%s\n",
                     launchJob.coreName.c_str(),
                     c.launchTag ? ", saves travel" : ", and so do saves");
    }
    // The baseline for a directory save, taken AFTER the restore above and
    // BEFORE the game has had a chance to write anything. What changes
    // between here and the quit is what this game saved. See GameSession.
    if (c.dirSub) {
        session.dirSaveRoot = c.saveDir + "/" + c.dirSub;
        session.dirSaveCore = launchJob.coreName;
        session.dirSavePlatform = launchJob.platformSlug;
        session.dirSaveTitleId = launchJob.titleId;
        session.dirAtLaunch = listDirSave(session);
        std::fprintf(stderr, "[save] %s holds %zu file(s) at launch\n",
                     session.dirSaveRoot.c_str(), session.dirAtLaunch.size());
    }
}

// Options again, AFTER the game is loaded. Some cores declare nothing
// until they know what they are running: FBNeo's and MAME's options are
// per-driver dipswitches, so their table does not exist until a machine
// is chosen. A count taken only at core-load time reports zero for them
// and looks like a core with nothing to configure.
void App::reportLoadedOptions(cab::Core& core) {
    const std::vector<cab::Core::OptionReport> opts = core.options();
    int asked = 0;
    for (const auto& o : opts) if (o.asked) ++asked;
    std::fprintf(stderr, "[options] %zu declared, %d asked for so far\n",
                 opts.size(), asked);
    // AND THE WHOLE TABLE, when asked for. --core-options-detail
    // cannot see these at all: a core that declares nothing until it
    // knows what it is running is invisible to an audit taken at core
    // load, and that is Dolphin, FBNeo and MAME — three of the most
    // configurable things this console ships.
    if (coreOptionsDetail) {
        for (const auto& o : opts) {
            std::fprintf(stderr, "  %s\n", o.key.c_str());
            if (!o.desc.empty())
                std::fprintf(stderr, "      what   %s\n", o.desc.c_str());
            std::fprintf(stderr, "      now    %s%s\n", o.chosen.c_str(),
                         o.overridden ? "   (ours)" : "");
            if (!o.values.empty()) {
                std::fprintf(stderr, "      takes ");
                for (size_t i = 0; i < o.values.size(); ++i)
                    std::fprintf(stderr, "%s%s", i ? " | " : " ",
                                 o.values[i].c_str());
                std::fprintf(stderr, "\n");
            }
        }
    }
    for (const std::string& k : core.undeclaredOptionAsks())
        std::fprintf(stderr, "[options] asked but never declared: %s\n",
                     k.c_str());
}

// OPEN THE AUDIO DEVICE. Until 2026-09-19 this happened ONLY on the
// `--core` developer path, so **a game launched from the library had
// no sound at all** — `audioStream` stayed null and nothing was ever
// opened. Found on the A9 Pro by MMagTech simply listening, with
// Crazy Taxi 2 running and PipeWire reporting zero streams.
//
// It survived this long because every audio claim in this project was
// made by COUNTING SAMPLES out of drainAudio() rather than by hearing
// anything — "2,384 frames, zero audio" is a sample count. A headless
// VM has nothing to listen with, so the one test nobody could run is
// the one that would have caught it.
//
// Opened here rather than at startup because the rate comes from the
// core: av_info is not known until a game is loaded.
//
// AND SET FOR EVERY GAME, NOT ONLY THE FIRST. Until 2026-09-27 the
// stream kept the rate of the first game played, so every later game
// at another rate was played too fast or too slow. MMagTech played
// Mortal Kombat II (SNES, 32040 Hz) and then DoDonPachi (FBNeo, 47997
// Hz): its sound went into a stream playing two-thirds as fast, fell
// further behind every second, and was still playing on Home after he
// quit, which read as the game running on in the background. Emptied
// here and at every exit, so no game hears another's leftovers.
// Stages 10 and 11: the sound opened or set for this game's rate, then
// the pace locked to the screen.
void App::openSoundAndPace(cab::Core& core) {
    SDL_AudioSpec src{};
    src.format = SDL_AUDIO_S16;
    src.channels = 2;
    src.freq = static_cast<int>(core.avInfo().sampleRate);
    if (audioStream) {
        SDL_ClearAudioStream(audioStream);
        if (SDL_SetAudioStreamFormat(audioStream, &src, nullptr))
            std::fprintf(stderr, "[frontend] audio in at %d Hz\n", src.freq);
        else
            std::fprintf(stderr, "[frontend] could not set audio to %d Hz: %s\n", src.freq,
                         SDL_GetError());
    } else {
        audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                &src, nullptr, nullptr);
        if (audioStream) {
            SDL_ResumeAudioStreamDevice(audioStream);
            std::fprintf(stderr, "[frontend] audio out at %d Hz\n", src.freq);
        } else {
            // A console with no sound card is still a console.
            std::fprintf(stderr, "[frontend] no audio device: %s\n", SDL_GetError());
        }
    }

    {
        // THE SCREEN'S OWN RATE, read for every game, so the console does
        // the right thing on whatever television it is plugged into. Off
        // by a little (gamescope says 59.980 for a 60.000 TV) is fine: the
        // rate control below takes up the difference.
        //
        // 5% IS RETROARCH'S LIMIT (video_max_timing_skew, default 0.05):
        // the furthest a game's sound may be sped up or slowed to lock it.
        // Further than that and it is paced by the clock instead, with a
        // repeated frame now and then, which is what RetroArch does too.
        static constexpr double kMaxSkew = 0.05;
        const SDL_DisplayMode* dm =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        const double hz = dm ? dm->refresh_rate : 0.0;
        paceScreenHz = hz;
        paceRatio = 1.0f;
        const double fps = core.avInfo().fps;
        paceEvery = 0;
        paceBaseRatio = 1.0;
        paceCount = 0;
        pace = PaceTally{};
        // Headless (offscreen) there is no refresh to lock to: nothing
        // waits for one, so a frame per loop would run the game flat out.
        const char* vd = SDL_GetCurrentVideoDriver();
        const bool realScreen =
            vd && std::strcmp(vd, "offscreen") != 0 && std::strcmp(vd, "dummy") != 0;
        // CABINETOS_PACE=clock: the old pacing, for comparing on the TV.
        const char* paceEnv = std::getenv("CABINETOS_PACE");
        const bool clockAsked = paceEnv && std::strcmp(paceEnv, "clock") == 0;
        if (realScreen && !clockAsked && !core.isPs2() && hz >= 20.0 && fps > 0.0) {
            const int k = std::max(1, static_cast<int>(std::lround(hz / fps)));
            const double ratio = hz / k / fps;
            if (std::fabs(ratio - 1.0) <= kMaxSkew) {
                paceEvery = k;
                paceBaseRatio = ratio;
            }
        }
        if (audioStream) SDL_SetAudioStreamFrequencyRatio(audioStream, 1.0f);
        if (paceEvery > 0)
            std::fprintf(stderr,
                         "[pace] screen %.3f Hz, game %.4f fps: locked, one game frame "
                         "every %d refresh%s, sound %+.2f%%\n",
                         hz, fps, paceEvery, paceEvery == 1 ? "" : "es",
                         (paceBaseRatio - 1.0) * 100.0);
        else
            std::fprintf(stderr, "[pace] screen %.3f Hz, game %.4f fps: paced by the "
                         "clock\n", hz, fps);
    }
}

// Stage 12: running. playing is set here, then RetroAchievements and the
// play clock begin.
void App::startPlaying(cab::Core& core) {
    // PCSX2 is not a libretro core and sets no core name; the line used to
    // print the previous core's ("running Beetle Saturn" for a PS2 game, #412).
    std::fprintf(stderr, "[launch] running %s\n",
                 core.isPs2() ? "PCSX2" : core.coreName().c_str());
    playing = true;
    // RetroAchievements, when this person signed in (achievements.h):
    // after the battery save and before the first frame it checks.
    ra::beginGame(session.raHash, session.platformSlug, core.isPs2());
    if (session.romId > 0) {
        playClock.begin(session.romId, playtime::wallMs());
        playCheckpointClock = 0.0f;
    }
}

// Reset the moment it stops, so the next launch starts its own clock
// and a second press cannot inherit the first one's patience.
void App::tickLaunchBusy(float dt) {
    launchJob.busyFor = launchJob.busy() ? launchJob.busyFor + dt : 0.0f;
}
