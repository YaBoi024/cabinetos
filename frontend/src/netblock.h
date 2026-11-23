// No emulator inside the console's program reaches the network: one block for
// every libretro core and PS2 (#557, "No emulator reaches the internet: one
// block for all of them").
//
// WHY. The console connects only to fixed places the code chooses
// (CONTRIBUTING.md, "The rules"), and an emulator reaches wherever its game
// does. Dolphin asked Nintendo's old servers for Mario Kart Wii's rankings,
// and one of those names still resolves to an address that never answers, so
// leaving the race waited out curl's 60 s (2026-10-22). One rule for every
// emulator, the ones added later included, instead of a setting or a patch
// for each.
//
// HOW. netblock.cpp defines socket(), the name lookups and pthread_create,
// and frontend/Makefile exports them from the program, so every library's
// call reaches them before the C library's; each calls the C library's own
// through dlsym(RTLD_NEXT). Each asks who called, by walking up the stack:
// the first frame that is either the console's program or a loaded emulator
// library decides, and the shared libraries in between (libcurl, libstdc++)
// are passed over. That matters because Dolphin and PCSX2 use the system's
// libcurl, the same one the console uses for RomM. A thread an emulator
// starts is the emulator's for its whole life, which is what catches curl's
// name-lookup threads; that mark decides only when no frame does.
//
// REFUSED, for an emulator: an internet socket (IPv4, IPv6, raw packets) and
// every name lookup, at once, logged once per game for each name asked for.
// LEFT ALONE: Unix, netlink and Bluetooth sockets, and everything the console
// itself asks for (RomM, RetroAchievements, Sunshine's API, the report), even
// from inside a call an emulator made into the console.
//
// Emulators that are programs of their own are started with no network at
// all instead (standalone.cpp, standalone_xemu.cpp). Why it is this way:
// docs/decisions/2026-10-26-emulators-get-no-network.md.
//
// LIMITS. An emulator that makes system calls without the C library, or
// starts a program of its own, gets past this. None does today.

#pragma once

#include <string>

namespace netblock {

// The library behind `handle` (dlopen's) is an emulator's from now on, named
// in the log by its file. It is also a new game: each refusal is logged once
// again. Called for every load, so loading the same library again is fine.
void addEmulator(void* handle, const std::string& path);

// After a dlclose: forget each emulator library that is gone. One the C
// library keeps loaded (a library with GNU unique symbols stays) is kept.
void dropUnloaded();

// --netblock-test: the rule above, with no core and no network.
int test();

}  // namespace netblock
