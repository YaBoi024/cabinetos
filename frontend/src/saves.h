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
    // channel's; empty when it did not come.
    std::vector<uint8_t> picked;
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
// The worker's half of a restore: listSaves, then the bytes of the save the
// pick names: the snapshot's, or of RomM's old rows the newest under `tag` in
// each region (the file saves and the battery) and of any kind (a folder
// save). Stops between requests once `cancel` is set.
void fetchSaves(romm::Client& client, const storage::User& user, int romId, int channelFileId,
                const char* tag, const std::atomic<bool>* cancel, SaveFetch* out);

// WHAT A START'S RESTORES DID, gathered across the three of them for the
// notes the start leaves (noteStart). `asideDir` is the caller's: where a
// save of this console's that the start replaces is kept first
// (storage::replacedSaveDir); empty keeps none. The rest is the restores'.
struct StartNotes {
    std::string asideDir;
    // The picked snapshot's save was put in place.
    bool fromChannel = false;
    // What was put in place was not what this console already had: it had no
    // save, or another.
    bool changedOwn = false;
    // What this start has kept aside, by its name in `asideDir`.
    std::vector<std::string> keptAside;
};
// A SNAPSHOT'S SAVE AS A CabinetOS CONSOLE READS IT, used as stored: a
// battery is the core's save memory as it is; a core that writes one save
// file, that file as it is; a core that writes several, a zip holding each
// under its path in the save folder; a folder save, the zip syncDirSave
// makes. A restore that cannot read the picked save that way leaves the
// console's own in place.
void restoreDirSave(const SaveFetch& fetched, const char* tag, const std::string& root,
                    const std::string& title, const std::string& oldCard = std::string(),
                    StartNotes* notes = nullptr);
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
// "Continuing from another device" (savepick::continuingElsewhere).
bool noteStart(const SaveFetch& fetched, const StartNotes& notes, const storage::User& user,
               GameSession* session);
