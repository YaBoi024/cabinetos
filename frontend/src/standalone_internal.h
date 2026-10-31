// What the standalone emulators' files share: the helpers, defined in
// standalone.cpp, and each emulator's own entry points, defined in its file.
// Internal: only standalone.cpp, standalone_eden.cpp, standalone_rpcs3.cpp,
// standalone_xenia.cpp, standalone_cemu.cpp, standalone_xemu.cpp,
// standalone_vita3k.cpp, standalone_sm2emu.cpp and standalone_supermodel.cpp
// include it.
// Moved out of standalone.cpp's anonymous namespace in the restructure (#398).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "standalone.h"

namespace cab::standalone::detail {

// Where a game's folder records what was installed into it and deleted.
constexpr const char* kInstalledNote = ".cabinetos-installed";
// The note a folder holds from the start of a game until its saves are sent
// (beforeStart): a game's folder for xemu, a person's for the others, and a
// person's for Xbox saves another person's start brought off the drive for
// them (App::recoverDriveSaves). cache.cpp spells it out as well, to leave a
// game's folder holding one alone.
constexpr const char* kPlayingNote = ".cabinetos-playing";
// The Vita storage inside an installed Vita game's folder, which holds the
// game at ux0/app/<title ID> (standalone_vita3k.cpp; bootPath finds it).
constexpr const char* kVitaGameFs = "vita";
// RPCS3's one user, as it names the folder. The console's people are told
// apart by whose save folder `hdd0` is, not by PS3 users.
constexpr const char* kPs3User = "00000001";

// Defined in standalone.cpp, each with what it does and why.
std::string binaryOf(const Emulator& e);
int64_t nowMs();
bool isDir(const std::string& p);
std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& data);
bool placeFile(const std::string& from, const std::string& to);
struct IniSet {
    const char* section;
    const char* key;
    const char* value;
};
std::string applyIni(const std::string& text, const std::vector<IniSet>& sets,
                     bool withDefaults = true);
bool isFile(const std::string& p);
bool removeTree(const std::string& path);
std::vector<std::string> listDir(const std::string& dir);
bool endsWithNoCase(const std::string& s, const char* suffix);
std::string yamlQuote(const std::string& v);
bool placeLink(const std::string& target, const std::string& link);
std::string arcadeSet(const std::string& romPath);

// Switch (standalone_eden.cpp).
bool prepareEden(const Emulator& e, const std::string& saveDir, const std::string& player,
                 int players, quality::Level level, bool* missingKeys, std::string* err);
std::string edenSaveRoot(const Emulator& e, const std::string& saveDir);

// PlayStation 3 (standalone_rpcs3.cpp).
bool prepareRpcs3(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                  const std::string& player, int players, quality::Level level, bool* missing,
                  std::string* err);
bool installRpcs3(const Emulator& e, const std::string& entryPath,
                  const std::atomic<bool>& cancel, std::string* romPath, std::string* message);

// Xbox 360 (standalone_xenia.cpp).
std::string xeniaContent(const std::string& saveDir);
bool prepareXenia(const Emulator& e, const std::string& saveDir, std::string* err);
bool xeniaBeforeStart(const std::string& saveDir, const std::string& player, std::string* err);
// Run::start's arguments for Edge. `full` is Run::start's command so far, the
// environment entries ending with the program: every environment entry goes in
// before the program (full.end() - 1), and the game's path is last.
void xeniaArgs(std::vector<std::string>& full, const std::string& dir, const std::string& saveDir,
               const std::string& logPath, quality::Level level, const std::string& romPath);

// Wii U (standalone_cemu.cpp).
std::string cemuMlc(const std::string& saveDir);
bool prepareCemu(const Emulator& e, const std::string& saveDir, int players, std::string* err);
// Run::start's arguments for Cemu, as for xeniaArgs: the environment entries
// before the program, then `-g`, the game's path and `-f` after it.
void cemuArgs(std::vector<std::string>& full, const std::string& dir, const std::string& romPath);

// PlayStation Vita (standalone_vita3k.cpp).
std::string vita3kSaveRoot(const std::string& saveDir);
bool prepareVita3k(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                   const std::string& player, int players, quality::Level level, bool* missing,
                   std::string* err);
bool installVita3k(const Emulator& e, const std::string& entryPath,
                   const std::atomic<bool>& cancel, std::string* romPath, std::string* message);
// Run::start's arguments for Vita3K, as for xeniaArgs: the environment entries
// before the program, then the game, by its title ID, after it.
void vita3kArgs(std::vector<std::string>& full, const Emulator& e, const std::string& dir,
                const std::string& romPath);

// Sega Model 2 (standalone_sm2emu.cpp).
std::string sm2emuNvram(const std::string& saveDir);
bool prepareSm2emu(const Emulator& e, const std::string& romPath, const std::string& saveDir,
                   int players, quality::Level level, std::string* err);
// Run::start's arguments for sm2-emu, as for xeniaArgs: the environment
// entries before the program, then its folders, the set and the game's zip.
void sm2emuArgs(std::vector<std::string>& full, const std::string& dir,
                const std::string& saveDir, const std::string& romPath);

// Sega Model 3 (standalone_supermodel.cpp).
std::string supermodelNvram(const std::string& saveDir);
bool prepareSupermodel(const Emulator& e, const std::string& romPath, const std::string& saveDir,
                       int players, quality::Level level, std::string* err);
// Run::start's arguments for Supermodel, as for sm2emuArgs, and before them
// its working folder, bwrap's --chdir after "bwrap".
void supermodelArgs(std::vector<std::string>& full, const Emulator& e, const std::string& dir,
                    const std::string& romPath);

// Xbox (standalone_xemu.cpp).
bool prepareXemu(const Emulator& e, const std::string& entryPath, int players,
                 quality::Level level, bool* missing, std::string* err);
bool installXbox(const std::string& entryPath, const std::atomic<bool>& cancel,
                 std::string* romPath, std::string* message);
bool xemuBeforeStart(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                     const std::string& note, std::string* err);
bool xemuAfterEnd(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                  std::string* err);

}  // namespace cab::standalone::detail
