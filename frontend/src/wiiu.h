// Which Wii U games play on an ordinary pad.
//
// THE RULE, MMagTech 2026-10-01 (docs/PROJECT.md open question 36), Wii's rule
// carried over: a Wii U game plays on a pad when GameTDB says it takes a Pro
// Controller or a Classic Controller. A game that takes a Wii Remote and
// neither of those needs a Wii Remote and is greyed for good, "Needs a Wii
// Remote" (#231, closed not planned). Everything else was made for the
// GamePad, its second screen and its touchscreen, which nothing can provide:
// greyed for good, "Needs a Wii U GamePad". Captain Toad: Treasure Tracker is
// one. One rule, no per-game exceptions. Player 1's pad is a GamePad to Cemu
// and every other player's a Pro Controller (MMagTech, 2026-10-22;
// standalone_cemu.cpp, cemuProfile), which brings the GamePad's buttons only.
//
// WHO KNOWS WHAT. GameTDB lists a game's controllers under its product code
// (`BWPE01` for Hyrule Warriors on disc, `WKNE` for Shovel Knight from the
// eShop); the console ships its list as /usr/share/cabinetos/wiiu-controls.txt,
// made by tools/wiiu-controls.py. RomM knows the title ID (`1017D800`) but not
// the product code, and GameTDB lists no title IDs. The product code is in the
// game itself, in its `meta/meta.xml` (`WUP-P-BWPE`), so the console reads it
// there: from the END of a `.wua`, where its table of contents is, in a few
// small HTTP ranges, without downloading the game. Measured on Hyrule Warriors
// (7.65 GB, base game, update and DLC): five reads, 434 KB.
//
// A game whose code cannot be read, or whose code GameTDB does not list, needs
// the GamePad. That is the safe side, as it is for Wii: nothing says it takes a
// pad.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wiiu {

// `bytes` of the game's file from `offset`; empty on any failure.
using ReadFn = std::function<std::vector<uint8_t>(uint64_t offset, size_t bytes)>;

// The four letters of the base game's product code (`BWPE`) in a `.wua` of
// `size` bytes, read through `read`. A `.wua` is a ZArchive (Exzap/ZArchive):
// a footer at the end names its file tree and name table, the files are
// zstd-compressed in 64 KiB blocks, and its top folders are one per title,
// `000500001017d800_v0` for the game, `0005000e...` for its update,
// `0005000c...` for DLC. Empty when it is not a `.wua` or cannot be read.
std::string codeFromWua(uint64_t size, const ReadFn& read);

// The four letters out of a meta.xml's `<product_code>` (`WUP-P-BWPE`).
std::string codeFromMeta(const std::string& xml);

// What the console has already read, per RomM rom id, so a file is read once.
// An empty answer is one (nothing could be read), as for Wii.
std::string rememberedCode(int romId, bool* known);
void rememberCode(int romId, const std::string& code);

// GameTDB's letters for a code (tools/wiiu-controls.py: p Pro Controller,
// c Classic Controller, w Wii Remote, n Nunchuk, g GamePad ...). Every listed
// ID that starts with the code: a disc is listed as BWPE01, its code is BWPE.
std::string controls(const std::string& code);

enum class Needs { Nothing, WiiRemote, GamePad };
Needs needs(const std::string& code);

}  // namespace wiiu
