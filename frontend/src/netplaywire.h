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

constexpr uint16_t kVersion = 1;
constexpr size_t kMaxMessage = 512;   // the type byte and the body
constexpr size_t kMaxText = 100;      // a name, a version or a reason

enum class Type : uint8_t {
    Hello = 1,    // joiner to host, first: the code and what it is running
    Accept = 2,   // host to joiner: the delay and the number of players
    Refuse = 3,   // either way: why not
    Inputs = 4,   // one player's buttons for one frame
    Digest = 5,   // a fingerprint of one frame, to catch a drift
    Bye = 6,      // leaving
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
    // Hello, Accept
    uint8_t players = 2;
    // Accept
    uint8_t delay = 0;            // frames
    // Refuse
    std::string reason;
    // Inputs, Digest
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
