// WHICH SAVE A START CONTINUES FROM (#346, save channels): the one rule, and
// whether the start says "Continuing from another device". The launch worker's
// fetch, the three restores, the game's page and the offline fetch all go by
// it (saves.cpp, detail.cpp, offline_sync.cpp); until #346 each had its own
// "newest row under my tag" loop. It asks nothing and reads no disk: what it
// is told is Facts, so its table of cases runs with no server
// (`--save-pick-test`).
//
// THE RULE. The game starts from the newest save RomM holds for this person
// that this console can use, in any of their channels or among the saves from
// before channels (RomM's old rows, under the console's tag as before). A
// save this console has not sent yet is sent first, when RomM answers
// (savepush::settleBeforeStart), and then weighed like any other, so a
// console back from a trip continues from a newer save made elsewhere
// meanwhile; one it could not send still wins, whatever RomM holds. When
// the newest save on RomM is one the console cannot use, it starts from the
// newest it can, or its own, and the session says so (`couldNotUseNewest`),
// so what it saves is kept RomM's way beside the newer one.
//
// AND NEVER FROM A SAVE MADE BEFORE ONE OF ITS OWN THAT RomM KEPT BESIDE A
// CHANNEL'S CURRENT. A push RomM keeps as a branch (savepush.h) is on RomM
// but is no channel's current, so nothing listed shows it: without this, a
// console whose save could not go on top would start next from an older save
// of its own that is still listed, over the newer one on its drive. The note
// of the snapshot held says when that save was made (savemirror::Held).
//
// WHICH SAVES IT CAN USE. The ones a CabinetOS console made, pushed under the
// tag this core's saves travel under or filed under it (ownForm), are used as
// stored. ANOTHER
// EMULATOR'S SAVE IS ASKED OF RomM IN THIS CONSOLE'S OWN FORM AND CHECKED
// (#346, step 8; decided 2026-10-12): when the newest is one, the pick names
// it (`other`) and the launch worker asks for it (saves.cpp, fetchSaves);
// one that cannot be used leaves the start to the newest this console can
// use as stored, the pick with others left out, which says so
// (`couldNotUseNewest`). AND ANOTHER APP'S SAVE IN NO CHANNEL IS WEIGHED THE
// SAME WAY (MMagTech, 2026-11-19): a row from before channels that another
// emulator made, from an app that does not use channels or uploaded by hand
// on RomM's website, and one RomM kept out of a channel's snapshots
// (otherRows). Its row is left as it is. BUT THE SNAPSHOT THIS CONSOLE
// HOLDS IS NEVER ANOTHER EMULATOR'S TO IT (#346, phase 5, 2026-11-21): RomM
// files a snapshot's save under the first of the person's rows with those
// bytes, which can be another app's, and a save this console took from
// another app was converted on the way in, so its own copy is that save in
// its own form, or what it played on from it.
//
// THE PILL IS NEVER A GUESS (MMagTech, 2026-11-17: the console shows no
// message that is wrong; one missed is accepted). It shows only when the
// console's own note or RomM's says the save was made somewhere else:
//   - The console holds a note of this game (savemirror::Held): the pill
//     shows when the save's content hash is none this console has had. By
//     the hash and not the snapshot's id, because RomM's website restores an
//     older snapshot as a NEW one with the same save inside (its
//     `restoreManifest`), and that is no other device's save.
//   - No note (a reinstall, or the game's first start from a channel): RomM
//     says which device made the snapshot and which hold it. The pill shows
//     only when another app's device made it and no CabinetOS console holds
//     it. Every CabinetOS console is one device on RomM, so a save another
//     console made cannot be told from this one's, and gets no pill.
// And never when the save is byte for byte what this console already had
// (continuingElsewhere).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "romm_types.h"
#include "savemirror.h"

namespace savepick {

// What a start knows before it picks.
struct Facts {
    // The tag this core's saves travel under (catalog::saveTag).
    std::string tag;
    // RomM's channels for this person and game, as asked or as last seen;
    // none when RomM has none or they are not known.
    std::vector<romm::Channel> channels;
    // When the newest of RomM's saves from before channels under `tag` was
    // stored (its `updated_at`); empty when there is none or they are not
    // known.
    std::string oldRowAt;
    // RomM's saves outside channels' snapshots that another emulator made
    // (otherRows).
    std::vector<romm::Asset> otherRows;
    // This console still owes RomM a save of the game, as a snapshot or as a
    // file from before channels (pending.h), and could not send it before the
    // start (savepush::settleBeforeStart): its own copy stands.
    bool owed = false;
    // The snapshot this console holds of the game; `haveHeld` false when it
    // holds none.
    bool haveHeld = false;
    savemirror::Held held;
};

struct Pick {
    enum class From {
        Own,       // nothing from RomM: what is on this console stands
        Channel,   // `channel`'s current snapshot
        OldRows,   // RomM's saves from before channels, by the old rule
        OtherRow,  // `row`, another app's save in no channel
    };
    From from = From::Own;
    romm::Channel channel;   // From::Channel only
    romm::Asset row;         // From::OtherRow only
    // The save named is another emulator's, to be asked of RomM in this
    // console's form and checked before it is used (the head of this file).
    bool other = false;
    // The notes say the save was made somewhere else (the head of this file).
    bool madeElsewhere = false;
    // The newest save on RomM is one this console cannot use.
    bool couldNotUseNewest = false;
    // Why, for the log.
    std::string why;
};

// With `others` false, another emulator's save is never named: the start
// after one could not be used.
Pick pick(const Facts& facts, bool others = true);

// Whether a start that put `pick`'s save in place says "Continuing from
// another device": only for a channel's save the notes say was made
// elsewhere, or another app's save in no channel, and never when it was what
// this console already had.
bool continuingElsewhere(const Pick& pick, bool sameAsOwn);

// The saves of `rows` outside channels' snapshots that another emulator made
// (not under `tag`, the one every CabinetOS console files them under): in no
// channel, or KEPT OUT OF ONE (#346, phase 5, 2026-11-21): a save an app
// filed the old way under a slot, which RomM put in the slot's channel but
// did not make its current because that app may not load the current save
// (RomM's `hold_legacy_upload`: Ludo's saves after a CabinetOS console's, its
// emulator's name never the console's). Such a row is newer than its
// channel's current (`channels`) and is not that current's save.
std::vector<romm::Asset> otherRows(const std::vector<romm::Asset>& rows, const std::string& tag,
                                   const std::vector<romm::Channel>& channels);

// RomM'S OWN RULE FOR WHO MAY LOAD A SAVE AS STORED (`may_load` in its
// handler/snapshots/legacy.py at 5.5.0-alpha.2): the save's core against this
// console's (`sigil`), else, when it names none, its emulator against this
// console's names for the core (`tag` or `sigil`), else yes.
bool mayLoad(const std::string& emulator, const std::string& core, const std::string& tag,
             const std::string& sigil);

// The note of the snapshot this console holds, after it took `taken` in
// `channelId` from RomM, at a start or as RomM's answer to a push: `before`
// (pass an empty one for none) with the snapshot, and its save's hash added
// to the hashes this console has had. Its save is dated by RomM's time for
// the snapshot, and it is no longer beside a current; a push says otherwise
// itself (savepush.cpp).
savemirror::Held heldAfter(const savemirror::Held& before, const std::string& channelId,
                           const romm::Snapshot& taken, int64_t now);

// When a save a start would continue from was stored, as RomM wrote it; empty
// for a start from the console's own. `oldRowAt` is Facts'.
std::string pickedAt(const Pick& pick, const std::string& oldRowAt);

// WHEN A SNAPSHOT'S SAVE WAS MADE, as RomM can say it: when the save first
// reached RomM (romm::Snapshot::saveAt), else when the snapshot did. A
// snapshot that changed only its states (#346, step 6) carries the save of
// the one before it, so its own time is not its save's; every "made later"
// of the rule goes by this.
std::string savedAt(const romm::Snapshot& s);
// Each channel's current dated by its save's own row in `rows`, the game's
// saves as RomM listed them (the row whose id is the save's).
void dateSaves(std::vector<romm::Channel>* channels, const std::vector<romm::Asset>& rows);

// A SNAPSHOT'S SAVE IS IN THE FORM `tag` NAMES when the device that pushed it
// named that emulator (romm::Snapshot::emulator), or when the row RomM filed
// its save under carries that name: the bytes are then what such a device
// wrote. Never by the row's name alone (#346, phase 5, 2026-11-21): RomM files
// a snapshot's save under the first of the person's rows with those bytes,
// which can be another app's, so a CabinetOS console's own save, pushed again
// with a state after the console took an app's save as stored, read as that
// app's to every other CabinetOS console. With RomM away such a console could
// not ask for it, said it could not use the newest, and what it saved was
// never put on top.
bool ownForm(const romm::Snapshot& s, const std::string& tag);

// --- RomM's saves from before channels ---------------------------------------
//
// One row per file, found by the tag and, for a core with several save files,
// by region. RomM keeps them after the switch, and a console not yet updated
// still writes them. RomM 5.5 lists a snapshot's save among a game's saves
// too, with its channel named (romm::Asset::channelId): that is no old row,
// and none of these takes it.

// Which region a row belongs to, by the extension the console uploaded it
// under: "cart", "rtc", "ps2", "raw", and "srm" for anything else.
std::string regionOfRow(const std::string& fileName);
// The newest row under `tag`: of any kind when `region` is null, else in that
// region. Null when there is none.
const romm::Asset* newestOldRow(const std::vector<romm::Asset>& rows, const std::string& tag,
                                const char* region = nullptr);
// Every row a start may take: the newest of any kind (a folder save), and the
// newest in each region (the file saves and the battery).
std::vector<const romm::Asset*> oldRowsAStartMayTake(const std::vector<romm::Asset>& rows,
                                                     const std::string& tag);

// --- A start that took them brings them into a channel (#346, step 7) -------
//
// DECISION 2 OF 2026-11-15: a game's save from before channels moves into a
// channel the first time the game is started after the update, game by game.
// When the pick is RomM's saves from before channels (the game has no
// channel, or that save is newer than every channel's: a console not yet
// updated made it), the start puts it in place exactly as before, then the
// game's push brings it into the channel this console saves into
// (savepush.h, markOldSave). RomM copies a row in itself (`copy_of`), so no
// byte is sent, and the row stays as it is, so a console still on the old
// version goes on reading and writing it. A save that was several rows
// (Sega CD's own memory and its cartridge) has to be one zip in a snapshot,
// as a start reads one (saves.h), which RomM cannot make of two rows: the
// console sends that zip. So does a battery with its clock (Cabinet's Game
// Boy `.rtc` row beside the battery's, #346, step 11).

// One row a restore put in place, and as what.
struct OldRowTaken {
    int rowId = 0;
    enum class As { Battery, Clock, File, Folder } as = As::Battery;
    // Its path in a zip of the save's parts: a file's, where its core reads
    // it; a battery's and its clock's, `<game>.srm` and `<game>.rtc`. Empty
    // for a folder.
    std::string path;
    // A file's place among the core's save files: 0 the first, which a
    // snapshot's one file is read as, unless it is a cartridge
    // (catalog::SaveFile::region), which only ever travels in a zip.
    size_t file = 0;
    bool cart = false;
};

// What the push brings in: the rows, and how they travel.
struct OldSaveCopy {
    std::vector<const romm::Asset*> rows;   // in `rows`, each once
    // Several rows: one zip of them, each under `paths[i]` (the core's own).
    // One row: RomM's copy of it.
    bool zipped = false;
    std::vector<std::string> paths;
    std::string shape;   // "SINGLE", "MULTI" or "FOLDER", RomM's words
};

// False when nothing is brought in: the pick is not RomM's saves from before
// channels, no restore took one (each came back empty or held no save), or a
// row taken is not a save from before channels in `rows`. Nor when the
// newest save on RomM is one this console cannot use: an old save it did not
// change is no progress to keep beside that one (answer 3 of 2026-11-17
// keeps the console's progress, which the session's own push carries), and
// RomM would keep another copy of it beside at every start. A folder save
// counts as taken only when it was a zip a start unpacked: GameCube's card
// from before #344 is read in by Dolphin, and that folder goes up at the
// leave as any changed folder does.
bool oldSaveToCopy(const Pick& pick, const std::vector<romm::Asset>& rows,
                   const std::vector<OldRowTaken>& taken, OldSaveCopy* out);

// The table of `--save-pick-test` (selftests.cpp runs it, then the restores in
// a scratch folder): the rule on every case, the pill, the note a start
// leaves and the old rows' helpers. Asks nothing, reads no disk. How many
// cases failed.
int test();

}  // namespace savepick
