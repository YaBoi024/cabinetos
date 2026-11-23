// Which Wii games play on an ordinary pad, and as what.
//
// THE RULE, MMagTech 2026-09-30 (docs/PROJECT.md open question 35): a Wii game
// plays on a pad when it accepts a Classic Controller or a GameCube pad. Every
// other Wii game was made for the Wii Remote, needs a real one, and is greyed
// out until one is paired (#200). One rule, no per-game exceptions: Kirby's
// Return to Dream Land would play well on a pad held like a sideways Remote and
// is greyed out anyway, because nothing in the data tells it apart from a
// pointer or motion game.
//
// WHO KNOWS WHAT. RomM reads each game's code off the file (`title_id`: the
// disc's four letters, or a WiiWare title's full sixteen hex digits). GameTDB
// lists the controllers each code accepts; the console ships its list as
// /usr/share/cabinetos/wii-controls.txt, made by tools/wii-controls.py. Dolphin
// maps a pad onto whichever controller it is told is plugged in; nobody maps
// buttons per game.
//
// A game with no code, or a code GameTDB does not list, needs a Remote. That is
// the safe side and it is the rule as stated: nothing says the game takes a pad.

#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace wii {

// The four letters a game is known by — `SUKE` for Kirby's Return to Dream
// Land — from RomM's `title_id`, which is hex: eight digits for a disc
// (`53554B45`), sixteen for a WiiWare or Virtual Console title, whose last
// eight are the same four letters (`0001000157414C45`, `WALE`). Empty when
// RomM has none or it is not four printable letters.
std::string codeFromTitleId(const std::string& hex);

// THE SAME FOUR LETTERS READ OFF THE FILE ITSELF, for a game RomM has no
// `title_id` for: a RomM before 5.3, a library not rescanned since, or a
// container RomM does not read (WIA, CISO). MMagTech, 2026-09-30: the console
// does not depend on RomM for what it can check itself, as for Switch.
//
// `head` is the start of the file; 64 KB covers every case. Read where each
// format keeps the disc header (a plain disc at 0, RVZ and WIA at 0x58, WBFS
// one sector in, CISO at 0x8000) and checked against the Wii's own magic; a
// WAD's title is in its ticket. GCZ is compressed and cannot be read this
// way, and a GameCube disc is not a Wii game: both come back empty.
std::string codeFromHeader(const unsigned char* head, size_t size);
constexpr size_t kHeaderBytes = 64 * 1024;

// A GAMECUBE GAME'S FOUR LETTERS, the same way, for one RomM has no title_id
// for (#484, ROMM-07): without them its region, and so the folder Dolphin
// keeps its card in, is unknown, and its saves would stay on this console.
// The disc header's magic is GameCube's (0xC2339F3D at 0x1C), in the same
// containers. Remembered with the Wii's codes (rememberedCode, below).
std::string gameCubeCodeFromHeader(const unsigned char* head, size_t size);

// What the console has already read, per RomM rom id, so a file is read once.
// `*known` says whether there is an answer at all; an empty answer is one (the
// file had no code that could be read), so it is not asked again.
std::string rememberedCode(int romId, bool* known);
void rememberCode(int romId, const std::string& code);

// A code back as RomM would send it, eight hex digits, so a game read here and
// a game RomM read look the same to everything after.
std::string titleIdOf(const std::string& code);

// GameTDB's letters for a code (tools/wii-controls.py names them: c Classic
// Controller, g GameCube pad, w Wii Remote, n Nunchuk ...). A four-letter code
// gathers every listed ID that starts with it: a disc is listed as SUKE01, and
// RomM only has SUKE. Empty when the code is not listed.
std::string controls(const std::string& code);

// What Dolphin is told is plugged into each player's port for this game, as
// its libretro device id: the Classic Controller when the game takes one, the
// GameCube pad when it takes only that, and 0 when it takes neither and needs
// a Wii Remote.
unsigned padDevice(const std::string& code);

// Whether GameTDB says the game requires MotionPlus (`M`; an optional one is
// `m`). Such a game plays only on a Remote whose MotionPlus works here
// (wiiremote::anyWithMotionPlus; MMagTech, 2026-10-22).
bool needsMotionPlus(const std::string& code);

// Dolphin's own ids (DolphinLibretro/Input.cpp at the pinned commit).
constexpr unsigned kClassicController = (4u << 8) | 1u;   // RETRO_DEVICE_WIIMOTE_CC, 1025
constexpr unsigned kGameCubePad = (6u << 8) | 1u;         // RETRO_DEVICE_GC_ON_WII, 1537

// --- Miis (#275) ------------------------------------------------------------
//
// THE MII CHANNEL IS AN ORDINARY ENTRY: its WAD alone on RomM, booted as any
// WiiWare title is. Its title is the same in every region, 00010002-48414341
// ("HACA"). Its NAND holds the Wii's Mii list, `RFL_DB.dat`, where desktop
// Dolphin keeps it (Common::GetMiiDatabasePath), and that file is the master
// copy of a person's Miis: it travels to RomM in the Mii Channel's own save
// (catalog::directorySaveRoot) and is copied into every other Wii game's NAND
// at launch. A game's copy is never uploaded, so Miis are made in the Mii
// Channel, as the Wii's own games expect.
constexpr const char* kMiiChannelTitleId = "0001000248414341";
bool isMiiChannel(const std::string& titleId);
// Relative to Dolphin's `User/Wii`.
constexpr const char* kMiiDatabase = "shared2/menu/FaceLib/RFL_DB.dat";

// --- Rumble (#274) ----------------------------------------------------------
//
// THE WII'S OWN RUMBLE SWITCH, IN THE HOME MENU, KEPT ACROSS GAMES. A Wii keeps
// it in SYSCONF (`BT.MOT`), one file for the console. Here every game has its
// own NAND, and the core overwrites `BT.MOT` at every boot from its option
// `dolphin_enable_rumble` (DolphinLibretro/Boot.cpp, SYSCONF_WIIMOTE_MOTOR).
// So when a Wii game closes the console reads `BT.MOT` back from that game's
// SYSCONF (Dolphin keeps a guest's change on a clean exit: WiiRoot.cpp,
// RestoreWiiSettings) and starts every later Wii game with the option to
// match. The option also sets the rumble of pads standing in for Remotes and
// of GameCube pads in Wii games (DolphinLibretro/Input.cpp), so off is off
// for every controller in every Wii game (MMagTech, 2026-10-06). Console-wide,
// as on a Wii. GameCube games are not touched.
//
// `BT.MOT` from a SYSCONF file: 1 on, 0 off, -1 when the file or the entry is
// not there.
int readMotor(const std::string& sysconfPath);
// THE ONE RULE, for the launch and the option check (optcheck.h): a Wii game
// on Dolphin is given `dolphin_enable_rumble` as the console last read it;
// every other game, nothing. Applied last, on top of the other options. The
// second form is told the switch, so the check can test both words.
std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug);
std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, bool rumbleOn);
// Reads a closed game's SYSCONF and keeps what it says.
void keepRumbleFrom(const std::string& sysconfPath);

}  // namespace wii
