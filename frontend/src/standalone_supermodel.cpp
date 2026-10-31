// Sega Model 3, through Supermodel in the image: its settings, written whole
// in its home before every start, with this person's NVRAM folder for the
// game linked in (prepareSupermodel), and its command line (supermodelArgs).

#include "standalone_internal.h"

#include "arcadematch.h"
#include "fightstick.h"
#include "players.h"
#include "storage.h"
#include "vpad.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace cab::standalone::detail {

// --- Sega Model 3 (Supermodel) -----------------------------------------------
//
// Decided with MMagTech 2026-10-28 (docs/decisions/2026-10-28-model-2-and-model-3.md):
// Supermodel at a pin, built here, never patched, drawing with OpenGL (it has
// no Vulkan renderer), and run with no network like every emulator (Run::start's
// sandbox). Its net board is on, linked to itself, which only that sandbox's
// empty network ever sees (MMagTech, 2026-10-29,
// docs/decisions/2026-10-29-model-3-net-board-linked-to-itself.md). No save
// states, as for every program of its own (docs/PROJECT.md question 25): its
// state keys are off below, and the pause menu is Resume, Screenshot and Exit
// to Home. Its own settings window shows only when it is started with no game,
// which the console never does.
//
// WHERE THINGS ARE. Supermodel keeps everything beside a `Config` folder in
// the folder it is started in (Src/OSD/Unix/FileSystemPath.cpp), so it runs in
// its home (Run::start, --chdir): `Config/Supermodel.ini`, written whole here
// before every start; `NVRAM`, a link to this person's `nvram/` for the game,
// where it keeps the board's `<set>.nv`, which is what travels to RomM as a
// zip (saveRoot); `Assets`, a link to the crosshair pictures in the image, which
// it will not start without; and its log, Supermodel.log. It writes NVRAM only
// as it quits, which SIGTERM is (Run::stop; measured on the A9, 2026-10-29).
//
// ITS FLAGS AND SETTINGS ARE CHECKED AGAINST ITS SOURCE when it is built:
// cores/build-supermodel.sh reads every flag this file passes and every row
// of its settings tables, and finds each in Supermodel's source at the pin.

std::string supermodelNvram(const std::string& saveDir) { return saveDir + "/nvram"; }

namespace {

// Its game list and crosshair pictures, beside the program in the image (or
// beside the one CABINETOS_BINARY_supermodel names): bin/supermodel's
// share/supermodel.
std::string share(const Emulator& e) {
    std::string bin = binaryOf(e);
    bin.erase(bin.find_last_of('/'));
    bin.erase(bin.find_last_of('/'));
    return bin + "/share/supermodel";
}

// Supermodel's name for a pad control in its gamepad mode, InputSystem
// sdlgamepad (Src/OSD/SDL/SDLInputSystem.cpp): BUTTON1 to 4 the south, east,
// west and north face buttons, 5 and 6 the shoulders, 7 Select, 8 Start; the
// triggers are the Z and RZ axes, the d-pad POV1.
const char* partOf(cab::Button b) {
    switch (b) {
        case cab::B: return "BUTTON1";
        case cab::A: return "BUTTON2";
        case cab::Y: return "BUTTON3";
        case cab::X: return "BUTTON4";
        case cab::L: return "BUTTON5";
        case cab::R: return "BUTTON6";
        case cab::L2: return "ZAXIS_POS";
        case cab::R2: return "RZAXIS_POS";
        default: return "NONE";
    }
}

// THE ARCADE LAYOUT ON A PAD, as every arcade emulator here has it: a game's
// buttons 1 to 4 on B, A, Y and X (FinalBurn Neo's and MAME 0.289's order),
// Coin on Select and Start on Start on every port, the lever on the d-pad
// and the left stick. Driving as in sm2-emu: steering on the left stick, the
// accelerator on the right trigger, the brake on the left, gear up and down
// on the right and left shoulders.
constexpr cab::Button kButtons[] = {cab::B, cab::A, cab::Y, cab::X};

// One player's four game buttons, as Supermodel names them for its port: on
// a pad, the layout above; on a fight stick the console knows, a game's
// buttons where the stick's rules put them (fightstick.h), as for sm2-emu. A
// game whose list gives it no buttons (a driving or gun game) plays a stick
// as a pad, as an arcade core's does.
std::vector<std::string> buttonsOf(int player, int players, int gameButtons) {
    std::vector<std::string> out;
    const std::string j = "JOY" + std::to_string(player + 1) + "_";
    for (cab::Button b : kButtons) out.push_back(j + partOf(b));
    SDL_Gamepad* gp = player < players ? players::gamepad(player) : nullptr;
    const fightstick::Stick* stick = gp ? players::stick(SDL_GetGamepadID(gp)) : nullptr;
    if (!stick || gameButtons <= 0) return out;
    std::vector<cab::InputDescriptor> declared;
    for (int k = 0; k < gameButtons && k < 4; ++k)
        declared.push_back({static_cast<unsigned>(kButtons[k]), "Button " + std::to_string(k + 1)});
    const std::optional<fightstick::Layout> l = fightstick::make(stick, "supermodel", declared, declared);
    if (!l) return out;
    std::string said;
    for (int s = 0; s < fightstick::kSlots; ++s) {
        for (int k = 0; k < gameButtons && k < 4; ++k) {
            if (l->id[s] != static_cast<int>(kButtons[k])) continue;
            out[k] = j + partOf(fightstick::slotButton(s));
            said += (said.empty() ? "" : ", ") + l->label[s] + " on " + std::to_string(s + 1);
        }
    }
    std::fprintf(stderr, "[standalone] Supermodel player %d on a fight stick (%s): %s\n",
                 player + 1, stick->name, said.c_str());
    return out;
}

// THE PICTURE FOR A LEVEL: its lines, at most the screen's, in the screen's
// shape. Supermodel draws at the size it is given. At the screen's own size
// it is fullscreen; smaller, it is a window of that size, which gamescope
// scales to the screen. Not fullscreen at a smaller size: gamescope's
// Xwayland gives a fullscreen window the screen's size whatever mode is
// asked for, and Supermodel then draws its 3D at the size it asked for and
// its 2D layer elsewhere (seen on the A9, 2026-10-29).
struct Picture {
    int w, h;
    bool fullscreen;
};
Picture pictureFor(quality::Level level) {
    int w = 3840, h = 2160;
    if (const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
        dm && dm->w > 0 && dm->h > 0) {
        w = dm->w;
        h = dm->h;
    }
    const int lines = std::min(h, quality::supermodelLines(level));
    return {lines == h ? w : (lines * w / h) & ~1, lines, lines == h};
}

struct Setting {
    const char* key;
    std::string value;
};

}  // namespace

bool prepareSupermodel(const Emulator& e, const std::string& romPath, const std::string& saveDir,
                       int players, quality::Level level, std::string* err) {
    const std::string dir = home(e);
    storage::makeDirs(dir + "/Config");
    storage::makeDirs(supermodelNvram(saveDir));
    if (!placeLink(supermodelNvram(saveDir), dir + "/NVRAM") ||
        !placeLink(share(e) + "/Assets", dir + "/Assets")) {
        *err = "could not link Supermodel's NVRAM and Assets in " + dir;
        return false;
    }
    const Picture pic = pictureFor(level);
    const int buttons = arcadematch::buttons("supermodel", arcadeSet(romPath) + ".zip");
    const std::vector<std::string> p1 = buttonsOf(0, players, buttons);
    const std::vector<std::string> p2 = buttonsOf(1, players, buttons);
    auto j1 = [](const char* part) { return std::string("JOY1_") + part; };
    auto j2 = [](const char* part) { return std::string("JOY2_") + part; };
    // THE CONSOLE'S SETTINGS, written whole before every start, so nothing
    // Supermodel kept can differ; a key left out is its own default. Names
    // from Src/OSD/SDL/Main.cpp at the pin.
    const std::vector<Setting> settings = {
        // Its own renderer and threads, as it ships.
        {"New3DEngine", "true"},
        {"QuadRendering", "true"},
        {"MultiThreaded", "true"},
        {"GPUMultiThreaded", "true"},
        // THE PICTURE at the level's size (quality.h, pictureFor), the
        // board's own shape, nothing stretched or widened; Supermodel's own
        // 2D upscaling and colour as they ship.
        {"FullScreen", pic.fullscreen ? "true" : "false"},
        {"BorderlessWindow", "true"},
        {"XResolution", std::to_string(pic.w)},
        {"YResolution", std::to_string(pic.h)},
        {"WideScreen", "false"},
        {"WideBackground", "false"},
        {"Stretch", "false"},
        {"Supersampling", "1"},
        // VSYNC ON, as every emulator has it: gamescope keeps one picture
        // waiting and replaces it, so without vsync frames are lost (#226).
        {"VSync", "true"},
        {"Throttle", "true"},
        {"RefreshRate", "60.000"},
        {"ShowFrameRate", "false"},
        // Light-gun games are greyed until #575 (arcadematch.h).
        {"Crosshairs", "0"},
        // SOUND IN TWO CHANNELS: the board's four are mixed down by
        // Supermodel itself (Src/OSD/SDL/Audio.cpp), as it does for any
        // stereo output, rather than left to the television.
        {"EmulateSound", "true"},
        {"EmulateDSB", "true"},
        {"NbSoundChannels", "2"},
        {"SoundVolume", "100"},
        {"MusicVolume", "100"},
        // THE DRIVE BOARD RUNS. Off, Supermodel switches the board off
        // entirely, and Star Wars Trilogy stops at "Failed feedback lever
        // initialize" (seen on the A9, 2026-10-29: the board answers the
        // game's lever check only when it runs). On, the wheel's and yoke's
        // forces reach a pad as rumble at Supermodel's own strengths, as
        // every system with motors rumbles (rumble.h, the console's one
        // switch) and sm2-emu's Model 2 driving games do.
        {"ForceFeedback", "true"},
        // THE NET BOARD, LINKED TO ITSELF: Supermodel's own way to run a
        // linked cabinet alone (Src/Model3/SimNetBoard.cpp). Scud Race and
        // Daytona USA 2 stop at "Network board not present" without one. It
        // listens on 1970 and connects to 127.0.0.1:1970, inside the
        // sandbox's empty network (Run::start), so nothing outside sees it.
        // Dirt Devils, set as a slave, waits for itself this way and plays
        // when Supermodel takes trzy/Supermodel#360 (no patch of ours,
        // MMagTech 2026-10-29).
        {"Network", "true"},
        {"SimulateNet", "true"},
        {"PortIn", "1970"},
        {"PortOut", "1970"},
        {"AddressOut", "\"127.0.0.1\""},
        // No lamp or motor outputs, to the network or anywhere else.
        {"Outputs", "none"},
        // Pads by SDL's gamepad names, which sees only the virtual ones, made
        // in the console's order (supermodelArgs).
        {"InputSystem", "sdlgamepad"},
    };
    // EVERY INPUT, so none keeps a default: Supermodel's defaults put most
    // controls on the keyboard and the guns on the mouse, and a keyboard
    // plugged into the console would reach them. NONE is nothing. Every key
    // shortcut is off: Escape quits, F5 and F7 save and load states, Alt+N
    // clears the NVRAM. Test and Service are off: no game is set up from its
    // test menu here.
    const std::vector<Setting> inputs = {
        {"InputUIExit", "NONE"},
        {"InputUIReset", "NONE"},
        {"InputUIPause", "NONE"},
        {"InputUIFullScreen", "NONE"},
        {"InputUISaveState", "NONE"},
        {"InputUIChangeSlot", "NONE"},
        {"InputUILoadState", "NONE"},
        {"InputUIMusicVolUp", "NONE"},
        {"InputUIMusicVolDown", "NONE"},
        {"InputUISoundVolUp", "NONE"},
        {"InputUISoundVolDown", "NONE"},
        {"InputUIClearNVRAM", "NONE"},
        {"InputUISelectCrosshairs", "NONE"},
        {"InputUIToggleFrameLimit", "NONE"},
        {"InputUIDumpInputState", "NONE"},
        {"InputUIDumpTimings", "NONE"},
        {"InputUIScreenShot", "NONE"},
        {"InputStart1", j1("BUTTON8")},
        {"InputStart2", j2("BUTTON8")},
        {"InputCoin1", j1("BUTTON7")},
        {"InputCoin2", j2("BUTTON7")},
        {"InputServiceA", "NONE"},
        {"InputServiceB", "NONE"},
        {"InputTestA", "NONE"},
        {"InputTestB", "NONE"},
        // The lever: the d-pad, or the left stick.
        {"InputJoyUp", j1("POV1_UP") + "," + j1("UP")},
        {"InputJoyDown", j1("POV1_DOWN") + "," + j1("DOWN")},
        {"InputJoyLeft", j1("POV1_LEFT") + "," + j1("LEFT")},
        {"InputJoyRight", j1("POV1_RIGHT") + "," + j1("RIGHT")},
        {"InputJoyUp2", j2("POV1_UP") + "," + j2("UP")},
        {"InputJoyDown2", j2("POV1_DOWN") + "," + j2("DOWN")},
        {"InputJoyLeft2", j2("POV1_LEFT") + "," + j2("LEFT")},
        {"InputJoyRight2", j2("POV1_RIGHT") + "," + j2("RIGHT")},
        // Virtua Fighter 3, Fighting Vipers 2: the cabinet's Guard, Punch,
        // Kick and Escape as buttons 1 to 4.
        {"InputGuard", p1[0]},
        {"InputPunch", p1[1]},
        {"InputKick", p1[2]},
        {"InputEscape", p1[3]},
        {"InputGuard2", p2[0]},
        {"InputPunch2", p2[1]},
        {"InputKick2", p2[2]},
        {"InputEscape2", p2[3]},
        // Spikeout: Shift, Beat, Charge, Jump.
        {"InputShift", p1[0]},
        {"InputBeat", p1[1]},
        {"InputCharge", p1[2]},
        {"InputJump", p1[3]},
        // Virtua Striker 2: short pass, long pass, shoot.
        {"InputShortPass", p1[0]},
        {"InputLongPass", p1[1]},
        {"InputShoot", p1[2]},
        {"InputShortPass2", p2[0]},
        {"InputLongPass2", p2[1]},
        {"InputShoot2", p2[2]},
        // Driving: steering on the left stick (the d-pad steers too),
        // accelerator and brake on the right and left triggers, gears up and
        // down on the right and left shoulders, for a four-speed box too
        // (Supermodel shifts its gears by them). The view buttons, the hand
        // brake, Harley's rear brake and music on the face buttons.
        {"InputSteeringLeft", j1("POV1_LEFT")},
        {"InputSteeringRight", j1("POV1_RIGHT")},
        {"InputSteering", j1("XAXIS")},
        {"InputAccelerator", j1("RZAXIS_POS")},
        {"InputBrake", j1("ZAXIS_POS")},
        {"InputGearShiftUp", j1("BUTTON6")},
        {"InputGearShiftDown", j1("BUTTON5")},
        {"InputGearShift1", "NONE"},
        {"InputGearShift2", "NONE"},
        {"InputGearShift3", "NONE"},
        {"InputGearShift4", "NONE"},
        {"InputGearShiftN", "NONE"},
        {"InputVR1", j1("BUTTON1")},
        {"InputVR2", j1("BUTTON2")},
        {"InputVR3", j1("BUTTON3")},
        {"InputVR4", j1("BUTTON4")},
        {"InputViewChange", j1("BUTTON4")},
        {"InputHandBrake", j1("BUTTON1")},
        {"InputRearBrake", j1("BUTTON2")},
        {"InputMusicSelect", j1("BUTTON3")},
        // Virtual On 2: moving and strafing on the left stick or the d-pad,
        // turning on the right stick, the two weapons on the triggers, their
        // turbo on the shoulders, jump and crouch on two face buttons.
        {"InputTwinJoyTurnLeft", j1("RXAXIS_NEG")},
        {"InputTwinJoyTurnRight", j1("RXAXIS_POS")},
        {"InputTwinJoyForward", j1("YAXIS_NEG") + "," + j1("POV1_UP")},
        {"InputTwinJoyReverse", j1("YAXIS_POS") + "," + j1("POV1_DOWN")},
        {"InputTwinJoyStrafeLeft", j1("XAXIS_NEG") + "," + j1("POV1_LEFT")},
        {"InputTwinJoyStrafeRight", j1("XAXIS_POS") + "," + j1("POV1_RIGHT")},
        {"InputTwinJoyJump", j1("BUTTON1")},
        {"InputTwinJoyCrouch", j1("BUTTON2")},
        {"InputTwinJoyLeft1", "NONE"},
        {"InputTwinJoyLeft2", "NONE"},
        {"InputTwinJoyRight1", "NONE"},
        {"InputTwinJoyRight2", "NONE"},
        {"InputTwinJoyUp1", "NONE"},
        {"InputTwinJoyUp2", "NONE"},
        {"InputTwinJoyDown1", "NONE"},
        {"InputTwinJoyDown2", "NONE"},
        {"InputTwinJoyShot1", j1("ZAXIS_POS")},
        {"InputTwinJoyShot2", j1("RZAXIS_POS")},
        {"InputTwinJoyTurbo1", j1("BUTTON5")},
        {"InputTwinJoyTurbo2", j1("BUTTON6")},
        // Star Wars Trilogy's flight yoke: the left stick, fire on the right
        // trigger or button 1, the event button on button 2.
        {"InputAnalogJoyLeft", j1("POV1_LEFT")},
        {"InputAnalogJoyRight", j1("POV1_RIGHT")},
        {"InputAnalogJoyUp", j1("POV1_UP")},
        {"InputAnalogJoyDown", j1("POV1_DOWN")},
        {"InputAnalogJoyX", j1("XAXIS")},
        {"InputAnalogJoyY", j1("YAXIS")},
        {"InputAnalogJoyTrigger", j1("RZAXIS_POS") + "," + j1("BUTTON1")},
        {"InputAnalogJoyEvent", j1("BUTTON2")},
        {"InputAnalogJoyTrigger2", j1("BUTTON3")},
        {"InputAnalogJoyEvent2", j1("BUTTON4")},
        // Light guns: none in Supermodel's list; nothing.
        {"InputGunLeft", "NONE"},
        {"InputGunRight", "NONE"},
        {"InputGunUp", "NONE"},
        {"InputGunDown", "NONE"},
        {"InputGunX", "NONE"},
        {"InputGunY", "NONE"},
        {"InputTrigger", "NONE"},
        {"InputOffscreen", "NONE"},
        {"InputGunLeft2", "NONE"},
        {"InputGunRight2", "NONE"},
        {"InputGunUp2", "NONE"},
        {"InputGunDown2", "NONE"},
        {"InputGunX2", "NONE"},
        {"InputGunY2", "NONE"},
        {"InputTrigger2", "NONE"},
        {"InputOffscreen2", "NONE"},
        // MOUNTED GUNS (L.A. Machineguns, The Ocean Hunter; The Lost World is
        // greyed as a light gun, arcadematch.h): aimed with the left stick or
        // the d-pad, fired with the right trigger or button 1.
        {"InputAnalogGunLeft", j1("POV1_LEFT")},
        {"InputAnalogGunRight", j1("POV1_RIGHT")},
        {"InputAnalogGunUp", j1("POV1_UP")},
        {"InputAnalogGunDown", j1("POV1_DOWN")},
        {"InputAnalogGunX", j1("XAXIS")},
        {"InputAnalogGunY", j1("YAXIS")},
        {"InputAnalogTriggerLeft", j1("RZAXIS_POS") + "," + j1("BUTTON1")},
        {"InputAnalogTriggerRight", j1("BUTTON2")},
        {"InputAnalogGunLeft2", j2("POV1_LEFT")},
        {"InputAnalogGunRight2", j2("POV1_RIGHT")},
        {"InputAnalogGunUp2", j2("POV1_UP")},
        {"InputAnalogGunDown2", j2("POV1_DOWN")},
        {"InputAnalogGunX2", j2("XAXIS")},
        {"InputAnalogGunY2", j2("YAXIS")},
        {"InputAnalogTriggerLeft2", j2("RZAXIS_POS") + "," + j2("BUTTON1")},
        {"InputAnalogTriggerRight2", j2("BUTTON2")},
        // Ski Champ: the skis on the left stick, the poles on the shoulders.
        {"InputSkiLeft", j1("POV1_LEFT")},
        {"InputSkiRight", j1("POV1_RIGHT")},
        {"InputSkiUp", j1("POV1_UP")},
        {"InputSkiDown", j1("POV1_DOWN")},
        {"InputSkiX", j1("XAXIS")},
        {"InputSkiY", j1("YAXIS")},
        {"InputSkiPollLeft", j1("BUTTON5")},
        {"InputSkiPollRight", j1("BUTTON6")},
        {"InputSkiSelect1", j1("BUTTON1")},
        {"InputSkiSelect2", j1("BUTTON2")},
        {"InputSkiSelect3", j1("BUTTON3")},
        // Magical Truck Adventure: the lever on the left stick or the d-pad,
        // the pedal on button 1.
        {"InputMagicalLeverUp1", j1("POV1_UP")},
        {"InputMagicalLeverDown1", j1("POV1_DOWN")},
        {"InputMagicalLever1", j1("YAXIS")},
        {"InputMagicalPedal1", j1("BUTTON1")},
        {"InputMagicalLeverUp2", j2("POV1_UP")},
        {"InputMagicalLeverDown2", j2("POV1_DOWN")},
        {"InputMagicalLever2", j2("YAXIS")},
        {"InputMagicalPedal2", j2("BUTTON1")},
        // Sega Bass Fishing: the rod on the left stick, the reel on the right
        // trigger, the line's tension on the left, cast and select on buttons
        // 1 and 2.
        {"InputFishingRodLeft", j1("POV1_LEFT")},
        {"InputFishingRodRight", j1("POV1_RIGHT")},
        {"InputFishingRodUp", j1("POV1_UP")},
        {"InputFishingRodDown", j1("POV1_DOWN")},
        {"InputFishingRodX", j1("XAXIS")},
        {"InputFishingRodY", j1("YAXIS")},
        {"InputFishingStickLeft", "NONE"},
        {"InputFishingStickRight", "NONE"},
        {"InputFishingStickUp", "NONE"},
        {"InputFishingStickDown", "NONE"},
        {"InputFishingStickX", j1("RXAXIS")},
        {"InputFishingStickY", j1("RYAXIS")},
        {"InputFishingReel", j1("RZAXIS_POS")},
        {"InputFishingCast", j1("BUTTON1")},
        {"InputFishingSelect", j1("BUTTON2")},
        {"InputFishingTension", j1("ZAXIS_POS")},
    };
    std::string x = ";; Written by CabinetOS before every start (standalone_supermodel.cpp).\n";
    x += "[ Global ]\n";
    // Its own file's form: settings bare, every input's mapping in quotes.
    for (const Setting& s : settings) x += std::string(s.key) + " = " + s.value + "\n";
    for (const Setting& s : inputs) x += std::string(s.key) + " = \"" + s.value + "\"\n";
    if (!writeFile(dir + "/Config/Supermodel.ini", x)) {
        *err = "could not write Supermodel's settings in " + dir + "/Config";
        return false;
    }
    std::fprintf(stderr, "[standalone] Supermodel at %dx%d%s, %d buttons for %s\n", pic.w, pic.h,
                 pic.fullscreen ? "" : " in a window", buttons, arcadeSet(romPath).c_str());
    return true;
}

// Run::start's arguments for Supermodel, in the order standalone_internal.h
// states.
void supermodelArgs(std::vector<std::string>& full, const Emulator& e, const std::string& dir,
                    const std::string& romPath) {
    // ITS HOME IS ITS WORKING FOLDER, where it looks for Config (above).
    full.insert(full.begin() + 1, {"--chdir", dir});
    // ONLY THE VIRTUAL CONTROLLERS, as for sm2-emu, with SDL told what each
    // one is; SDL on gamescope's Xwayland. Its SDL is SDL 2 on the image's
    // SDL 3 (sdl2-compat), which reads these names itself.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    full.insert(full.end() - 1, "SDL_GAMECONTROLLERCONFIG=" + db);
    full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0");
    full.insert(full.end() - 1, "SDL_VIDEO_DRIVER=x11");
    // The game's zip, whose name is its set, and its list in the image.
    full.insert(full.end(), {romPath, "-game-xml-file=" + share(e) + "/Games.xml"});
}

}  // namespace cab::standalone::detail
