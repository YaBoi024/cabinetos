// The Look row's lists by system, the stored choice and the preset it names
// (looks.h). The two looks defaultFor starts a system on are named in
// build_files/install-frontend.sh too. Moved out of screenfx.cpp in the
// restructure (#398).

#include "looks.h"

#include <cstdlib>
#include <cstring>
#include <map>

#include "arcadematch.h"
#include "prefs.h"

namespace screenfx {
namespace {

// THE TEST LIST (#122), every candidate MMagTech is judging on the TV. The
// labels are the shaders' own names so a choice can be named back; the list is
// cut to the keepers once he has seen them.
const std::vector<Look> kConsole = {
    {"plain", "Plain", nullptr},
    {"sharp-bilinear", "Sharp", "interpolation/sharp-bilinear-simple.glslp"},
    {"crt-easymode", "CRT easymode", "crt/crt-easymode.glslp"},
    {"crt-easymode-halation", "CRT easymode halation", "crt/crt-easymode-halation.glslp"},
    {"crt-lottes", "CRT lottes", "crt/crt-lottes.glslp"},
    {"crt-geom", "CRT geom", "crt/crt-geom.glslp"},
    {"zfast-crt", "CRT zfast", "crt/zfast-crt.glslp"},
    {"crt-aperture", "CRT aperture", "crt/crt-aperture.glslp"},
    {"crt-guest", "CRT guest", "crt/crt-guest-dr-venom.glslp"},
};
// N64, Dreamcast and 3DO are drawn above their own size (3DO at 640x480), and
// most CRT shaders draw one scanline per line of what they are given: close to
// one per TV row here, which crawls as the picture moves (zfast on N64 and
// Dreamcast, judged on the TV 2026-10-04), and crt-geom takes anything over 400
// lines for an interlaced signal and flickers, as do easymode-halation and
// guest (their own code: interlaced at 400 lines and up). easymode and lottes
// held steady; easymode by design, since it drops its scanlines at 400 lines
// and up (SCANLINE_CUTOFF) and keeps only its mask.
const std::vector<Look> kUpscaled = {
    {"plain", "Plain", nullptr},
    {"sharp-bilinear", "Sharp", "interpolation/sharp-bilinear-simple.glslp"},
    {"crt-easymode", "CRT easymode", "crt/crt-easymode.glslp"},
    {"crt-lottes", "CRT lottes", "crt/crt-lottes.glslp"},
    {"crt-aperture", "CRT aperture", "crt/crt-aperture.glslp"},
};
#define CABINETOS_LCD_LOOKS                                                     \
    {"plain", "Plain", nullptr},                                                \
    {"sharp-bilinear", "Sharp", "interpolation/sharp-bilinear-simple.glslp"},   \
    {"lcd3x", "LCD 3x", "handheld/lcd3x.glslp"},                                \
    {"lcd-grid-v2", "LCD grid", "handheld/lcd-grid-v2.glslp"},                  \
    {"zfast-lcd", "LCD zfast", "handheld/zfast-lcd.glslp"}
const std::vector<Look> kHandheld = {CABINETOS_LCD_LOOKS};
// The original Game Boy's screens. The Game Boy Light's backlit one was
// dropped on the TV: far too bright on a modern set (MMagTech, 2026-10-04). Each repaints the picture in its
// own shades, so they suit a mono game and override the Colors row.
const std::vector<Look> kGameBoy = {
    CABINETOS_LCD_LOOKS,
    {"gameboy", "Dot matrix", "handheld/gameboy.glslp"},
    {"gameboy-pocket", "Dot matrix Pocket", "handheld/gameboy-pocket.glslp"},
};
// Game Boy Color: the dot matrix that keeps the game's colours, since the
// green one turns a colour game green.
const std::vector<Look> kGameBoyColor = {
    CABINETOS_LCD_LOOKS,
    {"gbc-dot-matrix", "Dot matrix", "handheld/gbc-dot-matrix-white.glslp"},
};
#undef CABINETOS_LCD_LOOKS
const std::vector<Look> kNone;

// Up to and including Dreamcast, the systems this console draws itself
// (MMagTech, 2026-10-04), and every arcade folder (systemOf, below). Not PS2
// or newer, which are played upscaled.
const char* const kConsoleSlugs[] = {
    "nes", "snes", "genesis", "sms", "segacd", "sega32", "tg16", "turbografx-cd",
    "atari2600", "atari7800", "psx", "saturn",
};
const char* const kUpscaledSlugs[] = {"n64", "dc", "3do"};
const char* const kHandheldSlugs[] = {"gba", "gamegear", "neo-geo-pocket-color"};

// EVERY ARCADE FOLDER IS ONE SYSTEM HERE, whatever RomM labels it (CPS2,
// Naomi, MAME; arcadematch::isArcadeSlug): one list, and one stored choice.
std::string systemOf(const std::string& slug) {
    return arcadematch::isArcadeSlug(slug) ? "arcade" : slug;
}

std::string prefKey(const std::string& slug) { return "look." + systemOf(slug); }

// presetFor is asked every frame, and prefs reads the file each time.
std::map<std::string, std::string>& presetCache() {
    static std::map<std::string, std::string> m;
    return m;
}

}  // namespace

const std::vector<Look>& looksFor(const std::string& slug) {
    if (slug == "gb") return kGameBoy;
    if (slug == "gbc") return kGameBoyColor;
    for (const char* s : kHandheldSlugs)
        if (slug == s) return kHandheld;
    if (arcadematch::isArcadeSlug(slug)) return kConsole;
    for (const char* s : kConsoleSlugs)
        if (slug == s) return kConsole;
    for (const char* s : kUpscaledSlugs)
        if (slug == s) return kUpscaled;
    return kNone;
}

// ON BY DEFAULT (MMagTech, 2026-10-04): the plain picture is the weakest on a
// 4K set. Every TV system starts on crt-easymode: flat, so nothing in a corner
// is bent or cut, the brightest of the CRT looks, Batocera's own "Scanlines",
// and steady on N64. Every handheld starts on lcd3x, MMagTech's pick of the
// LCD looks on the TV.
static const char* defaultFor(const std::string& slug) {
    const auto& looks = looksFor(slug);
    if (looks.empty()) return "plain";
    if (&looks == &kConsole || &looks == &kUpscaled) return "crt-easymode";
    return "lcd3x";
}

int chosen(const std::string& slug) {
    const auto& looks = looksFor(slug);
    const std::string v = prefs::get(prefKey(slug), defaultFor(slug));
    for (size_t i = 0; i < looks.size(); ++i)
        if (v == looks[i].id) return static_cast<int>(i);
    // A look this system no longer offers (zfast on N64) falls back to the
    // default rather than being trusted, as in Cabinet.
    for (size_t i = 0; i < looks.size(); ++i)
        if (std::strcmp(defaultFor(slug), looks[i].id) == 0) return static_cast<int>(i);
    return 0;
}

void choose(const std::string& slug, int index) {
    const auto& looks = looksFor(slug);
    if (index < 0 || index >= static_cast<int>(looks.size())) return;
    prefs::set(prefKey(slug), looks[index].id);
    presetCache()[systemOf(slug)] =
        looks[index].preset ? shaderDir() + "/" + looks[index].preset : "";
}

std::string presetFor(const std::string& slug) {
    auto& cache = presetCache();
    auto it = cache.find(systemOf(slug));
    if (it != cache.end()) return it->second;
    const auto& looks = looksFor(slug);
    std::string out;
    if (!looks.empty()) {
        const Look& l = looks[chosen(slug)];
        if (l.preset) out = shaderDir() + "/" + l.preset;
    }
    cache[systemOf(slug)] = out;
    return out;
}

std::string shaderDir() {
    const char* env = std::getenv("CABINETOS_SHADERS");
    return env && *env ? env : "/usr/share/cabinetos/shaders";
}

}  // namespace screenfx
