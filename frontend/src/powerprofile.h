// The processor at full speed while a game runs, and balanced on Home (#150).
//
// BAZZITE SHIPS THE PROFILES AND NOTHING HERE SWITCHED THEM. tuned has
// `balanced` and `throughput-performance-bazzite`; on Bazzite they are picked
// from the desktop's power menu or Steam's performance menu, and CabinetOS has
// neither, so the A9 sat on balanced in games too. Balanced slows the cores
// down in every rest between frames.
//
// WHY IT MATTERS, measured on the A9, 2026-10-02: Mario Kart 8 Deluxe in Eden
// held 60 either way, with 6 frames over 20 ms in a lap on balanced and none on
// performance, and 11% less CPU time for the same work (124% of a core against
// 139%). Nothing a player sees today. What it changes is the reading: the
// automatic quality setting (#63) judges a game by how busy it is, and on
// balanced every game reads about a tenth busier than it is. GameCube showed
// no difference at all (#150), so this is for the heavy games and for #63.
//
// Home stays balanced: there the draw is the GPU redrawing 4K, and
// performance cost 11.02 W against 10.05 (#150). Power saver is not used.
//
// ANY GAME: the ones inside the console and the separate emulators alike.
// tuned owns the change; the console only asks it, over D-Bus, on a worker,
// so the frame loop never waits. Without tuned (another base, the VM's cage
// session without permission) the request fails, the log says so once, and
// nothing else happens. tuned keeps its last profile across a reboot, so the
// first call puts Home's back in case the last run ended inside a game.

#pragma once

#include <string>

namespace powerprofile {

// FORK (handheld): three tiers instead of two, so a Game Boy game does not run
// the processor as hard as a GameCube one. Home and Light both ask for
// Bazzite's battery-biased balanced profile (falling back to plain `balanced`
// where the base lacks it); Heavy asks for the full-speed one.
enum class Tier { Home, Light, Heavy };

// The tier for a game from a libretro core's own name (core.h coreName(), the
// library_name the core reports). An emulator run as its own program is always
// Heavy (the caller says so, not this). Unknown names are Light: a core the
// list does not know has never been measured as heavy.
Tier tierForCore(const std::string& libraryName);

// Once a frame: what is running? Acts only when the answer changes.
void update(Tier tier);

// Now, on this thread, and waits for tuned: for handing the screen to Steam
// (#223), where the frontend quits straight after and a worker would be cut
// off. The console's next start sets balanced again through update().
void applyNow(bool game);

}  // namespace powerprofile
