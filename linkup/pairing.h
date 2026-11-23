// Pairing: an address and a code in, the server's key remembered. PROTOCOL.md's "Pairing".

#pragma once

#include <string>

#include "keys.h"

namespace pairing {

enum class Result {
    Paired,
    WrongCode,        // the server said no waiting code matches
    TooManyTries,     // the server's rate limit
    Unreachable,      // nothing answered there, or not like a LinkUp server
    NotThatServer,    // something answered and could not prove it holds the code: a typo or a fake
    Refused,          // the code fitted and the server still refused this console
    Failed,           // this console could not do its side
};

// Twelve characters from what was typed, upper case with anything else out; empty when that
// does not leave twelve. A reader accepts more letters than a code is drawn from (PROTOCOL.md).
std::string tidy(const std::string& typed);

// Blocks for the slow hash and up to three requests: never on the main loop. `joining` is
// message 3's JSON. On Paired, `keys` holds the server. `serverName` comes back for the screen.
Result pair(Keys& keys, const std::string& address, const std::string& code, const std::string& joining,
            std::string& serverName);

}  // namespace pairing
