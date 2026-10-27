// See ps2.h. This is the frontend's side of the wire; the emulator's side is
// frontend/ps2/CabinetPS2Bridge.cpp and it is built separately, against PCSX2.

#include "ps2.h"

#include "netblock.h"
#include "rumble.h"
#include "storage.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

#include <json-c/json.h>

namespace {

void* gHandle = nullptr;
std::string gError;

// Every entry point the shared object publishes. Resolved once, at load, and
// asserted — because a symbol that resolves to null and is called later is a
// crash on the television with no message, while a missing one found here is
// one line saying which.
struct Api {
    int      (*start)(const char*, const char*, const char*, const char*, const char*, const char*, float, int, int) = nullptr;
    const char* (*error)() = nullptr;
    int      (*running)() = nullptr;
    void     (*stop)() = nullptr;
    void     (*setPaused)(int) = nullptr;
    void     (*setPad)(unsigned, uint32_t, float, float, float, float, float, float) = nullptr;
    int      (*takeFrame)(const uint32_t**, unsigned*, unsigned*, uint64_t*) = nullptr;
    unsigned (*drainAudio)(int16_t*, unsigned) = nullptr;
    unsigned (*sampleRate)() = nullptr;
    void     (*metrics)(float*, float*, double*) = nullptr;
    const char* (*version)() = nullptr;
    // Optional: a library built before rumble (#149) lacks it, and PS2 plays
    // on without motors rather than refusing to start.
    void     (*getRumble)(unsigned, float*, float*) = nullptr;
    // Optional too: a library built before #226 has no window and no capture,
    // and reads every frame back as it always did.
    void     (*setWindow)(const char*, unsigned long, unsigned, unsigned) = nullptr;
    int      (*captureFrame)(unsigned) = nullptr;
    // Optional: RetroAchievements (#74). A library without them plays on with
    // no achievements rather than refusing to start.
    uint8_t* (*memory)(unsigned, size_t*) = nullptr;
    void     (*setFrameCallback)(void (*)(void*), void*) = nullptr;
    // Optional: the per-game Widescreen row (#368). A library without them
    // keeps every patched game widescreen and the row never shows.
    void     (*setWidescreen)(int) = nullptr;
    int      (*hasWidescreenPatch)() = nullptr;
    // Optional: the BIOS by the game's region (#416). A library without it
    // takes the first BIOS it finds, as it always did.
    void     (*setDiscCode)(const char*) = nullptr;
} gApi;

bool gWindowed = false;
bool gPaused = false;
uint64_t gFramesTaken = 0;

uint64_t gFrameSerial = 0;

template <typename T>
bool resolve(T& fn, const char* name) {
    fn = reinterpret_cast<T>(dlsym(gHandle, name));
    if (!fn) {
        gError = std::string("cabinetos-ps2.so has no ") + name;
        return false;
    }
    return true;
}

} // namespace

bool ps2::load(const std::string& soPath) {
    // Loaded once and kept (ps2::unload), but every game is a new one to the
    // network block's log.
    if (gHandle) {
        netblock::addEmulator(gHandle, soPath);
        return true;
    }

    // RTLD_LOCAL for the same reason every libretro core gets it: PCSX2 brings
    // its own copies of fmt, imgui, zlib and a dozen more, and letting those
    // into the global namespace would let them answer a call the frontend
    // meant for its own.
    gHandle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!gHandle) {
        const char* why = dlerror();
        gError = "could not open " + soPath + ": " + (why ? why : "no reason given");
        return false;
    }
    // Before cps2_start, so every thread PCSX2 starts carries the mark.
    netblock::addEmulator(gHandle, soPath);

    const bool ok =
        resolve(gApi.start, "cps2_start") &&
        resolve(gApi.error, "cps2_error") &&
        resolve(gApi.running, "cps2_running") &&
        resolve(gApi.stop, "cps2_stop") &&
        resolve(gApi.setPaused, "cps2_set_paused") &&
        resolve(gApi.setPad, "cps2_set_pad") &&
        resolve(gApi.takeFrame, "cps2_take_frame") &&
        resolve(gApi.drainAudio, "cps2_drain_audio") &&
        resolve(gApi.sampleRate, "cps2_audio_sample_rate") &&
        resolve(gApi.metrics, "cps2_metrics") &&
        resolve(gApi.version, "cps2_version");

    gApi.getRumble =
        reinterpret_cast<decltype(gApi.getRumble)>(dlsym(gHandle, "cps2_get_rumble"));
    gApi.setWindow =
        reinterpret_cast<decltype(gApi.setWindow)>(dlsym(gHandle, "cps2_set_window"));
    gApi.captureFrame =
        reinterpret_cast<decltype(gApi.captureFrame)>(dlsym(gHandle, "cps2_capture_frame"));
    gApi.memory = reinterpret_cast<decltype(gApi.memory)>(dlsym(gHandle, "cps2_memory"));
    gApi.setFrameCallback = reinterpret_cast<decltype(gApi.setFrameCallback)>(
        dlsym(gHandle, "cps2_set_frame_callback"));
    gApi.setWidescreen =
        reinterpret_cast<decltype(gApi.setWidescreen)>(dlsym(gHandle, "cps2_set_widescreen"));
    gApi.hasWidescreenPatch = reinterpret_cast<decltype(gApi.hasWidescreenPatch)>(
        dlsym(gHandle, "cps2_has_widescreen_patch"));
    gApi.setDiscCode =
        reinterpret_cast<decltype(gApi.setDiscCode)>(dlsym(gHandle, "cps2_set_disc_code"));

    if (!ok) {
        dlclose(gHandle);
        netblock::dropUnloaded();
        gHandle = nullptr;
        return false;
    }

    std::fprintf(stderr, "[ps2] %s (PCSX2 %s)\n", soPath.c_str(), gApi.version());
    return true;
}

void ps2::unload() {
    if (!gHandle) return;
    // NOT dlclose'd, deliberately, and this is the same decision the libretro
    // path makes. PCSX2 starts threads and registers atexit handlers; closing
    // the library out from under them has no defined behaviour and the failure
    // would arrive at process exit, a long way from the cause. A console runs
    // one emulator at a time and re-loading the same object is free.
    gHandle = gHandle;
}

const std::string& ps2::error() { return gError; }

std::string ps2::version() {
    return gApi.version ? gApi.version() : std::string();
}

bool ps2::startGame(const std::string& discPath, const std::string& biosDir,
                    const std::string& memcardsDir, const std::string& memoryCard,
                    const std::string& scratchDir, const std::string& resourcesDir,
                    float upscale, int anisotropy, bool widescreen,
                    const std::string& discCode) {
    if (!gHandle) { gError = "no PlayStation 2 emulator is loaded"; return false; }

    gFrameSerial = 0;
    if (gApi.setWidescreen) gApi.setWidescreen(widescreen ? 1 : 0);
    if (gApi.setDiscCode) gApi.setDiscCode(discCode.c_str());
    if (!gApi.start(discPath.c_str(), biosDir.c_str(), memcardsDir.c_str(), memoryCard.c_str(),
                    scratchDir.c_str(), resourcesDir.c_str(), upscale, anisotropy, 1)) {
        gError = gApi.error();
        if (gError.empty()) gError = "PCSX2 would not start";
        return false;
    }
    return true;
}

// cps2_error is written once, by PCSX2's thread as it ends, before the
// machine counts as stopped, and cleared only by the next cps2_start; it is
// read here only while cps2_running says no.
std::string ps2::stoppedWhy() {
    if (!gHandle || gApi.running()) return {};
    const char* why = gApi.error();
    return why ? why : "";
}

bool ps2::hasWidescreenPatch() {
    return gHandle && gApi.hasWidescreenPatch && gApi.hasWidescreenPatch() != 0;
}

namespace {

std::string widescreenPath() { return storage::configDir() + "/ps2-widescreen.json"; }

json_object* loadWidescreen() {
    json_object* o = json_object_from_file(widescreenPath().c_str());
    if (o && json_object_get_type(o) == json_type_object) return o;
    if (o) json_object_put(o);
    return json_object_new_object();
}

}  // namespace

bool ps2::widescreenOn(int romId) {
    json_object* o = loadWidescreen();
    json_object* v = nullptr;
    const bool off = json_object_object_get_ex(o, std::to_string(romId).c_str(), &v) &&
                     json_object_get_type(v) == json_type_string &&
                     std::strcmp(json_object_get_string(v), "off") == 0;
    json_object_put(o);
    return !off;
}

void ps2::setWidescreenOn(int romId, bool on) {
    json_object* o = loadWidescreen();
    const std::string key = std::to_string(romId);
    if (on)
        json_object_object_del(o, key.c_str());
    else
        json_object_object_add(o, key.c_str(), json_object_new_string("off"));
    storage::makeDirs(storage::configDir());
    const std::string tmp = widescreenPath() + ".part";
    if (json_object_to_file_ext(tmp.c_str(), o, JSON_C_TO_STRING_PRETTY) != 0 ||
        std::rename(tmp.c_str(), widescreenPath().c_str()) != 0)
        std::fprintf(stderr, "[ps2] could not write %s\n", widescreenPath().c_str());
    json_object_put(o);
}

bool ps2::setWindow(const std::string& display, unsigned long window, unsigned width,
                    unsigned height) {
    gWindowed = false;
    if (!gHandle || !gApi.setWindow || !gApi.captureFrame) return window == 0;
    gApi.setWindow(display.c_str(), window, width, height);
    gWindowed = window != 0;
    return true;
}

bool ps2::windowed() { return gWindowed; }

bool ps2::captureFrame(unsigned timeoutMs) {
    if (!gHandle || !gApi.captureFrame) return false;
    return gApi.captureFrame(timeoutMs) != 0;
}

void ps2::report() {
    static uint64_t nextNs = 0, takenAtLast = 0;
    // Not while paused: PCSX2's reading is the last one from before the
    // pause, and a line saying 60 fps over a frozen game misleads.
    if (!gHandle || gPaused) return;
    const uint64_t now = SDL_GetTicksNS();
    if (nextNs == 0 || now < nextNs) {
        if (nextNs == 0) { nextNs = now + 10'000'000'000ull; takenAtLast = gFramesTaken; }
        return;
    }
    float fps = 0, speed = 0;
    double readbackUs = 0;
    gApi.metrics(&fps, &speed, &readbackUs);
    if (gWindowed)
        std::fprintf(stderr, "[ps2] %.1f fps, speed %.0f%%, presented by PCSX2\n", fps,
                     speed);
    else
        std::fprintf(stderr, "[ps2] %.1f fps, speed %.0f%%, %.1f new frames/s shown, "
                     "readback %.0f us\n", fps, speed,
                     (gFramesTaken - takenAtLast) / 10.0, readbackUs);
    takenAtLast = gFramesTaken;
    nextNs = now + 10'000'000'000ull;
}

void ps2::stopGame() {
    if (!gHandle) return;
    gApi.stop();
    gFrameSerial = 0;
    gPaused = false;
    // The window goes with the game; the next one is given its own, or none.
    if (gApi.setWindow) gApi.setWindow("", 0, 0, 0);
    gWindowed = false;
}

void ps2::setPaused(bool paused) {
    gPaused = paused;
    if (gHandle) gApi.setPaused(paused ? 1 : 0);
}

void ps2::setPad(int port, uint32_t buttons, float leftX, float leftY, float rightX, float rightY,
                 float leftTrigger, float rightTrigger) {
    if (!gHandle || port < 0) return;
    gApi.setPad(static_cast<unsigned>(port), buttons, leftX, leftY, rightX, rightY,
                leftTrigger, rightTrigger);
}

void ps2::pollRumble() {
    if (!gHandle || !gApi.getRumble) return;
    for (unsigned port = 0; port < 2; ++port) {
        float large = 0, small = 0;
        gApi.getRumble(port, &large, &small);
        auto level = [](float v) {
            return static_cast<uint16_t>(v <= 0 ? 0 : v >= 1 ? 65535 : v * 65535.0f);
        };
        rumble::set(port, 0, level(large));
        rumble::set(port, 1, level(small));
    }
}

bool ps2::takeFrame(const uint32_t** pixels, unsigned& width, unsigned& height) {
    if (!gHandle) return false;
    unsigned w = 0, h = 0;
    if (!gApi.takeFrame(pixels, &w, &h, &gFrameSerial)) return false;
    ++gFramesTaken;
    width = w;
    height = h;
    return true;
}

void ps2::drainAudio(std::vector<int16_t>& out) {
    if (!gHandle) return;
    // A generous slice: at 48 kHz a 60 Hz frame is 800 stereo frames, and a
    // frontend that stalls briefly must not lose the sound made while it did.
    // Asking for more than exists costs nothing — the stream pads with the
    // silence it would have played anyway.
    static constexpr unsigned kMaxFrames = 8192;
    const size_t at = out.size();
    out.resize(at + kMaxFrames * 2);
    const unsigned got = gApi.drainAudio(out.data() + at, kMaxFrames);
    out.resize(at + static_cast<size_t>(got) * 2);
}

uint8_t* ps2::memory(unsigned region, size_t* size) {
    *size = 0;
    if (!gHandle || !gApi.memory) return nullptr;
    return gApi.memory(region, size);
}

bool ps2::setFrameCallback(void (*cb)(void*), void* user) {
    if (!gHandle || !gApi.setFrameCallback) return false;
    gApi.setFrameCallback(cb, user);
    return true;
}
