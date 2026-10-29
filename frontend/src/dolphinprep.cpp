#include "dolphinprep.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fileio.h"
#include "storage.h"

// DOLPHIN'S SHADER CACHE, ONE PER CONSOLE. Dolphin keeps it in its user
// directory, which is under this person's save directory for this game, so
// every person compiled every effect again for themselves. Linked to one
// folder beside the other emulators' homes, as Xenia's already is: an effect
// is compiled once per console. docs/PROJECT.md open question 35.
//
// A real folder already there is a cache made before this, by this person for
// this game; checked on the reference console, it holds only Dolphin's own
// shader, cover and achievement caches, all of which it rebuilds, so it goes.
void shareDolphinCache(const std::string& saveDir) {
    const std::string shared = storage::emulatorsDir() + "/dolphin/Cache";
    const std::string link = saveDir + "/User/Cache";
    storage::makeDirs(shared);
    storage::makeDirs(saveDir + "/User");
    struct stat st;
    if (::lstat(link.c_str(), &st) == 0) {
        if (S_ISLNK(st.st_mode)) {
            char target[4096];
            const ssize_t n = ::readlink(link.c_str(), target, sizeof target - 1);
            if (n > 0 && std::string(target, static_cast<size_t>(n)) == shared) return;
        }
        storage::removeEntry(link);
    }
    if (::symlink(shared.c_str(), link.c_str()) != 0)
        std::fprintf(stderr, "[dolphin] could not share the shader cache at %s: %s\n",
                     link.c_str(), std::strerror(errno));
}

// THE CHEAT FILES DOLPHIN WROTE FOR RETROARCH, removed. Until
// `dolphin_cheats_import` was turned off (catalog::optionOverrides) every
// GameCube launch wrote `<root>/cheats/dolphin-emu/<game>.cht`, made from
// Dolphin's own game settings, for a program this console does not have.
// Nothing here ever reads that folder or puts anything else in it.
void clearDolphinCheats() {
    const std::string dir = storage::root() + "/cheats";
    if (!storage::exists(dir + "/dolphin-emu")) return;
    storage::removeEntry(dir + "/dolphin-emu");
    ::rmdir(dir.c_str());   // only if nothing else is in it
    std::fprintf(stderr, "[dolphin] removed the RetroArch cheat files it once wrote\n");
}

// A PATH DOLPHIN WILL OPEN. Dolphin decides disc or WAD by the file's
// extension (Core/Boot/Boot.cpp), and a file on a server need not have one,
// or the right one. So the console reads the start of the file, as it does
// everywhere else, and when the name does not say what the bytes are, hands
// Dolphin a link named for what they are. The file itself is not touched.
std::string dolphinReadablePath(const std::string& romPath, int romId) {
    static const char* const kKnown[] = {"iso", "gcm", "tgc", "wbfs", "ciso", "gcz",
                                         "wia", "rvz", "wad", "elf", "dol", "m3u",
                                         "json", "dff"};
    std::string ext;
    if (const size_t dot = romPath.find_last_of('.');
        dot != std::string::npos && romPath.find('/', dot) == std::string::npos) {
        ext = romPath.substr(dot + 1);
        for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    for (const char* k : kKnown)
        if (ext == k) return romPath;

    unsigned char b[0x40] = {};
    const int fd = ::open(romPath.c_str(), O_RDONLY);
    if (fd < 0) return romPath;
    const ssize_t n = ::pread(fd, b, sizeof b, 0);
    ::close(fd);
    if (n < static_cast<ssize_t>(sizeof b)) return romPath;
    auto be32 = [&](size_t at) {
        return (uint32_t(b[at]) << 24) | (uint32_t(b[at + 1]) << 16) |
               (uint32_t(b[at + 2]) << 8) | b[at + 3];
    };
    const char* want = nullptr;
    if (std::memcmp(b, "RVZ\x01", 4) == 0) want = "rvz";
    else if (std::memcmp(b, "WIA\x01", 4) == 0) want = "wia";
    else if (std::memcmp(b, "WBFS", 4) == 0) want = "wbfs";
    else if (std::memcmp(b, "CISO", 4) == 0) want = "ciso";
    else if (be32(0) == 0x01C00BB1u) want = "gcz";   // 0xB10BC001, little-endian
    else if (be32(0) == 0x20 && (be32(4) >> 16 == 0x4973 || be32(4) >> 16 == 0x6962 ||
                                 be32(4) >> 16 == 0x426B)) want = "wad";
    else if (be32(0x18) == 0x5D1C9EA3u || be32(0x1C) == 0xC2339F3Du) want = "iso";
    if (!want) {
        std::fprintf(stderr, "[dolphin] %s is not a disc or WAD Dolphin knows\n", romPath.c_str());
        return romPath;
    }
    const std::string dir = storage::emulatorsDir() + "/dolphin/links";
    storage::makeDirs(dir);
    const std::string link = dir + "/" + std::to_string(romId) + "." + want;
    ::unlink(link.c_str());
    if (::symlink(romPath.c_str(), link.c_str()) != 0) return romPath;
    std::fprintf(stderr, "[dolphin] the file says %s; handing Dolphin %s\n", want, link.c_str());
    return link;
}

// Dolphin's own configuration, written before the core boots.
//
// SLOT A IS DOLPHIN'S OWN DEFAULT, A FOLDER OF `.gci` FILES (#344), at its own
// default place, `User/GC/<region>/Card A/` in this person's save directory
// for the game. That is what every other Dolphin keeps and what RomM's save
// channels carry (#346), so it is said here only so that nothing can change
// it. Until 2026-10-11 slot A was one whole raw card at `<saveDir>/card.raw`,
// because Cabinet for Mac kept one; Dolphin stamped the region into that name
// (`card.USA.raw`), so a card restored from RomM never reached the game.
//
// MemcardAPath STAYS, FOR ONE THING: when the folder does not exist yet,
// Dolphin reads every save off the raw card at that path, region stamped
// (`card.USA.raw`), into the folder (MigrateFromMemcardFile). That is how a
// card made here before, or one still on RomM, becomes `.gci` files: Dolphin's
// own code, not ours. restoreDirSave puts RomM's card there when there is
// nothing newer.
//
// MemoryCardSize is not written: Dolphin refuses it from Dolphin.ini
// (ConfigLoaders/BaseConfigLoader.cpp, s_setting_disallowed), so the 2 once
// written here never did anything; every card was the default 2043 blocks.
void writeDolphinConfig(const std::string& saveDir) {
    const std::string dir = saveDir + "/User/Config";
    const std::string path = dir + "/Dolphin.ini";
    std::string ini;
    ini += "# Written by CabinetOS before every GameCube launch. See\n";
    ini += "# writeDolphinConfig in frontend/src/dolphinprep.cpp.\n";
    ini += "[Core]\n";
    // ExpansionInterface::EXIDeviceType::MemoryCardFolder, and None for slot B.
    ini += "SlotA = 8\n";
    ini += "SlotB = 255\n";
    ini += "MemcardAPath = " + saveDir + "/card.raw\n";
    // A GAME ON SEVERAL DISCS ASKS FOR THE NEXT ONE BY NAME, and Dolphin puts
    // it in from the list the console hands over (#162): no menu for it.
    ini += "AutoDiscChange = True\n";
    if (!cab::writeBytes(path, std::vector<uint8_t>(ini.begin(), ini.end()))) {
        // Said out loud. Dolphin's defaults are the same folder, so saves
        // still sync; only a card from before would not be read in.
        std::fprintf(stderr, "[save] could not write %s - an old GameCube card will not be "
                     "read into the save folder\n", path.c_str());
    }
}
