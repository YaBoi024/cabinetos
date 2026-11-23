// SWITCH SAVES THROUGH SIGIL, on this console (#346, the Switch step;
// docs/decisions/2026-11-20-romm-converts-saves.md).
//
// RomM converts another app's save for this console's emulator for every
// system but Switch: it converts a Switch save only when it knows the game's
// title ID, reads none from an encrypted dump, and on purpose never holds a
// player's keys. So for a Switch game the console reads the ID itself, from
// the game's dump with the keys Eden is given, through the Sigil pinned in
// frontend/third_party/sigil (its VERSION file), and puts another app's save
// in Eden's form here. Nothing about the game is written to RomM.
//
// The save between the console and RomM is Sigil's unit for the yuzu forks
// (its Switch page): `<title>/...` for the profile's own saves and
// `device/<title>/...` for the device's, which every profile shares. The
// console makes and reads that shape itself (dirsave.h, ZipShape::Switch);
// Sigil is called only for another app's.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dirsave.h"

namespace switchsave {

// A Switch game as Sigil reads it from its dump: the game's title ID (an
// update or a DLC gives the game's, whose folder its saves are in) and the
// folder its saves are in.
struct Game {
    std::string titleId;
    std::string saveId;
};

// Reads the game in `dump` (.nsp, .xci, .nsz, .xcz) with `keys`, a prod.keys
// file (standalone::switchKeys). Where the keys do not open it (none, or too
// old for the dump), an ID in the file's name, `[0100ABCD12345000]`, is taken,
// as Sigil's own default does. False, with why, when neither gives one.
bool readGame(const std::string& dump, const std::string& keys, Game* out, std::string* why);

// Another app's save as RomM stores it, a Switch unit, restored by Sigil for
// Eden (the profile Eden's form names, catalog::saveForm) and given back in
// the unit's shape: only the files Sigil places in this game's folders, the
// profile's and the device's, so a save of another game, or one holding
// anything Eden would not read, is refused. False, with why (Sigil's error
// and the member at fault).
bool fromOtherApp(const std::vector<uint8_t>& stored, const Game& game,
                  const std::string& contentName, const std::string& profile,
                  std::vector<cab::ZipFile>* out, std::string* why);

}  // namespace switchsave
