// The libretro host's shared state (#398). Libretro's callbacks are plain C
// function pointers with no context argument, so the host is one class over
// process-wide state (core.h), and that state was all in core.cpp's anonymous
// namespaces. The restructure splits the host into files by what they do;
// everything those files share is declared here, in one named namespace,
// under the file that defines it, so each piece has one owner and a copy is a
// link error rather than a second variable that silently goes its own way.
// Nothing outside the host includes this. The host's files name these
// unqualified, through `using namespace corestate;`.

#pragma once

#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "libretro.h"

namespace cab::corestate {

// The core's retro_* entry points, which Core::load resolves (core.cpp).
struct EntryPoints {
    unsigned (*api_version)(void) = nullptr;
    void (*get_system_info)(retro_system_info*) = nullptr;
    void (*get_system_av_info)(retro_system_av_info*) = nullptr;
    void (*set_environment)(retro_environment_t) = nullptr;
    void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*set_input_poll)(retro_input_poll_t) = nullptr;
    void (*set_input_state)(retro_input_state_t) = nullptr;
    void (*set_controller_port_device)(unsigned, unsigned) = nullptr;
    void (*init)(void) = nullptr;
    void (*deinit)(void) = nullptr;
    bool (*load_game)(const retro_game_info*) = nullptr;
    void (*unload_game)(void) = nullptr;
    void (*run)(void) = nullptr;
    size_t (*serialize_size)(void) = nullptr;
    bool (*serialize)(void*, size_t) = nullptr;
    bool (*unserialize)(const void*, size_t) = nullptr;
    void* (*get_memory_data)(unsigned) = nullptr;
    size_t (*get_memory_size)(unsigned) = nullptr;
};

// Defined in core.cpp.
extern EntryPoints g;
extern unsigned gPixelFormat;
extern unsigned gRotation;
extern bool gRotationAnnounced;
extern std::vector<uint8_t> gFrame;
extern unsigned gFrameW, gFrameH;
extern size_t gFramePitch;
extern bool gFrameDirty;
extern uint64_t gAudioFrames;
extern uint64_t gFramesRun;
extern bool gIsPs2;
extern std::string gPs2ResourcesDir;
extern float gPs2Upscale;
extern int gPs2Anisotropy;
extern bool gPs2Widescreen;
extern std::string gPs2DiscCode;

// Defined in core_options.cpp.
void resetOptions();
void checkOverrides(const std::string& core);
std::optional<bool> environmentOptions(unsigned cmd, void* data);

// Defined in core_input.cpp.
extern int gPlayers;
extern unsigned gPortsPlugged;
extern unsigned gPadDevice;
extern int gRealRemotesFrom;
extern unsigned gCorePorts;
extern bool gInputDescriptorsLogged;
void clearInputDescriptors();
void releaseKeys();
void forgetKeyboard();
void inputPoll(void);
int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id);
std::optional<bool> environmentInput(unsigned cmd, void* data);

// Defined in core_memory.cpp.
void keepMemoryMap(const retro_memory_map* mm);
std::optional<bool> environmentMemory(unsigned cmd, void* data);

// Defined in core_discs.cpp.
extern retro_disk_control_ext_callback gDisk;
extern bool gDiskSet;
extern int gInitialDisc;
extern std::string gInitialDiscPath;
std::optional<bool> environmentDiscs(unsigned cmd, void* data);

// Defined in core_hwrender.cpp.
extern bool gRefuseHWRender;
extern retro_hw_render_callback gHW;
extern bool gHWWanted;
extern bool gHWVulkan;
extern bool gHWContextLive;
extern GLuint gHWFBO, gHWColor;
extern unsigned gHWTargetW, gHWTargetH;
extern bool gHWFrame;
extern std::string gHWContextName;
extern bool gHWDesktop;
// The framebuffer the core draws into, in its own context for a desktop GL
// core (gHWFBO otherwise).
uintptr_t currentFramebuffer(void);
bool desktopGLServable();
void closeDesktopContext();
void destroyHWTarget();
bool ensureHWTarget(unsigned w, unsigned h);
// A desktop GL core's context is current for as long as one of these lives,
// and the UI's again after, with what the core drew finished first. Around
// every call into a core that can touch GL; nothing at all for any other
// core. See core_hwrender.cpp.
class CoreContext {
public:
    CoreContext();
    ~CoreContext();
    CoreContext(const CoreContext&) = delete;
    CoreContext& operator=(const CoreContext&) = delete;

private:
    bool switched_;
};
void restoreGLState();
std::optional<bool> environmentHWRender(unsigned cmd, void* data);

// Defined in core_picture.cpp.
void videoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);

}  // namespace cab::corestate
