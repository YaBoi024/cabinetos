// --audio-probe, --speed-test, --state-test and --netplay-test: a core and a ROM named on the
// command line, measured with the game loaded, after SDL, the window, GL and
// first run, because a hardware-rendered core needs the GL context. Returns
// the exit code when a test ran, nothing when none was asked for;
// --speed-test leaves the process from inside (std::_Exit), on purpose. Moved
// out of main.cpp in the restructure (#398).

#pragma once

#include <optional>

namespace cab {
class Core;
}

struct CoreTestOptions {
    bool audioProbe = false;
    bool speedTest = false;
    bool stateTest = false;
    bool netplayTest = false;   // FORK (handheld): the determinism spike (NETPLAY-PLAN.md, phase 0)
    // FORK (handheld): a lockstep run with another copy (netplayrun.h): 0 none, 1 host, 2 join.
    int netplayRole = 0;
    int netplayPort = 0;
    const char* netplayAddress = nullptr;
    const char* romPath = nullptr;
};

std::optional<int> coreTest(cab::Core& core, const CoreTestOptions& o);
