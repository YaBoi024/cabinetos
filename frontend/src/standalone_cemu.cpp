// Wii U, through Cemu in the image: its settings, its mlc in this person's
// folder for the game and each player's controller before every start
// (prepareCemu), and its command line (cemuArgs). Moved out of standalone.cpp
// in the restructure (#398).

#include "standalone_internal.h"

#include "gpu.h"
#include "storage.h"
#include "vpad.h"

#include <unistd.h>

#include <vector>

namespace cab::standalone::detail {

// --- Wii U (Cemu) -------------------------------------------------------------
//
// Decided with MMagTech 2026-10-01, issue #174, open question 36: Cemu main
// at a pin, never patched; no graphic packs (#207); one account ID for
// everyone, `80000001`, which Cemu makes by itself when there is none; the
// person's saves written straight into their own folder for the game.
//
// WHERE THINGS ARE. Its home holds its settings and controller profiles
// (XDG_CONFIG_HOME), its log (XDG_DATA_HOME) and its shader cache
// (XDG_CACHE_HOME), so the cache is one per console, as Dolphin's is. Its
// mlc, the Wii U's own storage where the saves and the account are, is
// `<saveDir>/mlc`: this person's folder for this game. What travels is
// `mlc/usr/save/00050000/`, Cemu's own layout, which drops into any Cemu.

std::string cemuMlc(const std::string& saveDir) { return saveDir + "/mlc"; }

namespace {

std::string xmlEscape(const std::string& v) {
    std::string out;
    for (const char c : v) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Cemu's numbers for a GamePad's inputs (input/emulated/VPADController.h,
// ButtonId, from 1) and a Pro Controller's (input/emulated/ProController.h),
// each against an SDL gamepad's (input/api/Controller.h: SDL's button
// numbers, then ZL 32, ZR 33, the d-pad 34 to 37, and the axes from 38: left
// stick X+ Y+, right X+ Y+, triggers L R, then the same negative). By
// position, as Switch is here: Nintendo's A is the pad's right face button.
// Left out: HOME on both, since Guide is the console's pause menu, and the
// GamePad's microphone and its button that shows the GamePad's screen. The
// same tables as Batocera's, less its microphone on L3.
constexpr int kCemuGamePad[][2] = {
    {1, SDL_GAMEPAD_BUTTON_EAST},          {2, SDL_GAMEPAD_BUTTON_SOUTH},
    {3, SDL_GAMEPAD_BUTTON_NORTH},         {4, SDL_GAMEPAD_BUTTON_WEST},
    {5, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {6, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {7, 42},                               {8, 43},
    {9, SDL_GAMEPAD_BUTTON_START},         {10, SDL_GAMEPAD_BUTTON_BACK},
    {11, SDL_GAMEPAD_BUTTON_DPAD_UP},      {12, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {13, SDL_GAMEPAD_BUTTON_DPAD_LEFT},    {14, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {15, SDL_GAMEPAD_BUTTON_LEFT_STICK},   {16, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {17, 45}, {18, 39}, {19, 44}, {20, 38},   // left stick up, down, left, right
    {21, 47}, {22, 41}, {23, 46}, {24, 40},   // right stick
};
constexpr int kCemuPro[][2] = {
    {1, SDL_GAMEPAD_BUTTON_EAST},          {2, SDL_GAMEPAD_BUTTON_SOUTH},
    {3, SDL_GAMEPAD_BUTTON_NORTH},         {4, SDL_GAMEPAD_BUTTON_WEST},
    {5, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {6, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {7, 42},                               {8, 43},
    {9, SDL_GAMEPAD_BUTTON_START},         {10, SDL_GAMEPAD_BUTTON_BACK},
    {12, SDL_GAMEPAD_BUTTON_DPAD_UP},      {13, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {14, SDL_GAMEPAD_BUTTON_DPAD_LEFT},    {15, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {16, SDL_GAMEPAD_BUTTON_LEFT_STICK},   {17, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {18, 45}, {19, 39}, {20, 44}, {21, 38},   // left stick up, down, left, right
    {22, 47}, {23, 41}, {24, 46}, {25, 40},   // right stick
};

// PLAYER 1 IS THE GAMEPAD, every other player a Pro Controller (MMagTech,
// 2026-10-22), as on a Wii U, where the GamePad is always there and always
// player 1. With none set up, Cemu still tells a game a GamePad is there and
// never presses it (Cafe/OS/libs/vpad/vpad.cpp, VPADRead), so a screen that
// reads only the GamePad saw nothing at all: Shovel Knight's first one did.
// Cemu's own guide, RetroArch's Cemu core, Batocera and RetroDECK all make
// player 1 the GamePad. Cemu gives a GamePad the first GamePad channel and a
// Pro Controller the first free Wii controller channel, whatever its slot, so
// player 2 is the game's first Pro Controller, as on a Wii U.
std::string cemuProfile(int player) {
    const bool gamePad = player == 0;
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<emulated_controller>\n";
    x += std::string("  <type>") + (gamePad ? "Wii U GamePad" : "Wii U Pro Controller") +
         "</type>\n  <controller>\n";
    x += "    <api>SDLController</api>\n";
    // `<n>_<GUID>`: the n-th pad with that GUID. Each player's virtual
    // controller has a GUID of its own (vpad.h), so always the first.
    x += "    <uuid>0_" + cab::vpad::sdlGuid(player) + "</uuid>\n";
    x += "    <display_name>CabinetOS player " + std::to_string(player + 1) + "</display_name>\n";
    x += "    <rumble>1</rumble>\n";
    for (const char* a : {"axis", "rotation", "trigger"})
        x += std::string("    <") + a + "><deadzone>0.25</deadzone><range>1</range></" + a + ">\n";
    x += "    <mappings>\n";
    for (const auto& m : gamePad ? kCemuGamePad : kCemuPro)
        x += "      <entry><mapping>" + std::to_string(m[0]) + "</mapping><button>" +
             std::to_string(m[1]) + "</button></entry>\n";
    x += "    </mappings>\n  </controller>\n</emulated_controller>\n";
    return x;
}

}  // namespace

bool prepareCemu(const Emulator& e, const std::string& saveDir, int players, std::string* err) {
    const std::string config = home(e) + "/Cemu";
    storage::makeDirs(config + "/controllerProfiles");
    storage::makeDirs(cemuMlc(saveDir));
    // THE CONSOLE'S SETTINGS, written whole before every start; Cemu writes
    // the file back on exit, so nothing it kept can differ. Without the file
    // Cemu opens its first-run wizard. Names from config/CemuConfig.cpp and
    // gui/wxgui/wxCemuConfig.cpp at the pin.
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<content>\n";
    x += "  <mlc_path>" + xmlEscape(cemuMlc(saveDir)) + "</mlc_path>\n";
    // English, as the console's own words are.
    x += "  <console_language>1</console_language>\n";
    x += "  <check_update>false</check_update>\n";
    x += "  <use_discord_presence>false</use_discord_presence>\n";
    x += "  <fullscreen>true</fullscreen>\n";
    x += "  <fullscreen_menubar>false</fullscreen_menubar>\n";
    // "Already shown": its Vulkan notice and its offer to download graphic
    // packs, which the console never takes (#207).
    x += "  <vk_warning>true</vk_warning>\n";
    x += "  <gp_download>true</gp_download>\n";
    x += "  <open_pad>false</open_pad>\n";
    x += "  <Graphic>\n";
    // VULKAN on the GPU the console chose, never llvmpipe (gpu.cpp).
    x += "    <api>1</api>\n";
    const std::string uuid = cab::gpu::vulkan().deviceUuid;
    if (uuid.size() == 32) x += "    <vkDevice>" + uuid + "</vkDevice>\n";
    // VSYNC ON (#209, MMagTech 2026-10-04), 1, as Batocera ships it. #209
    // first set 0; then PS2 showed that without vsync gamescope keeps one
    // picture waiting and replaces it, so frames are lost (#226). Never 3,
    // "match display": on Linux release builds its thread is an empty stub.
    x += "    <VSync>1</VSync>\n";
    x += "    <AsyncCompile>true</AsyncCompile>\n";
    x += "    <Overlay><FPS>false</FPS><DrawCalls>false</DrawCalls><CPUUsage>false</CPUUsage>"
         "<CPUPerCoreUsage>false</CPUPerCoreUsage><RAMUsage>false</RAMUsage>"
         "<VRAMUsage>false</VRAMUsage></Overlay>\n";
    x += "    <Notification><ControllerProfiles>false</ControllerProfiles>"
         "<ControllerBattery>false</ControllerBattery><ShaderCompiling>false</ShaderCompiling>"
         "<FriendService>false</FriendService></Notification>\n";
    x += "  </Graphic>\n";
    // SOUND THROUGH CUBEB TO THE SYSTEM'S OUTPUT. An empty TVDevice is no TV
    // sound at all (audio/IAudioAPI.cpp, CreateDeviceFromConfig), which the
    // log does not mention; `default` is Cemu's own "Default Device". Its
    // volume defaults to 20.
    x += "  <Audio>\n    <api>3</api>\n    <TVChannels>1</TVChannels>\n";
    x += "    <TVVolume>100</TVVolume>\n    <TVDevice>default</TVDevice>\n  </Audio>\n";
    // ITS KEYBOARD SHORTCUTS OFF: Escape leaves fullscreen, F11 and Alt+Enter
    // toggle it, F12 saves a screenshot, all on by default, and a keyboard
    // plugged into the console would reach them. `0 -1` is no key and no
    // controller button (wxCemuConfig.h, sHotkeyCfg).
    x += "  <Hotkeys>\n";
    for (const char* k : {"ExitFullscreen", "ToggleFullscreen", "ToggleFullscreenAlt",
                          "TakeScreenshot", "ToggleFastForward", "ExitApplication"})
        x += std::string("    <") + k + ">0 -1</" + k + ">\n";
    x += "  </Hotkeys>\n";
    x += "</content>\n";
    if (!writeFile(config + "/settings.xml", x)) {
        *err = "could not write Cemu's settings in " + config;
        return false;
    }
    // PLAYER 1 THE GAMEPAD AND THE OTHERS PRO CONTROLLERS (cemuProfile), each
    // on their own virtual controller, in the console's order; and no
    // profile for a seat nobody is in.
    for (int p = 0; p < 8; ++p) {
        const std::string file = config + "/controllerProfiles/controller" + std::to_string(p) + ".xml";
        if (p >= players) {
            ::unlink(file.c_str());
        } else if (!writeFile(file, cemuProfile(p))) {
            *err = "could not write Cemu's controller profile " + file;
            return false;
        }
    }
    return true;
}

// Run::start's arguments for Cemu, in the order standalone_internal.h states.
void cemuArgs(std::vector<std::string>& full, const std::string& dir, const std::string& romPath) {
    // ITS LOG under its home (XDG_DATA_HOME/Cemu); its settings and
    // shader cache are there already, by the two Run::start sets. ONLY THE
    // VIRTUAL CONTROLLERS, as for xemu and Edge, with SDL told what
    // each one is: Cemu (SDL 3) asks for gamepads, and its profiles
    // bind them by GUID (prepareCemu). GTK on gamescope's Xwayland.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    full.insert(full.end() - 1, "XDG_DATA_HOME=" + dir);
    full.insert(full.end() - 1, "SDL_GAMECONTROLLERCONFIG=" + db);
    full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0");
    full.insert(full.end() - 1, "GDK_BACKEND=x11");
    // `-g` starts the game with no game list and closes Cemu when it
    // ends; `-f` is fullscreen.
    full.insert(full.end(), {"-g", romPath, "-f"});
}

}  // namespace cab::standalone::detail
