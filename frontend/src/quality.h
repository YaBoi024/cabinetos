// Picture quality: Performance, Balanced or Quality, and what each emulator is
// told at each level. Issue #63, phase 1; the decisions and the measurements
// behind every value are in docs/PROJECT.md, "the automatic quality design"
// and the #63 decisions of 2026-10-02.
//
// ONE PLACE ANSWERS. Every emulator asks this file for its picture settings
// at launch and nothing else knows the levels exist. Settings are written
// fresh at every launch and nothing is synced, so the whole feature comes out
// in one ordinary update if it has to.
//
// THE DIAL MOVES TWO THINGS: the internal resolution, and on the emulators
// that offer it, whether shaders are built in the background (Balanced,
// Quality) or when first needed, with a short stutter (Performance). On the
// DS the resolution comes with the renderer: its software one, at its own
// size, at Performance, and its OpenGL one above (#219).
// Everything else that is free is fixed at its best value for every level
// (anisotropic filtering 16x, 3DO's high resolution, sharp vector lines), and
// nothing here needs a file from anywhere.
//
// THE LEVEL FOR A GAME is the game's own choice if the player made one in its
// pause menu, otherwise the console's. A game's choice survives a change of
// the console's (MMagTech, 2026-10-02). FORK (handheld): Performance while a
// stream is open (remoteplay::sessionOpen), whatever either says.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace quality {

enum class Level { Performance, Balanced, Quality };
constexpr int kLevelCount = 3;

// "Performance", for the screen.
const char* levelName(Level l);
// "performance", for the settings file: a word survives retuning.
const char* levelWord(Level l);
bool levelFromWord(const std::string& word, Level* out);

// The console's level: the dial's setting once a person has moved it,
// otherwise this machine's class. Never changed by the console after that.
Level console();
void setConsole(Level l);

// THE MACHINE CLASS (#63 phase 2, MMagTech 2026-10-04): the level this
// machine starts on, from its graphics chip alone, worked out at every start
// so a new graphics card is noticed. Shader units x top clock, against the
// A9 (16 units at 2.9 GHz, 46): 40 and up Quality, 20 and up Balanced,
// below that Performance. Hardware that does not report both (Intel and
// NVIDIA today, any machine without Vulkan) gets Performance and is raised
// with the dial. The processor is logged and does not count: the dial moves
// only what the graphics chip draws.
Level machineClass();

// A game's own choice from its pause menu, if it has one. Kept on this
// console only, never synced.
std::optional<Level> gameChoice(int romId);
void setGameChoice(int romId, std::optional<Level> l);

// What a game runs at: its own choice, else the console's.
Level forGame(int romId);

// Whether a level changes anything for `core` (its manifest name): the
// systems with a resolution to move, the DS only where its OpenGL renderer
// can run (Core::canServeDesktopGL, #219). Only these get the pause menu's
// row; Wii U (Cemu) has no lever without resolution packs (#207), Xbox 360 is
// 720p at every level since the A9 measured it (2026-10-04), and 3DO and the
// vector systems are fixed at every level.
bool hasLevels(const std::string& core);

// --- What each emulator is told ----------------------------------------------

// Libretro option values for `core` (the manifest name, e.g. "dolphin") on
// `platformSlug` (GameCube and Wii share Dolphin and differ at Quality).
// Applied on top of catalog::optionOverrides.
std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, Level l);
// The same, saying whether there is Vulkan and whether a core can have
// desktop OpenGL rather than asking the machine, so the option check
// (optcheck.h) can test both N64 renderers and both DS renderers on any
// machine.
std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, Level l,
                                               bool vulkan, bool desktopGL);

struct Ps2 {
    float upscale = 1.0f;
    int anisotropy = 0;
};
Ps2 ps2(Level l);

// One `[section] key=value` for an ini-style file.
struct Setting {
    std::string section, key, value;
};
// Eden's qt-config.ini.
std::vector<Setting> eden(Level l);
// RPCS3's config.yml: a whole `Video:` block, appended to the console's own.
std::string rpcs3(Level l);
// xemu's xemu.toml: lines for its `[display.quality]` table.
std::string xemu(Level l);
// Xenia Edge's command line.
std::vector<std::string> xenia(Level l);
// Vita3K's config.yml: lines for the whole file standalone_vita3k.cpp writes.
std::string vita3k(Level l);
// sm2-emu's render_scale, which standalone_sm2emu.cpp writes into its settings.
int sm2emu(Level l);
// Supermodel's picture height, which standalone_supermodel.cpp caps at the
// screen's and writes into its settings with the width in the screen's shape.
int supermodelLines(Level l);

// One line on stderr saying what was applied, so a log shows the level a game
// actually ran at.
void logApplied(const std::string& who, int romId, Level l);

}  // namespace quality
