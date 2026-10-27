// What Dolphin needs before a GameCube or Wii game boots: its configuration,
// a path it will open, and one shader cache per console; and the RetroArch
// cheat files it once wrote, removed at start-up. Moved out of main.cpp in the
// restructure (#398).

#pragma once

#include <string>

void shareDolphinCache(const std::string& saveDir);
void clearDolphinCheats();
std::string dolphinReadablePath(const std::string& romPath, int romId);
// `together`: a game of Play together (netplaylive.h), which both consoles
// must run exactly alike: one emulation thread and the same clock at power on.
void writeDolphinConfig(const std::string& saveDir, bool together = false);
