// A launch: the job a Play or a Download press starts, and the call that
// starts it. beginLaunch (launch.cpp) does the frame thread's half and starts
// a worker that fetches the game (launch_routes.cpp); the App owns the one
// LaunchJob (app.h) and reads it every frame. Moved out of main.cpp in the
// restructure (#398).

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "disc.h"
#include "romm.h"
#include "saves.h"
#include "standalone.h"
#include "storage.h"

// A launch in progress, off the frame thread.
//
// Downloading on the thread that draws is what the old planLaunch did, and it
// was fine only because Tetris is 19 KB. Press Enter on the reference library's
// 1.78 GB arcade set and the console freezes solid — no animation, nothing to
// look at, no way to cancel — for as long as a few gigabytes takes. That is
// worse than the memory cost, because it is the part a person experiences.
//
// So a worker does it, the frame loop reads a snapshot every frame, and the
// bytes go straight to disk. The client was deliberately built synchronous so
// that callers could do exactly this, the same way ImageCache already does.
struct LaunchJob {
    enum class Stage { Idle, Firmware, Downloading, Unpacking, Installing, Ready, Failed };

    std::atomic<Stage> stage{Stage::Idle};
    // How long this job has been doing something a person would wait for, in
    // seconds. Advanced by the frame loop rather than by the worker: it is a
    // fact about what has been on the screen, not about the work.
    float busyFor = 0.0f;
    std::atomic<int64_t> got{0};
    std::atomic<int64_t> total{0};
    std::atomic<bool> cancel{false};

    // The same fetch serves both things a person can ask for, because they are
    // the same fetch. Pressing Play on a game that is not here downloads it and
    // starts it; choosing Download puts it here and keeps it. The only
    // differences are what happens at the end, so they are two flags rather
    // than two code paths that would drift.
    bool playWhenReady = true;
    bool keepWhenReady = false;
    // WHAT A FAILED LAUNCH SAYS, in words (#146, #90). The worker notes
    // whether a system that cannot start without its BIOS got none from the
    // server; if the game then fails, that is what the page says, "No Saturn
    // BIOS on the server", rather than the core's own wording. It is never
    // a reason to refuse: the core is still asked, and may manage.
    std::atomic<bool> biosMissing{false};
    std::string systemName;
    // WHERE THE PERSON WAS WHEN THEY PRESSED PLAY, as an integer the launch
    // machinery does not have to understand. See `startedOn` at the press site
    // and the ready branch in pumpLaunch: a download that finishes while
    // somebody has moved on somewhere else must not drag them out of it.
    int startedOn = -1;
    // Whether the ready branch has started bringing the curtain down for this
    // job, so that a launch abandoned part way can put it back up without
    // touching a curtain somebody else lowered.
    bool loweredCurtain = false;

    // Written by the worker before it sets Ready or Failed, read by the frame
    // thread only after it observes one of those. The atomic stage is the
    // handover.
    std::string romPath;
    // A GAME ON SEVERAL DISCS (#162): its discs in order, and `romPath` is
    // the console's list in the game's folder. Empty for one disc.
    std::vector<disc::Disc> discs;
    std::string coreName;
    // Carried through so the session can be built when the game loads: which
    // game it is, which platform it belongs to, and where its files are.
    int romId = 0;
    // WHERE THE GAME LIVES ON DISK, which is `<location>/roms/<platform>/…`
    // when somebody keeps it and `<location>/cache/<platform>/…` when nobody
    // does. One entry, named `<romId> - <title>` — a file when the game is a
    // single payload and a directory when its archive unpacked into several.
    // The worker may rename it (a folder collapsed to one file, a file moved
    // into a folder), so it is read only after the stage is Ready or Failed.
    std::string entryPath;
    // THE DRIVE THE JOB WRITES TO, the location `entryPath` is under. Set by
    // beginLaunch before the worker starts and never written by the worker,
    // so the frame thread may read it while the job is busy: Eject asks it
    // whether the download must stop first (#404).
    std::string location;
    // ONE SPELLING OF A PLATFORM, EVERYWHERE. RomM's `fs_slug` — "Sony
    // Playstation", not "psx". See storage.h for why the short one lost.
    std::string platformFsSlug;
    // AND THE SHORT ONE AS WELL, for the one question that has to be asked of
    // the platform rather than the core: which file, if any, the core writes
    // its save into. `fs_slug` is what a server's owner can rename; `slug` is
    // RomM's own and is what catalog is keyed on. Both are carried because
    // each answers a different question, and the arcade rows need both.
    std::string platformSlug;
    // RomM's `title_id`, or the code this console read off the file when RomM
    // had none: Wii asks it which controller each player's port holds (wii.h).
    std::string titleId;
    // RomM's platform id: a Wii launch looks for the Mii Channel among its
    // games (copyMiisInto).
    int platformId = 0;
    // RomM's `ra_hash`, for RetroAchievements (achievements.h).
    std::string raHash;
    // RomM's `md5_hash`, for the Vectrex sheets (vectrexsheets.h).
    std::string md5;
    // The server's own file name for this game, with its extension removed —
    // `Ikaruga (Japan)`, `lethalen`. It is what a save's row on RomM is named
    // after; see saveRowName.
    std::string fsStem;
    std::string corePath;
    // AN ARCADE GAME (catalog::Coverage::arcade): its emulator was named at
    // the press by its zip's name, and the worker asks again of its files
    // (arcadematch.h). `arcadePicked` once it has; `coreChanged` when the
    // files named another emulator, `coreName` and `corePath` then being
    // that one's, which the frame thread loads behind the curtain
    // (pumpLaunch). Written by the worker before Ready, as `romPath` is.
    bool arcade = false;
    bool arcadePicked = false;
    bool coreChanged = false;
    std::string title;
    std::string message;
    // THE GAME'S SAVES AS RomM HOLDS THEM, fetched by the worker once the game
    // is on the disk (#481, LAUNCH-17; saves.h, SaveFetch), and for a Wii game
    // the Mii Channel's. The stages behind the curtain restore from these and
    // ask the server nothing. Released once the game is up.
    SaveFetch saves;
    SaveFetch miiSaves;

    std::thread worker;

    ~LaunchJob() { stop(); }
    void stop() {
        cancel = true;
        if (worker.joinable()) worker.join();
    }
    bool busy() const {
        const Stage st = stage.load();
        return st == Stage::Firmware || st == Stage::Downloading || st == Stage::Unpacking ||
               st == Stage::Installing;
    }
    // Finished, and not yet taken by the frame loop (takeFinishedJob): a
    // Ready game waits there while the curtain comes down. No new launch
    // replaces it (#472).
    bool waiting() const {
        const Stage st = stage.load();
        return st == Stage::Ready || st == Stage::Failed;
    }
};

// Starts one; the comment above it in launch.cpp says what it decides. The
// default arguments are here, on the declaration only.
bool beginLaunch(LaunchJob& job, romm::Client& client, const romm::Game& game,
                 const std::string& coreDir, std::string* err,
                 bool playWhenReady = true, bool keepWhenReady = false);

// --ps2-upscale and --ps2-aniso, from the command line (cli.cpp). The two values
// live in launch.cpp, the only place that reads them; see the comment there.
void setPs2Upscale(float upscale);
void setPs2Anisotropy(int anisotropy);
// --ps2-copy, defined beside the PS2 window (ps2window.cpp), which reads it,
// and set by the command line (cli.cpp).
extern bool gPs2Copy;

// beginLaunch's worker, in launch_routes.cpp, run on job.worker and called by
// nothing else. Its parameters are the thread's captures: job and client by
// reference, the other thirteen copies (`user`, the person pressing Play, is
// copied because the worker must not read the current one).
void launchWorker(LaunchJob& job, romm::Client& client, int id, int platformId,
                  std::string slug, std::string fsSlug, std::string fsName,
                  const cab::standalone::Emulator* emu, std::string entryPath,
                  std::string location, std::string validExts, bool blockExtract,
                  int64_t expectedSize, bool multiFile, storage::User user);
