// Does every option value the console sets exist in the core it is set on?
//
// THE OTHER HALF OF THE VERSION-BUMP CHECK (#63, MMagTech 2026-10-02).
// cores/check-options.sh fails when a core's options differ from cores/options/<core>.txt;
// this fails when the console's own tables (catalog::optionOverrides with and
// without Vulkan and for both sensor bar places, quality::coreOptions at every
// level and on both N64 renderers and both DS renderers, every choice of every
// sysopts row, and
// wii::coreOptions and vectrexsheets::coreOptions both ways) name a key a core does not declare, or a value
// it does not accept, according to those same files; and when a libretro core
// the catalog plays a system on has no file there (catalog::libretroCores,
// which also gives each core's systems). Run by CI on every frontend build,
// with no core and no GPU:
//
//   cabinetos-frontend --check-option-tables cores/options
//
// Exits 0 when everything fits, 1 with one line per misfit otherwise.
//
// PCSX2 is not here: it runs in the console's own host, which takes no core
// options (coreoptions.cpp's header), and has no list in cores/options/.

#pragma once

#include <string>

namespace optcheck {

int run(const std::string& dir);

}  // namespace optcheck
