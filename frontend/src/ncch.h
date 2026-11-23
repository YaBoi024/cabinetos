// Whether a Nintendo 3DS file is one Azahar can play, read from its own bytes
// before the core is handed it (docs/PROJECT.md, open question 50).
//
// Azahar plays decrypted games only: it removed encrypted-game support in
// 2025, and its loader refuses an NCCH whose NoCrypto flag is clear
// (core/file_sys/ncch_container.cpp). It loads no .cia either: a CIA is an
// installer, and the core installs nothing. But the core says why only
// through SET_MESSAGE, which the host does not answer, so a refused game
// would read as "Couldn't start this game". The console looks first and says
// the real reason, as a PS2 disc that will not run gets "Disc can't be read".
//
// By the bytes, never the name (docs/lessons/emulators.md): a cartridge dump
// (NCSD, `.3ds`/`.cci`) is judged by its first partition's NCCH header, a
// `.cxi` by its own, and Azahar's compressed form (Z3DS, `.zcci`/`.zcxi`) by
// the same header inside its first zstd frame. A CIA is known by its header's
// fixed sizes, or by the name a Z3DS file gives what it holds. Anything this
// cannot read goes to the core, which has the last word.

#pragma once

#include <string>

namespace ncch {

// Empty when the file can go to the core; otherwise the words the game's
// screen shows.
std::string refusal(const std::string& path);

// Whether a file's NAME says it is a CIA: `.cia`, Azahar's compressed
// `.zcia`, or an archive named for one (`.cia.7z`, `.cia.zip`). For what the
// console LISTS, which has only RomM's names to go on (MMagTech, 2026-10-20:
// a 3DS game that is a CIA shows nowhere, catalog::coverageFor); what it plays is
// still judged by the bytes, above.
bool namesCia(const std::string& fileName);

}  // namespace ncch
