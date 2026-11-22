// Saves that are a DIRECTORY rather than a file.
//
// Every other platform's save is one blob: a cartridge battery, a memory card,
// a chunk of NVRAM. PSP's is a folder — `PSP/SAVEDATA/<GAMEID><TITLE>/` holding
// PARAM.SFO, DATA.BIN and the icons — because that is what a memory stick holds
// and what PPSSPP reads and writes on every platform it ships on. There is no
// single-file PSP save and no upstream format for one.
//
// RomM stores exactly one opaque file per game and emulator, so a folder has to
// travel as an archive. THE ARCHIVE IS ZIP, and the reasons are worth keeping:
//
//   * it is what the PSP world already uses to move save folders around, so a
//     file taken off the server can be opened by anyone with any unzip tool and
//     dropped straight into a memory stick;
//   * the frontend already links libarchive for ROMs, so it costs nothing here;
//   * the alternative was Apple's `rtfd`, which the reference implementation
//     used and which is readable nowhere without Foundation. That was to
//     change on the Apple side too.
//
// THE CORE NEVER SEES THE ARCHIVE. It reads loose files out of the save
// directory exactly as it always has; the zip exists only between this console
// and RomM, and is unpacked before the core boots. Measured rather than assumed:
// zipping the real save folder, deleting it, and unpacking it back returns all
// four files byte-identical, and the game runs against the result.
//
// WHERE THE ZIP IS ROOTED, because this is the detail that silently breaks the
// other end: entries are relative to the SAVEDATA directory, so the first
// component is the save folder itself — `ULUS10002LUMINES/PARAM.SFO`. Opening
// one shows the game's save folder, which is what a person expects and what
// every PSP save download on the internet looks like. Unpack it into
// `<save dir>/PSP/SAVEDATA/`.

#pragma once

#include <locale.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cab {

// One entry in a directory save: a path relative to the archive root, and when
// it last changed. The time is what decides which folders belong to the game
// that just ran: see saves.cpp (listDirSave).
struct DirEntry {
    std::string relPath;    // "ULUS10002LUMINES/PARAM.SFO"
    // NANOSECONDS, and the size beside it, because seconds are not enough: a
    // save restored from the server and then rewritten by the game inside the
    // same second compares equal on a whole-second mtime, and the upload never
    // fires. A HAZARD CLOSED RATHER THAN A FAULT OBSERVED — it was first
    // reported here as a real failure and that was wrong, the game had simply
    // not written anything that run. The race is real; the sighting was not.
    int64_t mtimeNs = 0;
    int64_t size = 0;
    bool operator==(const DirEntry& o) const {
        return relPath == o.relPath && mtimeNs == o.mtimeNs && size == o.size;
    }
};

// Every regular file under `root`, depth first, with paths relative to it.
// A missing directory is an empty list rather than an error: a game that has
// never saved is the normal case, not a fault.
std::vector<DirEntry> listTree(const std::string& root);

// One file of an archive, in memory.
struct ZipFile {
    std::string relPath;
    std::vector<uint8_t> bytes;
};

// Zips the named files, which must be paths relative to `root`. Returns false
// with a reason when libarchive refuses; an empty selection returns false too,
// because "nothing to send" is a decision the caller should make rather than an
// empty archive nobody can tell apart from a failure.
bool zipTree(const std::string& root, const std::vector<std::string>& relPaths,
             std::vector<uint8_t>* out, std::string* err);
// The same from memory, each file under the name given: a save of several
// files, each under the path its core reads it from, whatever name the core
// left it under (savepush.h). False for none.
bool zipFiles(const std::vector<ZipFile>& files, std::vector<uint8_t>* out, std::string* err);

// Unpacks into `root`, creating directories as needed. Existing files are
// overwritten; files NOT in the archive are left alone, deliberately — a save
// folder is a whole slot rather than a newer version of one file, so if the
// newest-wins choice is ever wrong the cost should be a stale slot sitting
// beside a good one rather than somebody's save vanishing. That is the
// reference implementation's rule and the reasoning is its own.
//
// Refuses any entry whose path escapes `root` (absolute, or containing ".."),
// because an archive is data that came off a network.
bool unzipTree(const std::vector<uint8_t>& data, const std::string& root,
               std::string* err);

// Every file `data` holds, read into memory and written nowhere: for a save
// that is checked against what is on the disk before it is unpacked over it,
// and one that holds a core's files by name (saves.cpp). False, with a
// reason, when it cannot be read or a name is one unzipTree refuses.
bool readZip(const std::vector<uint8_t>& data, std::vector<ZipFile>* out, std::string* err);

// Whether the zip marks each of its names as UTF-8 (general purpose bit 11),
// keyed by the name's bytes, read from its central directory: where Python's
// zipfile, and so RomM, reads every name and its mark. libarchive gives a
// name and never says whether it was marked (the content hash,
// romm_snapshots.cpp). False when there is no central directory to read, or
// one name is in it both marked and not.
bool zipNameMarks(const std::vector<uint8_t>& zip, std::map<std::string, bool>* marked);

// Whether an archive member's name may be written under the folder it is
// unpacked into. An archive is data that arrived over a network, so its own
// names decide nothing about where bytes land: anything absolute or with a
// ".." component is refused outright rather than sanitised, because a
// rewritten path is a guess at what somebody meant. unzipTree and a game's
// archive (romfile.cpp, prepareFile) both go by it.
bool safeRelPath(const std::string& p);

// A ZIP'S NAMES READ AS THEIR BYTES, FOR THIS THREAD WHILE ONE OF THESE
// STANDS. The program runs in the C locale (it never calls setlocale), where
// libarchive cannot give a name a zip marks as UTF-8, the mark Python's
// zipfile and so RomM reads names by. With glibc's own C.UTF-8 it gives a
// name the zip marks rightly, and one it leaves unmarked, as the bytes the
// zip holds: readZip and unzipTree read
// under one, and RomM's content hash of a zip is read under one
// (romm_snapshots.cpp). zipTree and zipFiles choose the locale for each name
// they write, which is what decides the mark (dirsave.cpp).
class Utf8Names {
public:
    Utf8Names();
    ~Utf8Names();
    Utf8Names(const Utf8Names&) = delete;
    Utf8Names& operator=(const Utf8Names&) = delete;

private:
    locale_t utf8_;
    locale_t was_;
};

// Whether these bytes are a zip. Sniffed, never inferred from the name: the
// reference implementation's PSP saves are an Apple archive wearing an `.srm`
// extension, so the name says nothing at all about what is inside.
bool looksLikeZip(const std::vector<uint8_t>& data);

// RomM'S SHAPE FOR A FOLDER SAVE'S ZIP, WHERE IT IS NOT THE FOLDER AS IT IS
// (#346, step 8): Sigil's unit for the system (its sync page and its layout
// table at the commit RomM 5.5.0-alpha.2 builds in). Sigil reads a
// zip in its own shape only, so this console's saves reach the other apps
// only in it:
//   Wii: `<title>/data/...`, without Dolphin's `<kind>/` folder above it;
//   3DS: `<high>/<low>/data/...`, and the title's extra data as
//   `<high>/<low>/extdata/...`, without the SD card's `<id0>/<id1>/title/`
//   and `<id0>/<id1>/extdata/00000000/<low / 256>/` above them.
//   Switch (the Switch step): `<title>/...` for the profile's own saves and
//   `device/<title>/...` for the device's, which every profile shares,
//   without Eden's `<profile>/` and all-zero folder above them.
// A Wii or 3DS save holding any file that has no place in it travels in the
// folder's own shape, so nothing is ever left out. A Switch save is only
// what Sigil's unit holds: the profile's and the device's folders, without
// the size file each Switch emulator keeps beside a save
// (`.yuzu_save_size`), which Sigil never moves (inSwitchUnit). The zips
// already on RomM keep the folder's shape, which a start still reads
// (MMagTech's rule of 2026-10-28: no update breaks a save); a Switch zip
// from before was rooted in the profile's folder, `<title>/...`, the unit's
// own shape, and goes there whole.
enum class ZipShape { Folder, Wii, ThreeDS, Switch };
// `place`: Switch's profile folder (standalone::saveProfile); the other
// shapes take none.
std::vector<ZipFile> toZipShape(ZipShape shape, std::vector<ZipFile> files,
                                const std::string& place = std::string());
// Back to the folder's own paths; a zip in the folder's shape comes back as
// it is. `place`: the Wii title's kind folder, `00010000` for a disc; Switch's
// profile folder.
std::vector<ZipFile> fromZipShape(ZipShape shape, const std::string& place,
                                  std::vector<ZipFile> files);
// Whether a file under Eden's save root (standalone::saveRoot) is in the
// Switch unit: in `profile`'s folder or the device's, in a game's folder
// named by its title ID, and not a size file.
bool inSwitchUnit(const std::string& profile, const std::string& relPath);

}  // namespace cab
