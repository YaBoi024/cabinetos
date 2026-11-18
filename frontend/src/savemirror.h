// RomM's saves for the games on the drive, as last seen, so offline play
// starts from them (#88). MMagTech, 2026-10-05, the vacation case: a game
// played last on the Mac the night before a trip must not start on holiday
// from the console's older copy.
//
// WHAT IT IS: per person, per game, the list of save rows RomM returned and
// the bytes of the rows a launch would restore. Offline, a launch reads this
// exactly as it reads the server online (savesFor, in saves.cpp), so the one
// rule stays the rule: the console's own copy wins only while its upload is
// still owed (cache::isPending); otherwise the server's newest does.
//
// WHY IT CAN NEVER BE OLDER THAN A SAVE THE CONSOLE SENT: an upload that lands
// forgets this game's copy before anything else, and only then asks the
// server again. Should that ask fail, there is no copy, and offline falls back
// to the console's own save, which is the one just sent.
//
// RomM 5.5's SAVE CHANNELS (#346) are kept the same way beside the rows: the
// person's channels for the game as last seen, each with its current
// snapshot, and the bytes of the saves in them. With them, one note that is
// the console's own and not a copy: the snapshot this console holds of the
// game (Held). The words are RomM's, in romm_types.h.
//
// Where: `users/<id> - <name>/server-saves/<romId>/`: `saves.json` (the rows)
// and `<assetId>-<updated>.bin` (the bytes of a row at that version);
// `channels.json` (the channels, in RomM's own form: romm::channelsJson) and
// `snapshot-<content hash>.bin` (the bytes of a save in a snapshot);
// `held.json` (the snapshot this console holds). Each is replaced whole
// through cab::writeBytes, so a power cut leaves the one before.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "romm_types.h"
#include "storage.h"

namespace savemirror {

// The rows RomM returned for this game. Bytes kept for rows no longer listed,
// or at another version, are deleted.
void putList(const storage::User& u, int romId, const std::vector<romm::Asset>& rows);

// False when there is no copy for this game (never seen, or forgotten).
bool getList(const storage::User& u, int romId, std::vector<romm::Asset>* rows);

void putBytes(const storage::User& u, int romId, const romm::Asset& row,
              const std::vector<uint8_t>& bytes);
// Empty when these exact bytes (this row at this version) are not kept.
std::vector<uint8_t> getBytes(const storage::User& u, int romId, const romm::Asset& row);
bool hasBytes(const storage::User& u, int romId, const romm::Asset& row);

// This person's channels for this game as RomM listed them (fetchChannels).
// Bytes kept for a save that no listed channel's current holds are deleted.
void putChannels(const storage::User& u, int romId, const std::vector<romm::Channel>& channels);
// False when there is no copy (never seen, or forgotten).
bool getChannels(const storage::User& u, int romId, std::vector<romm::Channel>* channels);

// The bytes of a save in a snapshot, kept by its content hash: RomM names
// every save in a snapshot by it, and the same bytes in two channels are
// kept once. A save with no hash is not kept.
void putSnapshotSave(const storage::User& u, int romId, const romm::SnapshotFile& save,
                     const std::vector<uint8_t>& bytes);
// Empty when the bytes of this save are not kept.
std::vector<uint8_t> getSnapshotSave(const storage::User& u, int romId,
                                     const romm::SnapshotFile& save);
bool hasSnapshotSave(const storage::User& u, int romId, const romm::SnapshotFile& save);

// THE SNAPSHOT THIS CONSOLE HOLDS OF A GAME (#346, decision 9): the one it
// last started from or pushed, for this person. RomM keeps the same note per
// device (`held_by`), but every CabinetOS console is one device on RomM, so
// only the console itself can say which save it last had. It is this
// console's own note, so forgetting RomM's copy leaves it (forget); it goes
// with the game (forgetHeld) and with the person.
struct Held {
    std::string channelId;
    int snapshotId = 0;
    std::string digest;     // the snapshot's, "sha256:<hex>"
    std::string saveHash;   // its save's content hash; empty when it has none
    int64_t at = 0;         // when this console took it, seconds since 1970
};
void putHeld(const storage::User& u, int romId, const Held& held);
// False when this console holds none of this game (never, or since a
// reinstall).
bool getHeld(const storage::User& u, int romId, Held* held);

// Deletes this game's copy of RomM's saves: the rows, the channels and every
// byte kept. The snapshot this console holds stays.
void forget(const storage::User& u, int romId);
// Deletes the snapshot this console holds of this game, and the game's folder
// once it is empty: for a game RomM no longer has (gonegames.h).
void forgetHeld(const storage::User& u, int romId);

// A FETCH THAT STARTED BEFORE A FORGET MUST NOT PUT ITS OLDER LIST BACK. The
// fetch takes a mark before asking the server, and skips a game whose copy was
// forgotten after it (an upload landed meanwhile).
uint64_t mark();
bool forgottenSince(int romId, uint64_t mark);

}  // namespace savemirror
