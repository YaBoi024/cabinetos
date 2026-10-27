// FORK (handheld): a game played in lockstep with another copy of this
// program, with no screen and no menus (NETPLAY-PLAN.md, phase 1). One copy
// hosts and one joins, on the same machine or two; both run the same scripted
// buttons for their own player, and both print the same final digest when the
// session holds. Started by --netplay-host <port> and --netplay-join <port>
// (--netplay-address picks the address; 127.0.0.1 otherwise), from coretests.cpp.

#pragma once

#include <string>

namespace cab {
class Core;
}

namespace netplayrun {

struct Options {
    bool host = false;
    int port = 0;
    std::string address = "127.0.0.1";
    std::string romPath;   // hashed, to be sure both sides have the same game
};

// Returns the process's exit code: 0 when the whole run held in step.
int run(cab::Core& core, const Options& o);

}  // namespace netplayrun
