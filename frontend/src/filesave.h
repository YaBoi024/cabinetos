// Saves the CORE writes as a file, rather than ones this console can read out
// of the core's memory.
//
// WHAT WAS BROKEN, IN ONE SENTENCE: more than half the saves on a real RomM
// server belong to platforms this console could neither upload nor restore, so
// somebody who had been playing Dreamcast, arcade, 3DO, Sega CD, DS or Neo Geo
// Pocket on another device arrived here to an empty memory card and left
// without their progress reaching the server. Measured, not estimated — 47 of
// the 81 saves on the reference server, 58%, and the largest single block of
// them is Dreamcast's thirteen.
//
// It looked like an edge case because of how the fault presents. A core in
// this class answers `RETRO_MEMORY_SAVE_RAM` with nothing, so the console
// printed `[save] battery is 0 bytes` and did nothing further — which is a
// true statement about the core and says nothing at all about whether the game
// has a save. The save was on the disk the whole time, in a file the core
// opened for itself, and nobody was looking at it.
//
// THE MECHANISM IS THE ONE PSP ALREADY USES, and for the same two reasons that
// building PSP taught:
//
//   * RESTORE BEFORE THE CORE LOADS THE GAME. These cores read their save file
//     once, synchronously, while the machine is being built — Flycast while
//     `retro_load_game` sets up the Maple bus, Genesis Plus GX in `bram_load`,
//     Opera in `opera_lr_nvram_load`. A file that arrives afterwards is a file
//     the game has already decided is not there.
//   * CAPTURE AFTER `retro_unload_game`. A core buffers its writes and flushes
//     at shutdown; Flycast goes further and only closes the VMU in its
//     device's destructor at teardown, so the image on disk mid-session is
//     partial by construction. The reference implementation uploaded exactly
//     such a half-written card once and the game then reported it corrupt.
//
// AND THE THIRD RULE, WHICH IS THIS FILE'S OWN: compare against a baseline
// taken at launch. What changed between the restore and the quit is what this
// game saved, and everything else is a no-op. That is what keeps a console
// from filing a row on the server every time somebody looks at a title screen.
//
// savetable.h owns the TABLE — which platform writes what, where, and how to
// tell a real save from a formatted-empty one. This owns the mechanism.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fileio.h"
#include "savetable.h"

namespace cab {

// One of a game's save files as this console is tracking it across a session.
struct FileSaveState {
    catalog::SaveFile spec;
    // Where the core actually reads and writes it, in this game's save folder.
    std::string path;
    // The bytes that were there once the restore had run and before the game
    // had a chance to write anything. Empty means there was no file at all,
    // which is the ordinary case for a game nobody has ever saved.
    std::vector<uint8_t> atLaunch;
    // True when `atLaunch` came from somewhere real — this console's own disk
    // or the server — rather than from nothing. It is what decides whether the
    // freshness guard below applies: once a real save has existed, every later
    // change travels, erasing one included, because losing history is worse
    // than an empty row.
    bool hadOne = false;
    // The file on this console that holds `atLaunch`, for the snapshot pushed
    // when the game is left (savepush.h): the one the core wrote. Empty when
    // there is no file.
    std::string kept;
};

// Whether these bytes are a save somebody made, or the empty thing a core
// writes just by being switched on. See catalog::Untouched for why each rule
// is what it is.
bool holdsASave(const std::vector<uint8_t>& data, catalog::Untouched rule);

// WHETHER ANOTHER EMULATOR'S SAVE IS A FILE THIS CORE READS (#346, step 8;
// decided 2026-10-12): a size `check` allows, the marks it names, and
// something a game wrote (holdsASave by `rule`), which an empty file never
// holds. Why not, in `why`.
bool passesCheck(const std::vector<uint8_t>& data, const catalog::SaveCheck& check,
                 catalog::Untouched rule, std::string* why);

// The file the core actually left, which is not always the name we predicted:
// Flycast names a game's card after the disc's product number. Returns the
// spec's own path when there is no scan to do, the newest file the core wrote
// when it names the file itself (catalog::SaveFile::coreNamesItself), and the
// empty string when nothing is there. The scan never leaves `path`'s folder.
std::string writtenFile(const catalog::SaveFile& spec, const std::string& path);
// Removes every file in `path`'s folder that the scan above would find, but
// `path`: for a core that names the file itself, which would read one of
// those before `path`.
void removeOtherCaptures(const catalog::SaveFile& spec, const std::string& path);

}  // namespace cab
