// FORK (handheld): the saved game two players share in Play together
// (netplaylive.h). The game is run in a folder of its own on each console, so
// the player's solo saves are never read or changed by it; this file is what
// makes the saves in the two folders the same before the first frame, and
// says when they are not.
//
// What counts as the shared saved game: the battery files this frontend
// writes at the top of the folder (.srm, and .rtc for a clock). Nothing else
// in the folder is part of it: configuration and shader caches stay each
// console's own.
//
// The host's saved game is the one both play on. When the two fingerprints
// differ, the host's files are sent (netplay.h) and `unpack` puts them in the
// joiner's folder, the joiner's own set moved to `previous-together/` first so
// nothing the joiner had is lost.
//
// Bytes from the network are untrusted: `unpack` accepts only the names this
// file would itself have listed, with no folders, no `..` and a size limit.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace netplaysave {

// A short fingerprint of the shared saved game in `dir`, "" when there is none.
std::string fingerprint(const std::string& dir);

// Every file of it in one piece, for sending. Empty when there is none.
std::vector<uint8_t> pack(const std::string& dir);

// Whether `dir` holds a battery file (.srm) of the shared saved game.
bool hasBattery(const std::string& dir);

// What a blob held, for the caller to put into a running game: the battery
// file's name ("" for none).
struct Unpacked {
    std::string battery;
};

// Replaces the shared saved game in `dir` with `blob` (from the other side's
// pack). An empty blob replaces it with none. False, with the reason, when
// the blob is not something pack would have made; nothing is changed then.
bool unpack(const std::string& dir, const std::vector<uint8_t>& blob, std::string* why,
            Unpacked* what = nullptr);

// --netplay-save-test: every case, in a folder under /tmp. 0 when all pass.
int test();

}  // namespace netplaysave
