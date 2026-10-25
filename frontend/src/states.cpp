#include "states.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "core.h"
#include "disc.h"
#include "discs.h"
#include "fileio.h"
#include "app.h"
#include "imagecodec.h"
#include "proc.h"
#include "screenparts.h"
#include "statefile.h"
#include "storage.h"

// A SAVE STATE GOES IN ON ITS OWN DISC (#162 item 9). The disc is in the
// state's name ("(Disc 2)"), so a state from any device carries it; it is
// put in before the state, because only PS1's core restores it by itself.
//
// A CORE WITH NO DRIVE (3DO) CANNOT TAKE ANOTHER DISC'S STATE: the state is
// the machine without its disc, and nothing can put that disc in, so it
// would run on in the wrong disc's data. It is refused and the notice names
// its disc; Insert and restart on that disc, then Load latest state.
static int gStateOnDisc = 0;   // set when a state was refused for its disc
bool loadStateOnItsDisc(GameSession& sess, const std::string& name,
                        const std::vector<uint8_t>& bytes) {
    gStateOnDisc = 0;
    if (!sess.discs.empty() && sess.discChange == disc::Change::Insert) {
        const int want = disc::discInStateName(name);
        if (want > 0 && want != sess.disc) discSwapNow(sess, want, "for the state");
    }
    if (!sess.discs.empty() && sess.discChange == disc::Change::StartOver) {
        const int want = disc::discInStateName(name);
        if (want > 0 && want != sess.disc) {
            std::fprintf(stderr, "[disc] the state is on disc %d, disc %d is in: not loaded\n",
                         want, sess.disc);
            gStateOnDisc = want;
            return false;
        }
    }
    // THE CORE'S BYTES, AND THE ACHIEVEMENT PROGRESS BESIDE THEM when the
    // state carries it (#279, statefile.h): RetroArch's blocks, or core
    // data alone, as every state before #279 is.
    statefile::Parts parts;
    if (!statefile::split(bytes, &parts)) return false;
    cab::Core& core = cab::Core::shared();
    const bool ok = parts.wrapped
                        ? core.loadState(std::vector<uint8_t>(parts.core, parts.core + parts.coreSize))
                        : core.loadState(bytes);
    if (ok && !sess.discs.empty()) syncDiscFromCore(sess);
    // The memory jumped, and what each achievement was waiting for with it.
    if (ok) ra::stateLoaded(parts.progress, parts.progressSize);
    return ok;
}

std::string stateRefusedText() {
    return gStateOnDisc > 0 ? "That state is on Disc " + std::to_string(gStateOnDisc)
                            : std::string("Couldn't load that state");
}

void saveStateNow(GameSession& sess, Uploader& up, MenuNotice& notice) {
    cab::Core& core = cab::Core::shared();
    if (sess.discInserting) {
        notice.say("Not while the disc is changing", Tone::Info);
        return;
    }
    // A REFUSAL IS A FAILURE, NOT A SYSTEM WITHOUT STATES (#222). A core
    // with no states at all says a size of 0 (Game & Watch, VeMUlator), and
    // that is still "No save states". One that has them and would not make
    // this one (Flycast's "Serializer overflow", seen once off-screen)
    // failed, and says so.
    std::vector<uint8_t> st;
    if (!core.saveState(st) || st.empty()) {
        if (core.stateSize() == 0) {
            std::fprintf(stderr, "[state] this core cannot serialize\n");
            notice.say("No save states for this system", Tone::Info);
        } else {
            std::fprintf(stderr, "[state] the core would not make this state\n");
            notice.say("Couldn't save the state", Tone::Problem);
        }
        return;
    }
    // WITH THE ACHIEVEMENT PROGRESS (#279), as RetroArch's states carry it,
    // so a state resumed here or on another console goes on counting where
    // it stopped. None to carry, and the core's bytes go alone, as before.
    st = statefile::join(st, ra::progress());
    // Named with a timestamp because states accumulate on purpose; a save
    // overwrites, a state does not.
    //
    // CABINET'S NAME, TO THE CHARACTER (#99, MMagTech 2026-09-26): the game's
    // file name without its extension, then the UTC time to the millisecond
    // with its separators dashed, "Super Mario World (USA) [2026-09-26
    // 17-45-12-345]" (TVPlayerView.swift, stateFileStem). It was the title
    // and local time to the second, so a list of states read differently
    // depending on where each was made. Nothing here reads the name back:
    // the newest local state is found by date.
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm utc{};
    gmtime_r(&ts.tv_sec, &utc);
    char stamp[48];
    const size_t n = std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &utc);
    std::snprintf(stamp + n, sizeof stamp - n, "-%03ld", ts.tv_nsec / 1000000L);
    // RomM's own rewrite of the name, so the state on the drive and on RomM
    // have one name, and rotation finds what it sent (#484, ROMM-02).
    std::string base =
        rommKeptStem(sess.fsStem.empty() ? sanitisedStem(sess.title) : sess.fsStem);
    // AND ITS DISC, on a game with several (#162): "Panzer Dragoon (Disc 2)
    // [...]". It travels with the state, so loading it puts that disc in.
    if (!sess.discs.empty()) base += disc::stateNamePart(sess.disc);
    const std::string stem = base + " [" + stamp + "]";
    const std::string name = stem + ".state";

    storage::makeDirs(sess.stateDir);
    const std::string path = sess.stateDir + "/" + name;
    if (!cab::writeBytes(path, st)) {
        std::fprintf(stderr, "[state] could not write locally, not uploading\n");
        notice.say("Couldn't save the state", Tone::Problem);
        return;
    }
    std::fprintf(stderr, "[state] %zu bytes saved locally\n", st.size());

    // THE PICTURE (#99): the game's own frame, the menu not in it, upright,
    // at the core's resolution, as Cabinet sent. Kept beside the state, so a
    // state that has to wait for the server still has it when it goes. A
    // state without one is still a state: nothing here stops the save.
    std::vector<uint8_t> png;
    std::string shotName, shotPath;
    {
        std::vector<uint8_t> rgba;
        unsigned w = 0, h = 0;
        if (core.snapshot(rgba, w, h) && ui::encodePNG(rgba, w, h, png)) {
            shotName = stem + ".png";
            shotPath = sess.stateDir + "/" + shotName;
            if (!cab::writeBytes(shotPath, png)) shotPath.clear();
            std::fprintf(stderr, "[state] picture %ux%u, %zu bytes\n", w, h, png.size());
        } else {
            png.clear();
            std::fprintf(stderr, "[state] no picture for this state\n");
        }
    }

    if (sess.stateTag.empty()) {
        std::fprintf(stderr, "[state] no settled tag for this core — not uploaded\n");
        // SAVED, and honest about the half that did not happen. A state this
        // console cannot tag is one no other device will be offered, which is
        // worth knowing before somebody relies on it being there.
        notice.say("Saved on this console only", Tone::Info);
        return;
    }
    // NOT "saved" YET. The bytes are on the disk, which is the guarantee that
    // matters, but the sentence a person reads should not claim the server has
    // it before the server has it. The frame loop finishes this sentence when
    // the uploader reports back — see Uploader::stateOutcome.
    notice.say("Saving\xE2\x80\xA6", Tone::Busy);
    Uploader::Job job{sess.romId, sess.stateTag, name, std::move(st), true};
    job.localPath = path;
    job.shotName = shotName;
    job.shot = std::move(png);
    job.shotPath = shotPath;
    up.push(std::move(job));
}

// A SCREENSHOT (#79): the shortcut button and Y. The game's own frame, as a
// state's picture is (the menu is never in it), saved on this console first
// and then sent to this person's gallery on RomM, retried like a save when
// RomM is away. Named as a state is, the game's file name and the UTC time,
// so two never collide. No gallery on the console: they are seen and
// deleted in RomM (decided 2026-09-27).
void screenshotNow(GameSession& sess, Uploader& up, MenuNotice& notice) {
    cab::Core& core = cab::Core::shared();
    std::vector<uint8_t> rgba, png;
    unsigned w = 0, h = 0;
    if (!core.snapshot(rgba, w, h) || !ui::encodePNG(rgba, w, h, png)) {
        std::fprintf(stderr, "[screenshot] no picture to take\n");
        notice.say("No screenshots for this system", Tone::Info);
        return;
    }
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm utc{};
    gmtime_r(&ts.tv_sec, &utc);
    char stamp[48];
    const size_t n = std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &utc);
    std::snprintf(stamp + n, sizeof stamp - n, "-%03ld", ts.tv_nsec / 1000000L);
    // RomM's own rewrite of the name, so the state on the drive and on RomM
    // have one name, and rotation finds what it sent (#484, ROMM-02).
    std::string base =
        rommKeptStem(sess.fsStem.empty() ? sanitisedStem(sess.title) : sess.fsStem);
    const std::string name = base + " [" + stamp + "].png";
    const std::string dir =
        storage::screenshotsDir(storage::currentUser()) + "/" + std::to_string(sess.romId);
    storage::makeDirs(dir);
    const std::string path = dir + "/" + name;
    if (!cab::writeBytes(path, png)) {
        notice.say("Couldn't save the screenshot", Tone::Problem);
        return;
    }
    std::fprintf(stderr, "[screenshot] %ux%u, %zu bytes, %s\n", w, h, png.size(), name.c_str());
    notice.say("Screenshot saved", Tone::Done);
    Uploader::Job job;
    job.romId = sess.romId;
    job.emulator = "screenshot";
    job.fileName = name;
    job.data = std::move(png);
    job.localPath = path;
    job.isScreenshot = true;
    up.push(std::move(job));
}

static std::string newestLocalState(const GameSession& sess);

// `wantId`: a particular state, picked on the launch screen, rather than the
// newest. Everything else is the same, the fallback included.
void beginLoadLatestState(StateLoad& load, GameSession& sess,
                          romm::Client& client, MenuNotice& notice, int wantId,
                          bool quiet) {
    if (load.running.load()) return;
    if (sess.discInserting) {
        if (!quiet) notice.say("Not while the disc is changing", Tone::Info);
        return;
    }

    load.quiet = quiet;
    if (!quiet) notice.say("Loading\xE2\x80\xA6", Tone::Busy);
    if (sess.stateTag.empty()) {
        // THIS SYSTEM'S STATES NEVER LEAVE THE CONSOLE, so the newest one here
        // IS the latest. Until 2026-09-23 this refused outright — so on NES,
        // SNES, N64 and six others Save state wrote a state that Load latest
        // state would then refuse to find.
        const std::string local = newestLocalState(sess);
        const std::vector<uint8_t> bytes =
            local.empty() ? std::vector<uint8_t>{} : cab::readBytes(local);
        if (bytes.empty()) {
            std::fprintf(stderr, "[state] no settled tag and nothing saved here\n");
            if (!quiet) notice.say("No saved state for this game", Tone::Info);
            return;
        }
        const bool ok = loadStateOnItsDisc(sess, local, bytes);
        std::fprintf(stderr, "[state] local-only %s -> %s\n", local.c_str(),
                     ok ? "restored" : "REFUSED");
        notice.say(ok ? "State loaded" : stateRefusedText(), ok ? Tone::Done : Tone::Problem);
        load.loaded = ok;
        return;
    }
    if (load.worker.joinable()) load.worker.join();
    load.running = true;
    load.ready = false;
    load.serverFailed = false;
    const int romId = sess.romId;
    const std::string tag = sess.stateTag;
    load.worker = std::thread([&load, &client, romId, tag, wantId]() {
        std::vector<romm::Asset> states;
        std::string err;
        if (!client.fetchStates(romId, &states, &err)) {
            // The server could not be reached or would not answer, which is
            // Cabinet's offline case by another name. The newest state on this
            // machine is what is left, and it is better than a refusal.
            load.note = err;
            load.serverFailed = true;
            load.tryLocal = true;
            load.ready = true;
            load.running = false;
            return;
        }
        const romm::Asset* best = nullptr;
        int skipped = 0;
        for (const auto& a : states) {
            // A state from another emulator is SKIPPED, never attempted.
            // Loading one does not fail cleanly: it boots something that looks
            // like the game and is not.
            if (a.emulator != tag) { ++skipped; continue; }
            if (wantId) {
                if (a.id == wantId) best = &a;
                continue;
            }
            if (!best || a.updatedAt > best->updatedAt) best = &a;
        }
        if (!best) {
            load.note = "none for " + tag + " (" + std::to_string(skipped) +
                        " for other emulators)";
            load.tryLocal = true;
        } else {
            load.data = client.fetchAsset("states", best->id);
            load.note = best->fileName;
        }
        // READY BEFORE NOT RUNNING (#484, PLAY-16): loadPendingStates ends a
        // launch-screen wait when both read false, so in the other order a
        // frame between the two stores ran the game one frame before the
        // state was applied.
        load.ready = true;
        load.running = false;
    });
}

// The newest .state file this console wrote for this game, or empty.
//
// THE FALLBACK, NOT THE FIRST CHOICE — settled against the reference 2026-09-21.
//
// MMagTech: *"i just did save on a couple of games and it looked like nothing
// happened even tried loading after saving."* Two faults with one symptom.
// Nothing LOOKED like it happened because neither button said anything — see
// MenuNotice. And loading after saving genuinely did nothing, because the
// upload queued moments earlier had not landed and the server was asked.
//
// The first fix here was to read this file FIRST. That was wrong, and reading
// Cabinet settled it — `TVPlayerView.loadLatestState`, whose own comment is
// *"offline falls back to the newest local state, online the server stays the
// source of truth"*. It is right, and the reason is that states live on RomM so
// that "latest" can mean latest across every device a person owns. A console
// that preferred its own copy would quietly stop being one of those devices.
//
// Cabinet got away with it because its Save AWAITED the upload and only then
// said "Saved to RomM.", so by the time you could press Load the server had it.
// This console queues on a worker instead — right for a machine that must not
// stall its frame loop — so the race was real here and not there. It is closed
// where it belongs, in Save's own reporting, rather than by changing what
// "latest" means.
static std::string newestLocalState(const GameSession& sess) {
    DIR* d = ::opendir(sess.stateDir.c_str());
    if (!d) return {};
    std::string best;
    time_t bestAt = 0;
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < 7 || name.compare(name.size() - 6, 6, ".state") != 0) continue;
        const std::string full = sess.stateDir + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (best.empty() || st.st_mtime > bestAt) { best = full; bestAt = st.st_mtime; }
    }
    ::closedir(d);
    return best;
}

// Called once per frame. Applying the state is the only part that touches the
// core, so it happens here and nowhere else.
void pumpStateLoad(StateLoad& load, GameSession& sess,
                   MenuNotice& notice) {
    if (!load.ready.load()) return;
    load.ready = false;
    if (load.worker.joinable()) load.worker.join();
    if (load.data.empty()) {
        std::fprintf(stderr, "[state] %s\n", load.note.c_str());
        // THE FALLBACK. The server had nothing for this core or could not be
        // asked; this machine's own newest state is what is left. Read here
        // rather than on the worker because loading one touches the core.
        if (load.tryLocal) {
            load.tryLocal = false;
            if (const std::string local = newestLocalState(sess); !local.empty()) {
                const std::vector<uint8_t> bytes = cab::readBytes(local);
                if (!bytes.empty()) {
                    const bool ok = loadStateOnItsDisc(sess, local, bytes);
                    std::fprintf(stderr, "[state] fell back to local %s -> %s\n",
                                 local.c_str(), ok ? "restored" : "REFUSED");
                    notice.say(ok ? "State loaded" : stateRefusedText(),
                               ok ? Tone::Done : Tone::Problem);
                    load.loaded = ok;
                    return;
                }
            }
        }
        // NOT `note` itself, which is a line for the journal ("none for
        // gambatte-native (3 for other emulators)", or a server error) and was
        // shown on the television raw until 2026-09-23. It stays in the log
        // above; the person gets the sentence that means the same thing.
        if (!load.quiet)
            notice.say(load.serverFailed ? "Couldn't reach RomM" : "No saved state for this game",
                       load.serverFailed ? Tone::Problem : Tone::Info);
        return;
    }
    const bool ok = loadStateOnItsDisc(sess, load.note, load.data);
    std::fprintf(stderr, "[state] %s (%zu bytes) -> %s\n", load.note.c_str(),
                 load.data.size(), ok ? "restored" : "REFUSED");
    // A REFUSED state is the one that matters most. It means the bytes were
    // found and the core would not take them, which is a different problem from
    // there being none — and silently carrying on with the game running from
    // where it was is indistinguishable from nothing having happened.
    notice.say(ok ? "State loaded" : stateRefusedText(), ok ? Tone::Done : Tone::Problem);
    load.loaded = ok;
    load.data.clear();
}

// ---- Play from a state, on a game's page ---------------------------------
// A member of the App (app.h), moved out of apply in batch (e).

void App::playFromState(const screens::Result& res) {
    if (refuseRemoteStart(detailScreen.game().romId)) return;
    sound::play(sound::Cue::Activate);
    std::fprintf(stderr, "[detail] play from state %d\n", res.value);
    // Negative: one of this console's own states, by its file.
    const bool local = res.value < 0 &&
                       static_cast<size_t>(-res.value) <= detailLocalStates.size();
    // SET ONLY WHEN THE PRESS COUNTS, and then in place of what an earlier
    // press of the same launch asked for (#484). A press refused because
    // another game is downloading cleared that game's picked state, and two
    // picks on a joined download loaded both, one over the other.
    if (!launchById(detailScreen.game().romId)) return;
    forgetLaunchAsks();
    if (local) {
        pendingStatePath = detailLocalStates[static_cast<size_t>(-res.value) - 1];
        // ON THE STATE'S OWN DISC (#162): no swap to wait on,
        // and the only way in for a core with no drive (3DO).
        gNextStartDisc = disc::discInStateName(pendingStatePath);
    } else {
        pendingStateId = res.value;
    }
}

// ---- The states in play (#398) --------------------------------------------
// A standalone emulator's screenshot, the states picked before a game, the
// hold after a load and the upload's answer. Members of the App (app.h),
// moved out of main.cpp in batch (f).

// A SCREENSHOT OF A GAME THIS WINDOW DID NOT DRAW: gamescope's own, of the
// game's plane only, which leaves the menu out (open question 24, and
// measured: about two seconds for a 4K PNG, written by gamescope after
// the command returns). The menu says so once the file is whole, and it
// travels to RomM's gallery the way every other screenshot does.
void App::standaloneScreenshot() {
    if (!standaloneShot.path.empty()) return;   // one at a time
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm utc{};
    gmtime_r(&ts.tv_sec, &utc);
    char stamp[48];
    const size_t n = std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &utc);
    std::snprintf(stamp + n, sizeof stamp - n, "-%03ld", ts.tv_nsec / 1000000L);
    std::string base =
        rommKeptStem(session.fsStem.empty() ? sanitisedStem(session.title) : session.fsStem);
    const std::string dir = storage::screenshotsDir(storage::currentUser()) + "/" +
                            std::to_string(session.romId);
    storage::makeDirs(dir);
    standaloneShot.name = base + " [" + stamp + "].png";
    standaloneShot.path = dir + "/" + standaloneShot.name;
    standaloneShot.romId = session.romId;
    standaloneShot.size = -1;
    standaloneShot.askedMs = standaloneShot.checkedMs = SDL_GetTicks();
    const proc::Result r = proc::run({"gamescopectl", "screenshot", standaloneShot.path}, 5);
    if (!r.ok()) {
        std::fprintf(stderr, "[screenshot] gamescopectl: %s\n", proc::trimmed(r.err).c_str());
        standaloneShot.path.clear();
        menuNotice.say("Couldn't save the screenshot", Tone::Problem);
    }
}

// Twice a second: the file is whole when its size stops changing. A file
// gamescope never writes is given up on after ten seconds (#484, PLAY-17),
// or every later Screenshot would wait behind it for the life of the process.
void App::pumpStandaloneShot() {
    if (standaloneShot.path.empty()) return;
    const uint64_t now = SDL_GetTicks();
    if (now - standaloneShot.checkedMs < 500) return;
    standaloneShot.checkedMs = now;
    struct stat st;
    if (::stat(standaloneShot.path.c_str(), &st) != 0 || st.st_size <= 0) {
        if (now - standaloneShot.askedMs < 10000) return;
        std::fprintf(stderr, "[screenshot] gamescope wrote nothing in 10 s: %s\n",
                     standaloneShot.name.c_str());
        standaloneShot.path.clear();
        menuNotice.say("Couldn't save the screenshot", Tone::Problem);
        return;
    }
    if (st.st_size != standaloneShot.size) {
        standaloneShot.size = st.st_size;
        return;
    }
    std::vector<uint8_t> png = cab::readBytes(standaloneShot.path);
    std::fprintf(stderr, "[screenshot] %zu bytes, %s\n", png.size(),
                 standaloneShot.name.c_str());
    menuNotice.say("Screenshot saved", Tone::Done);
    Uploader::Job job;
    job.romId = standaloneShot.romId;
    job.emulator = "screenshot";
    job.fileName = standaloneShot.name;
    job.data = std::move(png);
    job.localPath = standaloneShot.path;
    job.isScreenshot = true;
    uploader.push(std::move(job));
    standaloneShot.path.clear();
}

// A state picked before the game started, loaded once the core runs, and
// the hold after any load (the frame loop calls this every frame).
void App::loadPendingStates() {
    // THE STATE PICKED ON THE LAUNCH SCREEN goes in once the core is
    // running, the same way Load latest state does, by id.
    if (pendingStateId && playing && cab::Core::shared().running() &&
        cab::Core::shared().framesRun() >= 1 && !stateLoad.running.load()) {
        beginLoadLatestState(stateLoad, session, liveClient, menuNotice, pendingStateId);
        pendingStateId = 0;
        if (stateLoad.running.load()) stateHoldWaiting = true;
    }
    // ONE OF THIS CONSOLE'S OWN STATES, picked on the game's page (#88):
    // read off the disk, so there is nothing to wait for.
    if (!pendingStatePath.empty() && playing && cab::Core::shared().running() &&
        cab::Core::shared().framesRun() >= 1 && !stateLoad.running.load()) {
        const std::vector<uint8_t> bytes = cab::readBytes(pendingStatePath);
        const bool ok = !bytes.empty() && loadStateOnItsDisc(session, pendingStatePath, bytes);
        std::fprintf(stderr, "[state] picked on the game's page: %s -> %s\n",
                     pendingStatePath.c_str(), ok ? "restored" : "REFUSED");
        menuNotice.say(ok ? "State loaded" : stateRefusedText(),
                       ok ? Tone::Done : Tone::Problem);
        stateLoad.loaded = ok;   // and it waits on that frame, as any load does
        pendingStatePath.clear();
    }
    if (pendingResume && playing && cab::Core::shared().running() &&
        cab::Core::shared().framesRun() >= 1 && !stateLoad.running.load()) {
        pendingResume = false;
        if (session.snapshots) {
            std::fprintf(stderr, "[resume] loading the newest state, if there is one\n");
            beginLoadLatestState(stateLoad, session, liveClient, menuNotice, 0, true);
            if (stateLoad.running.load()) stateHoldWaiting = true;
        }
    }
    if (stateLoad.loaded) {
        stateLoad.loaded = false;
        rewindKeep.reset();   // the history led somewhere else
        rewindLastFrame = cab::Core::shared().framesRun();
        closeOverlay();
        stateHold = true;
        stateHoldWaiting = false;
    }
    // A launch-screen state that did not load: the game just goes on.
    if (stateHoldWaiting && !stateLoad.running.load() && !stateLoad.ready.load())
        stateHoldWaiting = false;
}

// THE WAIT AFTER A STATE LOADS: the game's frozen picture, a little
// darker, and the prompt low on it. Not while the pause menu is up.
void App::drawStateHold(float sc) {
    if (playing && stateHold && overlayFade.value() < 0.001f) {
        renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                               ui::Color::black(0.35f)});
        drawPressPrompt(renderer, text, sc, "Press", "to continue",
                        ui::kCanvasHeight * 0.82f, 1.0f);
    }
}

// What became of the last state saved, said once the uploader knows.
void App::sayStateUploaded() {
    // The uploader's outcome (uploader.cpp): 1 on RomM, 2 RomM away and the
    // state waiting for it, 3 RomM there and refusing it (#477), still here.
    if (const int outcome = uploader.stateOutcome.exchange(0); outcome != 0)
        menuNotice.say(outcome == 1   ? "Saved to RomM"
                       : outcome == 2 ? "Saved. Will upload when RomM is back"
                                      : "Saved on this console. RomM refused it",
                       outcome == 1 ? Tone::Done : outcome == 2 ? Tone::Info : Tone::Problem);
}
