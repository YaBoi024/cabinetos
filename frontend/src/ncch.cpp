#include "ncch.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "romfile.h"

namespace ncch {
namespace {

// Offsets from Azahar's own structures at the pinned 2126.2
// (core/file_sys/ncch_container.h, common/file_derived.h, core/loader).
constexpr uint64_t kMediaUnit = 0x200;        // NCSD partition offsets count these
constexpr size_t kMagicAt = 0x100;            // "NCSD" or "NCCH", after the signature
constexpr size_t kPartition0At = 0x120;       // NCSD: partition 0's offset, in media units
constexpr size_t kCryptoFlagsAt = 0x18F;      // NCCH: flags[7]
constexpr uint8_t kNoCrypto = 0x04;           // its bit 2, NoCrypto
constexpr uint32_t kCiaHeaderSize = 0x2020;   // a CIA's first word
constexpr uint32_t kCiaCertChainSize = 0xA00; // and its third
// A compressed game's first frame is 256 KiB (Z3DSWriteIOFile::DEFAULT_FRAME_SIZE)
// and a cartridge's first partition starts 16 KiB in; a megabyte of either
// side reaches it with room to spare and costs nothing to read.
constexpr size_t kCompressedRead = 1 << 20;

enum class Kind { Playable, Encrypted, Cia, Unknown };

using Reader = std::function<std::vector<uint8_t>(uint64_t at, size_t n)>;

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) | static_cast<uint32_t>(b[at + 1]) << 8 |
           static_cast<uint32_t>(b[at + 2]) << 16 | static_cast<uint32_t>(b[at + 3]) << 24;
}

bool magic(const std::vector<uint8_t>& b, size_t at, const char* m, size_t n = 4) {
    return b.size() >= at + n && std::memcmp(b.data() + at, m, n) == 0;
}

Kind fromNcch(const std::vector<uint8_t>& h) {
    if (!magic(h, kMagicAt, "NCCH") || h.size() <= kCryptoFlagsAt) return Kind::Unknown;
    return (h[kCryptoFlagsAt] & kNoCrypto) ? Kind::Playable : Kind::Encrypted;
}

// What the bytes `read` gives are, without the Z3DS wrapper.
Kind classify(const Reader& read) {
    const std::vector<uint8_t> head = read(0, 0x200);
    if (magic(head, kMagicAt, "NCSD") && head.size() >= kPartition0At + 4)
        return fromNcch(read(le32(head, kPartition0At) * kMediaUnit, 0x200));
    if (magic(head, kMagicAt, "NCCH")) return fromNcch(head);
    // Homebrew, which is never encrypted.
    if (magic(head, 0, "3DSX") || magic(head, 0, "\x7f" "ELF")) return Kind::Playable;
    if (head.size() >= 12 && le32(head, 0) == kCiaHeaderSize &&
        le32(head, 8) == kCiaCertChainSize)
        return Kind::Cia;
    return Kind::Unknown;
}

}  // namespace

bool namesCia(const std::string& fileName) {
    std::string n = fileName;
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* end : {".cia", ".zcia", ".cia.7z", ".cia.zip", ".cia.rar", ".cia.zst"}) {
        const size_t k = std::strlen(end);
        if (n.size() >= k && n.compare(n.size() - k, k, end) == 0) return true;
    }
    return false;
}

std::string refusal(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    const Reader file = [fd](uint64_t at, size_t n) {
        std::vector<uint8_t> b(n);
        const ssize_t got = ::pread(fd, b.data(), n, static_cast<off_t>(at));
        b.resize(got > 0 ? static_cast<size_t>(got) : 0);
        return b;
    };

    Kind kind = Kind::Unknown;
    const std::vector<uint8_t> head = file(0, 0x20);
    if (magic(head, 0, "Z3DS") && head.size() >= 0x10) {
        // Azahar's compressed form: a 0x20-byte header naming what it holds,
        // its metadata, then zstd frames of the file as it was.
        if (magic(head, 4, "CIA\0", 4)) {
            kind = Kind::Cia;
        } else if (magic(head, 4, "3DSX")) {
            kind = Kind::Playable;
        } else {
            const uint64_t start = (static_cast<uint64_t>(head[0x0A]) | head[0x0B] << 8) +
                                   le32(head, 0x0C);
            const std::vector<uint8_t> inner =
                romfile::unzstd(file(start, kCompressedRead), kCompressedRead);
            kind = classify([&inner](uint64_t at, size_t n) {
                if (at >= inner.size()) return std::vector<uint8_t>{};
                const size_t end = std::min(inner.size(), static_cast<size_t>(at) + n);
                return std::vector<uint8_t>(inner.begin() + static_cast<long>(at),
                                            inner.begin() + static_cast<long>(end));
            });
        }
    } else {
        kind = classify(file);
    }
    ::close(fd);

    switch (kind) {
        case Kind::Encrypted:
            std::fprintf(stderr, "[3ds] %s is an encrypted dump; Azahar plays decrypted "
                                 "games only, so it is refused before the core\n",
                         path.c_str());
            return "Encrypted 3DS games can't be played";
        case Kind::Cia:
            std::fprintf(stderr, "[3ds] %s is a CIA, which has to be installed; Azahar "
                                 "installs nothing, so it is refused before the core\n",
                         path.c_str());
            return "3DS CIA files can't be played";
        case Kind::Unknown:
            std::fprintf(stderr, "[3ds] %s: no 3DS header read; the core decides\n",
                         path.c_str());
            return {};
        case Kind::Playable:
            return {};
    }
    return {};
}

}  // namespace ncch
