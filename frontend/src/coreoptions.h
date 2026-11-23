// The core options this console sets on purpose (optionOverrides); every
// option it does not name is answered with the core's own default.
// catalog.h includes this; moved out of it in the restructure (#398).

#pragma once

#include <map>
#include <string>

namespace catalog {

// Deliberate core-option choices, keyed by the core's file or manifest name.
//
// EMPTY IS THE CORRECT STARTING POINT AND IT IS NOT THE OLD BEHAVIOUR. With no
// overrides at all, every option a core declares is still answered — with the
// core's own stated default. That alone fixes the thing that was actually
// broken: an unanswered option is not the default, it is the zero the C global
// was initialised to. See core.h.
//
// This is where a choice goes when CabinetOS wants something OTHER than what a
// core ships with. Cabinet hand-picked a subset per platform rather than dumping
// everything a core reports (docs/history/CABINET.md, `NativeCoreOptions.swift`),
// and that list is the obvious thing to bring across, one platform at a time,
// with a reason recorded for each.
std::map<std::string, std::string> optionOverrides(const std::string& coreName);
// The same, told whether there is Vulkan (the N64's renderer) and where the
// sensor bar is rather than asking the GPU and the settings, so the option
// check (optcheck.h) can test every answer on any machine.
std::map<std::string, std::string> optionOverrides(const std::string& coreName, bool vulkan,
                                                   bool sensorBarAbove);

}  // namespace catalog
