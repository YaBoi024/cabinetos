// Sega Model 2, through sm2-emu in the image: its settings, written whole in
// this person's folder for the game before every start (prepareSm2emu), and
// its command line (sm2emuArgs).

#include "standalone_internal.h"

#include "arcadematch.h"
#include "fightstick.h"
#include "gpu.h"
#include "players.h"
#include "storage.h"
#include "vpad.h"

#include <cstdio>
#include <string>
#include <vector>

namespace cab::standalone::detail {

// --- Sega Model 2 (sm2-emu) ---------------------------------------------------
//
// Decided with MMagTech 2026-10-28 (docs/decisions/2026-10-28-model-2-and-model-3.md):
// sm2-emu at a pin, built here without libcurl, so its artwork downloader is
// not in it, never patched, drawing with Vulkan, and run with no network like
// every emulator (Run::start's sandbox). No save states, as for every program
// of its own (docs/PROJECT.md question 25): its own state keys and Guide
// shortcuts are turned off below, and the pause menu is Resume, Screenshot and
// Exit to Home. Its overlay menu and game list never show.
//
// WHERE THINGS ARE. Everything it keeps for a game is in this person's folder
// for it (`saveDir`): its settings, `config/sm2-emu.ini` (`--config`), and the
// board's NVRAM and EEPROM, `nvram/<set>.nv` and `nvram/<set>.eeprom`
// (`--nvram`), which is what travels to RomM as a zip (saveRoot). It writes
// both only as it quits, which SIGTERM is (Run::stop; measured on the A9,
// 2026-10-28). Its home holds nothing but where a screenshot would go, which
// nothing can take.
//
// ITS FLAGS AND KEYS ARE CHECKED AGAINST ITS SOURCE when it is built:
// cores/build-sm2emu.sh lists every one used here, and a new one goes there
// too.

std::string sm2emuNvram(const std::string& saveDir) { return saveDir + "/nvram"; }

namespace {

// sm2-emu's number for a pad control (core/config.h): an SDL gamepad button's
// own, or 100 + an SDL axis for that axis pushed past half travel, which is
// how it hears a trigger.
int bindingOf(cab::Button b) {
    switch (b) {
        case cab::B: return SDL_GAMEPAD_BUTTON_SOUTH;
        case cab::A: return SDL_GAMEPAD_BUTTON_EAST;
        case cab::Y: return SDL_GAMEPAD_BUTTON_WEST;
        case cab::X: return SDL_GAMEPAD_BUTTON_NORTH;
        case cab::L: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
        case cab::R: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        case cab::L2: return 100 + SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        case cab::R2: return 100 + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        default: return -1;
    }
}

// THE ARCADE LAYOUT ON A PAD, as every arcade emulator here has it: a game's
// buttons 1 to 4 on B, A, Y and X (FinalBurn Neo's and MAME 0.289's order),
// Coin on Select and Start on Start on every port, the lever on the d-pad,
// and gear up and down on the right and left shoulders. Which is also
// sm2-emu's own, written out so a default it changes cannot move a button.
// Its analogue controls stay as it has them: steering and aim on the left
// stick, the accelerator on the right trigger, the brake on the left, Virtual
// On's second lever on the right stick.
constexpr cab::Button kButtons[] = {cab::B, cab::A, cab::Y, cab::X};

// One player's buttons, for sm2-emu's [gamepad] section: on a pad, the
// layout above; on a fight stick the console knows, a game's buttons where
// the stick's rules put them (fightstick.h), the rest as on a pad. A game
// whose list gives it no buttons (a driving or gun game) plays a stick as a
// pad, as an arcade core's does.
std::string padLines(int player, int players, int gameButtons) {
    int button[4];
    for (int k = 0; k < 4; ++k) button[k] = bindingOf(kButtons[k]);
    SDL_Gamepad* gp = player < players ? players::gamepad(player) : nullptr;
    const fightstick::Stick* stick = gp ? players::stick(SDL_GetGamepadID(gp)) : nullptr;
    if (stick && gameButtons > 0) {
        std::vector<cab::InputDescriptor> declared;
        for (int k = 0; k < gameButtons && k < 4; ++k)
            declared.push_back({static_cast<unsigned>(kButtons[k]), "Button " + std::to_string(k + 1)});
        if (const std::optional<fightstick::Layout> l =
                fightstick::make(stick, "sm2emu", declared, declared)) {
            std::string said;
            for (int s = 0; s < fightstick::kSlots; ++s) {
                for (int k = 0; k < gameButtons && k < 4; ++k) {
                    if (l->id[s] != static_cast<int>(kButtons[k])) continue;
                    button[k] = bindingOf(fightstick::slotButton(s));
                    said += (said.empty() ? "" : ", ") + l->label[s] + " on " +
                            std::to_string(s + 1);
                }
            }
            std::fprintf(stderr, "[standalone] sm2-emu player %d on a fight stick (%s): %s\n",
                         player + 1, stick->name, said.c_str());
        }
    }
    const std::string p = "pad" + std::to_string(player + 1) + "_button_";
    std::string x;
    for (int k = 0; k < 4; ++k)
        x += p + "button" + std::to_string(k + 1) + " = " + std::to_string(button[k]) + "\n";
    x += p + "up = " + std::to_string(SDL_GAMEPAD_BUTTON_DPAD_UP) + "\n";
    x += p + "down = " + std::to_string(SDL_GAMEPAD_BUTTON_DPAD_DOWN) + "\n";
    x += p + "left = " + std::to_string(SDL_GAMEPAD_BUTTON_DPAD_LEFT) + "\n";
    x += p + "right = " + std::to_string(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) + "\n";
    x += p + "start = " + std::to_string(SDL_GAMEPAD_BUTTON_START) + "\n";
    x += p + "coin = " + std::to_string(SDL_GAMEPAD_BUTTON_BACK) + "\n";
    x += p + "gear_up = " + std::to_string(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) + "\n";
    x += p + "gear_down = " + std::to_string(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) + "\n";
    return x;
}

}  // namespace

bool prepareSm2emu(const Emulator& e, const std::string& romPath, const std::string& saveDir,
                   int players, quality::Level level, std::string* err) {
    const std::string config = saveDir + "/config";
    storage::makeDirs(config);
    storage::makeDirs(sm2emuNvram(saveDir));
    storage::makeDirs(home(e) + "/screenshots");
    const std::string set = arcadeSet(romPath);
    // THE CONSOLE'S SETTINGS, written whole before every start; sm2-emu
    // writes the file back as it quits, so nothing it kept can differ, and a
    // key left out is its own default. Names from core/config.cpp at the pin.
    std::string x = "# Written by CabinetOS before every start (standalone_sm2emu.cpp).\n";
    x += "[video]\n";
    // VULKAN on the GPU the console chose, by the name sm2-emu's --list-gpus
    // gives it, never llvmpipe (gpu.cpp).
    x += "graphics_backend = vulkan\n";
    x += "gpu = " + cab::gpu::vulkan().deviceName + "\n";
    // VSYNC ON, as every emulator has it: gamescope keeps one picture waiting
    // and replaces it, so without vsync frames are lost (#226). sm2-emu still
    // paces the board to its own 57.52 Hz.
    x += "vsync = true\n";
    x += "throttle = true\n";
    x += "fullscreen = true\n";
    x += "show_fps = false\n";
    // Its own messages ("State saved") over the game, which nothing here can
    // cause, off.
    x += "show_notifications = false\n";
    x += "render_scale = " + std::to_string(quality::sm2emu(level)) + "\n";
    // Replacement textures would be read from the save folder; none are ever
    // put there.
    x += "custom_textures = false\n";
    x += "\n[paths]\n";
    x += "screenshot_dir = " + home(e) + "/screenshots\n";
    // Built without libcurl, it cannot fetch artwork anyway (build-sm2emu.sh).
    x += "scrape_artwork = false\n";
    x += "\n[lightgun]\n";
    // Light-gun games are greyed until #575 (arcadematch.h).
    x += "lightgun = false\n";
    x += "\n[network]\n";
    // No linked cabinets and no lamp outputs over the network.
    x += "link_enabled = false\n";
    x += "outputs_network = false\n";
    x += "outputs_windows = false\n";
    x += "\n[system]\n";
    x += "language = en\n";
    // THE CONTROLLERS EVEN WHEN ITS WINDOW IS NOT IN FRONT: the console's own
    // window is the overlay above it (overlaywin.h), and its pads are the
    // virtual ones, which hear nothing while the pause menu is up.
    x += "input_background = true\n";
    x += "rewind = false\n";
    // EVERY KEYBOARD SHORTCUT OFF: Escape quits, F10 opens its overlay menu,
    // F6 and F7 save and load states, and a keyboard plugged into the
    // console would reach them. Empty is no key.
    for (const char* k : {"exit", "quit", "overlay", "fullscreen", "screenshot", "pause",
                          "quick_save", "quick_load", "fps", "fast_forward", "rewind"})
        x += std::string("key_") + k + " = \n";
    x += "log_level = info\n";
    x += "\n[gamepad]\n";
    // ITS GUIDE SHORTCUTS OFF (Guide with A opens its menu, with X and Y
    // saves and loads a state): Guide is the console's pause menu, as for a
    // host that maps its own hotkeys (core/config.h).
    x += "pad_shortcuts = false\n";
    // Players by the order its SDL finds the pads, which sees only the
    // virtual ones, made in the console's order (sm2emuArgs).
    x += "pad1_device = \npad2_device = \n";
    const int buttons = arcadematch::buttons("sm2emu", set + ".zip");
    x += padLines(0, players, buttons);
    x += padLines(1, players, buttons);
    if (!writeFile(config + "/sm2-emu.ini", x)) {
        *err = "could not write sm2-emu's settings in " + config;
        return false;
    }
    return true;
}

// Run::start's arguments for sm2-emu, in the order standalone_internal.h states.
void sm2emuArgs(std::vector<std::string>& full, const std::string& dir,
                const std::string& saveDir, const std::string& romPath) {
    // ONLY THE VIRTUAL CONTROLLERS, as for Cemu and Vita3K, with SDL told
    // what each one is. Anything it would put under the platform's data
    // folder goes in its home. SDL on gamescope's Xwayland.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    full.insert(full.end() - 1, "XDG_DATA_HOME=" + dir);
    full.insert(full.end() - 1, "SDL_GAMECONTROLLERCONFIG=" + db);
    full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0");
    full.insert(full.end() - 1, "SDL_VIDEO_DRIVER=x11");
    // `--game` names the set, which its own match by the zip's files would
    // otherwise choose among a parent and its clones.
    full.insert(full.end(), {"--config", saveDir + "/config", "--nvram", sm2emuNvram(saveDir),
                             "--game", arcadeSet(romPath), romPath});
}

}  // namespace cab::standalone::detail
