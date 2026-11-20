// Saves, and which way they travel: what RomM holds of a game's save fetched
// for a start (its save channels and its saves from before them, and the one
// the start continues from, savepick.h), a directory save zipped and unzipped,
// the files a core writes, and the battery a core exposes. A save is written
// to this console before anything is sent, and what a game saved goes up as
// one snapshot once it has closed (savepush.h). Moved out of main.cpp in the
// restructure (#398).

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "catalog.h"
#include "core.h"
#include "dirsave.h"
#include "filesave.h"
#include "gamesession.h"
#include "romm.h"
#include "savepick.h"
#include "standalone.h"
#include "storage.h"
#include "uploader.h"

// WHAT A RESTORE NEEDS FROM RomM, fetched on the launch worker once the game
// is on the disk (#481, LAUNCH-17): what RomM holds of one game's save, which
// of it the start continues from (savepick.h), and that save's bytes. The
// stages behind the curtain (launch.cpp) write what was fetched and never ask
// the server, so a slow or hanging server holds the launch's busy clock, not
// the picture. One per game a launch reads saves for: the game's own, and the
// Mii Channel's for a Wii game (wiimii.h).
struct SaveFetch {
    int romId = 0;
    std::string title;       // the Mii Channel's, which names its zip on this console
    bool asked = false;      // the worker asked: a token, and a tag the saves travel under
    // RomM, or its copy offline, answered for everything the pick goes by.
    // False leaves the pick at the console's own save.
    bool listed = false;
    std::string error;       // why not, when it did not
    // RomM's saves from before channels, and the bytes of each a restore may
    // take, by row id, when the pick is among them.
    std::vector<romm::Asset> rows;
    std::map<int, std::vector<uint8_t>> bytes;
    // The file RomM keys this game's save channels to (RomFile::channelFile):
    // as the caller knew it, or as RomM gave it when it had to be asked; 0
    // when not known.
    int channelFileId = 0;
    // Which save the start continues from, and when the newest of the old
    // rows was stored (savepick::Facts).
    savepick::Pick pick;
    std::string oldRowAt;
    // The picked snapshot's save as RomM stores it, when the pick is a
    // channel's; empty when it did not come. Another emulator's save
    // (`converted`) is RomM's answer for this console's form instead, put
    // the way a start reads a snapshot's save and checked (fetchSaves).
    std::vector<uint8_t> picked;
    bool converted = false;
    // What the pick went by, for the start that falls back when another
    // emulator's save cannot be used.
    savepick::Facts facts;
    // savemirror::mark() taken before the ask. A save of this game that this
    // console sends after it makes what was fetched older than the console's
    // own copy (saves.cpp, fetchIsStale).
    uint64_t mark = 0;
};
// What RomM holds of the game's save and the pick, with no bytes: all the
// game's page reads. `channelFileId` is the file the game's channels are
// keyed to when the caller knows it, else 0, and RomM is asked. Asks nothing
// without a tag or a token.
void listSaves(romm::Client& client, const storage::User& user, int romId, int channelFileId,
               const char* tag, SaveFetch* out);
// THE FORM THIS CONSOLE'S CORE READS ITS SAVE IN, for another emulator's
// save asked of RomM (#346, step 8): how RomM is asked (catalog::SaveForm),
// and what the save is: the files the core writes (catalog::saveFiles), a
// folder (its directorySaveRoot, or an emulator run as a program), or else
// the battery the core exposes.
struct OwnForm {
    catalog::SaveForm form;
    std::string tag;   // the tag this core's saves travel under (catalog::saveTag)
    std::string platform;
    std::vector<catalog::SaveFile> files;
    bool folder = false;
    // An arcade game takes no other app's save, whichever emulator it names:
    // a board's memory carries no mark of the emulator or its version that
    // wrote it (MMagTech, 2026-11-20). CabinetOS consoles' saves travel
    // under the tag and are never asked for in this form.
    bool arcade = false;
};
// The worker's half of a restore: listSaves, then the bytes of the save the
// pick names: the snapshot's, or of RomM's old rows the newest under `tag` in
// each region (the file saves and the battery) and of any kind (a folder
// save). ANOTHER EMULATOR'S SAVE (savepick.h) is asked of RomM in `own`'s
// form and checked; one that cannot be used, or any with `own` null, leaves
// the start to the newest this console can use as stored, and the pick says
// it could not use the newest. Stops between requests once `cancel` is set.
void fetchSaves(romm::Client& client, const storage::User& user, int romId, int channelFileId,
                const char* tag, const OwnForm* own, const std::atomic<bool>* cancel,
                SaveFetch* out);
// RomM's answer for another emulator's save, put the way a start reads a
// snapshot's save in `own`'s form (the head of saves.h), and checked: each
// file by its catalog::SaveCheck, a battery by batteryCheck, a folder by its
// system's shape. Handed over as stored (`plain`: asked with no core, once
// RomM said it converts nothing for the system; or one file with no path in
// the emulator's folder), a save where Sigil names a difference, or a folder,
// is used only when this core made it (savepick::mayLoad, by `madeBy` and
// `madeByCore`, the names the save carries). False, with why, when it is no
// save this console's core reads.
bool toOwnForm(const romm::SaveAnswer& answer, bool plain, const OwnForm& own,
               const std::string& madeBy, const std::string& madeByCore,
               std::vector<uint8_t>* out, std::string* why);
// The pill a start says when the newest save on RomM was another emulator's
// and could not be used (#346, decision 5; MMagTech's wording, 2026-11-17).
inline constexpr char kCouldNotUseNewest[] = "Couldn't use the newest save";

// WHAT A START'S RESTORES DID, gathered across the three of them for the
// notes the start leaves (noteStart). `asideDir` is the caller's: where a
// save of this console's that the start replaces is kept first
// (storage::replacedSaveDir); empty keeps none. The rest is the restores'.
struct StartNotes {
    std::string asideDir;
    // The pick's save was put in place: a snapshot's, or another app's in no
    // channel.
    bool tookPick = false;
    // What was put in place was not what this console already had: it had no
    // save, or another.
    bool changedOwn = false;
    // What this start has kept aside, by its name in `asideDir`.
    std::vector<std::string> keptAside;
    // RomM's saves from before channels a restore put in place (#346, step
    // 7), which the start brings into a channel (savepick::oldSaveToCopy).
    std::vector<savepick::OldRowTaken> oldRowsTaken;
};
// A SNAPSHOT'S SAVE AS A CabinetOS CONSOLE READS IT, used as stored: a
// battery is the core's save memory as it is; a core that writes one save
// file, that file as it is; a core that writes several, a zip holding each
// under its path in the save folder; a folder save, the zip syncDirSave
// makes. A restore that cannot read the picked save that way leaves the
// console's own in place.
// A ZIP IN SIGIL'S SHAPE (dirsave.h, ZipShape) is put back in the folder's
// own; `shape` and `wiiKind` from zipShapeOf and wiiKindOf.
void restoreDirSave(const SaveFetch& fetched, const char* tag, const std::string& root,
                    const std::string& title, const std::string& oldCard = std::string(),
                    StartNotes* notes = nullptr, cab::ZipShape shape = cab::ZipShape::Folder,
                    const std::string& wiiKind = std::string());
// RomM's shape for this core's folder save (dirsave.h): Sigil's for a Wii
// game (the Mii Channel's stays the folder's) and a 3DS game.
cab::ZipShape zipShapeOf(const std::string& core, const std::string& platform,
                         const std::string& titleId);
// A Wii title's kind folder from RomM's `title_id`: the first eight of a
// channel's sixteen digits (`00010001` for WiiWare), `00010000` for a disc.
std::string wiiKindOf(const std::string& titleId);
std::vector<cab::DirEntry> listDirSave(const GameSession& sess);
// WHAT A GAME SAVED IS GATHERED ON THIS CONSOLE FIRST, by the three below: a
// save folder zipped (syncDirSave), the files a core wrote read back once it
// has shut down (syncFileSaves), the battery read out of the core (syncSave,
// which gathers a save folder instead where the game has one). Each writes
// this console's copy only when the save changed, and says so in the session
// (GameSession::saveChanged). None sends anything: the one snapshot goes up
// once the game has closed (savepush.h, leave).
void syncDirSave(GameSession& sess);
std::string dirSaveZipName(const std::string& title);
void finishInterruptedSave(const cab::standalone::Emulator& emu, const std::string& saveDir,
                           Uploader& up);
// A GAME IN A CORE THAT NEVER ENDED (#346, step 9; MMagTech, 2026-11-20): a
// core that writes its save as files while the game runs (DS, Dreamcast,
// PlayStation 2, PSP, 3DS, GameCube, Wii) leaves the progress a crash or a
// power cut interrupted on this console, as an emulator run as a program
// does. notePlaying writes the session's note in its save folder once the
// restores are done (the game's own files, or a save folder, and a tag its
// saves travel under; nothing for a battery, which lives in the core);
// App::finishExit takes it away once the game is left. finishInterruptedPlay
// reads a note left behind, at the next start of that game and at start-up:
// every save file the core wrote after the note was written is gathered as
// the session's save, as the leave would have, and goes up as its snapshot,
// owed until it lands, so the start that follows keeps it rather than
// putting RomM's older save over it. A file that fails the save check
// another app's save gets (catalog::SaveCheck: its size, its marks, a save
// in it) is not kept: a power cut can leave one half written. A save folder
// has no such check and goes as the core left it. True when a save went.
void notePlaying(GameSession& sess);
bool finishInterruptedPlay(const std::string& saveDir, Uploader& up);
// The note an emulator run as a program is started with, and the session it
// reads back as (saves.cpp).
std::string playingNoteFor(const GameSession& sess, const std::string& saveDir);
GameSession sessionOfPlayingNote(const std::string& text, std::string* saveDir = nullptr);
std::string saveRowName(const std::string& fsStem, const std::string& region);
std::vector<cab::FileSaveState> restoreFileSaves(
        const std::vector<catalog::SaveFile>& specs, const std::string& fsStem,
        const std::string& saveDir, const char* tag, const SaveFetch& fetched,
        StartNotes* notes = nullptr);
void syncFileSaves(GameSession& sess);
void syncSave(GameSession& sess);
void restoreBatterySave(cab::Core& core, GameSession& session, const std::string& saveDir,
                        const SaveFetch& fetched, StartNotes* notes = nullptr);
// The pill a start says when the save it continued from was made on another
// device (#346, decision 5; MMagTech's wording).
inline constexpr char kContinuingElsewhere[] = "Continuing from another device";
// AFTER THE RESTORES: where the session started from goes into `session`
// (the channel and snapshot, for the snapshot pushed when the game is left,
// savepush.h), and a start from a channel's snapshot becomes the snapshot
// this console holds (savemirror::Held). True when the start says
// "Continuing from another device" (savepick::continuingElsewhere). A start
// that put RomM's save from before channels in place marks the game's push
// that brings it into a channel (savepush::markOldSave), and `*bringsOldSave`
// says so: the caller queues it (Uploader::pushSnapshot), so it goes while
// the game plays.
bool noteStart(const SaveFetch& fetched, const StartNotes& notes, const storage::User& user,
               GameSession* session, bool* bringsOldSave = nullptr);
