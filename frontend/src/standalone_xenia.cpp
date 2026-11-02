// Xbox 360, through Xenia Edge in the image: the controller list and no kept
// settings file before every start (prepareXenia), the profile named for this
// person once their saves are down (xeniaBeforeStart), and Edge's settings,
// all on its command line (xeniaArgs). Moved out of standalone.cpp in the
// restructure (#398).

#include "standalone_internal.h"

#include "gpu.h"
#include "storage.h"
#include "vpad.h"
#include "x360profile.h"

#include <unistd.h>

#include <cstdio>
#include <vector>

namespace cab::standalone::detail {

// --- Xbox 360 (Xenia Edge) --------------------------------------------------
//
// Decided with MMagTech 2026-09-29, issue #192, open question 34: Edge as it
// ships, never patched; one profile ID on every console for good
// (x360profile.h), named for the person on Home; the person's saves written
// straight into their own folder for the game, as Eden's are.
//
// WHERE THINGS ARE. Edge's storage root is its home here, shared by every
// game and person: its shader cache and the game-scratch `cache` folders,
// none of which is progress. Its CONTENT ROOT, where the profile and the
// saves live, is `<saveDir>/content`: this person's folder for this game.
// So two people and two games never share one, nothing is copied in or out
// around a game, and the zip that travels is Xenia's own layout,
// `<XUID>/...` and `0000000000000000/...`, which drops into any Xenia.
//
// WHY THE WHOLE PROFILE FOLDER TRAVELS. A game's own GPD (achievements, and
// the settings some games keep there) is rewritten empty at start unless the
// profile's dashboard GPD, FFFE07D1.gpd, lists that game (user_profile.cc);
// measured on the A9: 41,780 bytes became 35,208. Both sit in the profile
// folder, which is in the content root, so they come down together.

std::string xeniaContent(const std::string& saveDir) { return saveDir + "/content"; }

bool prepareXenia(const Emulator& e, const std::string& saveDir, std::string* err) {
    const std::string dir = home(e);
    storage::makeDirs(dir);
    storage::makeDirs(xeniaContent(saveDir));
    // EACH PLAYER'S CONTROLLER, BY NAME, as for xemu: Edge binds slots to
    // these SDL GUIDs (`--slot_bindings_passthrough`) and never sees a real
    // pad (Run::start).
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    if (!writeFile(dir + "/gamecontrollerdb.txt", db)) {
        *err = "could not write Xenia's controller list";
        return false;
    }
    // THE CONSOLE'S SETTINGS ARE ALL ON THE COMMAND LINE (Run::start), so the
    // settings file Edge writes back is thrown away before every start and
    // nothing it kept from last time can differ.
    ::unlink((dir + "/xenia-edge.config.toml").c_str());
    return true;
}

// What beforeStart does for Edge, once this person's saves are down.
bool xeniaBeforeStart(const std::string& saveDir, const std::string& player, std::string* err) {
    // AFTER the save came down, which may hold a profile file of its own
    // from another console or an older name: this one is who is here.
    const std::string tag = x360profile::gamertag(player);
    if (!x360profile::writeAccount(x360profile::accountPath(xeniaContent(saveDir)), tag,
                                   err)) {
        ::unlink((saveDir + "/" + kPlayingNote).c_str());
        return false;
    }
    std::fprintf(stderr, "[x360] signed in as %s (%s)\n", tag.c_str(), x360profile::kXuid);
    return true;
}

// Run::start's arguments for Edge, in the order standalone_internal.h states.
void xeniaArgs(std::vector<std::string>& full, const std::string& dir, const std::string& saveDir,
               const std::string& logPath, quality::Level level, const std::string& romPath) {
    // ONLY THE VIRTUAL CONTROLLERS, as for xemu: Edge's menus read
    // what is bound to a slot, and its slots auto-bind whatever SDL
    // shows it. SDL's own hint, no patch. GTK on gamescope's Xwayland.
    full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0");
    full.insert(full.end() - 1, "GDK_BACKEND=x11");
    std::string slots;
    for (int p = 0; p < 4; ++p) {
        if (p) slots += ';';
        if (p < cab::vpad::count()) slots += cab::vpad::sdlGuid(p);
    }
    const int gpu = cab::gpu::vulkan().deviceIndex;
    // The settings the A9 measurement ran with (Batocera's), the
    // integration's own, and nothing else. Names are Edge's cvars.
    for (const std::string& a : std::vector<std::string>{
             "--storage_root=" + dir,
             "--content_root=" + xeniaContent(saveDir),
             "--log_file=" + logPath,
             "--log_to_stdout=false",
             "--logged_profile_slot_0_xuid=" + std::string(x360profile::kXuid),
             "--fullscreen=true",
             // Vulkan on the GPU the console chose, never llvmpipe.
             "--gpu=vulkan",
             "--vulkan_device=" + std::to_string(gpu),
             "--discord=false",
             // Guide is the console's pause menu; no mouse menu.
             "--guide_button=false",
             "--disable_game_window_mouse=true",
             // Arcade games as the full game, not the trial.
             "--license_mask=1",
             "--mappings_file=" + dir + "/gamecontrollerdb.txt",
             "--slot_bindings_passthrough=" + slots,
             "--render_target_path=performance",
             // UNREAL ENGINE 3'S TILED ANTI-ALIASING (Gears of War, Mass
             // Effect, Batman) leaves black patches on characters unless
             // rasterized at the Xenos sample positions. The window offset
             // stays in the viewport, as every game drew at our earlier pin:
             // the EDRAM way fixes Halo 4's bottom tile but bands these.
             // docs/decisions/2026-11-01-xbox-360-unreal-engine-3-fix.md.
             "--xenos_sample_positions=true",
             "--window_offset_in_edram=false",
             "--occlusion_query=fast",
             "--async_shader_compilation=true",
             "--mount_scratch=true",
             "--protect_zero=false",
             // VSYNC ON (#209, MMagTech 2026-10-04). Edge has no vsync
             // setting: it takes the first present mode allowed of
             // immediate, mailbox, relaxed FIFO, FIFO, and under
             // gamescope got mailbox, which loses frames (#226). With
             // the first three off it is plain FIFO, vsync.
             "--vulkan_allow_present_mode_immediate=false",
             "--vulkan_allow_present_mode_mailbox=false",
             "--vulkan_allow_present_mode_fifo_relaxed=false",
         })
        full.push_back(a);
    // PICTURE QUALITY (#63), and the game last.
    for (const std::string& a : quality::xenia(level)) full.push_back(a);
    full.push_back(romPath);
}

}  // namespace cab::standalone::detail
