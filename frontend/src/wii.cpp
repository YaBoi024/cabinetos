#include "wii.h"

#include "codecache.h"
#include "gametdb.h"
#include "prefs.h"

#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace wii {
namespace {

// Wii's controller list and the codes read off its files, and GameCube's
// codes too (gametdb.h, codecache.h; wiiu.cpp has its own of each). RomM
// numbers every game on a server once, so one file by rom id holds both.
cab::GameTdb gControls("CABINETOS_WII_CONTROLS", "/usr/share/cabinetos/wii-controls.txt", "wii",
                       "every Wii game needs a Remote");
cab::CodeCache gCodes("wii-codes.txt");

uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

std::string fourLetters(const unsigned char* p) {
    std::string code;
    for (int i = 0; i < 4; ++i) {
        if (!std::isalnum(p[i])) return {};
        code.push_back(static_cast<char>(p[i]));
    }
    return code;
}

// Where a container keeps the disc's own header: a plain disc at 0, RVZ and
// WIA at 0x58 (their copy of it), WBFS one sector in, CISO after its 0x8000
// byte map. GCZ is compressed and has none to read.
uint64_t discHeaderAt(const unsigned char* b) {
    if (std::memcmp(b, "RVZ\x01", 4) == 0 || std::memcmp(b, "WIA\x01", 4) == 0) return 0x58;
    if (std::memcmp(b, "WBFS", 4) == 0 && b[8] < 32) return uint64_t(1) << b[8];
    if (std::memcmp(b, "CISO", 4) == 0) return 0x8000;
    return 0;
}

}  // namespace

std::string codeFromHeader(const unsigned char* b, size_t n) {
    if (n < 0x20) return {};
    // A WAD: a 0x20-byte header, then the certificates, the ticket and the
    // TMD, each aligned to 64 bytes. The title ID's last four bytes are the
    // code (`00010001` + `WALE`). The same reading RomM's own does.
    const uint32_t wadType = (uint32_t(b[4]) << 8) | b[5];
    if (be32(b) == 0x20 && (wadType == 0x4973 || wadType == 0x6962 || wadType == 0x426B)) {
        auto align = [](uint64_t v) { return (v + 63) & ~uint64_t(63); };
        const uint64_t ticket = align(0x20) + align(be32(b + 8));
        const uint32_t ticketSize = be32(b + 0x10);
        const uint64_t at = ticketSize > 0 ? ticket + 0x1DC
                                           : align(ticket + ticketSize) + 0x18C;
        return at + 8 <= n ? fourLetters(b + at + 4) : std::string();
    }
    const uint64_t disc = discHeaderAt(b);
    if (disc + 0x20 > n || be32(b + disc + 0x18) != 0x5D1C9EA3u) return {};
    return fourLetters(b + disc);
}

std::string gameCubeCodeFromHeader(const unsigned char* b, size_t n) {
    if (n < 0x20) return {};
    const uint64_t disc = discHeaderAt(b);
    if (disc + 0x20 > n || be32(b + disc + 0x1C) != 0xC2339F3Du) return {};
    return fourLetters(b + disc);
}

std::string rememberedCode(int romId, bool* known) { return gCodes.remembered(romId, known); }

void rememberCode(int romId, const std::string& code) { gCodes.remember(romId, code); }

std::string titleIdOf(const std::string& code) {
    static const char* kHex = "0123456789ABCDEF";
    std::string hex;
    for (unsigned char c : code) { hex.push_back(kHex[c >> 4]); hex.push_back(kHex[c & 15]); }
    return hex;
}

std::string codeFromTitleId(const std::string& hex) {
    if (hex.size() != 8 && hex.size() != 16) return {};
    std::string code;
    for (size_t i = hex.size() - 8; i < hex.size(); i += 2) {
        const long v = std::strtol(hex.substr(i, 2).c_str(), nullptr, 16);
        if (v < 0x20 || v > 0x7E) return {};
        code.push_back(static_cast<char>(v));
    }
    return code;
}

std::string controls(const std::string& code) { return gControls.controls(code); }

unsigned padDevice(const std::string& code) {
    const std::string c = controls(code);
    if (c.find('c') != std::string::npos) return kClassicController;
    if (c.find('g') != std::string::npos) return kGameCubePad;
    return 0;
}

bool needsMotionPlus(const std::string& code) { return controls(code).find('M') != std::string::npos; }

// By its four letters, so a code read off the file (eight hex digits, when
// RomM had none) is the same answer as RomM's sixteen.
bool isMiiChannel(const std::string& titleId) {
    return codeFromTitleId(titleId) == codeFromTitleId(kMiiChannelTitleId);
}

// SYSCONF (WiiBrew, "/shared2/sys/SYSCONF"): "SCv0", a big-endian count, then
// count + 1 offsets; each entry is a byte holding its type (top three bits) and
// name length less one, the name, then its data. `BT.MOT` is a byte (type 3).
int readMotor(const std::string& sysconfPath) {
    std::ifstream in(sysconfPath, std::ios::binary);
    std::string b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto u8 = [&](size_t at) { return static_cast<unsigned char>(b[at]); };
    if (b.size() < 6 || b.compare(0, 4, "SCv0") != 0) return -1;
    const size_t count = (size_t(u8(4)) << 8) | u8(5);
    for (size_t i = 0; i < count && 8 + 2 * i <= b.size(); ++i) {
        const size_t at = (size_t(u8(6 + 2 * i)) << 8) | u8(7 + 2 * i);
        if (at >= b.size()) continue;
        const unsigned type = u8(at) >> 5;
        const size_t nameLen = (u8(at) & 0x1f) + 1;
        if (at + 1 + nameLen >= b.size()) continue;
        if (type == 3 && b.compare(at + 1, nameLen, "BT.MOT") == 0 && nameLen == 6)
            return u8(at + 1 + nameLen) ? 1 : 0;
    }
    return -1;
}

namespace {
bool rumbleOn() { return prefs::get("wii_rumble", "on") != "off"; }
// The core's word for the switch.
const char* rumbleWord(bool on) { return on ? "enabled" : "disabled"; }
}  // namespace

std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug) {
    return coreOptions(core, platformSlug, rumbleOn());
}

std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, bool on) {
    if (platformSlug != "wii" || core != "dolphin") return {};
    return {{"dolphin_enable_rumble", rumbleWord(on)}};
}

void keepRumbleFrom(const std::string& sysconfPath) {
    const int motor = readMotor(sysconfPath);
    if (motor < 0) {
        std::fprintf(stderr, "[wii] no BT.MOT in %s; rumble stays %s\n", sysconfPath.c_str(),
                     rumbleWord(rumbleOn()));
        return;
    }
    const std::string was = prefs::get("wii_rumble", "on");
    const std::string now = motor ? "on" : "off";
    if (now != was) prefs::set("wii_rumble", now);
    std::fprintf(stderr, "[wii] rumble %s in the game's HOME Menu settings (was %s)\n",
                 now.c_str(), was.c_str());
}

}  // namespace wii
