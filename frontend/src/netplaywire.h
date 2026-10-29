// FORK (handheld): what two consoles say to each other in a netplay session
// (NETPLAY-PLAN.md, 6.6), and the parser that reads it. No sockets and no
// SDL here, so --netplay-wire-test can walk every case on its own.
//
// A message is a length (2 bytes, big endian), a type (1 byte) and a body.
// The length counts the type and the body and is never more than kMaxMessage.
// The parser trusts nothing: a length, a type or a body that is not exactly
// what the type allows fails the parser for good, and nothing after it is read.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace netplaywire {

constexpr uint16_t kVersion = 3;
constexpr size_t kMaxMessage = 512;   // the type byte and the body
constexpr size_t kMaxText = 100;      // a name, a version or a reason
constexpr size_t kMaxSaveChunk = 400; // bytes of a saved game in one message
constexpr uint32_t kMaxSave = 16u * 1024 * 1024;   // a saved game, whole
constexpr int kMaxLead = 8;           // frames
constexpr int kMaxPlayers = 8;        // slots in one session: the host is 0, guests 1 and up

enum class Type : uint8_t {
    Hello = 1,    // joiner to host, first: the code and what it is running
    Accept = 2,   // host to joiner: the delay and the number of players
    Refuse = 3,   // either way: why not
    Inputs = 4,   // one player's buttons for one frame
    Digest = 5,   // a fingerprint of one frame, to catch a drift
    Bye = 6,      // leaving
    Lead = 7,     // either way: "send your buttons at least this many frames ahead"
    Save = 8,     // host to joiner after Accept: a piece of the saved game they will share
    Start = 9,    // host to each guest, once, when the host begins: how many play, and which one you are
    Roster = 10,  // host to guests while they wait: how many are here
    Left = 11,    // host to guests: a player has gone, their buttons are neutral from `frame` on
};

// One player's controller for one frame. Sticks are -32767..32767, triggers 0..255.
struct Pad {
    uint16_t buttons = 0;
    int16_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    uint8_t leftTrigger = 0, rightTrigger = 0;
    bool operator==(const Pad&) const = default;
};

struct Message {
    Type type = Type::Bye;
    // Hello
    uint16_t version = kVersion;
    uint16_t code = 0;            // the four digits on the host's screen
    std::string core, coreVersion, gameHash;   // gameHash: a hash of the game file
    // Hello (1: one player from this console), Accept (the most the host allows),
    // Start (how many play), Roster (how many are here, the host included)
    uint8_t players = 2;
    std::string saveHash;         // a fingerprint of the shared saved game this side holds, "" for none
    // Start
    uint8_t delay = 0;            // frames
    // Accept
    uint8_t sync = 0;             // 1: the host's saved game follows, as Save messages
    uint32_t saveSize = 0;        // its size in bytes, when sync is 1
    // Lead
    uint8_t lead = 0;             // frames
    // Save
    uint32_t saveOffset = 0;      // where in the saved game this piece goes
    std::vector<uint8_t> saveBytes;
    // Refuse
    std::string reason;
    // Inputs, Digest, Start, Left: the slot concerned (0 is the host)
    uint8_t player = 0;
    // Inputs, Digest, Left
    uint32_t frame = 0;
    Pad pad;
    uint64_t digest = 0;
};

// The bytes of a message, or nothing when it does not fit the format (a text
// longer than kMaxText).
std::vector<uint8_t> encode(const Message& m);

class Parser {
public:
    // Bytes as they arrive, in pieces of any size.
    void feed(const uint8_t* data, size_t n);
    // The next whole message, or false when there is none yet (or the parser has failed).
    bool next(Message& out);
    bool failed() const { return failed_; }
    const std::string& error() const { return error_; }

private:
    void fail(const char* why);
    std::vector<uint8_t> buf_;
    bool failed_ = false;
    std::string error_;
};

// --netplay-wire-test: every case, no network. 0 when all pass.
int test();

}  // namespace netplaywire
