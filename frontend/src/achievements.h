// RetroAchievements (#74): softcore only, opt-in, per person.
//
// WHAT IT IS FOR. MMagTech, 2026-10-06: an incentive for other people to use
// the console. Somebody who never signs in never sees it: no row on a game's
// page, no pop-up, nothing (the Settings row is the one way in). Somebody who
// does gets a pop-up that fades in and out over the game when they earn one,
// never pausing it or taking a button, and a list on the game's page.
//
// SOFTCORE, AND ONLY SOFTCORE (decided 2026-10-05 on #74). Hardcore needs
// RetroAchievements to approve this frontend and keep every release to their
// checklist; none of it is built, hidden or otherwise. States and rewind work
// as they always do.
//
// THE LIBRARY IS rcheevos' rc_client, vendored in third_party/rcheevos, which
// is what RetroArch, Dolphin and PCSX2 use. achievements.cpp gives it the
// three things it asks a frontend for: the network (libcurl, on a worker, in
// ra_net.cpp), the emulated machine's memory (the core's map through
// rc_libretro, or PCSX2's memory through the bridge), and somewhere to say
// what happened (the pop-up queue and the log). The game's page's list is in
// ra_list.cpp, and what the files share in ra_state.h.
//
// THE GAME IS IDENTIFIED BY RomM's `ra_hash`, which RomM works out when it
// scans: the fingerprint RetroAchievements knows the game by. The console never
// reads the game file for it, so a .chd disc needs no disc reader here.
//
// THREADS. Everything here runs on the frame thread, with two exceptions: the
// PlayStation 2 frame callback runs on PCSX2's CPU thread (rc_client's own
// lock covers that, as in PCSX2's own achievements code, and gFrameMutex
// lets endGame wait out a frame's check that is running), and fetchBytes,
// for the image cache's workers, runs on any thread. Network answers are
// handed back to rc_client on the frame thread, in pump().

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ra {

// --- The client -------------------------------------------------------------

// Once at startup. `consoleVersion` goes in the User-Agent RetroAchievements
// asks every client to send ("CabinetOS/2026.10.06 (Linux) ...").
void init(const std::string& consoleVersion);
void shutdown();

// Once a frame on the frame thread: hands network answers to rc_client.
void pump();

// --- Signing in -------------------------------------------------------------

// The person now using the console (accounts::activeId()); 0 for nobody. Signs
// in quietly with their stored token, or signs out if they have none.
void useAccount(int accountId);

// Whether the current person has a RetroAchievements sign-in on this console.
// True from the moment one is stored, before the server has confirmed it,
// because the rows that depend on it must not flicker while it does.
bool signedIn();
// Their RetroAchievements username, as they typed it. Empty when signedIn()
// is false.
std::string username();

// The password sign-in, from Settings. The password goes to RetroAchievements
// once and is not kept; the token it answers with is (accounts::setRaLogin).
// `done` runs on the frame thread; `why` is ready to show in the field.
void signIn(const std::string& user, const std::string& password,
            std::function<void(bool ok, const std::string& why)> done);
void signOut();

// --- A game -----------------------------------------------------------------

// RetroAchievements' number for a platform, 0 for one it does not cover.
uint32_t consoleFor(const std::string& platformSlug);

// Whether a game in this core can earn anything here. MAME 2003-Plus returns
// nothing from retro_get_memory_data and sends no map, so a game it runs can
// earn nothing even when RetroAchievements has a set: its page says "None"
// rather than a count that could never move. Measured on the A9 with
// CABINETOS_RA_PROBE, 2026-10-06; every other core gives memory. AND AN ARCADE
// GAME (`arcade`, catalog::Coverage::arcade) earns only in FinalBurn Neo:
// RetroAchievements' Arcade sets are written for its memory, so one Flycast
// plays as a Naomi board has none to read.
bool coreReadable(const std::string& manifestCore, bool arcade);

// After the core has loaded the game. Does nothing unless somebody is signed
// in, the platform is covered and RomM gave a hash. `ps2` reads PCSX2's
// memory through the bridge instead of the libretro core's.
void beginGame(const std::string& raHash, const std::string& platformSlug, bool ps2);
// Before the core unloads it.
void endGame();
// THE ACHIEVEMENT PROGRESS A SAVE STATE CARRIES (#279), as RetroArch's do
// (cheevos/cheevos.c): rcheevos' own record of where each achievement and
// leaderboard stands, counts and all (rc_client_serialize_progress_sized),
// for the state's "ACHV" block (statefile.h). Empty when no game's
// achievements are being checked, which leaves the state as it was.
std::vector<uint8_t> progress();
// After a save state is loaded (not rewind): the memory jumped. With the
// progress the state carried, every achievement goes on from where it stood
// when the state was saved (rc_client_deserialize_progress_sized). Without
// it (a state from before #279, or saved by somebody not signed in), or when
// rcheevos will not take it, what each was waiting for starts again
// (rc_client_reset), as before.
void stateLoaded(const uint8_t* progress, size_t size);
// After the core ran this frame's frames (libretro only; PS2 calls itself).
void frame();
// Instead of frame() while the game is not running (the pause menu): keeps the
// session alive without reading memory.
void idle();
// --ra-sample only: once a frame while a game is up.
void sampleTick();

// --- What happened ----------------------------------------------------------

struct Popup {
    std::string title;      // the achievement's, or the game's when complete
    std::string detail;     // its description
    std::string badgeUrl;   // the picture, through the image cache
    uint32_t points = 0;
    bool complete = false;  // every achievement in the set is now unlocked
};
// The next pop-up to show, in the order they happened.
bool takePopup(Popup* out);

// --- The game's page --------------------------------------------------------

struct Achievement {
    uint32_t id = 0;
    std::string title, description;
    std::string badgeUrl, lockedBadgeUrl;
    uint32_t points = 0;
    bool unlocked = false;
};
struct GameList {
    // Asked and answered. False while waiting, or when it could not be asked
    // and nothing was kept from last time.
    bool known = false;
    // RetroAchievements has no achievements for this game (or does not know it).
    bool none = false;
    std::vector<Achievement> items;  // unlocked first, then locked, each in set order
    int unlocked = 0;
    int total = 0;
};
// The set and this person's unlocks for a game, on a worker; `done` on the
// frame thread. The last answer is kept, per person and game, so the page
// still has it offline.
void fetchList(const std::string& raHash, std::function<void(const GameList&)> done);

// --ra-sample (cli.cpp; started in startup.cpp): signed in as "sample", a
// made-up set of 40 with 12 unlocked on every page, and one unlock five seconds into any game. Nothing
// is sent anywhere. For captures only.
void useSample();

// A badge's bytes, for the image cache's workers. Blocking; any thread.
std::vector<uint8_t> fetchBytes(const std::string& url);
// Whether an image-cache key is a RetroAchievements picture.
bool isBadgeUrl(const std::string& key);

}  // namespace ra
