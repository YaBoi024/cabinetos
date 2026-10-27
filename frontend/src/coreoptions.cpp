// optionOverrides (coreoptions.h): every core option this console sets on
// purpose, each with its reason. Moved out of catalog.cpp in the restructure
// (#398).
//
// TWO ANSWERS HERE ARE HALF OF A PAIR, AND CHANGE WITH THE OTHER HALF IN
// savetable.cpp (saveFiles and firmwareAliases):
// opera_nvram_storage=shared is what gives saveFiles the 3DO's one fixed save
// path (opera/shared/nvram.0.srm), and opera_bios names the file
// firmwareAliases stages the 3DO's firmware under (panafz10.bin).
//
// NONE FOR PLAYSTATION 2: PCSX2 runs in the console's own host
// (frontend/ps2), which reads no core options. The host sets the card, one
// per game and named from the rom, and the pads' analogue mode itself
// (CabinetPS2Host.cpp), so the three pcsx2_* answers that were here reached
// nothing and were removed (#492).

#include "coreoptions.h"

#include <map>

#include "catalog_internal.h"
#include "gpu.h"
#include "wiiremote.h"

namespace catalog {

std::map<std::string, std::string> optionOverrides(const std::string& core) {
    return optionOverrides(core, cab::gpu::vulkan().available, wiiremote::sensorBarAbove());
}

std::map<std::string, std::string> optionOverrides(const std::string& core, bool vulkan,
                                                   bool sensorBarAbove) {
    // Callers hold the core by two different names — the manifest's, which is
    // what the launch path has, and the FILE's, which is what the options
    // audit has when it is walking a directory. So this takes either, and
    // resolves it against the table rather than by stripping suffixes blindly:
    // fbneo_libretro's manifest name ends in _libretro already, so a blind
    // strip would quietly invent a third name for it.
    //
    // Getting this wrong is silent. An override that matches nothing leaves
    // every option at its default and looks exactly like having no override —
    // which is the shape of bug catalog.cpp already warns about twice.
    const std::string coreName = manifestName(core);

    // Nearly empty, deliberately. Every option is answered with the core's own
    // default, which is the correct baseline and is what was missing. A choice
    // belongs here only when there is a reason for it, and the reason belongs
    // beside it — an override with no justification is the thing that goes
    // stale and that nobody can later tell apart from a mistake.
    if (coreName == "ppsspp") {
        // THE CPU ENGINE IS THE CORE'S OWN DEFAULT, "JIT", the native
        // recompiler, from 2026-09-27 (#147). This used to answer "IR JIT",
        // upstream's IR interpreter, to match Cabinet, whose bench found it
        // faster on an Apple M4 (Lumines 1.93 ms against 3.05 ms): Apple's
        // platforms may not run a recompiler, and the IR interpreter is what
        // PPSSPP built for them. On a PC the recompiler is what PPSSPP,
        // RetroArch and the libretro documentation all name as the default
        // and recommended engine, and MMagTech took that as enough
        // (2026-09-27). A state is the emulated PSP, not the engine, so the
        // states made under "IR JIT" should still load; that is checked on the TV.
        //
        // ppsspp_internal_resolution deliberately does NOT appear here. Its
        // declared default is "480x272", the PSP's own screen, which is what
        // RetroArch would answer and what Cabinet shipped on a television. It is
        // also the option that used to be this core's defaults trap: the
        // internal default is 0, "Auto (native)", which sizes the render to a
        // display a libretro frontend never reports, so every frame comes out
        // 0x0 and is dropped. Answering declared defaults already closes that,
        // which is this console's first free ride from the core-options work.
        // Raising it is a look-and-performance decision and it waits for real
        // hardware.
        //
        // PSP moved to Vulkan on 2026-09-20 with everything else (open question
        // 20), and crashed the console as it started until the host raised
        // every Vulkan instance to 1.1 as RetroArch does (vkhost.cpp) —
        // 2026-09-27. Lumines plays its demo on Vulkan on the A9.
        return {};
    }

    // Genesis Plus GX, and this one decides WHERE THE SAVE IS, not how it
    // plays. The Sega CD's internal backup RAM is the console's own 8 KB, so
    // the core names its file after the BIOS region — `scd_U.brm` — and every
    // Sega CD game in the library shares it. That is real hardware's own
    // behaviour and it is wrong here for two reasons: RomM files a save
    // against ONE rom, so a shared file would have to be filed under whichever
    // game happened to be played last; and 8 KB shared across a library fills
    // up. The reference implementation forced this for the same reasons on
    // 2026-08-16 and its save path assumes it.
    //
    // MEASURED, not read across: the first run of the save work restored
    // Lunar's real 8 KB card to `<stem>.brm` and Genesis Plus GX ignored it
    // and made a fresh `scd_U.brm` beside it. The option is the difference.
    // Each game already has its own save directory here, so "per game" costs
    // nothing beyond the name.
    //
    // cart_size stays at the core's declared "4meg", which already matches —
    // it is recorded here only because the reference forces it explicitly and
    // the reason is worth keeping: unanswered, the core's cart_size global
    // stays 0 and Sonic CD refuses to boot past "RAM cartridge not
    // initialized". Answering declared defaults is what closes that, which is
    // this console's second free ride from the core-options work.
    //
    // THE SOUND CHIP IS NUKED'S, to match Cabinet — 2026-09-27 (#89 audit).
    // A Genesis state records which FM core made it and restores into that
    // core's structures (core/sound/sound.c, sound_context_save/load at our
    // pin), so a state from Cabinet (Nuked) loaded under MAME's chip, or the
    // other way round, comes back with the running chip untouched and the
    // music wrong until the game rewrites it. Nuked is also the accurate one.
    // It costs more processor than MAME's: fine on the A9, untested on
    // weaker machines.
    if (coreName == "genesis_plus_gx") {
        return {
            {"genesis_plus_gx_system_bram", "per game"},
            {"genesis_plus_gx_ym2612", "nuked (ym2612)"},
        };
    }

    // PlayStation 1: NO SECOND MEMORY CARD, to match Cabinet — 2026-09-27
    // (#89 audit). The declared default "shared" puts one card in slot 2 for
    // every game, in the system directory, which the save sync never reads:
    // a game that saved there would keep that save on this console and never
    // reach RomM or another device. Card 1 is already "libretro", per game,
    // which is the one that syncs. No card 2 had ever been written on the
    // A9 when this changed.
    if (coreName == "pcsx_rearmed") {
        return {{"pcsx_rearmed_memcard2", "none"}};
    }

    // Virtual Boy: THE RIGHT STICK IS THE SECOND D-PAD (#89 audit). The
    // console has two d-pads and games use both (Red Alarm, Teleroboxer); a
    // modern pad has one, so without this half the controls are nowhere.
    // Cabinet forced it for the same reason. Input only.
    if (coreName == "beetle_vb") {
        return {{"vb_right_analog_to_digital", "enabled"}};
    }

    // DS: THE SOFTWARE RENDERER ON ITS OWN THREAD (#89 audit), as Cabinet
    // had it. It changes how the drawing work is split, not what a save or
    // state holds, and it is the headroom a weaker machine needs. melonDS
    // turns it off by itself under its OpenGL renderer (picture quality's,
    // #219).
    //
    // THE TOUCH SCREEN ON THE RIGHT STICK (#374): the stick moves a pointer
    // on the bottom screen and R3 touches, as Batocera ships it. The core's
    // default, Mouse, reads only a mouse, which this console never answers,
    // so the touch screen did nothing in any DS game. The DS has no sticks,
    // so nothing else wants the right one. A PlayStation pad's touchpad as
    // the touch screen was set aside, MMagTech 2026-10-18 (#288): melonDS
    // takes one touch mode at a time (src/libretro/input.cpp at our pin), so
    // those pads would lose the stick, and its Touch mode draws no cursor.
    // The stick is the one rule for every pad.
    if (coreName == "melonds") {
        return {
            {"melonds_threaded_renderer", "enabled"},
            {"melonds_touch_mode", "Joystick"},
        };
    }

    // Nintendo 3DS, in Azahar (docs/PROJECT.md question 50): the two screens
    // SIDE BY SIDE for every game, not the core's stacked default. On a 16:9
    // TV both screens draw about 19% larger that way (a 4K TV: the top screen
    // 2133x1280 against 1800x1080 stacked), and the touch screen stays large
    // enough to aim the pointer at; Large Screen draws it at a quarter of the
    // top screen's scale. Decided 2026-10-20. The controls are the core's own,
    // with no override: the right stick is the touch pointer and the New 3DS
    // C-stick at once, R3 touches (#374's rule, as on the DS). L3, the
    // core's swap, shows one screen alone instead and never reaches it
    // (screenview.h), which draws this layout as "both". quality.cpp's levels
    // are sized for this layout.
    if (coreName == "azahar") return {{"citra_layout_option", "side_by_side"}};

    // Beetle Saturn, and this one also decides where the save is. "libretro"
    // hands the Saturn's backup RAM to the frontend through SAVE_RAM, which
    // is what saveTag's `saturn-native` rides; "mednafen" makes the core keep
    // its own .bkr file instead, which nothing here reads, so every Saturn
    // save would stay on this console and never reach the server.
    //
    // It is already the declared default at the pinned ed549bda. Forced
    // anyway, because the reference forces it for exactly this reason
    // (NativeCoreOptions.swift): its Saturn saves had nowhere to go once
    // before, and a core bump that changed the default would strand them
    // again without a word.
    if (coreName == "beetle_saturn") {
        return {{"beetle_saturn_save_method", "libretro"}};
    }

    // NINTENDO 64 DRAWS WITH ParaLLEl-RDP WHEREVER THERE IS VULKAN — 2026-09-27
    // (#82). The declared default is GLideN64, which redraws the N64's
    // graphics with modern techniques and gets textures wrong in game after
    // game: MMagTech saw it in "almost every game", Mario Kart, Wave Race and
    // Hydro Thunder, and on Cabinet too. What the two share is GLideN64 built
    // for GLES3 (build-core.sh, FORCE_GLES3); RetroArch and Batocera run the
    // same default on desktop GL, which this frontend's GLES context cannot
    // host. ParaLLEl-RDP runs the N64's own graphics chip on the GPU, and is
    // what those frontends offer when accuracy matters (Batocera exposes it
    // per system). It needs the LLE RSP beside it; both are compiled in.
    // Run headless on the A9 with the three games: Vulkan device from the
    // core, 60 fps, every wall, the water and the boat drawn.
    //
    // ONLY WITH VULKAN, and that is the fallback, not an afterthought: a
    // machine without it (the test VM, older or odd hardware) keeps GLideN64
    // on GLES exactly as before.
    //
    // ITS RESOLUTION IS PICTURE QUALITY'S (quality.h, #63), on either
    // renderer: the 2x that stood here was a starting value until then.
    if (coreName == "mupen64plus" && vulkan) {
        return {
            {"mupen64plus-rdp-plugin", "parallel"},
            {"mupen64plus-rsp-plugin", "parallel"},
        };
    }
    // GameCube and Wii, one Dolphin. Both from the settings audit of
    // 2026-09-30 (docs/PROJECT.md open question 35), checked against Batocera:
    //
    // * CHEAT IMPORT OFF. On, the core writes RetroArch `.cht` files beside
    //   the system directory, for a program this console does not have; the
    //   reference console had /var/lib/cabinetos/cheats/dolphin-emu/ from two
    //   GameCube games. dolphinprep.cpp clears what it already wrote.
    // * DOLPHIN'S OWN ON-SCREEN MESSAGES OFF, as Batocera ships them. The
    //   console says what needs saying itself.
    //
    // Resolution, shader compilation and filtering are picture quality's
    // (quality.h, #63).
    if (coreName == "dolphin") {
        return {
            {"dolphin_cheats_import", "disabled"},
            {"dolphin_osd_enabled", "disabled"},
            // ERRORS ONLY IN THE LOG (#204). At Dolphin's default, Info,
            // Mario Kart Wii writes "Oversized EFB copy" about sixty times a
            // second into the journal: harmless to the game, wearing on the
            // disk, and burying the lines that matter. 2 is Error
            // (DolphinLibretro/Common/Options.cpp).
            {"dolphin_log_level", "2"},
            // WHERE THE SENSOR BAR IS, as the console was told under
            // Controllers (#200). 0 is below the TV, 1 above.
            {"dolphin_sensor_bar_position", sensorBarAbove ? "1" : "0"},
            // A REMOTE SWITCHED ON DURING A GAME JOINS IT (#269, MMagTech
            // 2026-10-06), as on a Wii: without this Dolphin looks for Remotes
            // once, at boot. Only for one the console has not met since it
            // started (the bridge keeps a stand-in for every Remote it has
            // seen); a Remote a game dropped still cannot come back, which is
            // Dolphin's. This build scans with hidapi only, no Bluetooth
            // inquiry: measured 0.3% of one core on the A9.
            {"dolphin_wiimote_continuous_scanning", "enabled"},
        };
    }
    // FLICKER BLENDED, NO ROW (#73, MMagTech 2026-10-02). Games on both drew
    // more than the hardware allowed by flickering sprites every other frame
    // and let the screen smear them together; on a modern display that is
    // just flicker. GBA on mGBA's Smart blending, which blends only the
    // pixels that flicker. The 2600 on Stella's Medium ghosting (75%):
    // judged on the TV with Ms. Pac-Man (MMagTech, 2026-10-02), whose ghosts
    // cycle over more frames than a plain two-frame average smooths; Heavy
    // (85%) left trails behind everything. Looks, not saves or states.
    // MAME 2003-PLUS STARTS EVERY GAME WITHOUT ITS COPYRIGHT NOTICE. The
    // declared default draws MAME's disclaimer over the first eight seconds
    // of each game (ui_copyright_and_warnings in src/usrintrf.c at our pin);
    // MMagTech asked for it off, 2026-10-25, at the TV, testing #577. Its
    // warnings about a game's known faults stay at their default: the core
    // sends those as a message to the frontend (RETRO_ENVIRONMENT_SET_MESSAGE),
    // which this console does not show.
    if (coreName == "mame2003_plus") return {{"mame2003-plus_skip_disclaimer", "enabled"}};
    if (coreName == "mgba") return {{"mgba_interframe_blending", "mix_smart"}};
    if (coreName == "stella2014") return {{"stella2014_mix_frames", "ghost_75"}};

    // Opera, and both of these are the difference between 3DO working and
    // 3DO not starting at all.
    //
    // opera_bios is the defaults trap at its purest. Its declared default is
    // "disabled", its value has to be a BIOS FILENAME, and answered with the
    // default the core has no BIOS ROM and there is no boot. RomM holds the
    // 3DO firmware under exactly this name on the reference server and the
    // launch path fetches every file a platform lists into `bios/`, so the
    // two halves meet. THE WEAK JOINT, said out loud: this is a filename
    // written down here and a filename on somebody's server, and nothing
    // checks that they agree. The reference stages whatever 1 MB firmware the
    // platform has UNDER this name, which is the stronger answer and is worth
    // building the day a server is found that calls it something else.
    //
    // opera_nvram_storage is a declared-default-versus-code-fallback mismatch:
    // the option table says "per game" and the core's own unanswered fallback
    // is "shared". Forced to shared deliberately, because it buys a fixed
    // filename, `opera/shared/nvram.0.srm`, that the save sync can rely on,
    // and each game already has its own save directory here so shared IS per
    // game. catalog::saveFiles depends on this being set.
    if (coreName == "opera") {
        return {
            {"opera_bios", "panafz10.bin"},
            {"opera_nvram_storage", "shared"},
        };
    }
    return {};
}

}  // namespace catalog
