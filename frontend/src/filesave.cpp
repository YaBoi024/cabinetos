#include "filesave.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace cab {
namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return suffix.size() <= s.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string parentOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// The VMU's own directory, blocks 253 down to 241, sixteen 32-byte entries to
// a 512-byte block. An entry's first byte says what occupies the slot: 0x33 a
// data file, 0xCC a game, 0x00 nothing.
bool vmuHoldsAFile(const std::vector<uint8_t>& d) {
    constexpr size_t kBlock = 512;
    constexpr size_t kCardBytes = 256 * kBlock;   // 131072
    // Anything that is not a whole 128 KB card is not a card this can read,
    // and refusing is the safe direction: the byte scan below would be reading
    // past the end of something else.
    if (d.size() != kCardBytes) return false;
    // The root block's signature, sixteen 0x55 bytes. Every one of the
    // thirteen real cards on the reference server carries it.
    const size_t root = 255 * kBlock;
    for (size_t i = 0; i < 16; ++i)
        if (d[root + i] != 0x55) return false;
    for (size_t blk = 241; blk <= 253; ++blk) {
        const size_t off = blk * kBlock;
        for (size_t e = 0; e < 16; ++e) {
            const uint8_t kind = d[off + e * 32];
            if (kind == 0x33 || kind == 0xCC) return true;
        }
    }
    return false;
}

// A PlayStation 2 card the console's BIOS has formatted. An untouched one is
// 8,650,752 bytes of 0xFF with no header at all — see savetable.h.
bool ps2IsFormatted(const std::vector<uint8_t>& d) {
    static const char kMagic[] = "Sony PS2 Memory Card Format";
    const size_t n = sizeof(kMagic) - 1;
    return d.size() > n && std::memcmp(d.data(), kMagic, n) == 0;
}

// A GameCube card with at least one save in its directory. Block 1 is the
// directory and block 2 its backup, 127 entries of 64 bytes each, and an
// entry's first four bytes are the game code with 0xFF meaning the slot is
// free. Both copies are read because a card interrupted mid-write can have
// one of them stale, and either naming a save is enough to keep the card.
bool gameCubeHoldsASave(const std::vector<uint8_t>& d) {
    constexpr size_t kBlock = 8192;
    constexpr size_t kEntry = 64;
    constexpr size_t kEntries = 127;
    // A card smaller than its own metadata is not a card this can read, and
    // refusing is the safe direction — the scan below would run off the end.
    if (d.size() < 5 * kBlock) return false;
    for (size_t blk = 1; blk <= 2; ++blk) {
        const size_t base = blk * kBlock;
        for (size_t e = 0; e < kEntries; ++e) {
            const uint8_t* code = d.data() + base + e * kEntry;
            const bool free =
                (code[0] == 0xFF && code[1] == 0xFF && code[2] == 0xFF && code[3] == 0xFF) ||
                (code[0] == 0x00 && code[1] == 0x00 && code[2] == 0x00 && code[3] == 0x00);
            if (!free) return true;
        }
    }
    return false;
}

bool anyRealByte(const std::vector<uint8_t>& d, size_t skipFront, size_t skipBack) {
    if (d.size() <= skipFront + skipBack) return false;
    for (size_t i = skipFront; i < d.size() - skipBack; ++i)
        if (d[i] != 0x00 && d[i] != 0xFF) return true;
    return false;
}

}  // namespace

bool holdsASave(const std::vector<uint8_t>& d, catalog::Untouched rule) {
    if (d.empty()) return false;
    switch (rule) {
        case catalog::Untouched::Blank:
            return anyRealByte(d, 0, 0);
        case catalog::Untouched::Uniform:
            return std::any_of(d.begin(), d.end(),
                               [first = d.front()](uint8_t b) { return b != first; });
        case catalog::Untouched::SegaCDBackup:
            return anyRealByte(d, 16, 64);
        case catalog::Untouched::ThreeDONvram:
            return anyRealByte(d, 176, 0);
        case catalog::Untouched::VmuDirectory:
            return vmuHoldsAFile(d);
        case catalog::Untouched::Ps2Format:
            return ps2IsFormatted(d);
        case catalog::Untouched::GameCubeDirectory:
            return gameCubeHoldsASave(d);
    }
    return anyRealByte(d, 0, 0);
}

bool passesCheck(const std::vector<uint8_t>& data, const catalog::SaveCheck& check,
                 catalog::Untouched rule, std::string* why) {
    auto no = [why](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (!check.sizes.empty() &&
        std::find(check.sizes.begin(), check.sizes.end(), data.size()) == check.sizes.end())
        return no(std::to_string(data.size()) + " bytes is no size this emulator's file has");
    if (!check.mark.empty()) {
        const size_t n = check.mark.size();
        const bool fits = check.markFromEnd ? data.size() >= check.markAt && check.markAt >= n
                                            : data.size() >= check.markAt + n;
        if (!fits) return no("too short to carry its format's mark");
        const size_t at = check.markFromEnd ? data.size() - check.markAt : check.markAt;
        if (std::memcmp(data.data() + at, check.mark.data(), n) != 0)
            return no("it does not carry its format's mark");
    }
    if (!holdsASave(data, rule)) return no("it holds no save");
    return true;
}

// Every file in `path`'s folder that ends in the spec's capture suffix and is
// not the other region's, by full path, sorted so a folder holding two answers
// the same way twice rather than however readdir felt.
static std::vector<std::string> captures(const catalog::SaveFile& spec, const std::string& path) {
    std::vector<std::string> out;
    const std::string dir = parentOf(path);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (!endsWith(name, spec.captureSuffix)) continue;
        // The exclusion is the whole of what keeps Sega CD's two regions
        // apart: `4Mbit_cart.brm` also ends in `.brm`, and without this the
        // cartridge would be captured as the internal backup RAM and then
        // restored over it.
        if (!spec.captureExclude.empty() && endsWith(name, spec.captureExclude)) continue;
        const std::string cand = dir + "/" + name;
        struct stat st;
        if (::lstat(cand.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        out.push_back(cand);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

std::string writtenFile(const catalog::SaveFile& spec, const std::string& path) {
    struct stat st;
    if (spec.captureSuffix.empty())
        return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? path : std::string();

    // A core that names the file itself reads its own name first and writes
    // only under it (catalog::SaveFile::coreNamesItself): the newest of the
    // files is the one it last wrote, `path` on a tie.
    if (spec.coreNamesItself) {
        std::string found;
        int64_t foundAt = 0;
        for (const std::string& c : captures(spec, path)) {
            if (::stat(c.c_str(), &st) != 0) continue;
            const int64_t at = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 +
                               st.st_mtim.tv_nsec;
            if (found.empty() || at > foundAt || (at == foundAt && c == path)) {
                found = c;
                foundAt = at;
            }
        }
        return found;
    }

    // The predicted name first. A scan is only ever a fallback, because it
    // cannot tell two files apart if a core ever writes more than one.
    if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return path;
    const std::vector<std::string> all = captures(spec, path);
    return all.empty() ? std::string() : all.front();
}

void removeOtherCaptures(const catalog::SaveFile& spec, const std::string& path) {
    for (const std::string& c : captures(spec, path))
        if (c != path) removeFile(c);
}

}  // namespace cab
