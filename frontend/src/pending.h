// What this console still owes RomM: a marker on the disk for each save,
// state and screenshot not yet uploaded, and one per game for a snapshot not
// yet pushed, so it survives a crash or a power cut. cache.h includes this;
// moved out of it in the restructure (#398).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "users.h"

namespace cache {

// --- What has not reached RomM yet ------------------------------------------
//
// The one genuinely irreplaceable thing on the machine. A ROM comes back, a
// state that has been uploaded comes back, and a state written two minutes ago
// on a console that cannot see its server comes back from nowhere.
//
// Eviction never takes save data — it cannot, it only walks `cache/` — so this
// is not protecting files from the evictor. It is making "unsynced" a FACT ON
// DISK rather than something only the running process knows. A marker is
// written before an upload is attempted and removed when it succeeds, so a
// queue interrupted by a crash or a power cut is still visible on the next
// boot, and its bytes still count against the save floor.
//
// Per user, because the thing it is tracking is a save.
//
// A MARKER SAYS WHAT TO SEND, NOT ONLY THAT SOMETHING IS OWED (2026-09-26).
// It used to hold the byte count and nothing else, so nothing could ever send
// it again: a state saved while the server was away stayed on this console
// for good, and a save went only if the game changed it next time. Now it
// names the kind, the emulator tag, the name it travels under and the file on
// disk (and a state's picture), and owed() reads them back so the uploader
// can try again. The byte count stays on the first line, where pendingBytes()
// reads it. A marker from before this has only that line, and stays owed.
struct Owed {
    int romId = 0;
    bool isState = false;
    bool isScreenshot = false;   // #79; `emulator` is then just "screenshot"
    std::string emulator;
    std::string fileName;     // the name it travels under
    std::string localPath;    // the bytes to send
    std::string shotName;     // a state's picture: its name, and where it is
    std::string shotPath;
};
void markPending(const storage::User& u, const Owed& o, int64_t bytes);

// Everything this person's console still owes the server that can be sent
// again, oldest first.
std::vector<Owed> owed(const storage::User& u);
void clearPending(const storage::User& u, int romId, const std::string& fileName);

// Whether this console still owes the server this exact file. Read off the
// disk like everything else here, so it survives the console being switched
// off between the save and the upload.
//
// IT IS WHAT STOPS A LAUNCH UNDOING A SAVE. A game saved with no network
// leaves a newer file on this machine than anything the server holds; without
// this the next launch would fetch the server's older copy and write it over
// the top, and the person would lose the session they made offline.
bool isPending(const storage::User& u, int romId, const std::string& fileName);

// --- A game's snapshot not on RomM yet (#346) --------------------------------
//
// ONE MARKER PER PERSON AND GAME for RomM 5.5's save channels: what a session
// left of a game, its save and its states, to go up together as one snapshot
// into the channel the session started from (the words are RomM's, in
// romm_types.h). The same rule as the markers above: written before the push
// is tried, removed when RomM has it, so a crash or a power cut leaves it
// owed. A newer one replaces it whole, so what goes is always the newest.
// Kept beside the markers above, one per file, which a screenshot keeps.
//
// In `pending/` as `<romId>.push`: the byte count on the first line, where
// pendingBytes() reads it, then "key value" lines as above. A state's lines
// follow its own "state" line.
struct OwedPush {
    int romId = 0;
    // New each time the marker is written. A push that lands clears the
    // marker only while it is still the one that push sent (clearPushOwed),
    // so one written meanwhile stays owed.
    std::string stamp;
    // When this console wrote the save, in seconds since 1970. A push carries
    // no time the save was made, and of two saves the one made later wins.
    int64_t madeAt = 0;
    // Where it goes (romm::SnapshotPush): the game's channel file; the
    // channel, or none to make one named `label`; the current it builds on,
    // 0 for an empty channel; the snapshot it was made from, 0 for that
    // current.
    int romFileId = 0;
    std::string channelId;
    std::string label;
    int expectedCurrentId = 0;
    int parentId = 0;
    // The session started from this console's own save because the newest
    // could not be used: RomM keeps the push as it comes, and it is never put
    // on top.
    bool couldNotUseNewest = false;
    // The emulator's tag and the core's own name, as the push names them.
    std::string emulator;
    std::string core;
    // The save, when the session changed it: the name it goes up under, the
    // file on disk, and how it travels (romm::SnapshotFile's shape).
    bool hasSave = false;
    std::string saveName;
    std::string savePath;
    std::string saveShape;
    // The states it carries, each in the bank's slot RomM files it under,
    // with its picture.
    struct State {
        std::string core;
        std::string slot;
        std::string fileName;
        std::string path;
        std::string pictureName;
        std::string picturePath;
    };
    std::vector<State> states;
};
// Writes `push` with a new stamp, which it returns; empty when the marker
// could not be written.
std::string markPushOwed(const storage::User& u, const OwedPush& push, int64_t bytes);
// Every game this person's console still owes a push for, oldest first.
std::vector<OwedPush> owedPushes(const storage::User& u);
// This game's, when it is owed. While it is, the console's own save and
// states of the game are newer than anything RomM holds of them, as
// isPending says of one file above.
bool owedPush(const storage::User& u, int romId, OwedPush* push);
bool isPushOwed(const storage::User& u, int romId);
// Removes the marker if it still has this stamp.
void clearPushOwed(const storage::User& u, int romId, const std::string& stamp);
// The name an owed push goes by where an owed file goes by its own: gone
// games asks the upload queue's refusals by it (gonegames.cpp).
extern const char kOwedPushName[];

// Across everybody on the machine: the floor is a fact about the disk, and the
// disk does not care whose upload is queued on it. Both kinds of marker.
int64_t pendingBytes();

// How many things this one person still owes the server, sendable or not (a
// marker from before 2026-09-26 cannot be sent again but is still owed), a
// game's owed push counted as one. Removing a person (#194) asks before
// throwing any of them away.
int pendingCount(const storage::User& u);

}  // namespace cache
