// The emulated machine's memory: save states, the battery save, the memory
// regions, and the memory map a core hands over for RetroAchievements
// (environmentMemory). Moved out of core.cpp in the restructure (#398).
// Core::loadGame (core.cpp) clears the map before retro_load_game, because a
// core hands its map over inside it.

#include "core.h"
#include "core_state.h"
#include "vkhost.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "libretro.h"

namespace cab {
namespace corestate {

// THE CORE'S MEMORY MAP, KEPT FOR RETROACHIEVEMENTS (#74). A core hands it
// over once, inside retro_load_game, and owns the descriptors' storage only
// for the length of that call; the pointers INTO the emulated machine stay
// valid until the game is unloaded. So the descriptors are copied, and the
// address-space names with them. rcheevos' rc_libretro reads this the way
// RetroArch hands it over. Cleared with the game.
std::vector<retro_memory_descriptor> gMemDescs;
std::vector<std::string> gMemNames;
retro_memory_map gMemMap{nullptr, 0};
// Counts the maps handed over. Dolphin sends its map from inside the first
// retro_run, not from retro_load_game, so whoever laid memory out from an
// earlier one does it again when this moves.
unsigned gMemMapGeneration = 0;

void keepMemoryMap(const retro_memory_map* mm) {
    ++gMemMapGeneration;
    gMemDescs.clear();
    gMemNames.clear();
    gMemMap = {nullptr, 0};
    if (!mm || !mm->descriptors || mm->num_descriptors == 0) return;
    gMemDescs.assign(mm->descriptors, mm->descriptors + mm->num_descriptors);
    // Reserved first: the descriptors point at these strings, so the vector
    // must not move once they do.
    gMemNames.reserve(gMemDescs.size());
    for (retro_memory_descriptor& d : gMemDescs) {
        gMemNames.emplace_back(d.addrspace ? d.addrspace : "");
        d.addrspace = d.addrspace ? gMemNames.back().c_str() : nullptr;
    }
    gMemMap.descriptors = gMemDescs.data();
    gMemMap.num_descriptors = static_cast<unsigned>(gMemDescs.size());
}

// The memory-map call of the environment callback. core.cpp's environment
// asks here before its own switch; nothing for any other call.
std::optional<bool> environmentMemory(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
            keepMemoryMap(static_cast<const retro_memory_map*>(data));
            return true;

        default:
            return std::nullopt;
    }
}

}  // namespace corestate

using namespace corestate;

// IN THE CORE'S OWN CONTEXT, the size as well as the state: Azahar makes the
// whole state inside retro_serialize_size, its emulation loop and all, so on
// OpenGL that call needs the core's context current as retro_serialize does.
size_t Core::stateSize() const {
    if (!gameLoaded_ || !g.serialize_size) return 0;
    CoreContext in;
    return g.serialize_size();
}

bool Core::saveState(std::vector<uint8_t>& out) {
    if (!gameLoaded_ || !g.serialize_size || !g.serialize) return false;
    const uint64_t soundBefore = gAudioFrames;
    bool saved;
    {
        CoreContext in;
        const size_t n = g.serialize_size();
        if (n == 0) return false;  // some cores genuinely cannot; that is not an error
        out.resize(n);
        saved = g.serialize(out.data(), n);
    }
    // A SAVE THAT MADE SOUND RAN THE GAME (#254). Mupen64Plus-Next runs the
    // emulated machine to its next vertical interrupt to take a state
    // (libretro.c, retro_serialize), so each save is a whole N64 frame: its
    // sound, and its picture handed over. Uncounted, the rewind snapshots
    // made 3.3% more sound per frame than promised and the speaker held a
    // frame twice a second. Counted as the frame it is; the frame loop takes
    // such a core's snapshots in place of a run (pace.cpp).
    if (gAudioFrames != soundBefore) {
        if (!saveRunsFrame_)
            std::fprintf(stderr, "[core] a state save runs a frame of the game (%llu "
                                 "sound frames); counted as one\n",
                         static_cast<unsigned long long>(gAudioFrames - soundBefore));
        saveRunsFrame_ = true;
        ++gFramesRun;
        accumulator_ -= 1.0 / std::max(av_.fps, 1.0);   // runFor's budget
        if (afterFrame_) afterFrame_();
    }
    return saved;
}

size_t Core::saveRamSize() const {
    if (!gameLoaded_ || !g.get_memory_size) return 0;
    return g.get_memory_size(RETRO_MEMORY_SAVE_RAM);
}

bool Core::readSaveRam(std::vector<uint8_t>& out) const {
    out.clear();
    const size_t n = saveRamSize();
    if (n == 0 || !g.get_memory_data) return false;   // no battery in this cart
    const void* p = g.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!p) return false;
    out.resize(n);
    std::memcpy(out.data(), p, n);
    return true;
}

bool Core::writeSaveRam(const std::vector<uint8_t>& data) {
    const size_t n = saveRamSize();
    if (n == 0 || data.empty() || !g.get_memory_data) return false;
    void* p = g.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!p) return false;
    // Sizes can differ between a save written elsewhere and what this core
    // exposes. Copy what fits rather than refusing: a short save is a smaller
    // cart image and the remainder is already zeroed, and a long one is not
    // ours to truncate silently — but dropping the tail is still better than
    // losing the save entirely.
    std::memcpy(p, data.data(), std::min(n, data.size()));
    return true;
}

bool Core::loadState(const std::vector<uint8_t>& data) {
    if (!gameLoaded_ || !g.unserialize || data.empty()) return false;
    bool loaded;
    {
        CoreContext in;
        loaded = g.unserialize(data.data(), data.size());
    }
    // A VULKAN CORE MAY REBUILD ITS RENDERER TO LOAD A STATE, and Azahar does:
    // the image it last handed over is destroyed, and the next present()
    // blitted from it and crashed the console 2 ms after the load (A9,
    // 2026-10-20, radv in vkCmdBlitImage). The frame loop presents while the
    // game is held on "Press (A) to continue", before the core runs again. So
    // the image is forgotten here: the picture on screen stays until the core
    // hands over its next one, which every Vulkan core here does each frame.
    if (gHWVulkan) vk::forgetCoreImage();
    return loaded;
}

bool Core::saveRAM(std::vector<uint8_t>& out) const {
    return memoryRegion(RETRO_MEMORY_SAVE_RAM, out);
}

bool Core::memoryRegion(unsigned id, std::vector<uint8_t>& out) const {
    if (!gameLoaded_ || !g.get_memory_data || !g.get_memory_size) return false;
    const size_t n = g.get_memory_size(id);
    const void* p = g.get_memory_data(id);
    if (!p || n == 0) return false;
    out.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
    return true;
}

const retro_memory_map* Core::memoryMap() const {
    return (gameLoaded_ && !gIsPs2 && gMemMap.num_descriptors > 0) ? &gMemMap : nullptr;
}

unsigned Core::memoryMapGeneration() const { return gMemMapGeneration; }

uint8_t* Core::memoryPointer(unsigned id, size_t* size) const {
    *size = 0;
    if (!gameLoaded_ || gIsPs2 || !g.get_memory_data || !g.get_memory_size) return nullptr;
    void* p = g.get_memory_data(id);
    const size_t n = g.get_memory_size(id);
    if (!p || n == 0) return nullptr;
    *size = n;
    return static_cast<uint8_t*>(p);
}

}  // namespace cab
