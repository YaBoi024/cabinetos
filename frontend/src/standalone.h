// Emulators that are whole applications, played as games.
//
// WHAT THIS IS FOR. Switch (Eden), PS3 (RPCS3), Xbox (xemu), Xbox 360 (Xenia
// Edge), Wii U (Cemu) and PlayStation Vita (Vita3K) are not libretro cores and
// cannot be loaded into this process the way the other systems are (PS2
// included). Eden and xemu come from Flathub, pinned in flatpaks.list; RPCS3
// is the RPCS3 team's own build, in the image (build_files/install-rpcs3.sh),
// because its Flatpak shows a warning box on every start; Xenia Edge and
// Vita3K are their developers' own builds, in the image too
// (build_files/install-xenia.sh, install-vita3k.sh), because neither is on
// Flathub.
// Each one opens its own window and draws its own picture.
// So the console starts the emulator as a separate program, hands it the
// television, and takes the screen back when it ends. docs/PROJECT.md, open
// questions 21, 24 and 32; issue #169.
//
// ONE MECHANISM, ONE ROW PER EMULATOR. Everything here is the same for all of
// them; what differs (where it comes from, the program's name, where its keys
// and settings go) is one row in the table in standalone.cpp and one `prepare`
// case. PS3 is a row, not a second copy of this file; what it needs that
// Switch did not (games that are installed, a close through the window) is a
// field on the row, so the next emulator that needs it gets it the same way.
//
// WHERE THE CODE IS. standalone.cpp is the mechanism and the table; each
// emulator's own setup is in its file (standalone_eden.cpp,
// standalone_rpcs3.cpp, standalone_xemu.cpp, standalone_xenia.cpp,
// standalone_cemu.cpp and standalone_vita3k.cpp), and what they share is
// standalone_internal.h. Since the restructure (#398).
//
// WHERE ITS STATE LIVES. Each emulator gets `emulators/<core>/` under the
// storage root and runs with that as its working directory. Eden treats a
// `user` folder in its working directory as its whole home (keys, system
// memory, saves, settings, shader cache), so nothing lands in the Flatpak's
// hidden default under ~/.var/app, and no source patch is needed. Batocera
// patched Citron's source to move its keys; this does not have to.
//
// MEASURED ON THE A9, 2026-09-28, before any of this was written: Eden 0.2.1
// from Flathub played Retro City Rampage DX with RomM's v19 keys and no
// firmware; gamescope gave it the screen on its own; SIGTERM to Eden closed
// it cleanly in 0.8 s with `confirmStop=2`.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "quality.h"

namespace cab::standalone {

struct Emulator {
    // The name the catalog uses for it, the way a libretro row names its core.
    const char* core;
    // WHERE IT COMES FROM: the Flatpak, or the program's path in the image.
    // Exactly one is set.
    const char* flatpak;
    const char* binary;
    // The program itself, by process name, which is the process that takes
    // signals: inside the sandbox for a Flatpak, where it is found by this
    // name (a program in the image is bwrap's one child, Run::findProgram).
    const char* program;
    // Its own log, and the two phrases it writes there when a game will not
    // load. That case is an error box on the television that nobody can
    // close, so the console reads the log and closes it instead. The log is
    // under its home, or under the game's own folder when `cacheInGame`;
    // nullptr when it keeps none and what it prints is the log (xemu).
    const char* log;
    const char* loadFailed[2];
    // And the ones among those failures that mean the keys on the console are
    // too old for this game: a newer prod.keys in RomM is the whole fix, so
    // the console says that rather than "Couldn't start".
    const char* keysTooOld[4];
    // A load that failed because the game's licence is not on the console
    // (a PS3 PKG without its `.rap`), so the console says that instead.
    const char* noLicence;
    // WHAT IT WRITES WHEN IT HAS STOPPED FOR GOOD MID-GAME and left its window
    // up with the picture held (RPCS3's "Emulation has been frozen!"). Watched
    // for the whole game, not only the first minute, and closed as a crash.
    const char* froze;
    // HOW TO TELL THE GAME HAS STOPPED while the emulator stays open on its
    // own window, which the console then closes as if the game had been
    // exited. Its window title starts with `titlePrefix`, and has at least
    // `titleParts` " | "-separated parts only while a game is running.
    //
    // NOT THE LOG. The first version read Eden's log for its "Force stopping
    // EmuThread" line, and it never arrived in time: Eden writes its log in
    // batches and flushes at once only for errors, so the line landed when
    // Eden exited. Shredder's Revenge sat on Eden's game list for minutes
    // with the log still ending at 12 s (2026-09-28).
    const char* titlePrefix;
    int titleParts;
    // ITS OWN ERROR BOX, by window title, for a game it could not open: the
    // ones that say so only in the box and not in the log. Cemu's "Unable to
    // launch game because the base files were not found" is a box titled
    // "Error" over its empty window (gui/wxgui/MainWindow.cpp, FileLoad).
    // Watched in the first minute, as the log is, and closed the same way.
    const char* errorBox;
    // The game files it opens as they are, libretro's `valid_extensions`
    // format. The console never unpacks them.
    const char* extensions;
    // What it cannot start a game without, in the words the console refuses
    // with: "No Switch keys on your server", "No PS3 firmware on your server".
    const char* needs;
    // GAMES THAT HAVE TO BE INSTALLED BEFORE THEY RUN (a PS3 PKG, a Vita
    // game's zip). The game is always a folder, fetched file by file into it,
    // and installGame runs before it is started. The folder is also where the emulator keeps what
    // it builds for that game (`cacheInGame`), so removing the game removes
    // both, and the cache never has to be told about either.
    bool installs;
    bool cacheInGame;
    // ASKED TO CLOSE THROUGH ITS WINDOW, as a person closing it would, rather
    // than by SIGTERM: RPCS3 has no handler for SIGTERM and simply dies, where
    // a close asks the game to stop first. Set for RPCS3, Xenia and Cemu; not
    // for Vita3K, whose SDL takes SIGTERM as its quit.
    bool closesByWindow;
    // BACK AND START TOGETHER OPEN THE EMULATOR'S OWN MENU (xemu: for pads
    // with no Guide button), which nothing can turn off. So the console holds
    // back whichever of the two is pressed second. MMagTech, 2026-09-29.
    bool blocksBackStart;
    // THE XBOX'S DASHBOARD MEANS THE GAME HAS ENDED. A game that quits goes
    // to the dashboard on the drive, inside the same xemu, and the console
    // must never show it (MMagTech, 2026-09-29). So the program the Xbox is
    // running is read once a second over qemu's control socket (QMP): the
    // title ID in its header, where xemu itself reads it (xemu-xbe.c). The
    // dashboard after a game closes xemu as a quit; the dashboard before any
    // game means the disc did not boot.
    bool watchesDashboard;
    // IT WRITES THE SAVES STRAIGHT INTO THE PERSON'S FOLDER FOR THE GAME
    // (Xenia's content root, Cemu's mlc, Eden's user/save, RPCS3's hdd0
    // savedata; the last two since #213), so a note there says a game is
    // running until its saves are zipped. One still there at the next start
    // of the console, or of that game, means the last game never got its
    // saves sent, and the whole folder goes then (beforeStart).
    bool notesPlaying;
    // ITS GAMES ARE TOUCHED THROUGH THE CONSOLE'S POINTER: two dots on the
    // sticks, shown and hidden by a long press of L3, touching through the
    // virtual touch screen (vtouch.h; App::pointerButton). The Vita, played
    // as a PS TV (docs/PROJECT.md question 49, decision 4).
    bool touchPointer;
};

// The emulator for a catalog core name, or nullptr for an ordinary core.
const Emulator* find(const std::string& core);
// Every one, for what is done for all that share a field (notesPlaying).
std::vector<const Emulator*> all();

// Whether this console has it. First boot installs a Flatpak from Flathub
// (cabinetos-flatpak-setup), so on a new machine, or one with no internet,
// the answer can be no for a while or for good. One in the image is there.
bool installed(const Emulator& e);

// Its whole home: `<storage root>/emulators/<core>`.
std::string home(const Emulator& e);

// --- Games that are installed (`installs`) ---------------------------------
//
// Whether `fileName` (as it is named on the disk), of `sizeBytes`, was already
// installed into the game at `entryPath` and deleted, so it is not fetched
// again.
bool installedFile(const std::string& entryPath, const std::string& fileName,
                   int64_t sizeBytes);
// What to start in a game's folder, or empty when there is nothing: the
// installed PS3 game's EBOOT.BIN, an installed Vita game's folder, or a disc
// image.
std::string bootPath(const std::string& entryPath);
// Runs on the download worker, after the files are in `entryPath`: whatever
// the console must install once (PS3 firmware, from `bios/`), then every
// package in the folder, each deleted once it is in. `*romPath` is what to
// start. False with `*message`, the sentence for the game's screen.
bool installGame(const Emulator& e, const std::string& entryPath,
                 const std::atomic<bool>& cancel, std::string* romPath,
                 std::string* message);

// Everything the emulator needs before a game starts: the folders, its keys
// out of `bios/` under the names it opens, and the settings the console
// decides for it (fullscreen, no prompts, docked). `entryPath` is the game's
// own file or folder on the disk.
//
// `saveDir` is this person's folder for this game (storage::savesDir), and
// the emulator writes the game's save inside it, so two people and two games
// never share one. `player` is the name a game shows for the person playing.
//
// False with `*missingKeys` when what it cannot run without (`needs`) is not
// on the console; false with an `*err` for anything else.
//
// `players` is how many virtual controllers (vpad.h) there are, and each
// player's controls are written for theirs, in the console's order.
//
// `level` is the picture quality this game runs at (quality.h), written into
// the emulator's settings with everything else.
bool prepare(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
             const std::string& player, int players, quality::Level level, bool* missingKeys,
             std::string* err);

// Where, inside `saveDir`, the game's own save folders are: the root of what
// travels to RomM as a zip, whose top-level folders are one per game. For
// Eden, `user/save/0000000000000000/<profile>`, each folder inside named by
// the game's title ID, which is why no title ID is ever asked for. For RPCS3,
// `hdd0/home/00000001/savedata`, whose folders are named by the game too.
std::string saveRoot(const Emulator& e, const std::string& saveDir);

// AROUND A GAME. `beforeStart` runs after this person's saves are unpacked
// into saveRoot, `afterEnd` after the emulator has gone and before they are
// zipped, `finished` once the zip is written and owed to the server.
//
// xemu: the saves are carried onto its hard drive image and off it again.
// While a game runs, the GAME's folder holds a note saying whose saves are on
// the drive, written once they are on it; eviction leaves such a folder alone
// (cache.cpp), and the next start of the console, or the next launch of that
// game, finishes it after a crash, a power cut or a copy-off that failed
// (App::recoverDriveSaves): this person's go up; another person's are copied
// into that person's folder with the note moved there, so they go up as them
// the way the next paragraph's do, and the game starts. Only saves the drive
// will not give up keep the game from starting.
//
// Xenia, Cemu, Eden and RPCS3 (`notesPlaying`): the note goes in the PERSON's folder for
// the game (`saveDir`), where the emulator writes the saves itself. A note
// still there at the next start or the next launch of that game means the
// last one never got its saves zipped, and the whole folder is sent then.
// Xenia's profile is written too, named `player` (x360profile.h).
//
// Nothing for the others.
bool beforeStart(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                 const std::string& player, const std::string& note, std::string* err);
bool afterEnd(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
              std::string* err);
void finished(const Emulator& e, const std::string& saveDir);
// The note in a folder (a game's for xemu, a person's for the others and for
// Xbox saves brought off the drive for them), or empty; one written there,
// and one taken away.
std::string playingNote(const std::string& folder);
bool hasPlayingNote(const std::string& folder);
bool writePlayingNote(const std::string& folder, const std::string& note);
void removePlayingNote(const std::string& folder);

// One running game.
class Run {
public:
    // Starts `romPath` in `e`, able to write `saveDir`. `entryPath` is the
    // game's own file or folder. False with a reason when it could not start.
    // `level` as for prepare: Xenia takes its settings on the command line.
    bool start(const Emulator& e, const std::string& romPath, const std::string& entryPath,
               const std::string& saveDir, quality::Level level, std::string* err);
    // Once a frame. True while the game is still going; false once it has
    // ended, after which `ended()` says how.
    bool poll();
    // Asks it to close the way its own window close would: SIGTERM to the
    // program, or the close itself (`closesByWindow`). `poll` forces it after
    // a deadline if it does not. A frozen
    // program is thawed first, or it never hears the request (measured: Eden
    // asked while frozen had to be forced after the full grace).
    void stop();
    // THE PAUSE MENU'S PAUSE (decision C): the whole program stops where it
    // is, sound and all, and carries on from the same instant when thawed.
    // Measured on the A9 2026-09-28: frozen for 10, 20 and 30 s, Eden's
    // picture held still and moved again the moment it was thawed.
    void freeze();
    void thaw();
    bool frozen() const { return frozen_; }

    bool active() const { return root_ > 0; }
    // What is running, or nullptr.
    const Emulator* emulator() const { return active() ? emu_ : nullptr; }
    bool stopping() const { return stopAtMs_ > 0; }

    enum class End { None, Asked, Quit, Crashed, CouldNotLoad, KeysTooOld, NoLicence };
    End ended() const { return ended_; }

private:
    void kill9();
    int findProgram() const;

    const Emulator* emu_ = nullptr;
    int root_ = -1;          // what we started: its sandbox (flatpak run, or bwrap)
    int program_ = -1;       // the emulator itself, once it is found
    int64_t startMs_ = 0;
    int64_t stopAtMs_ = 0;
    std::string logPath_;    // the emulator's own log, read for a failed load
    long logRead_ = 0;
    bool failedLoad_ = false;
    bool oldKeys_ = false;
    bool noLicence_ = false;
    bool froze_ = false;
    int64_t logCheckMs_ = 0;
    bool frozen_ = false;
    bool sawGameTitle_ = false;
    int64_t titleCheckMs_ = 0;
    int64_t errorCheckMs_ = 0;   // the last look for its error box (errorBox)
    // The Xbox's running program, over QMP (watchesDashboard).
    int qmpFd_ = -1;
    int64_t qmpCheckMs_ = 0;
    uint32_t xboxTitle_ = 0;   // the game's, once one has run
    uint32_t pollXboxTitle();
    // Run::start's and Run::poll's pieces for the Xbox (standalone_xemu.cpp,
    // as is pollXboxTitle).
    bool startXemu(const Emulator& e, const std::string& romPath, const std::string& dir,
                   const std::string& gameDir, const std::string& out, std::string* err);
    void watchDashboard(int64_t now);
    End ended_ = End::None;
};

}  // namespace cab::standalone
