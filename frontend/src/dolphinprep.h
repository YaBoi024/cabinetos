// What Dolphin needs before a GameCube or Wii game boots: its configuration,
// a path it will open, and one shader cache per console; and the RetroArch
// cheat files it once wrote, removed at start-up. Moved out of main.cpp in the
// restructure (#398).

#pragma once

#include <string>

void shareDolphinCache(const std::string& saveDir);
void clearDolphinCheats();
std::string dolphinReadablePath(const std::string& romPath, int romId);
void writeDolphinConfig(const std::string& saveDir);
