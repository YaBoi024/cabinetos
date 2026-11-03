// PlayStation 2's side of Core: its switches (setPs2), its pause, and the
// start of a game (startPs2Game). Moved out of core.cpp in the restructure
// (#398). The PlayStation 2 branches of the other Core calls stay where they
// are, in core.cpp, so each call still shows its PCSX2 case, and the switches
// are defined there with gIsPs2 (core_state.h).

#include "core.h"
#include "core_state.h"

#include <string>

#include "ps2.h"

namespace cab {

using namespace corestate;

// Core::loadGame's PlayStation 2 start: the card named from the rom, the three
// folders, and PCSX2 started on the game. loadGame calls it inside the CPU
// placement (cpus.h) and returns at once, as it did with this inline.
bool Core::startPs2Game(const std::string& romPath, const std::string& systemDir,
                        const std::string& saveDir) {
    // THE CARD IS NAMED FROM THE ROM, HERE, BY THE RULE EVERY OTHER SAVE
    // GOES BY. `catalog::saveFiles` (savetable.cpp) says a PlayStation 2 card
    // is `<stem>.ps2` in the save directory, where the stem is the rom's
    // filename without its extension — and `filesave.cpp` already restores
    // that file before launch, captures it after, refuses to upload an
    // unformatted one, and files it on the server under the name Cabinet's
    // Mac used.
    //
    // Deriving it here rather than being told means the two cannot drift.
    // A card PCSX2 wrote under a name the save layer does not look for is
    // a save that exists on disk and never reaches the server, and nothing
    // would say so.
    std::string stem = romPath;
    if (const size_t slash = stem.find_last_of('/'); slash != std::string::npos)
        stem.erase(0, slash + 1);
    if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos)
        stem.erase(dot);

    // Firmware is shared and comes from RomM; the card is this person's and
    // this game's; the scratch is throwaway. Three places, deliberately.
    //
    // PCSX2 IS HANDED `bios/` ITSELF (#153), which is where the firmware
    // step puts whatever RomM serves, under RomM's own names. No name is
    // needed: the library looks at every file of 4 to 8 MB in the folder,
    // not in subfolders, keeps those whose contents are a PS2 BIOS, so no
    // other system's firmware can be mistaken for one, and picks one by
    // the game's region from its disc code (#416, CabinetPS2Bios.h).
    // It was `bios/pcsx2/bios` until 2026-09-27, which nothing filled:
    // PS2 played only on the one console where the files were copied
    // there by hand, and on a fresh install `bios/pcsx2` is the image's
    // read-only resources link, so nothing could have. The failure is a
    // VM destroyed in the millisecond it starts, after "Searching for a
    // BIOS image in ..." and no error.
    const std::string biosDir = systemDir;
    const std::string scratchDir = saveDir + "/pcsx2-scratch";

    if (!ps2::startGame(romPath, biosDir, saveDir, stem + ".ps2", scratchDir,
                        gPs2ResourcesDir, gPs2Upscale, gPs2Anisotropy, gPs2Widescreen,
                        gPs2DiscCode)) {
        error_ = ps2::error();
        return false;
    }
    gameLoaded_ = true;
    gFramesRun = 0;
    gAudioFrames = 0;
    frameWidth_ = 0;
    frameHeight_ = 0;
    // av_ is what the rest of the frontend reads for display. PCSX2 paces
    // ITSELF, so nothing here drives the emulator; the 60 is the NTSC
    // figure and a PAL disc simply runs at its own rate underneath.
    av_.fps = 60.0;
    av_.sampleRate = 48000.0;
    return true;
}

void Core::setPs2(const std::string& resourcesDir, float upscale, int anisotropy,
                  bool widescreen, const std::string& discCode) {
    gPs2ResourcesDir = resourcesDir;
    gPs2Upscale = upscale > 0.0f ? upscale : 1.0f;
    gPs2Anisotropy = anisotropy;
    gPs2Widescreen = widescreen;
    gPs2DiscCode = discCode;
}

void Core::setPaused(bool paused) {
    // Every libretro core stops because pace.cpp stops calling runFor. Only
    // PCSX2 has a thread that needs telling.
    if (gIsPs2) ps2::setPaused(paused);
}

bool Core::isPs2() const { return gIsPs2; }

}  // namespace cab
