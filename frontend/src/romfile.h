// What to hand a core, given whatever the server actually had.
//
// A RomM server holds whatever its owner put in it, and that is not uniform.
// Measured on the reference library, 1644 games: 801 `.zip`, 237 `.chd`,
// 84 `.7z`, a long tail of plain uncompressed ROMs — `.a78`, `.jag`, `.vb`,
// `.nds`, `.sfc`, `.nes`, `.md`, `.gb` — and **32 files with no extension at
// all**. Any of those is a normal thing for someone to own.
//
// Three rules come out of that, and each one rules out an obvious shortcut.
//
// NEVER DISPATCH ON THE FILE EXTENSION. Thirty-two files in one library do not
// have one, and a name is metadata a server happens to carry rather than a fact
// about the bytes. The codebase already holds this rule, in decodeImage: "the
// format is detected from the magic bytes, never from a file extension: a
// server hands you a content type and a body, not a filename."
//
// "COMPRESSED" DOES NOT MEAN "EXTRACT IT". `.chd` and `.rvz` are compressed
// formats that cores read natively — `chd` is in genesis_plus_gx's own
// valid_extensions. Helpfully unpacking one produces something no core can
// load. What decides is not whether the payload is compressed but whether the
// CORE can read it as it stands.
//
// ASK THE CORE, NOT THE PLATFORM. `retro_get_system_info` reports both the
// extensions a core accepts and `block_extract`, which is a core saying "give
// me the archive, I read it myself". Both are per core and neither is knowable
// from the platform.

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace romfile {

// What a payload actually is, from its leading bytes.
enum class Kind {
    Plain,      // a ROM, or a format we should not touch
    Zip,
    SevenZip,
    Rar,
    Tar,
    Gzip,
    // Called out so they are never mistaken for archives to unpack: both are
    // compressed containers that cores open themselves.
    Chd,
    Rvz,
    // NOT A GAME AT ALL. A web page where a ROM should be.
    //
    // FOUND ON THE REFERENCE LIBRARY, 2026-09-19, and it is not a curiosity:
    // three Master System entries — Gangster Town, Rambo III, Assault City —
    // are 5 to 9 KB files beginning `<!DOCTYPE HTML>`. They are error pages
    // from wherever the ROMs were fetched, saved with a `.7z` extension. RomM
    // serves them with HTTP 200 and reports `missing_from_fs: false`, so
    // nothing on that side notices either.
    //
    // WHAT IT LOOKED LIKE BEFORE THIS EXISTED: the console downloaded a web
    // page, handed it to Genesis Plus GX, the core accepted it, reported
    // correct Master System geometry, ran, and drew black. No error anywhere.
    // Three thousand frames later it was still black. **A black screen is the
    // worst possible way to say "that file is not a game"**, and this is the
    // one case where the bytes say so outright.
    NotAGame,
};

Kind sniff(const std::vector<uint8_t>& bytes);
const char* kindName(Kind k);
bool isContainer(Kind k);   // something libarchive should open

struct Member {
    std::string name;
    std::vector<uint8_t> bytes;
};

// Unpacks every member. All of them, not just the interesting one: a fullpath
// core given a `.cue` needs its `.bin` sitting beside it, and a multi-disc game
// needs the discs its `.m3u` names.
bool extractAll(const std::vector<uint8_t>& in, std::vector<Member>* out,
                std::string* err);

// What a core should be given.
struct Prepared {
    // Hand over the downloaded bytes untouched. True for a plain ROM, for a
    // `.chd`, and for a core that asked not to have its archives opened.
    bool passThrough = false;
    std::vector<Member> members;
    // Index into `members` of the file the core should actually be pointed at.
    int primary = -1;
    Kind kind = Kind::Plain;
};

// `validExtensions` is the core's own, pipe-separated, as libretro reports it:
// "gb|gbc|dmg". `blockExtract` is the core's own flag.
//
// In-memory, so COVERS-SIZED inputs only. A ROM goes through prepareFile.
bool prepare(const std::vector<uint8_t>& downloaded, const std::string& validExtensions,
             bool blockExtract, Prepared* out, std::string* err);

// The same decision made about a file already on disk, without reading it into
// memory. This is the one ROMs use: the reference library holds a 1.78 GB
// arcade set, and nothing about it should ever be resident.
Kind sniffFile(const std::string& path, std::string* err = nullptr);

// What this archive will become on disk, read from its own index rather than
// estimated. Zero when nothing will be written — a plain ROM, a .chd, a core
// that opens its own archives — and also when the format declines to say, which
// is a real answer and not an error.
//
// A multiplier was the first design here and it was wrong in the direction that
// fills a disk: an archive is COMPRESSED, so what comes out is not the size
// that went in. 868 KB of Space Harrier becomes 2 MB, and a DS ROM padded with
// empty space compresses far harder again. The number was already sitting in
// the file; nothing needed guessing.
int64_t unpackedSize(const std::string& path, const std::string& validExtensions,
                     bool blockExtract);

// Unpacks an archive into `outDir`, streaming each member through a buffer, and
// reports which file the core should be pointed at. Every member is written,
// in the folders the archive gives it: a .cue is useless without its .bin
// beside it. A member named outside `outDir` (absolute, or with a ".." part)
// or a name that comes twice fails the whole unpacking (#482, FILES-07).
//
// Returns the path to hand the core in `primaryPath`. When the file is not a
// container, or the core reads it as it stands, `primaryPath` is the input and
// nothing is written.
//
// `cancel` is the launch's (#472): set, the unpacking stops between two
// blocks and returns false, and what was written so far stays for the
// launch's own clean-up, as after a failed read.
//
// Each file written whole is given the archive's own modification time
// (#530), which is how alreadyUnpacked knows it later.
bool prepareFile(const std::string& downloadedPath, const std::string& outDir,
                 const std::string& validExtensions, bool blockExtract,
                 std::string* primaryPath, Kind* kindOut, std::string* err,
                 const std::atomic<bool>& cancel);

// WHETHER AN EARLIER prepareFile ALREADY UNPACKED THIS ARCHIVE INTO `outDir`
// (#530), so a start can skip writing it all again: 37 s at every start for
// Super Smash Bros. on the 3DS. Read from the archive's headers, never its
// data. True only when every file prepareFile would write is there at the
// size the archive lists AND carries the archive's own time, which
// prepareFile gives each file it finishes; `primaryPath` is then the file
// prepareFile would point the core at. Anything else unpacks again: a new
// download (the archive's time is new), an unpacking cut off (a short file,
// or one never stamped), anything that wrote into a file, a copy from
// another drive (fresh times). False too when prepareFile would write
// nothing, the file being handed over as it stands.
//
// Equal times, not "no older": each filesystem keeps a time at its own grain
// (exFAT's is 10 ms), and the file's time is the archive's as that same
// filesystem stored it, so the two stay equal on every drive.
bool alreadyUnpacked(const std::string& downloadedPath, const std::string& outDir,
                     const std::string& validExtensions, bool blockExtract,
                     std::string* primaryPath);

// Whether `fileName`'s extension is one the core lists ("bin|cue|chd").
bool accepts(const std::string& validExtensions, const std::string& fileName);

// Zstandard frames, decompressed through libarchive's raw reader (the console
// already links libarchive, built with zstd, for game archives), up to about
// `limit` bytes of output. What came out before an error is kept, so the
// start of a stream cut short still reads. A Wii U archive's blocks
// (wiiu.cpp) and a compressed 3DS game's first frame (ncch.cpp).
std::vector<uint8_t> unzstd(const std::vector<uint8_t>& in, size_t limit = SIZE_MAX);

}  // namespace romfile
