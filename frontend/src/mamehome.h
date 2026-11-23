// MAME 0.289's home folder, set up before each of its games: its plugins,
// the ini that starts its high-score plugin, and this person's high scores
// (docs/decisions/2026-10-28-prebuilt-mame-core.md).
//
// LIBRETRO MAME SPLITS ITS FOLDERS IN TWO (Set_Path_Option in
// src/osd/libretro/libretro-internal/retro_init.cpp at our pin): cfg, diff,
// nvram, input, states and snaps go under `<save folder>/mame/`, which is
// this person's and this game's (launch.cpp, loadCore); its home, ini,
// plugins, artwork and the rest under `<system folder>/mame/`, which is the
// console's one `bios/`, everybody's. So, before each game:
//   * `bios/mame/plugins` is a link to the plugins the image carries beside
//     the core, `<core folder>/mame/plugins`: boot.lua, json and hiscore,
//     all from the core's own commit (cores/fetch-mame.sh). Without boot.lua
//     and json the high-score plugin never starts (measured on the A9,
//     2026-10-28).
//   * `bios/mame/ini/mame.ini` says `plugin hiscore`, which MAME reads with
//     mame_read_config on (coreoptions.cpp). Written when it says otherwise.
//   * `bios/mame/hiscore`, where the plugin keeps every game's scores (its
//     homepath and /hiscore, plugins/hiscore/init.lua), is a link to
//     `mame/hiscore` in this person's save folder for the game, made again
//     at every start, or everybody would share one table.
// MAME writes its NVRAM, settings and scores in retro_deinit, which the
// console calls as a game is left (play.cpp, finishExit). Both folders are
// asserted at our pin (cores/fetch-mame.sh).

#pragma once

#include <string>

namespace mamehome {

// Before a MAME 0.289 game loads: `saveDir` is this person's save folder for
// it, `coreDir` the folder the core is in, `biosDir` the console's system
// folder. What cannot be made is said in the log, and the game plays without
// high scores.
void prepare(const std::string& saveDir, const std::string& coreDir, const std::string& biosDir);

}  // namespace mamehome
