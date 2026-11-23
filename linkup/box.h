// Sealed boxes, PROTOCOL.md's "Between two consoles": what the server only carries, an invite's
// game and a game's connection details, readable by the two consoles alone. Noise_K, one way,
// bound to one invite. Noise_K has no protection against a box being sent twice, so each box
// also says who it is from and to, how long it lasts and which it is in a count, and a reader
// drops one that does not fit.
//
// Everything a friend's console or the server can say ends up on a television, and the server
// cannot filter what it cannot read, so names are cleaned here on the way in.

#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <utility>

#include "noise.h"

namespace box {

// `fixedEphemeral` is only for the tests.
bool seal(const noise::KeyPair& me, const noise::Key& peer, const std::string& invite, const noise::Bytes& plain,
          noise::Bytes& out, const noise::KeyPair* fixedEphemeral = nullptr);
bool open(const noise::KeyPair& me, const noise::Key& peer, const std::string& invite, const noise::Bytes& sealed,
          noise::Bytes& plain);

// Control and direction-changing characters out, broken UTF-8 out, and cut at `maxBytes` on a
// whole character. What a person reads of anything that came over the network passes through this.
std::string cleanText(const std::string& text, size_t maxBytes);

constexpr size_t kMaxGameName = 120;
constexpr size_t kMaxUsername = 128;
constexpr size_t kMaxBoxInvite = 2 * 1024;
constexpr size_t kMaxBoxSignal = 16 * 1024;

struct Contents {
    std::string type;       // "invite" or "signal"
    uint64_t n = 0;
    int64_t expires = 0;
    std::string from, to;
    int stream = 0;         // an invite's
    std::string game;       // an invite's, cleaned
    std::string details;    // a signal's: JSON text, for whoever made it
};

// Makes a box's text. `n` counts up per sender and invite.
std::string write(const Contents& c);

// Reads what one sender says about one invite, and remembers each `n`.
class Reader {
public:
    // False for anything that does not fit: not JSON, another type, a count already seen, past
    // `expires`, or not from and to the people expected.
    bool read(const noise::Bytes& plain, const std::string& wantType, const std::string& wantFrom,
              const std::string& wantTo, int64_t now, Contents& out);

private:
    std::set<uint64_t> seen_;
};

// The console's own side of the same rule: each console keeps its own count per invite.
struct Counter {
    uint64_t next = 1;
    uint64_t take() { return next++; }
};

}  // namespace box
