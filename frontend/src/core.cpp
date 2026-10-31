#include "core.h"
#include "core_state.h"
#include "cpus.h"
#include "netblock.h"
#include "ps2.h"

#include <dlfcn.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <optional>

#include "libretro.h"
#include "rumble.h"
#include "vkhost.h"

namespace cab {
namespace corestate {

// Process-wide rather than instance state, and deliberately. Libretro's
// callbacks are plain C function pointers with no context argument, so the
// process holds exactly one core's worth of state however this is shaped.
EntryPoints g;

// Dolphin's id for a real Wii Remote: a subclass of NONE, so never mask it to
// its base type (DolphinLibretro/Input.cpp, RETRO_DEVICE_REAL_WIIMOTE).
constexpr unsigned kRealWiiRemote = (6u << 8) | RETRO_DEVICE_NONE;

unsigned gPixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
// THE TWO FOLDERS A CORE IS TOLD, IN BUFFERS THAT NEVER MOVE. A core may keep
// the pointer it was handed in retro_init and read it later: MAME 0.289 builds
// every one of its folders from it at retro_load_game. These were strings,
// set again at loadGame to the same folders, and the assignment freed the
// buffer the core still pointed at: MAME read freed memory and wrote its
// NVRAM and settings into a folder with a garbage name in the working folder
// (measured on the A9, 2026-10-28). Copied into place, never reallocated, so
// a kept pointer always reads the folder in use.
char gSystemDir[4096] = "";
char gSaveDir[4096] = "";

// How the core wants its picture turned, in 90-degree counter-clockwise steps.
//
// A vertical arcade board — DoDonPachi, Ikaruga, most shmups — has its monitor
// bolted in sideways in the cabinet, so the board renders a picture that is
// sideways in memory and asks the frontend to turn it. A frontend that ignores
// the ask draws a rotated game, which is what this console did for every TATE
// board in the library until 2026-09-19.
//
// THIS IS THE CORE'S ANSWER, NOT THE PLATFORM'S. It cannot be looked up from
// the system, because the same arcade emulator serves upright and vertical
// boards from one platform row and only knows which after the game is loaded.
unsigned gRotation = 0;
bool gRotationAnnounced = false;

// A NEW SHAPE OR RATE ASKED FOR MID-GAME (#220): SET_GEOMETRY (the
// picture's size and aspect, the maximum kept) and SET_SYSTEM_AV_INFO (the
// frame and sound rates too). Dolphin sends the first when a GameCube game
// switches to widescreen, and both when it moves between 50 and 60 Hz
// (DolphinLibretro/Main.cpp at the pinned 1a0f97270b, lines 343-380).
// Refused, the game went on drawn at its first shape: a widescreen game
// squeezed into 4:3. Held here and taken after the retro_run that asked
// (Core::takeAvAsked), so nothing changes in the middle of a frame.
bool gGeometryAsked = false;
retro_game_geometry gGeometry{};
bool gTimingAsked = false;
retro_system_timing gTiming{};

// The core writes into its own buffer and reuses it between calls, so a frame
// is copied out rather than referenced.
std::vector<uint8_t> gFrame;
unsigned gFrameW = 0, gFrameH = 0;
size_t gFramePitch = 0;
bool gFrameDirty = false;

std::vector<int16_t> gAudio;
std::vector<int16_t> gAudioDrain;
uint64_t gAudioFrames = 0;
uint64_t gFramesRun = 0;

void audioSample(int16_t left, int16_t right) {
    gAudio.push_back(left);
    gAudio.push_back(right);
    ++gAudioFrames;
}

size_t audioSampleBatch(const int16_t* data, size_t frames) {
    gAudio.insert(gAudio.end(), data, data + frames * 2);
    gAudioFrames += frames;
    return frames;
}

// Cores are chatty at INFO, so warnings and above are what reaches the log.
//
// RAISING THIS WAS TRIED AND IT BREAKS PPSSPP. A flag that let everything
// through was built to read one INFO line the core prints — and with it on, the
// process aborts at exit with "terminate called without an active exception",
// reproducibly, twice, while the identical run without it exits 0. The cause is
// not established; the flag is gone, and the one line worth having says itself
// at WARN instead (cores/build-core.sh's cpu-engine probe).
void logCallback(enum retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_WARN) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[core] %s", buf);
}

bool environment(unsigned cmd, void* data) {
    // The calls another file of the host answers are asked there first
    // (core_state.h); each says nothing to a call that is not its own.
    if (std::optional<bool> answer = environmentOptions(cmd, data)) return *answer;
    if (std::optional<bool> answer = environmentInput(cmd, data)) return *answer;
    if (std::optional<bool> answer = environmentMemory(cmd, data)) return *answer;
    if (std::optional<bool> answer = environmentDiscs(cmd, data)) return *answer;
    if (std::optional<bool> answer = environmentHWRender(cmd, data)) return *answer;
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            // Yes: a core may hand back a null frame meaning "unchanged", and
            // the draw loop keeps presenting the last one.
            *static_cast<bool*>(data) = true;
            return true;

        case RETRO_ENVIRONMENT_SET_ROTATION: {
            // 0, 1, 2, 3 — 90 degrees counter-clockwise each. Answered rather
            // than refused: a core that is told no is entitled to carry on
            // drawing sideways, and several do.
            const unsigned r = *static_cast<const unsigned*>(data);
            if (r > 3) return false;
            gRotation = r;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_GEOMETRY:
            gGeometry = *static_cast<const retro_game_geometry*>(data);
            gGeometryAsked = true;
            return true;

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
            const auto* av = static_cast<const retro_system_av_info*>(data);
            gGeometry = av->geometry;
            gGeometryAsked = true;
            gTiming = av->timing;
            gTimingAsked = true;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const unsigned fmt = *static_cast<const enum retro_pixel_format*>(data);
            if (fmt > RETRO_PIXEL_FORMAT_RGB565) return false;
            gPixelFormat = fmt;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *static_cast<const char**>(data) = gSystemDir;
            return true;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            // Must outlive the session. See the header, and gSaveDir's note.
            *static_cast<const char**>(data) = gSaveDir;
            return true;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback*>(data)->log = logCallback;
            return true;

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;

        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
            return true;

        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;

        default:
            return false;
    }
}

}  // namespace corestate

using namespace corestate;

Core& Core::shared() {
    static Core instance;
    return instance;
}

void* Core::symbol(const char* name) {
    dlerror();  // clear, so the next read belongs to this lookup
    void* s = dlsym(handle_, name);
    if (!s) {
        const char* why = dlerror();
        error_ = std::string("missing symbol ") + name + (why ? std::string(": ") + why : "");
    }
    return s;
}

namespace corestate {
// Cores get ABSOLUTE directories, always, and they are created before the core
// is told about them.
//
// The frontend names both relative to where it runs, which is convenient here
// and wrong to hand over. A core is entitled to do more with these than fopen
// them: PPSSPP wraps them in a path type of its own that asks whether a path is
// absolute and behaves differently when it is not, and handed "romcache/saves"
// it mounted the memory stick somewhere it could not write — the game ran, the
// save failed, and the only sign was a line in the core's own log saying
// "Error writing file ms0:/PSP/SAVEDATA/...". That is the melonDS lesson
// arriving a second time by a different route, so it is fixed once, here,
// rather than per core.
//
// The mkdir matters as much as the absolute path: a core that is handed a
// directory which does not exist yet has nowhere to put a save, and finds that
// out silently.
std::string absoluteDir(const std::string& dir) {
    if (dir.empty()) return dir;
    std::string abs = dir;
    if (abs.front() != '/') {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) abs = std::string(cwd) + "/" + abs;
    }
    SDL_CreateDirectory(abs.c_str());
    return abs;
}

// `dir`, made absolute, copied into one of the two buffers above in place.
void keepDir(char (&kept)[4096], const std::string& dir) {
    const std::string abs = absoluteDir(dir);
    if (abs.size() >= sizeof kept) {
        std::fprintf(stderr, "[core] a folder too long for a core, not given: %s\n", abs.c_str());
        kept[0] = '\0';
        return;
    }
    std::memcpy(kept, abs.c_str(), abs.size() + 1);
}
}  // namespace corestate

void Core::setDirectories(const std::string& systemDir, const std::string& saveDir) {
    keepDir(gSystemDir, systemDir);
    keepDir(gSaveDir, saveDir);
}

// PLAYSTATION 2 IS NOT A LIBRETRO CORE, AND THESE FEW BRANCHES ARE THE WHOLE
// PRICE OF THAT.
//
// PCSX2 is a complete emulator that runs its own machine on its own thread and
// does not return until the game stops. The frame loop therefore cannot step
// it, which is the one thing every other emulator here has in common. What it
// CAN do is take the finished picture, take the finished sound, hand over the
// pad and stop it — and those are the same four things this class already does
// for the other twenty-one.
//
// So the branch lives here, at the bottom, rather than as a second Core the
// screens have to know about. Everything above this file — the launch screen,
// the overlay, the save sync, the frame loop in main.cpp — is unchanged and
// does not learn a new concept. See ps2.h.
namespace corestate {
bool gIsPs2 = false;
std::string gPs2ResourcesDir;
float gPs2Upscale = 1.0f;
int gPs2Anisotropy = 0;
bool gPs2Widescreen = true;
std::string gPs2DiscCode;
}  // namespace corestate

bool Core::load(const std::string& soPath) {
    unload();
    // WHAT EACH PLAYER'S PORT HOLDS IS THE NEXT GAME'S TO SAY, and a Wii
    // game the core refused never reached unloadGame, which resets them
    // after a game: the next game of any system was told its ports held
    // Wii devices (#398). Every launch loads its core at the press, and
    // restoreBeforeLoad sets a Wii game's after that.
    gPadDevice = RETRO_DEVICE_JOYPAD;
    gRealRemotesFrom = -1;

    // The emulator is recognised by its filename, the same way every core is.
    // catalog::coreFileName produces this for the `ps2` platform.
    if (soPath.size() >= 16 &&
        soPath.compare(soPath.size() - 16, 16, "cabinetos-ps2.so") == 0) {
        if (!ps2::load(soPath)) {
            error_ = ps2::error();
            return false;
        }
        gIsPs2 = true;
        handle_ = reinterpret_cast<void*>(1); // unload() and loadGame() test handle_
        return true;
    }
    gIsPs2 = false;
    // Whatever the last core asked for is not this one's business either.
    gDisk = {};
    gDiskSet = false;
    gInitialDisc = -1;
    gInitialDiscPath.clear();
    gHW = {};
    gHWWanted = false;
    gHWVulkan = false;
    gHWFrame = false;
    gHWContextName.clear();
    // Nor are the last core's Vulkan negotiation interface, which points
    // into a core that is dlclosed by now, and its pixel format: libretro's
    // default is 0RGB1555 for a core that never says (#489, EMU-01, EMU-07).
    // Every Vulkan core shipped today sends its interface again during its
    // own load, and every core states its format, so nothing changes today.
    vk::setNegotiation(nullptr);
    gPixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
    // Whatever the last core declared is not this one's business. Cleared here
    // rather than in unload() so a caller that inspects the table after a core
    // is unloaded still sees what that core actually asked for.
    resetOptions();
    // RTLD_LOCAL is the whole reason this is simple on Linux: each core's
    // retro_* symbols stay private to it, so many cores can be loaded over a
    // session without colliding, and none of Cabinet's prefix-and-merge
    // machinery is needed.
    handle_ = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        // Read dlerror() ONCE. It clears itself on read, so calling it twice
        // returns null the second time — and assigning that null to a
        // std::string is a segfault, which is how a perfectly clear "file not
        // found" turned into a crash with no message at all.
        const char* why = dlerror();
        error_ = why ? why : "dlopen failed and gave no reason";
        return false;
    }
    // Before retro_init, so every thread the core starts carries the mark.
    netblock::addEmulator(handle_, soPath);

    g.api_version = reinterpret_cast<unsigned (*)(void)>(symbol("retro_api_version"));
    g.get_system_info =
        reinterpret_cast<void (*)(retro_system_info*)>(symbol("retro_get_system_info"));
    g.get_system_av_info = reinterpret_cast<void (*)(retro_system_av_info*)>(
        symbol("retro_get_system_av_info"));
    g.set_environment =
        reinterpret_cast<void (*)(retro_environment_t)>(symbol("retro_set_environment"));
    g.set_video_refresh = reinterpret_cast<void (*)(retro_video_refresh_t)>(
        symbol("retro_set_video_refresh"));
    g.set_audio_sample =
        reinterpret_cast<void (*)(retro_audio_sample_t)>(symbol("retro_set_audio_sample"));
    g.set_audio_sample_batch = reinterpret_cast<void (*)(retro_audio_sample_batch_t)>(
        symbol("retro_set_audio_sample_batch"));
    g.set_input_poll =
        reinterpret_cast<void (*)(retro_input_poll_t)>(symbol("retro_set_input_poll"));
    g.set_input_state =
        reinterpret_cast<void (*)(retro_input_state_t)>(symbol("retro_set_input_state"));
    g.set_controller_port_device = reinterpret_cast<void (*)(unsigned, unsigned)>(
        symbol("retro_set_controller_port_device"));
    g.init = reinterpret_cast<void (*)(void)>(symbol("retro_init"));
    g.deinit = reinterpret_cast<void (*)(void)>(symbol("retro_deinit"));
    g.load_game = reinterpret_cast<bool (*)(const retro_game_info*)>(symbol("retro_load_game"));
    g.unload_game = reinterpret_cast<void (*)(void)>(symbol("retro_unload_game"));
    g.run = reinterpret_cast<void (*)(void)>(symbol("retro_run"));
    g.serialize_size = reinterpret_cast<size_t (*)(void)>(symbol("retro_serialize_size"));
    g.serialize = reinterpret_cast<bool (*)(void*, size_t)>(symbol("retro_serialize"));
    g.unserialize =
        reinterpret_cast<bool (*)(const void*, size_t)>(symbol("retro_unserialize"));
    g.get_memory_data =
        reinterpret_cast<void* (*)(unsigned)>(symbol("retro_get_memory_data"));
    g.get_memory_size =
        reinterpret_cast<size_t (*)(unsigned)>(symbol("retro_get_memory_size"));

    if (!g.api_version || !g.init || !g.run || !g.load_game) {
        unload();
        return false;
    }

    const unsigned api = g.api_version();
    if (api != RETRO_API_VERSION) {
        char buf[128];
        snprintf(buf, sizeof(buf), "core speaks libretro %u, this frontend speaks %u", api,
                 RETRO_API_VERSION);
        error_ = buf;
        unload();
        return false;
    }

    retro_system_info info{};
    g.get_system_info(&info);
    coreName_ = info.library_name ? info.library_name : "?";
    validExtensions_ = info.valid_extensions ? info.valid_extensions : "";
    blockExtract_ = info.block_extract;
    needFullpath_ = info.need_fullpath;
    coreVersion_ = info.library_version ? info.library_version : "?";

    // EVERY CORE IS PACED BY THE SPEAKER (pace.cpp, RetroArch's audio sync),
    // which also holds the two that a count of retro_run calls cannot: PPSSPP,
    // whose retro_run takes one swap from its own emulation thread, a game
    // frame rather than a vblank (a 30fps PSP game ran at twice speed paced by
    // the count), and Flycast, whose emulation thread runs ahead of retro_run.
    // A brake against the wall clock for a core whose sound ran ahead came
    // first (#144); it was off for every core once the speaker paced them, and
    // was removed (#492).

    // Order matters: the environment callback must be installed before
    // retro_init, because a core may call it from there.
    g.set_environment(environment);
    g.init();
    g.set_video_refresh(videoRefresh);
    g.set_audio_sample(audioSample);
    g.set_audio_sample_batch(audioSampleBatch);
    g.set_input_poll(inputPoll);
    g.set_input_state(inputState);

    std::fprintf(stderr, "[core] %s %s (libretro %u)\n", coreName_.c_str(),
                 coreVersion_.c_str(), api);
    return true;
}

void Core::setRefuseHWRender(bool on) { gRefuseHWRender = on; }

bool Core::canServeDesktopGL() { return desktopGLServable(); }


void Core::unload() {
    cpus::gameEnd();
    if (!handle_) return;
    if (gameLoaded_) unloadGame();
    if (gIsPs2) {
        ps2::unload();
        gIsPs2 = false;
        handle_ = nullptr;
        if (texture_) { glDeleteTextures(1, &texture_); texture_ = 0; }
        return;
    }
    if (g.deinit) g.deinit();
    forgetKeyboard();
    dlclose(handle_);
    netblock::dropUnloaded();
    handle_ = nullptr;
    g = {};
    // The name and version go with the core: a later game on another path
    // must not read as this one (#412).
    coreName_.clear();
    coreVersion_.clear();
    gDisk = {};
    gDiskSet = false;
    gInitialDisc = -1;
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    destroyHWTarget();
    closeDesktopContext();
    if (gHWVulkan) vk::destroyContext();
    gHWWanted = false;
    gHWVulkan = false;
    gHWFrame = false;
}

bool Core::loadGame(const std::string& romPath, const std::string& systemDir,
                    const std::string& saveDir) {
    rumble::reset();
    // The last game's buttons go with it, whatever this one declares, and
    // no key it pressed is still down.
    clearInputDescriptors();
    releaseKeys();
    if (!handle_) {
        error_ = "no core loaded";
        return false;
    }
    // Before the emulator starts its threads, so they inherit the fast cores
    // (cpus.h, #163). A game that fails to start hands them back.
    cpus::gameStart();
    struct Placed {
        bool started = false;
        ~Placed() { if (!started) cpus::gameEnd(); }
    } placed;
    if (gIsPs2) {
        if (!startPs2Game(romPath, systemDir, saveDir)) return false;
        placed.started = true;
        return true;
    }
    keepDir(gSystemDir, systemDir);
    keepDir(gSaveDir, saveDir);

    // Cleared HERE, not in load(), because rotation is a fact about the GAME
    // and not about the core. One arcade emulator serves upright and vertical
    // boards out of the same .so, and it calls SET_ROTATION from inside
    // retro_load_game — so a vertical game followed by an upright one that
    // never calls it at all would otherwise leave the upright one sideways.
    gRotation = 0;
    gRotationAnnounced = false;
    gInputDescriptorsLogged = false;

    // need_fullpath means the core opens the file ITSELF, and libretro is
    // explicit that the frontend must then not load it. This used to read it
    // anyway, which was invisible while the cores that ask for it were handed
    // small files — and stops being invisible at PSP, where The Warriors is
    // 1.8 GB on a machine with 4 GB of memory. The bytes were allocated, read,
    // and then ignored by the core.
    //
    // The file is still checked, because "the core refused it" and "there is
    // no file there" are different failures and only one of them is the core's.
    std::vector<uint8_t> rom;
    size_t romSize = 0;
    if (FILE* f = std::fopen(romPath.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            romSize = static_cast<size_t>(n);
            if (!needFullpath_) {
                rom.resize(romSize);
                if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) rom.clear();
            }
        }
        std::fclose(f);
    }
    if (romSize == 0 || (!needFullpath_ && rom.empty())) {
        error_ = "cannot read " + romPath;
        return false;
    }

    retro_game_info info{};
    info.path = romPath.c_str();
    info.data = needFullpath_ ? nullptr : rom.data();
    info.size = needFullpath_ ? 0 : rom.size();

    // A map left from the last game would point into freed memory.
    keepMemoryMap(nullptr);
    // START ON DISC N (#162), where the core takes it: the list's index and
    // the exact path it will build from the list, as RetroArch does. A core
    // that answers no, or registers its drive too late to be asked, starts
    // on the list's first disc and says so; the app checks discIndex().
    if (gInitialDisc >= 0) {
        const bool asked = gDiskSet && gDisk.set_initial_image &&
                           gDisk.set_initial_image(static_cast<unsigned>(gInitialDisc),
                                                   gInitialDiscPath.c_str());
        std::fprintf(stderr, "[disc] start on index %d: %s\n", gInitialDisc,
                     asked ? "asked of the core" : "the core takes no start disc");
        gInitialDisc = -1;
    }
    if (!g.load_game(&info)) {
        // Deliberately does not claim the core is wrong, because nothing here
        // knows that: a refusal looks identical for a missing BIOS, an
        // incomplete set, or a genuinely bad file.
        error_ = "the core refused " + romPath;
        return false;
    }
    gameLoaded_ = true;
    checkOverrides(coreName_);

    // TELL THE CORE WHAT IS PLUGGED IN, and this was never being said.
    //
    // libretro's own documentation says a joypad is assumed on every port, and
    // it is a poor assumption to lean on: a core is free to build its idea of
    // the machine out of these calls and Flycast does exactly that. It
    // constructs the Dreamcast's Maple bus from `retro_set_controller_port_device`,
    // so a port nothing was ever said about carries no controller — and a VMU
    // lives in a CONTROLLER'S expansion socket, not in the console. With no
    // call, Ikaruga opens on "memory card not connected" however good the card
    // sitting on the disk is.
    //
    // That symptom has been in this project's notes for days, filed as a
    // curiosity of the file-writing save class. It is not: the save work put
    // a real 128 KB card in exactly the right place and the game still said
    // the slot was empty, which is what made it worth looking at the call
    // nobody was making. Found 2026-09-19 by photographing the screen rather
    // than by reading the log, which said nothing at all.
    //
    // EVERY PORT THE CORE HAS, not just the ones this console drives, and that
    // is the half that took the longest to find. Flycast will not act on any
    // of these calls until it has heard about ALL FOUR of its ports — it
    // returns early while any port is still unset, so the code that reads
    // `device_port1_slot1` and puts a VMU in the controller's expansion socket
    // never runs. Telling it about one port is the same as telling it about
    // none.
    //
    // A CONTROLLER IN EACH PLAYER'S PORT, AND NO MORE. Player one's port is
    // always a joypad; the others are joypads only when somebody is sitting
    // at them (players.h). Saying every port has a controller would have
    // Flycast create four memory cards for a console with one player, three
    // of which nothing would ever sync and all of which would sit in the
    // system directory.
    gPortsPlugged = 0;
    if (g.set_controller_port_device) {
        const unsigned ports = std::max(gCorePorts, 1u);
        const unsigned players = std::clamp<unsigned>(static_cast<unsigned>(gPlayers), 1u, ports);
        if (gRealRemotesFrom >= 0) {
            // Pads first, in the console's order, then a real Remote in every
            // port left: a Remote that connects mid-game takes the next one.
            const unsigned pads = std::min(players, static_cast<unsigned>(gRealRemotesFrom));
            for (unsigned p = 0; p < ports; ++p)
                g.set_controller_port_device(p, p < pads ? gPadDevice : kRealWiiRemote);
            gPortsPlugged = ports;
            std::fprintf(stderr, "[input] %u pad port(s) holding device %u, %u real Wii Remote port(s)\n",
                         pads, gPadDevice, ports - pads);
        } else {
            for (unsigned p = 0; p < ports; ++p)
                g.set_controller_port_device(p, p < players ? gPadDevice : RETRO_DEVICE_NONE);
            gPortsPlugged = players;
            if (gPadDevice != RETRO_DEVICE_JOYPAD)
                std::fprintf(stderr, "[input] each player's port holds device %u\n", gPadDevice);
        }
    }

    retro_system_av_info av{};
    g.get_system_av_info(&av);
    // Whatever was asked for while loading, get_system_av_info has the last
    // word on now.
    gGeometryAsked = gTimingAsked = false;
    av_.fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
    av_.sampleRate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100.0;
    av_.baseWidth = av.geometry.base_width;
    av_.baseHeight = av.geometry.base_height;
    av_.maxWidth = av.geometry.max_width;
    av_.maxHeight = av.geometry.max_height;
    av_.aspectRatio = av.geometry.aspect_ratio;

    if (gHWWanted) {
        // The target can only be built now: its size is the maximum geometry,
        // and that is not known until the game is loaded. Cores ask for the
        // context during retro_load_game, so this is the first moment both
        // facts exist.
        const unsigned w = std::max(av_.maxWidth, av_.baseWidth);
        const unsigned h = std::max(av_.maxHeight, av_.baseHeight);
        if (gHWVulkan) {
            // NOTHING IS ALLOCATED HERE FOR THE PICTURE, and that is the
            // difference. A Vulkan core renders into images it owns and hands
            // one over per frame, so what this builds is the device, the queue
            // and the one exported image the picture crosses on — sized later,
            // from the frame the core actually produces, rather than from its
            // declared maximum.
            std::string verr;
            if (!vk::createContext(&verr)) {
                error_ = "this core wants Vulkan and " + verr;
                g.unload_game();
                gameLoaded_ = false;
                return false;
            }
        } else if (!ensureHWTarget(w, h)) {
            error_ = "the core needs a render target this context cannot build";
            {
                CoreContext in;
                g.unload_game();
            }
            gameLoaded_ = false;
            return false;
        }
        // The core's GL resources do not exist until it is told the context is
        // ready, and it must be told before the first retro_run.
        if (gHW.context_reset) {
            CoreContext in;
            gHW.context_reset();
        }
        gHWContextLive = true;
        // context_reset builds shaders and buffers, and leaves them bound.
        restoreGLState();
    }

    accumulator_ = 0.0;
    saveRunsFrame_ = false;
    gFramesRun = 0;
    gAudioFrames = 0;
    gAudio.clear();

    std::fprintf(stderr, "[core] loaded %s\n", romPath.c_str());
    std::fprintf(stderr, "[core] %ux%u, %.4f fps, %.0f Hz, aspect %.4f\n", av_.baseWidth,
                 av_.baseHeight, av_.fps, av_.sampleRate, av_.aspectRatio);
    placed.started = true;
    return true;
}

void Core::unloadGame() {
    rumble::reset();
    // Home is back on every core, whatever happens below (cpus.h).
    struct BackOnAllCores { ~BackOnAllCores() { cpus::gameEnd(); } } allCores;
    if (gIsPs2) {
        if (!gameLoaded_) return;
        // BLOCKS until PCSX2 has actually stopped, which is what makes the
        // memory card capture that follows meaningful: the card is flushed
        // during shutdown, so anything read before this returns is the card as
        // it was when the game started.
        ps2::stopGame();
        gameLoaded_ = false;
        return;
    }
    if (!gameLoaded_) return;
    // The core frees its GL resources here, and it happens BEFORE the game is
    // unloaded — which is the order RetroArch uses, and it is not arbitrary: a
    // core's context_destroy walks structures that retro_unload_game is about
    // to tear down.
    if (gHWContextLive) {
        if (gHW.context_destroy) {
            CoreContext in;
            gHW.context_destroy();
        }
        gHWContextLive = false;
        restoreGLState();
    }
    // This is the one moment a file-writing core flushes its save. RetroArch
    // does exactly this, in this order, and the reference implementation lost
    // saves for months by unloading lazily at the NEXT launch instead.
    // melonDS frees its renderer here, so its context is current.
    if (g.unload_game) {
        CoreContext in;
        g.unload_game();
    }
    gameLoaded_ = false;
    gPadDevice = RETRO_DEVICE_JOYPAD;   // the next game says its own
    gRealRemotesFrom = -1;
    gHWFrame = false;
    destroyHWTarget();
    closeDesktopContext();
    // AFTER unload_game, for the same reason context_destroy runs before it:
    // the core's own Vulkan objects live on this device, and pulling the
    // device out from under a core that has not finished tearing down is a
    // crash inside the driver with no useful backtrace.
    if (gHWVulkan) vk::destroyContext();
}

int Core::runFor(double dt) {
    if (!gameLoaded_) return 0;
    if (gIsPs2) {
        // NOTHING TO STEP, AND THAT IS NOT A GAP. PCSX2 runs its own machine
        // on its own thread at its own pace, with its own frame limiter — so
        // there is no "advance one frame" to call. The frame counter follows
        // what actually arrived, which is what the rest of the frontend reads.
        //
        // The one thing that IS still this function's job on every other core
        // — not stepping while the overlay is open — is handled by the caller
        // simply not calling it, and for PlayStation 2 that is not enough: a
        // thread does not stop because nobody asked it to. Pausing is wired
        // separately, through setPaused.
        (void)dt;
        return 1;
    }

    const double interval = 1.0 / std::max(av_.fps, 1.0);
    // Fast forward is simply more emulated time per real second: the
    // accumulator fills faster, and the caps below scale with it.
    accumulator_ += dt * speed_;
    const int maxRuns = 2 * static_cast<int>(std::ceil(speed_));
    // Anything beyond a couple of frames behind is time that is simply gone.
    // Letting it accumulate would make the core sprint to catch up, which
    // stutters the picture and floods the audio buffer.
    if (accumulator_ > interval * 2 * maxRuns) accumulator_ = interval;

    int ran = 0;
    while (accumulator_ >= interval && ran < maxRuns) {
        step();
        ++ran;
        accumulator_ -= interval;
    }
    return ran;
}

int Core::runFrame() {
    if (!gameLoaded_) return 0;
    if (gIsPs2) return 1;   // paces itself, as in runFor
    // The accumulator is runFor's; emptied so a switch back to it (fast
    // forward) starts from now, not from a debt.
    accumulator_ = 0.0;
    step();
    return 1;
}

void Core::step() {
    {
        // A desktop GL core runs in its own context (core_hwrender.cpp).
        CoreContext in;
        if (gHWWanted && currentFramebuffer()) {
            // Bound before the core runs as well as answered on request.
            // Cores differ about when they ask for the framebuffer, and one
            // that draws before asking would otherwise land on the window.
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(currentFramebuffer()));
            glViewport(0, 0, static_cast<GLsizei>(gHWTargetW),
                       static_cast<GLsizei>(gHWTargetH));
        }
        g.run();
    }
    // Only the GLES path disturbs our GL state. A Vulkan core has not
    // touched the context at all, nor has a desktop GL core, which drew in
    // its own, and calling this for them would be a handful of redundant GL
    // calls per frame for nothing.
    if (gHWWanted && !gHWVulkan && !gHWDesktop) restoreGLState();
    ++gFramesRun;
    takeAvAsked();
    if (afterFrame_) afterFrame_();
}

// A shape or a rate the core asked for during the run just made (#220).
void Core::takeAvAsked() {
    if (!gGeometryAsked && !gTimingAsked) return;
    if (gGeometryAsked) {
        gGeometryAsked = false;
        if (gGeometry.base_width > 0 && gGeometry.base_height > 0) {
            av_.baseWidth = gGeometry.base_width;
            av_.baseHeight = gGeometry.base_height;
        }
        av_.aspectRatio = gGeometry.aspect_ratio > 0.0f ? gGeometry.aspect_ratio : 0.0f;
        // A NEW MAXIMUM ONLY FOR A PICTURE IN MEMORY, which is copied at
        // whatever size each frame comes. A hardware core's target was built
        // from the maximum at load; RetroArch rebuilds its video driver for a
        // bigger one, and this host does not.
        if (!gHWWanted && gGeometry.max_width > 0 && gGeometry.max_height > 0) {
            av_.maxWidth = gGeometry.max_width;
            av_.maxHeight = gGeometry.max_height;
        }
    }
    if (gTimingAsked) {
        gTimingAsked = false;
        if (gTiming.fps > 0) av_.fps = gTiming.fps;
        if (gTiming.sample_rate > 0 && gTiming.sample_rate != av_.sampleRate) {
            av_.sampleRate = gTiming.sample_rate;
            ++soundRateChanges_;
        }
    }
    std::fprintf(stderr, "[core] now %ux%u, %.4f fps, %.0f Hz, aspect %.4f\n", av_.baseWidth,
                 av_.baseHeight, av_.fps, av_.sampleRate, av_.aspectRatio);
}

bool Core::uploadFrame() {
    if (gIsPs2) {
        // PCSX2 PRESENTS ITS OWN PICTURE (#226): there is nothing to upload,
        // and nothing of the game for this window to draw.
        if (ps2::windowed()) return false;
        const uint32_t* pixels = nullptr;
        unsigned w = 0, h = 0;
        if (!ps2::takeFrame(&pixels, w, h)) {
            // Nothing new. The last frame stays uploaded, which is what makes
            // a paused game sit under the overlay rather than going black.
            return texture_ != 0;
        }
        if (!texture_) {
            glGenTextures(1, &texture_);
            glBindTexture(GL_TEXTURE_2D, texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, texture_);
        }
        // Reallocated only when the shape changes. It does change mid-game:
        // Homura goes from 640x448 to 640x512 the moment it leaves its own
        // video-mode menu, and a PAL game switching modes is ordinary.
        if (w != frameWidth_ || h != frameHeight_) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            frameWidth_ = w;
            frameHeight_ = h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        gFramesRun++;
        return true;
    }
    if (gHWVulkan) {
        // The one place the two APIs meet. present() copies whatever the core
        // last handed over into the exported image, which IS the GL texture —
        // same memory, no readback. It runs here rather than in the frame loop
        // because this is already the call every screen makes on the GL thread
        // before it draws a picture.
        frameWidth_ = gFrameW;
        frameHeight_ = gFrameH;
        gFrameDirty = false;
        vk::present();
        return vk::texture() != 0;
    }
    if (gHWFrame) {
        // Nothing to do, and that is the whole point of sharing one context:
        // the core has already drawn into the texture the player is about to
        // sample. No copy, no upload, no readback.
        frameWidth_ = gFrameW;
        frameHeight_ = gFrameH;
        gFrameDirty = false;
        return gHWColor != 0;
    }
    return uploadSoftwareFrame();
}

const std::vector<int16_t>& Core::drainAudio() {
    if (gIsPs2) {
        gAudioDrain.clear();
        ps2::drainAudio(gAudioDrain);
        // Counted here rather than in the emulator, because this is the number
        // `running()` is built on — audio is the one output that does not
        // appear until the machine actually runs.
        gAudioFrames += gAudioDrain.size() / 2;
        return gAudioDrain;
    }
    gAudioDrain.swap(gAudio);
    gAudio.clear();
    return gAudioDrain;
}

uint64_t Core::framesRun() const { return gFramesRun; }
uint64_t Core::audioFramesTotal() const { return gAudioFrames; }

}  // namespace cab
