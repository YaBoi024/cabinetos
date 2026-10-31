// The libretro host.
//
// This is the part that makes CabinetOS an emulator host rather than a
// launcher: it loads a core as a shared library, feeds it a ROM and controller
// input, and owns the loop that decides when it runs.
//
// Shape, and why.
//
// ONE CORE AT A TIME, IN PROCESS-GLOBAL STATE. Libretro's callbacks are plain C
// function pointers with no context argument, so the process holds exactly one
// core's worth of state no matter how this class is shaped. The singleton is
// the honest interface to that, and it matches the reference implementation,
// which reached the same conclusion for the same reason.
//
// ONE .so PER CORE, dlopen'd with RTLD_LOCAL. On Apple, where everything is
// statically linked into one binary, only one core can carry the standard
// retro_* names, so Cabinet renamed every other core's symbols with a prefix
// and merged each into a single relocatable object. On Linux that entire
// apparatus is unnecessary: RTLD_LOCAL gives each core its own namespace for
// free. See docs/PROJECT.md, open question 13.
//
// THE FRONTEND DECIDES WHEN THE CORE RUNS. Not the display, not the core. See
// `runFor`.

#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct retro_memory_map;

namespace cab {

// What the core asked to be run at, and how big its picture is.
struct AVInfo {
    double fps = 60.0;
    double sampleRate = 44100.0;
    unsigned baseWidth = 0, baseHeight = 0;
    unsigned maxWidth = 0, maxHeight = 0;
    float aspectRatio = 0.0f;  // 0 means "derive it from the pixel dimensions"
};

// libretro's RetroPad, which every core speaks regardless of what the real
// hardware had. Repeated here rather than pulling libretro.h into the UI: the
// frontend above this layer has no business knowing what libretro is, and these
// values are a stable part of its ABI rather than an implementation detail.
// Verified against libretro.h.
enum Button : uint32_t {
    B = 0,
    Y = 1,
    Select = 2,
    Start = 3,
    Up = 4,
    Down = 5,
    Left = 6,
    Right = 7,
    A = 8,
    X = 9,
    L = 10,
    R = 11,
    L2 = 12,
    R2 = 13,
    L3 = 14,
    R3 = 15,
};

inline constexpr uint32_t bit(Button b) { return 1u << static_cast<uint32_t>(b); }

// Bit N of the mask is RetroPad button N.
struct PadState {
    uint32_t buttons = 0;
    float leftX = 0, leftY = 0;
    float rightX = 0, rightY = 0;
    // HOW FAR THE TRIGGERS ARE PRESSED, 0 to 1, as well as the L2/R2 bits
    // above. Both, because a trigger is two things at once: a Dreamcast reads
    // it as a continuous value and a SNES reads its shoulder as pressed or
    // not, and the same physical pull has to serve both.
    //
    // A pad whose triggers are digital — a Switch Pro Controller's ZL and ZR
    // are switches, not springs — simply reports 0 or 1 here.
    float leftTrigger = 0, rightTrigger = 0;
};

// One of a game's joypad buttons as the core declared it
// (RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS): the RetroPad id, and the core's
// own word for what it does in this game.
struct InputDescriptor {
    unsigned id = 0;
    std::string label;
    bool operator==(const InputDescriptor&) const = default;
};

class Core {
public:
    static Core& shared();

    // Where a core looks BIOS files up by name, and where a core that writes
    // its own save files puts them.
    //
    // CALL THIS BEFORE load(), NOT BEFORE loadGame(). A core is allowed to ask
    // for these directories inside retro_init, and several do: melonDS reads
    // both there, keeps them in a static buffer, and never asks again. Setting
    // them at game-load time is too late — the core has already built its save
    // path out of an empty string, and melonDS was writing `/Tetris DS.sav`, at
    // the root of the filesystem, where it cannot be written.
    //
    // That failure is silent. The game runs, the save simply never lands, and
    // nothing says so. It hits exactly the cores that write their own save
    // files rather than exposing RETRO_MEMORY_SAVE_RAM — melonDS, Neo Geo
    // Pocket, Sega CD, FBNeo's NVRAM — which is the class the reference
    // implementation already lost saves to once, by a different route.
    //
    // saveDir MUST outlive the session: pointing it at a per-launch temp
    // directory is the other way that same lesson gets learned.
    void setDirectories(const std::string& systemDir, const std::string& saveDir);

    // --- Core options --------------------------------------------------------
    //
    // AN UNANSWERED OPTION IS NOT THE DEFAULT, it is whatever the core's C
    // global was initialised to, which is zero — silence for a sample rate,
    // black for brightness, off for every toggle whose useful state is on. The
    // core skips the case entirely rather than falling back. So the host
    // captures the table a core declares and answers every key in it.
    //
    // See core_options.cpp for the three generations of the declaration API
    // and why we report version 2.
    struct OptionReport {
        std::string key;
        std::string desc;
        std::vector<std::string> values;
        std::string defaultValue;   // what the core says its default is
        std::string chosen;         // what we answered with
        bool overridden = false;    // chosen because we said so, not the default
        bool asked = false;         // the core actually came back for it
    };

    // The control, for measuring what answering options actually changes.
    // Off means GET_VARIABLE answers nothing, which is what this host did
    // before the table was captured. Not a product setting — a way to run the
    // comparison rather than assert it.
    static void setAnswerOptions(bool on);

    // The control for the OTHER half of what a core needs from this host: with
    // this set, SET_HW_RENDER is refused however serveable the ask was. See
    // core_hwrender.cpp. It tells "this core cannot get a picture out of this
    // host" apart from "this core cannot run in this host at all".
    static void setRefuseHWRender(bool on);

    // Whether a core that asks for desktop OpenGL can have it, in a context
    // of its own beside the UI's (#219; melonDS's renderer is the one). Found
    // out by making one, once, the first time it is asked with a window up;
    // false, and asked again later, before that. On the frame thread.
    static bool canServeDesktopGL();

    // Deliberate choices, keyed by option. Set BEFORE load(): a core may read
    // its options during retro_init, and several do.
    //
    // Empty is the honest starting point and is not the same as the old
    // behaviour: with no overrides every option is still answered, with the
    // core's own stated default. An override is for the cases where CabinetOS
    // wants something other than what the core ships with.
    void setOptionOverrides(const std::map<std::string, std::string>& overrides);

    // One option from the command line (`--core-option key=value`,
    // repeatable), for finding out what a value does before it goes into
    // catalog::optionOverrides or quality.cpp, without a rebuild per value.
    // Laid ON TOP of every setOptionOverrides, so it reaches a library launch
    // (`--launch <id>`, or Play on the television) as well as `--core`, wins
    // over the picture level and the system's own rows, and can take a
    // shipped override back off to see what it was buying. Said in the log
    // at each launch. Before the first launch: cli.cpp, as it reads the flag.
    static void setCommandLineOption(const std::string& key, const std::string& value);

    // One option changed WHILE THE GAME RUNS (the pause menu's system rows,
    // #73): kept with the overrides, and the core is told on its next
    // GET_VARIABLE_UPDATE that something changed, so it reads it again. Only
    // for options the core applies live; one that says it needs a restart
    // takes it at the next start.
    void setOptionLive(const std::string& key, const std::string& value);

    // What the currently loaded core declared, in declaration order, with what
    // it was answered. This is the audit: it is the only way to see what a core
    // can be configured with, and what it is actually running on.
    std::vector<OptionReport> options() const;

    // Keys the core asked for and never declared. Nothing can honestly be
    // answered for these; an empty list is the expected result.
    std::vector<std::string> undeclaredOptionAsks() const;

    // dlopen, resolve the retro_* entry points, retro_init. Returns false with
    // a reason on `error()`.
    bool load(const std::string& soPath);
    void unload();

    // The directories are the ones given to setDirectories, and are re-applied
    // here for the cores that ask at load-game time instead.
    bool loadGame(const std::string& romPath, const std::string& systemDir,
                  const std::string& saveDir);
    void unloadGame();

    // Advances emulation by however many frames are due, given the wall clock.
    //
    // Deliberately not "run one frame per draw". The panel's refresh rate and
    // the core's frame rate are different numbers — an NTSC core wants 59.94
    // and a television may be at 60, 120 or anything else — and running once
    // per draw makes the emulated machine run fast or slow by exactly that
    // ratio. The reference implementation measured the symptom on Dreamcast:
    // 65,000-85,000 audio frames a second against 44,100 of realtime, which is
    // what made the music play back sped up.
    //
    // Returns how many emulated frames actually ran.
    int runFor(double dtSeconds);

    // EXACTLY ONE emulated frame, whatever the clock says (#221). For a game
    // locked to the screen: one per refresh, the sound bent to match.
    int runFrame();

    // FAST FORWARD (#77): how many emulated seconds pass per real one. 1 is
    // normal. runFor steps up to this many times more frames per draw; the
    // caller discards the sound meanwhile. No effect on PlayStation 2, whose
    // emulator paces itself; fast forward is not offered there anyway (it goes
    // with states, which PlayStation 2 does not have).
    void setSpeed(double s) { speed_ = s < 1.0 ? 1.0 : s; }
    double speed() const { return speed_; }

    // Whether the emulated machine is actually RUNNING, as opposed to still
    // being built. Audio is the signal because it is the one output that does
    // not appear until the machine runs — a picture arrives while a core is
    // still initialising, so frames cannot answer this.
    //
    // It exists because of what happens otherwise: PPSSPP builds the machine on
    // a thread of its own, and unloading the game while that is in flight frees
    // the memory out from under it. The core dies in `__PPGeInit`, off
    // `PSP_InitStart`, and it is repeatable — quit a PSP game two seconds after
    // starting it. Only PPSSPP has a thread like this, but the question is
    // asked of every core because "is it running yet" is not core-specific.
    bool running() const { return audioFramesTotal() > 0; }

    // Uploads the most recent frame into `texture()`. Call on the GL thread.
    // Returns false if the core has not produced a picture yet.
    //
    // A hardware-rendered core has nothing to upload — see below — and this
    // becomes a no-op for it rather than a second code path at every call
    // site.
    bool uploadFrame();
    GLuint texture() const;
    unsigned frameWidth() const { return frameWidth_; }
    unsigned frameHeight() const { return frameHeight_; }

    // --- Hardware-rendered cores ---------------------------------------------
    //
    // WHAT THIS IS FOR. Three cores in the set do not hand back a buffer of
    // pixels: Flycast, Mupen64Plus and PPSSPP draw with GL themselves, which is
    // Dreamcast, Naomi, N64 and PSP — forty-three games in the reference
    // library today and every heavy system after it. They ask for a context
    // with RETRO_ENVIRONMENT_SET_HW_RENDER, and while this host refused that
    // ask those cores could be built and could never be run (`catalog` had an
    // answer of its own for them, removed with nothing left to give it, #492).
    //
    // WHAT IS ACTUALLY HANDED OVER, AND WHY IT IS NOT A CONTEXT. The frontend
    // already owns one GLES 3.0 context and draws the whole UI in it. So the
    // core is given a framebuffer object inside that same context, not a
    // context of its own: it renders into a texture this class owns, and the
    // player then draws that texture. Nothing is ever read back. On Apple the
    // readback is unavoidable and costs a full frame copy per frame; here it
    // is simply absent, which is the thing docs/PROJECT.md calls "free on
    // Linux" in "Video: two paths".
    //
    // THE TARGET IS THE CORE'S DECLARED MAXIMUM, NOT ITS CURRENT PICTURE. A
    // core that upscales internally reports a small base size and a large
    // maximum, and changes the size it actually draws whenever its resolution
    // option moves. So the texture is allocated once at the maximum and the
    // picture occupies a corner of it — which is why the picture's position in
    // the texture is a question with an answer rather than "all of it", and
    // why frameUV exists.
    //
    // THE PICTURE IS UPSIDE DOWN, and that is not a bug to be discovered. GL
    // renders bottom-left origin and every software core hands back a top-down
    // buffer. The core states which convention it used in its callback, so
    // this is read rather than guessed.
    //
    // Where the picture sits inside texture(), as texture coordinates, with
    // the vertical flip already applied when the core rendered bottom-up. A
    // software core answers the whole texture, the right way up.
    void frameUV(float& u0, float& v0, float& u1, float& v1) const;

    // How far the picture has to be turned before it is shown, in 90-degree
    // counter-clockwise steps: 0, 1, 2 or 3.
    //
    // A VERTICAL ARCADE BOARD RENDERS SIDEWAYS ON PURPOSE. Its monitor was
    // bolted into the cabinet turned ninety degrees, so the board draws a
    // picture that is sideways in memory and asks the frontend to turn it
    // round with RETRO_ENVIRONMENT_SET_ROTATION. Ignore the ask and every
    // TATE shmup in the library plays on its side — which is what this
    // console did until 2026-09-19.
    //
    // It is the CORE's answer and not the platform's, and it cannot be
    // anything else: FBNeo and MAME each serve upright and vertical boards
    // from one platform row and only know which after the game is loaded.
    //
    // It composes with frameUV rather than replacing it. frameUV says where
    // the picture is in the texture and which way up its rows are; this says
    // how the picture, once found, is turned. A hardware-rendered vertical
    // board needs both.
    unsigned rotation() const;

    // True when the turn is an odd number of quarters, which is the case that
    // changes the SHAPE of the picture: a 320x240 board becomes a 240x320
    // picture, so the aspect ratio inverts and the window on a 16:9 panel is
    // tall rather than wide. Callers laying the picture out need this; callers
    // drawing it just pass rotation() through.
    bool rotatedQuarterTurn() const { return (rotation() & 1u) != 0u; }

    // Whether the loaded core is rendering through GL rather than handing back
    // a buffer. For the audit and for the catalog, not for the draw path —
    // the draw path asks frameUV and does not need to know.
    bool hardwareRendered() const;

    // Drains audio produced since the last call, as interleaved 16-bit stereo
    // at the core's own rate.
    const std::vector<int16_t>& drainAudio();

    void setPad(int port, const PadState& pad);
    // A key of the core's keyboard, as libretro numbers it (RETROK_*), held
    // or let go, for port 0: asked by the core, and told to a core that
    // gave its keyboard callback, as it changes. No real keyboard is passed
    // to a core; the one key pressed is a MAME's menu key
    // (arcadesettings.h). Every key is let go when a game loads.
    void setKey(unsigned retrok, bool down);
    // How many players there are, so the core is told a controller is in
    // each of their ports. Before loadGame it sets what the game starts with;
    // during a game a higher number plugs the new ports in. Never unplugs: a
    // player whose pad went off keeps their controller in the machine.
    void setPlayers(int n);

    // What each player's port holds, as a libretro device id, set before
    // loadGame and kept until the game unloads. A plain joypad unless a
    // platform says otherwise, and two do: Dolphin reads the joypad as an
    // upright Wii Remote, so a Wii game that takes a Classic Controller is
    // told one is plugged in (wii.h); and every PlayStation game gets a
    // DualShock (kPs1DualShock, #345; launch.cpp says why).
    void setPadDevice(unsigned device);
    // PCSX ReARMed's DualShock, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1),
    // 517 (RETRO_DEVICE_PSE_DUALSHOCK in its frontend/libretro.c).
    static constexpr unsigned kPs1DualShock = (2u << 8) | 5u;

    // REAL WII REMOTES IN THE PORTS AFTER THE PADS (#200). Set before loadGame
    // with the number of ports the pads take (0 for a game that needs a Remote:
    // pads have no player in it); every port from there up is told a real Wii
    // Remote is plugged in, and Dolphin seats each Remote the OS has connected
    // in the first free one, now or mid-game. Cleared when the game unloads.
    void setRealRemotesFrom(int firstPort);

    // WHAT EACH PORT'S BUTTONS DO IN THIS GAME, as the core last declared
    // them and in its order: the joypad's ids, index 0, for the four ports
    // (core_input.cpp). Empty for a port the core said nothing about.
    // Replaced when the core declares them again, emptied when a game loads,
    // and the version moves only when they change, so a reader can keep
    // what it made of them (fightstick.cpp).
    const std::vector<InputDescriptor>& inputDescriptors(int port) const;
    unsigned inputDescriptorsVersion() const;
    // THE GAME'S BUTTONS DECLARED BY THE CONSOLE, for every port, in place of
    // what the core declared, for a core that names none of a game's own:
    // MAME 0.289, whose list is the same for every game and is emptied with
    // the rest at loadGame (fightstick::mameButtons). After loadGame.
    void declareButtons(const std::vector<InputDescriptor>& buttons);

    // --- PlayStation 2 ---------------------------------------------------
    //
    // PCSX2 is a whole emulator rather than a libretro core, so three things
    // have to be said before a game starts that no core needs told. See
    // frontend/src/ps2.h.
    //
    //   resourcesDir  PCSX2's own game database, fonts and GS shaders. It
    //                 REFUSES TO START without them rather than degrading,
    //                 the same way PPSSPP needs its system files.
    //   the card      NOT passed in. loadGame names it from the rom, by the
    //                 same rule catalog::saveFiles uses — `<stem>.ps2` in the
    //                 per-game save directory — so the existing save
    //                 machinery works on it unchanged and the two cannot
    //                 drift apart.
    //   upscale       1.0 is the PlayStation 2's own resolution.
    //   widescreen    PCSX2's "Widescreen 16:9" patch, where the game has one
    //                 (ps2::widescreenOn).
    //   discCode      RomM's `title_id`, `SLUS-21090`, empty for none: the
    //                 BIOS is chosen by the game's region from it (#416).
    void setPs2(const std::string& resourcesDir, float upscale, int anisotropy,
                bool widescreen, const std::string& discCode);

    // Freezes the emulated machine. A NO-OP FOR EVERY LIBRETRO CORE, which
    // stops simply because the frame loop stops stepping it — and not optional
    // for PlayStation 2, which runs on a thread of its own and would otherwise
    // go on being played behind the overlay.
    void setPaused(bool paused);

    // Whether the loaded emulator is PCSX2 rather than a libretro core. The
    // frontend needs this in exactly two places: to know that pausing takes a
    // call, and that a save state is not available yet.
    bool isPs2() const;

    // --- Save states ---------------------------------------------------------
    //
    // The whole product rests on these. A state written on another console has
    // to load on this one; core parity once meant Cabinet's Apple TV app too
    // (docs/PROJECT.md, "Core parity is a hard constraint", since overtaken).
    //
    // THREADING: both must run on the thread that drives runFor. A snapshot
    // taken part-way through a retro_run is corrupt by definition.
    size_t stateSize() const;
    bool saveState(std::vector<uint8_t>& out);
    // Whether this game's state save runs the emulated machine through a
    // frame first, as Mupen64Plus-Next's does (#254): learned from the first
    // save that made sound. Such a save counts in framesRun, runs the
    // after-frame call and uses up a frame of runFor's budget, and the frame
    // loop takes rewind's snapshots in place of a run rather than after one.
    bool saveRunsFrame() const { return saveRunsFrame_; }
    // Returns false when the core rejects the bytes, which is the expected
    // failure for a state written by a different build of the same core — the
    // exact thing the manifest exists to prevent.
    bool loadState(const std::vector<uint8_t>& data);

    // The battery-backed save: a cartridge's save RAM, a PS1 memory card,
    // Saturn's internal backup. RETRO_MEMORY_SAVE_RAM, which is a DIFFERENT
    // mechanism from a save state and must not be conflated with one — a state
    // is a snapshot of the whole machine and only loads in the build that
    // wrote it, while this is the game's own save and outlives everything.
    //
    // The pointer the core hands back is live: it is the core's own memory, so
    // reading it mid-frame is how a snapshot is taken, and writing into it
    // before the game boots is how one is restored.
    bool readSaveRam(std::vector<uint8_t>& out) const;
    bool writeSaveRam(const std::vector<uint8_t>& data);
    size_t saveRamSize() const;

    // --- In-game saves, read by --state-test ---------------------------------
    //
    // A different mechanism from save states, and the one people assume is
    // safe once the game says it saved. Most cores expose the cartridge
    // battery here; some write their own files into the save directory
    // instead and this returns nothing for them.
    bool saveRAM(std::vector<uint8_t>& out) const;
    // Any region the core exposes, by its RETRO_MEMORY_* id. Game Boy keeps
    // its real-time clock in a region of its own.
    bool memoryRegion(unsigned id, std::vector<uint8_t>& out) const;

    // --- Discs (#162) ----------------------------------------------------------
    //
    // The core's disc drive, for a game on several discs handed over as a
    // list (disc.h). Index is the LIST's, from 0; the app maps it to the
    // disc's real number. All on the thread that drives runFor.
    bool hasDiscControl() const;
    int discCount() const;          // 0 when the core has no drive to ask
    int discIndex() const;          // -1 when not known
    bool discEjected() const;
    bool setDiscEjected(bool ejected);
    // Beetle Saturn takes an index only while the lid is open.
    bool setDiscIndex(int index);
    // The path the core built for a list entry, or empty.
    std::string discPath(int index) const;
    // BEFORE loadGame: start on this list entry, where the core can
    // (pcsx_rearmed, Flycast). `path` must be the exact string the core
    // builds from the list.
    void setInitialDisc(int index, const std::string& path);

    // --- RetroAchievements (#74) ---------------------------------------------
    //
    // The emulated machine's memory, LIVE rather than copied: rcheevos reads it
    // every frame. The map is the one the core handed over in retro_load_game
    // (null when it gave none); the pointer is retro_get_memory_data's. Both
    // libretro only; PlayStation 2's memory comes from ps2.h. Valid until
    // unloadGame.
    const retro_memory_map* memoryMap() const;
    // Moves each time a core hands over a map (Dolphin's comes on its first
    // frame), so a reader knows to lay memory out again.
    unsigned memoryMapGeneration() const;
    // Called after every retro_run while set: RetroAchievements checks each
    // emulated frame, and one screen refresh can run several. The frame loop
    // sets it around normal play only, so rewind and the pause menu's redraw
    // frames are not counted. Not used for PlayStation 2 (ps2.h).
    void setAfterFrame(void (*fn)()) { afterFrame_ = fn; }
    uint8_t* memoryPointer(unsigned id, size_t* size) const;

    // What the core asked to be run at: at load, and again whenever it asks
    // mid-game (SET_GEOMETRY, SET_SYSTEM_AV_INFO, #220), taken after the
    // run that asked. The layout reads it every frame. A new sound rate also
    // moves soundRateChanges(), so whoever feeds the speaker sets it again.
    const AVInfo& avInfo() const { return av_; }
    unsigned soundRateChanges() const { return soundRateChanges_; }
    const std::string& coreName() const { return coreName_; }
    // The core's own answers about what it will open. Both come from
    // retro_get_system_info and neither is knowable from the platform: a
    // platform does not have an opinion about archives, and two cores serving
    // the same platform can differ. See romfile.h.
    const std::string& validExtensions() const { return validExtensions_; }
    bool blockExtract() const { return blockExtract_; }
    const std::string& coreVersion() const { return coreVersion_; }
    const std::string& error() const { return error_; }

    // A hash of the current picture, for determinism checks. Cheap, and it
    // catches a divergence that audio alone would not.
    uint64_t frameDigest() const;

    // THE GAME'S OWN PICTURE, AS RGBA, THE WAY UP IT IS SHOWN: for the
    // picture a save state carries (#99). Read from the game's frame, never
    // from the screen, so the pause menu drawn over it is not in it. Rows top
    // first, flipped and quarter-turned exactly as the player draws it, at the
    // core's own resolution (as Cabinet sent). False with nothing to
    // read: no frame yet, or the PS2 path, which has no states. Call on the
    // GL thread.
    bool snapshot(std::vector<uint8_t>& rgba, unsigned& width, unsigned& height) const;

    // Emulated frames run since the game loaded, and audio frames produced.
    // Audio against the core's own sample rate is the only direct read on
    // whether emulated time is advancing at realtime.
    uint64_t framesRun() const;
    uint64_t audioFramesTotal() const;

private:
    Core() = default;
    void* symbol(const char* name);
    // uploadFrame's software path (core_picture.cpp).
    bool uploadSoftwareFrame();
    // loadGame's PlayStation 2 start (core_ps2.cpp).
    bool startPs2Game(const std::string& romPath, const std::string& systemDir,
                      const std::string& saveDir);
    void takeAvAsked();

    void* handle_ = nullptr;
    bool gameLoaded_ = false;
    std::string error_;
    std::string coreName_, coreVersion_, validExtensions_;
    bool blockExtract_ = false;
    bool needFullpath_ = false;
    AVInfo av_;
    unsigned soundRateChanges_ = 0;

    GLuint texture_ = 0;
    unsigned frameWidth_ = 0, frameHeight_ = 0;

    // Wall-clock pacing. Capped so a stall cannot bank a debt the core then
    // tries to repay all at once, which stutters and floods the audio buffer.
    void step();   // one retro_run, with the GL state around it
    void (*afterFrame_)() = nullptr;
    double accumulator_ = 0.0;
    double speed_ = 1.0;
    bool saveRunsFrame_ = false;   // see saveRunsFrame
};

}  // namespace cab
