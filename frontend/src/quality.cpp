#include "quality.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

#include <json-c/json.h>

#include "core.h"
#include "gpu.h"
#include "prefs.h"
#include "remoteplay.h"
#include "storage.h"

namespace quality {

namespace {

// THE TABLE, one row per level: Performance, Balanced, Quality. Performance is
// each system's own resolution, Balanced about 1080p, Quality about 4K, using
// the emulator's own "~1080p" and "~4K" steps where it labels them. Every
// Quality value was measured on the A9 (2026-10-02) and stepped down where
// 4K was tight there: PS2 and Wii 5x rather than 6x, N64 4x rather than 8x.
// Starting values, judged on the TV before they ship and adjustable in one
// line each.
template <typename T>
const T& pick(Level l, const T (&row)[kLevelCount]) {
    return row[static_cast<int>(l)];
}

std::string gamesPath() { return storage::configDir() + "/picture-quality.json"; }

json_object* loadGames() {
    std::string body;
    if (FILE* f = std::fopen(gamesPath().c_str(), "rb")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
    }
    json_object* o = body.empty() ? nullptr : json_tokener_parse(body.c_str());
    if (o && json_object_get_type(o) == json_type_object) return o;
    if (o) json_object_put(o);
    return json_object_new_object();
}

}  // namespace

const char* levelName(Level l) {
    static const char* const kNames[] = {"Performance", "Balanced", "Quality"};
    return kNames[static_cast<int>(l)];
}

const char* levelWord(Level l) {
    static const char* const kWords[] = {"performance", "balanced", "quality"};
    return kWords[static_cast<int>(l)];
}

bool levelFromWord(const std::string& word, Level* out) {
    for (int i = 0; i < kLevelCount; ++i) {
        if (word == levelWord(static_cast<Level>(i))) {
            *out = static_cast<Level>(i);
            return true;
        }
    }
    return false;
}

namespace {

// What the log says about the processor, which does not decide anything:
// its threads, its fastest core and its widest vector instructions.
std::string processorLine() {
    unsigned long topKhz = 0;
    const unsigned threads = std::thread::hardware_concurrency();
    for (unsigned i = 0; i < threads; ++i) {
        std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(i) +
                        "/cpufreq/cpuinfo_max_freq");
        unsigned long khz = 0;
        if (f >> khz && khz > topKhz) topKhz = khz;
    }
    std::ifstream info("/proc/cpuinfo");
    std::string line, vector = "no AVX2";
    while (std::getline(info, line)) {
        if (line.compare(0, 5, "flags") != 0) continue;
        if (line.find(" avx512f") != std::string::npos) vector = "AVX-512";
        else if (line.find(" avx2") != std::string::npos) vector = "AVX2";
        break;
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "%u threads, top %lu MHz, %s", threads, topKhz / 1000,
                  vector.c_str());
    return buf;
}

}  // namespace

Level machineClass() {
    static const Level kClass = [] {
        const auto t0 = std::chrono::steady_clock::now();
        const cab::gpu::VulkanCaps& v = cab::gpu::vulkan();
        Level l = Level::Performance;
        char why[256];
        if (!v.available || v.vendorId != 0x1002 || v.computeUnits == 0 || v.topClockMhz == 0) {
            std::snprintf(why, sizeof why, "%s does not report its size (%u units, %u MHz)",
                          v.deviceName.empty() ? "no Vulkan device" : v.deviceName.c_str(),
                          v.computeUnits, v.topClockMhz);
        } else {
            const double score = v.computeUnits * (v.topClockMhz / 1000.0);
            if (score >= 40) l = Level::Quality;
            else if (score >= 20) l = Level::Balanced;
            std::snprintf(why, sizeof why, "%s, %u units at %u MHz, score %.0f",
                          v.deviceName.c_str(), v.computeUnits, v.topClockMhz, score);
        }
        const std::string cpu = processorLine();
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr,
                     "[quality] this machine: %s (%s; processor %s, not counted; %lld ms)\n",
                     levelName(l), why, cpu.c_str(), ms);
        return l;
    }();
    return kClass;
}

Level console() {
    Level l;
    if (levelFromWord(prefs::get("picture_quality", ""), &l)) return l;
    return machineClass();
}

void setConsole(Level l) { prefs::set("picture_quality", levelWord(l)); }

// A FILE OF ITS OWN, `config/picture-quality.json`, `{"<rom id>": "balanced"}`,
// rather than lines in settings.json: it grows with the library, and
// settings.json is the handful of words a person reads over file access.
std::optional<Level> gameChoice(int romId) {
    json_object* o = loadGames();
    std::optional<Level> out;
    json_object* v = nullptr;
    Level l;
    if (json_object_object_get_ex(o, std::to_string(romId).c_str(), &v) &&
        json_object_get_type(v) == json_type_string && levelFromWord(json_object_get_string(v), &l))
        out = l;
    json_object_put(o);
    return out;
}

void setGameChoice(int romId, std::optional<Level> l) {
    json_object* o = loadGames();
    const std::string key = std::to_string(romId);
    if (l)
        json_object_object_add(o, key.c_str(), json_object_new_string(levelWord(*l)));
    else
        json_object_object_del(o, key.c_str());
    storage::makeDirs(storage::configDir());
    const std::string tmp = gamesPath() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY), f);
        std::fputc('\n', f);
        const bool ok = std::fflush(f) == 0;
        std::fclose(f);
        if (!ok || std::rename(tmp.c_str(), gamesPath().c_str()) != 0)
            std::fprintf(stderr, "[quality] could not write %s\n", gamesPath().c_str());
    }
    json_object_put(o);
}

Level forGame(int romId) {
    // FORK (handheld): WHILE A STREAM IS OPEN THE GAME RUNS AT PERFORMANCE, its
    // own choice or not. The picture is drawn and encoded for the friend by the
    // same chip, and a heavy game (PS2, GameCube) that falls behind clips its
    // sound. Decided when the game starts: a friend who joins later does not
    // change a game already running.
    if (remoteplay::sessionOpen()) return Level::Performance;
    if (const std::optional<Level> own = gameChoice(romId)) return *own;
    return console();
}

bool hasLevels(const std::string& core) {
    if (core == "melonds") return cab::Core::canServeDesktopGL();
    for (const char* c : {"pcsx2", "dolphin", "ppsspp", "mupen64plus", "flycast", "azahar",
                          "eden", "rpcs3", "xemu", "vita3k"})
        if (core == c) return true;
    return false;
}

std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, Level l) {
    return coreOptions(core, platformSlug, l, cab::gpu::vulkan().available,
                       core == "melonds" && cab::Core::canServeDesktopGL());
}

std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, Level l,
                                               bool vulkan, bool desktopGL) {
    // GameCube and Wii: internal resolution, and shaders built in the
    // background (2, Async UberShaders) above Performance, when first needed
    // (0, Synchronous) at it. Anisotropic filtering 16x is `4` in Dolphin's
    // numbering. Wii's Quality is 5x: 6x was tight on the A9 in Mario Kart
    // Wii's racing (1.2 to 1.5x realtime), GameCube's 6x comfortable (2.6x).
    if (core == "dolphin") {
        static const char* const kGameCube[] = {"1", "3", "6"};
        static const char* const kWii[] = {"1", "3", "5"};
        static const char* const kShaders[] = {"0", "2", "2"};
        return {
            {"dolphin_efb_scale", pick(l, platformSlug == "wii" ? kWii : kGameCube)},
            {"dolphin_shader_compilation_mode", pick(l, kShaders)},
            {"dolphin_max_anisotropy", "4"},
        };
    }
    // PSP: 480x272, then 4x (1920x1088) and 8x (3840x2176), PPSSPP's own
    // steps. Anisotropic filtering is already 16x by default; said anyway, so
    // a core bump that changed the default is caught by the option check.
    if (core == "ppsspp") {
        static const char* const kRes[] = {"480x272", "1920x1088", "3840x2176"};
        return {
            {"ppsspp_internal_resolution", pick(l, kRes)},
            {"ppsspp_texture_anisotropic_filtering", "16x"},
        };
    }
    // Nintendo 64. ParaLLEl-RDP with Vulkan (catalog::optionOverrides chooses
    // it): 1x, 2x, 4x. Quality is 4x because 8x left about 10% to spare at
    // 60 fps on the A9. Balanced was 4x too until phase 2 (MMagTech,
    // 2026-10-04): once machines start on their class, Balanced has to be
    // lighter than Quality to be worth choosing. Without Vulkan, GLideN64 by
    // the same rule: 320x240, 640x480, 1280x960.
    if (core == "mupen64plus") {
        if (vulkan) {
            static const char* const kScale[] = {"1x", "2x", "4x"};
            return {{"mupen64plus-parallel-rdp-upscaling", pick(l, kScale)}};
        }
        static const char* const kSize[] = {"320x240", "640x480", "1280x960"};
        return {{"mupen64plus-43screensize", pick(l, kSize)}};
    }
    // Nintendo DS (#219): the software renderer at Performance, which is the
    // DS's own 256x192 a screen and how DS always looked here; melonDS's
    // OpenGL renderer above it, at 4x (the two screens 1024x1536) and 6x
    // (1536x2304, about 4K). Not 3x: with the OpenGL renderer melonDS builds
    // its picture at 4x or more whatever it is asked (screenlayout.cpp, "at
    // least 4x the native res"), so 3x only stretched a smaller 3D render to
    // 4x (A9, 2026-10-18). Only the 3D is drawn larger; the 2D layers stay
    // the DS's own pixels, enlarged. On the A9, 6x kept the graphics chip 30
    // to 36% busy at 60 fps under gamescope (Mario Kart DS). RetroArch and Batocera
    // ship the OpenGL renderer off and leave it to the player: melonDS's
    // software renderer is the accurate one, and a game the OpenGL renderer
    // gets wrong plays at Performance from its pause menu. No desktop OpenGL
    // here (Core::canServeDesktopGL), no OpenGL renderer: melonDS draws
    // nothing at all when it is on and its context is refused.
    if (core == "melonds") {
        if (l == Level::Performance || !desktopGL)
            return {{"melonds_opengl_renderer", "disabled"}};
        static const char* const kRes[] = {"1x native (256x192)", "4x native (1024x768)",
                                           "6x native (1536x1152)"};
        return {
            {"melonds_opengl_renderer", "enabled"},
            {"melonds_opengl_resolution", pick(l, kRes)},
        };
    }
    // Dreamcast and Naomi: 640x480, 1440x1080, 2880x2160. Anisotropic
    // filtering from Flycast's 4x to 16x.
    if (core == "flycast") {
        static const char* const kRes[] = {"640x480", "1440x1080", "2880x2160"};
        return {
            {"reicast_internal_resolution", pick(l, kRes)},
            {"reicast_anisotropic_filtering", "16"},
        };
    }
    // Nintendo 3DS (docs/PROJECT.md question 50), sized for the side by side
    // layout coreoptions.cpp sets, which draws the top screen at 1067x640 on
    // a 1080p TV and 2133x1280 on a 4K one: 1x, the 3DS's own 400x240, as
    // PSP, N64 and DS are at Performance; 3x (1200x720), just above a 1080p
    // TV's; 6x (2400x1440), just above a 4K TV's. Everything else at the
    // core's own value, accurate shader multiplication included.
    if (core == "azahar") {
        static const char* const kFactor[] = {"1", "3", "6"};
        return {{"citra_resolution_factor", pick(l, kFactor)}};
    }
    // FIXED AT EVERY LEVEL (MMagTech, 2026-10-02): the processor-side
    // extras that cost nothing a modern PC notices.
    //
    // 3DO at 640x480 rather than 320x240, and its sound processor on its own
    // thread: both as Batocera ships them.
    if (core == "opera") {
        return {
            {"opera_high_resolution", "enabled"},
            {"opera_dsp_threaded", "enabled"},
        };
    }
    // Vector games drawn at their sharpest: thin lines stretched to 4K
    // otherwise look soft and thick.
    if (core == "vecx") return {{"vecx_res_multi", "4"}};
    if (core == "mame2003_plus") return {{"mame2003-plus_vector_resolution", "1707x1280"}};
    return {};
}

Ps2 ps2(Level l) {
    // PS2: 1x, 3x (PCSX2's "~1080p"), 4x. Since #226 PCSX2 presents in a
    // window of its own and no frame is copied back, and the limit is the
    // GPU itself. Burnout 3, a race on the A9, 40 s, frames.py, 2026-10-04:
    // 3x, 11 and 14 frames late of ~2,390, GPU 36% busy; 4x, 17, 98 and 129
    // late, GPU 57%, the late ones its heaviest moments; 5x, PCSX2 itself at
    // 68 to 77% speed with the GPU at 99%, and the same with anisotropic
    // filtering off, so filtering costs next to nothing and stays 16x. 4x
    // is 2560x1792 (about 3186x1792 with a widescreen patch); the TV is
    // 3840x2160. Until #226 the copy held Quality at 3x (5x copied: 312 of
    // 1,196 over 20 ms, "running horribly", 2026-10-02).
    static const float kUpscale[] = {1.0f, 3.0f, 4.0f};
    Ps2 p;
    p.upscale = pick(l, kUpscale);
    p.anisotropy = 16;
    return p;
}

std::vector<Setting> eden(Level l) {
    // Switch, always docked (1920x1080 is its own resolution): 1x, 1x, 2x.
    // Eden numbers its steps from a quarter, so 1x is 3 and 2x is 6
    // (settings_enums.h, ResolutionSetup). Anisotropic 16x is 5 (AnisotropyMode:
    // Automatic, Default, X2, X4, X8, X16). Shaders in the background above
    // Performance. Not yet measured on the A9.
    static const char* const kRes[] = {"3", "3", "6"};
    static const char* const kAsync[] = {"false", "true", "true"};
    return {
        {"Renderer", "resolution_setup", pick(l, kRes)},
        {"Renderer", "max_anisotropy", "5"},
        {"Renderer", "use_asynchronous_shaders", pick(l, kAsync)},
    };
}

std::string rpcs3(Level l) {
    // PS3, whose games are 720p: 100%, 150% (1080p), 150% again at Quality.
    // Measured on the A9, 2026-10-04 (God of War III, frames.py): 300% ran
    // 21 frames a second with the graphics chip 87% busy at its top clock;
    // 150% 54, at about a third of its capacity; 100% 57. 200% was not
    // measured (the game would not load past its menu that once). The game's
    // own video mode stays 720p; that one changes the game. Lines inside
    // RPCS3's `Video:` section, which standalone_rpcs3.cpp opens.
    static const char* const kScale[] = {"100", "150", "150"};
    return std::string("  Resolution Scale: ") + pick(l, kScale) +
           "\n  Anisotropic Filter Override: 16\n";
}

std::string xemu(Level l) {
    // Xbox, 640x480: 1x, 2x, 4x. xemu has no anisotropic filtering setting.
    // Not yet measured.
    static const char* const kScale[] = {"1", "2", "4"};
    return std::string("surface_scale = ") + pick(l, kScale) + "\n";
}

std::vector<std::string> xenia(Level l) {
    // Xbox 360, 720p, at every level on the A9. Measured 2026-10-04 (Forza
    // Horizon 2, frames.py): 3x ran 7.5 frames a second with the graphics
    // chip 99% busy; 1x a steady 30 at under a third of its capacity, so 2x,
    // four times the pixels, would not hold either. A machine stronger than
    // the A9 could take more; that waits for a class above it. Edge's
    // anisotropic override counts 1 to 5 for 1x to 16x.
    static const char* const kScale[] = {"1", "1", "1"};
    const std::string s = pick(l, kScale);
    return {
        "--draw_resolution_scale_x=" + s,
        "--draw_resolution_scale_y=" + s,
        "--anisotropic_override=5",
    };
}

std::string vita3k(Level l) {
    // PlayStation Vita, 960x544: 1x, 2x (1920x1088, the 1080p step) and 4x
    // (3840x2176, the 4K step), the candidates of docs/PROJECT.md question
    // 49's plan, by this file's rule. Not yet measured on the A9: the hand
    // test's frame times fix them, stepped down where the A9 cannot hold a
    // game's full speed. Shaders built in the background (Vita3K's
    // asynchronous pipeline compilation, on as it ships) above Performance
    // and when first needed at it, as Dolphin's and Eden's are; kept only if
    // the hand test's first runs show it helps. Anisotropic filtering 16x at
    // every level. Lines of Vita3K's config.yml, which
    // standalone_vita3k.cpp writes whole.
    static const char* const kScale[] = {"1", "2", "4"};
    static const char* const kAsync[] = {"false", "true", "true"};
    return std::string("resolution-multiplier: ") + pick(l, kScale) +
           "\nasync-pipeline-compilation: " + pick(l, kAsync) +
           "\nanisotropic-filtering: 16\n";
}

void logApplied(const std::string& who, int romId, Level l) {
    const bool own = gameChoice(romId).has_value();
    std::fprintf(stderr, "[quality] %s: %s (%s)\n", who.c_str(), levelName(l),
                 remoteplay::sessionOpen() ? "a stream is open"
                 : own                     ? "this game's own choice"
                                           : "console setting");
}

}  // namespace quality
