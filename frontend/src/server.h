// The RomM server this console belongs to: changing its address, and signing
// out of it. docs/SETTINGS.md, Network; issues #60 and #61.
//
// ONE SERVER AT A TIME. Games on the disk are filed by that server's game
// numbers and saves by its user numbers, and both mean something else on
// another server. So a different server is never an address change: it is
// Sign out, then first run.
//
// NOTHING HERE DRAWS. The app asks, shows the answer, and restarts itself to
// take it up (the address is read once, at startup, and so is first run).

#pragma once

#include <string>

namespace server {

// --- Change server address (#60) -------------------------------------------

enum class Check {
    Same,       // the new address answers for the signed-in account: same server
    Different,  // something answered and refused the token: another server
    NoServer,   // nothing speaks RomM there
    Failed,     // it answered and could not say; `detail` has why
};

// IS IT THE SAME SERVER. Asks `address` who the signed-in account's token
// belongs to. Only the server that issued a token accepts it, so the same
// person coming back means the same server. BLOCKS on the network: run it on
// a worker.
Check check(const std::string& address, std::string* detail);

// Whether the address is this console's to change. False when root set it
// (/etc/cabinetos/session.env, or CABINETOS_ROMM), which outranks anything
// this program writes; serveraddress.h says why.
bool addressIsOurs();

// Saves `to` as the server's address, and moves the covers filed under `from`
// so none are downloaded again. Takes effect at the next start.
bool changeAddress(const std::string& from, const std::string& to, std::string* err);

// --- Sign out (#61) --------------------------------------------------------

// Saves on this console that never reached the server, across every account.
// By design there are none: leaving a game uploads, and Settings is reached
// with the server answering. One is left only when the server was away as a
// game was quit and nothing has sent it since. Counted by cache::pendingCount,
// so only whole markers count, as Remove counts them (#482).
int unsentSaves();

// Signs out: writes the signed-out marker, and nothing else. The clearing is
// done by the next start, before anything runs, so no download or cover
// write can land in a folder that has just been emptied.
bool signOut(std::string* err);

// AT STARTUP, when the marker says signed out and not cleared. Removes that
// server's games (kept and cached, on every drive that is plugged in), what
// the console learned about them by rom id (their records in games/, the Wii
// and Wii U codes read off their files, the per-game picture and widescreen
// choices), every account's saves and states on this console, its covers, the
// accounts and their tokens, the PIN, and the address. Keeps Wi-Fi,
// controllers, BIOS and the console's own settings. A games drive that is not
// plugged in keeps its games, and nothing marks them as the last server's
// (RomM gives a server no identity): plugged in later under a new server,
// they are matched to its games by rom id (cache::findAll), online the drive
// backfill writes the new server's record over theirs, and a launch of the
// new server's game of that number downloads into the old one's entry (#484;
// what should become of them is not decided; docs/SETTINGS.md, Sign out).
void finishSignOut();

}  // namespace server
