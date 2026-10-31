// One game in play: what the launch set up and the saves and states need at
// the end. Moved out of main.cpp in the restructure (#398).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dirsave.h"
#include "disc.h"
#include "filesave.h"

// Saves and states, and which way they travel.
//
// THE DISK COPY IS WRITTEN FIRST, ALWAYS, before any upload is attempted. That
// was Cabinet's guarantee and the reason is plain: losing signal must never mean
// losing a save. The upload is a second step that is allowed to fail, and a
// file that failed to upload is still sitting on disk to try again.
//
// A save and a state are different things and are kept apart. The save is the
// game's own — a cartridge battery, a memory card — it outlives everything, and
// it uploads with overwrite so a game keeps one card rather than one per
// session. A state is a snapshot of this exact build and a history of them is
// the point, so they never overwrite.
struct GameSession {
    int romId = 0;
    // RomM's `ra_hash` for RetroAchievements (achievements.h); empty when none.
    std::string raHash;
    // The core's manifest name, for the pause menu's Picture quality row
    // (quality::hasLevels). Standalones set it to the emulator's.
    std::string core;
    // RomM's platform slug, for the pause menu's system rows (sysopts.h).
    std::string platformSlug;
    // An arcade game (catalog::Coverage::arcade): Flycast plays Dreamcast
    // discs and Naomi boards, and only the boards are arcade games.
    bool arcade = false;
    // The core, for what only an arcade game gets (the fight stick's layout,
    // Controls, holding Start), or empty for any other game.
    std::string arcadeCore() const { return arcade ? core : std::string(); }
    std::string title;
    // The server's own file name for the game, without its extension. A
    // save's row on RomM is named after it — see saveRowName.
    std::string fsStem;
    // TWO TAGS, NOT ONE, and the difference is what lets five platforms' saves
    // travel at all. A STATE is a photograph of the emulator's insides and the
    // tag is the promise that the build about to load it is the build that
    // wrote it; a SAVE in the file-writing class is the emulated machine's own
    // storage — a VMU image, a board's NVRAM — and no build flag moves it. So
    // a save tag can be given where a state's portability is unproven: a
    // memory card does not care. Flycast's state tag is given anyway, by
    // decision (savetable.cpp, 2026-09-23). See catalog::saveTag.
    //
    // Empty means: do not upload, we cannot vouch for it.
    std::string saveTag;
    std::string stateTag;
    // False on PlayStation 2, GameCube and later: no Save state, no Load
    // latest state, true to those consoles. See catalog::snapshotsAllowed.
    bool snapshots = true;
    // Where this person's copies live, which is no longer beside the ROM.
    //
    //   users/<id> - <name>/saves/<platform>/<romId>/<core>/
    //   users/<id> - <name>/states/<platform>/<romId>/<core>/
    //
    // The save directory is ALSO the one handed to the core, so a core that
    // writes its own file — a Sega CD's .brm, MAME's nvram, PSP's memory stick
    // — writes it into the same place. That is what closes the oldest fault in
    // this subsystem: there were two save directories and a save written one
    // way was not seen the other way, and every core on the machine shared one
    // flat pile so a file did not say which game wrote it.
    std::string saveDir;
    std::string stateDir;
    std::vector<uint8_t> saveAtLaunch;   // to tell whether it actually changed
    // An emulator of its own: which, and the game's folder, for whatever it
    // needs doing once it has gone (standalone::afterEnd).
    std::string standaloneCore;
    std::string entryPath;

    // --- Directory saves, which is PSP and nothing else -------------------
    //
    // Where the tree lives, and what was in it before the game ran. The
    // baseline is how a save is attributed to the game that wrote it: this
    // console gives every core ONE shared save directory, so
    // `PSP/SAVEDATA` accumulates a folder per PSP game ever played, and
    // uploading the whole thing under one rom id would file four games'
    // saves against whichever was launched last.
    //
    // So the rule is: a folder that was created or touched while this game
    // was running belongs to this game. That needs no disc id, no parsing
    // of the ISO, and no table.
    //
    // THE SAVE DIRECTORY IS NOW PER ROM, which is the answer this comment
    // called "the better answer eventually", so the baseline no longer has
    // anything to disambiguate — the tree holds one game's saves, and all of
    // it is sent (syncDirSave). It is kept because it still answers a second
    // question the layout does not: whether this game wrote anything at all
    // this run, which is what decides between an upload and a no-op.
    std::string dirSaveRoot;                  // empty but for PPSSPP and Wii
    std::vector<cab::DirEntry> dirAtLaunch;
    // Which files under the root are the save (catalog::inDirectorySave):
    // all of them for PSP, only the games' `data/` folders for Wii.
    std::string dirSaveCore;
    std::string dirSavePlatform;
    std::string dirSaveTitleId;   // the Mii Channel's is rooted higher (#275)

    // --- Saves the core writes as a FILE ----------------------------------
    //
    // Empty for the cores that expose a battery, which this frontend reads out
    // of the core's own memory above. For the rest — Dreamcast, arcade, 3DO,
    // Sega CD, Neo Geo Pocket, DS — this is where the game's progress actually
    // lives, and each entry carries what was in the file once the restore had
    // run and before the game had a chance to write. See filesave.h.
    std::vector<cab::FileSaveState> fileSaves;

    // --- A game on several discs (#162) -----------------------------------
    //
    // Empty for a game on one disc, which then shows none of it. `discOrder`
    // is the list the core was handed: its index i holds disc discOrder[i],
    // so the console counts discs by their real number whatever order the
    // list was written in (Start over writes disc N first on some systems).
    std::vector<disc::Disc> discs;
    std::vector<int> discOrder;
    int disc = 0;                     // the disc in the drive, by number
    disc::Change discChange = disc::Change::None;
    // INSERT IN PROGRESS: the lid is open and the game runs until the frame
    // it closes on. No save state is made or loaded meanwhile (RetroArch
    // #5094: a state taken with the tray open does not come back right).
    int discInserting = 0;
    uint64_t discCloseAt = 0;
};
