// PlayStation 2, which is an emulator rather than a libretro core.
//
// WHAT THIS IS AND WHY IT IS NOT IN core.cpp. Most of this console's
// emulators are libretro cores: the frontend loads a `.so`, calls `retro_run`
// once per frame, and is handed back a picture and some samples. PCSX2 is a
// whole emulator. It runs its own machine on its own thread and does not
// return until the game stops, which is the same shape Cabinet found on the
// Mac — `CabinetPS2::Run` "blocks for the entire life of the game and must be
// given its own thread".
//
// So the frame loop cannot drive it. What it can do is take the finished
// picture and the finished sound, hand it the pad, and stop it — which is
// exactly what this header is, and it is deliberately the same four things
// `Core` already does for everything else. `core.cpp` routes to here when the
// platform is `ps2`; nothing above that layer learns a new concept.
//
// THE EMULATOR ARRIVES AS A `.so` AND IS `dlopen`ED, like every core. That is
// not incidental: PCSX2 is GPLv3 and this repository is MIT, and
// docs/LICENCES.md rests its position on exactly that distinction. See
// frontend/ps2/CabinetPS2Bridge.cpp.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2 {

// Opens cabinetos-ps2.so. Returns false with a reason on `error()` — a console
// missing it should say so once, plainly, rather than failing later in a way
// that reads as a broken game.
bool load(const std::string& soPath);
void unload();

const std::string& error();

// The PCSX2 revision the shared object was built from. Empty when nothing is
// loaded. The same question `retro_get_system_info` answers for a core, for
// the same reason: an emulator that cannot say which revision it is cannot be
// audited, and a save state's compatibility is a fact about the build.
std::string version();

// Starts a game.
//
// The folders are given separately rather than derived from one root, because
// on this console they live in three different places for three different
// reasons: the firmware comes from RomM and is shared, the card belongs to one
// person and one game, and the scratch is throwaway. Deriving them from a
// single root put PCSX2's card and log under the BIOS directory, which is
// root-owned, and it failed to create either.
//
// `memoryCard` is a bare filename inside `memcardsDir`. The caller names it the
// way catalog::saveFiles already says a PlayStation 2 card is named, so the
// whole existing save machinery works on it unchanged.
//
// `discCode` is RomM's `title_id` for the game, `SLUS-21090`, empty when RomM
// has none: the library picks the BIOS in `biosDir` by the game's region from
// it (#416, frontend/ps2/CabinetPS2Bios.h).
bool startGame(const std::string& discPath, const std::string& biosDir,
               const std::string& memcardsDir, const std::string& memoryCard,
               const std::string& scratchDir, const std::string& resourcesDir,
               float upscale, int anisotropy, bool widescreen,
               const std::string& discCode);

// WHY THE MACHINE STOPPED BY ITSELF BEFORE IT RAN (#506), or empty while it
// is starting, runs, or ended as asked. startGame returns as soon as PCSX2's
// thread is started, before the machine is built, so a disc PCSX2 cannot
// open is known only here: its thread ends with PCSX2's reason ("PCSX2
// would not boot <disc>") and the machine never ran.
std::string stoppedWhy();

// Asks the game to stop and WAITS. Blocking is the point: PCSX2 flushes the
// memory card during shutdown, so a card captured before this returns is the
// card as it was when the game started.
void stopGame();

// WIDESCREEN, PER GAME (#368). Every game whose PCSX2 patch file has a
// "Widescreen 16:9" section plays widescreen (#217); the pause menu offers
// "Widescreen: On / Off" on exactly those games, because some patches widen
// the 3D view and leave the HUD stretched, and some players want 4:3 anyway.
// Whether the running game has the patch: known once its program has booted.
bool hasWidescreenPatch();
// The choice, kept on this console in config/ps2-widescreen.json,
// `{"<rom id>": "off"}`, the way the Picture row keeps its own; a game not in
// it is on. Takes effect from the game's next start. MEASURED ON THE A9,
// 2026-10-12, Rumble Racing: PCSX2 switches the patch ON mid-game correctly
// (VMManager::ApplySettings), but OFF does not give the 4:3 game back,
// because the patch's writes stay in the game's memory.
bool widescreenOn(int romId);
void setWidescreenOn(int romId, bool on);

// Freezes the emulated machine, for the in-game overlay. Not optional the way
// it is for a libretro core: those stop because the frame loop stops stepping
// them, and this one runs on a thread of its own.
void setPaused(bool paused);

void setPad(int port, uint32_t buttons, float leftX, float leftY, float rightX, float rightY,
            float leftTrigger, float rightTrigger);

// A GUITAR IN `port` (guitar.h), 0 or 1, for the next startGame: PCSX2 is
// told the port holds its guitar rather than a DualShock 2, and setPad is
// then handed the guitar's state (guitar::ps2State). False, and a DualShock
// 2 in the port, when the library is older than this.
bool setGuitar(int port, bool on);
// Whether the game now running was told `port` holds a guitar.
bool guitar(int port);

// Hands what the game last asked of each DualShock 2's motors to rumble.h.
// Once a frame; PCSX2 runs on its own thread, so this is read, not told.
void pollRumble();

// THE WINDOW PCSX2 PRESENTS TO (#226), before startGame; 0 for none. With
// one, PCSX2 draws its own picture there and gamescope shows it, as it shows
// the separate emulators, and no frame is copied back: takeFrame answers only
// after captureFrame. False when the library is older than this and cannot
// take a window, which leaves it reading every frame back as before.
bool setWindow(const std::string& display, unsigned long window, unsigned width, unsigned height);
// Whether the game now starting was given a window.
bool windowed();

// Reads the picture on screen back once, for a screenshot, and waits up to
// `timeoutMs` for it; takeFrame then has it.
bool captureFrame(unsigned timeoutMs);

// Once a frame while a game runs. Every ten seconds it logs PCSX2's own
// frame rate and speed, and on the copy path how many new frames reached
// the screen: gamescope's frame times see only the window that presents.
void report();

// Takes the newest finished frame, if there is one. The pixels stay valid
// until the next call.
bool takeFrame(const uint32_t** pixels, unsigned& width, unsigned& height);

// Drains sound as interleaved 16-bit stereo, appending to `out` — the same
// contract as `Core::drainAudio`.
void drainAudio(std::vector<int16_t>& out);

// RETROACHIEVEMENTS (#74). The emulated machine's memory, live: region 0 is
// the EE's 32 MB of main RAM, region 1 its 16 KB scratchpad, in the order
// RetroAchievements numbers PS2 addresses. Null until the game is running,
// and null for a library built before this.
uint8_t* memory(unsigned region, size_t* size);
// `cb` once per emulated frame, ON PCSX2's CPU THREAD and only while the game
// runs, which is where PCSX2's own achievements code checks them: the memory
// is still between frames there. Null clears it. False when the library is
// older than this.
bool setFrameCallback(void (*cb)(void*), void* user);

} // namespace ps2
