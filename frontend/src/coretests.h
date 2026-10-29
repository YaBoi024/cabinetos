// --audio-probe, --speed-test and --state-test: a core and a ROM named on the
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
};

std::optional<int> coreTest(cab::Core& core, const CoreTestOptions& o);
