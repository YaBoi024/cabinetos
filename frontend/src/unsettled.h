// The files a launch marked complete that their drive may not have yet
// (#559).
//
// A download renamed into place, an unpacked file stamped with its archive's
// time, an install's note: each tells a later start that the game is whole.
// After a power cut an exFAT or NTFS drive can hold the name, the time or
// the note with the bytes it vouches for never written (fileio.h), and the
// console would keep a damaged game. Waiting for the drive before each mark
// costs minutes on a slow USB drive: Mario Kart 7's first start from a card
// writing 9 MB/s went from 19 s to 3 min 27 s. So each mark is listed here
// first, on the console's own drive, and the game starts at once; a thread
// waits for the drive, then drops the list. A list left from an earlier boot
// means the power went before the drive had it all: the files in it are
// deleted when that game next starts, so it is downloaded, unpacked or
// installed again.
//
// One list per game, `<config>/unsettled/<rom id>`: the boot it was written
// in, then one path a line. begin and settle run around each launch
// (launch.cpp); note is called by whatever marks a file complete while it
// runs.

#pragma once

#include <string>
#include <vector>

namespace unsettled {

// Starts game `romId`'s list. One left from an earlier boot is acted on
// first: every file in it is deleted. One from this boot is kept and added
// to. `boot` is this boot's id (update::bootId), a parameter so the files
// self-test can play two boots.
void begin(int romId, const std::string& boot);

// Lists `paths` before the files there are marked complete, and returns once
// the list is on the console's own drive. False with no list begun, or when
// the list cannot be written: the caller then waits for the drive itself.
bool note(const std::vector<std::string>& paths);

// Ends the list begun. A thread of its own waits until each drive in it has
// everything written to it (cab::syncDrive), then deletes the list, unless
// a later begin for the same game has taken it over.
void settle();

}  // namespace unsettled
