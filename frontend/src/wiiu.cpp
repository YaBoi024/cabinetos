#include "wiiu.h"

#include "codecache.h"
#include "gametdb.h"
#include "romfile.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace wiiu {
namespace {

// Wii U's controller list and the codes read off its files (gametdb.h,
// codecache.h; wii.cpp has its own of each).
cab::GameTdb gControls("CABINETOS_WIIU_CONTROLS", "/usr/share/cabinetos/wiiu-controls.txt", "wiiu",
                       "every Wii U game needs a GamePad");
cab::CodeCache gCodes("wiiu-codes.txt");

uint64_t be(const uint8_t* p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

// ZArchive's layout (zarchivecommon.h at Exzap/ZArchive): everything big-endian.
constexpr size_t kFooterSize = 144;   // six (offset, size) pairs, a hash, size, version, magic
constexpr uint32_t kMagic = 0x169f52d6;
constexpr uint32_t kVersion1 = 0x61bf3a01;
constexpr uint64_t kBlock = 64 * 1024;
constexpr size_t kRecordSize = 40;    // a 64-bit base offset and sixteen 16-bit sizes
constexpr size_t kEntrySize = 16;
// The table of contents is small (Hyrule Warriors, 1,580 files across three
// titles: 377 KB); anything far bigger is not one.
constexpr uint64_t kMaxTable = 16 * 1024 * 1024;

struct Entry {
    uint32_t nameAndType;   // the top bit set for a file; the rest the name's offset
    uint32_t a, b, c;       // a file: offset low, size low, both highs; a folder: first, count
    bool isFile() const { return (nameAndType & 0x80000000u) != 0; }
};

std::string nameAt(const std::vector<uint8_t>& names, uint32_t off) {
    if (off == 0x7FFFFFFF || off >= names.size()) return {};
    size_t len = names[off] & 0x7F;
    size_t at = off + 1;
    if (names[off] & 0x80) {
        if (at >= names.size()) return {};
        len |= static_cast<size_t>(names[at]) << 7;
        ++at;
    }
    if (at + len > names.size()) return {};
    return std::string(reinterpret_cast<const char*>(names.data() + at), len);
}

std::string lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

}  // namespace

std::string codeFromMeta(const std::string& xml) {
    const size_t tag = xml.find("<product_code");
    if (tag == std::string::npos) return {};
    const size_t open = xml.find('>', tag);
    const size_t close = xml.find('<', open);
    if (open == std::string::npos || close == std::string::npos) return {};
    const std::string value = xml.substr(open + 1, close - open - 1);
    // WUP-P-BWPE, WUP-N-WKNE: the last four are the code GameTDB keys on.
    if (value.size() < 4) return {};
    std::string code = value.substr(value.size() - 4);
    for (const char ch : code)
        if (!std::isalnum(static_cast<unsigned char>(ch))) return {};
    return code;
}

std::string codeFromWua(uint64_t size, const ReadFn& read) {
    if (size <= kFooterSize) return {};
    const std::vector<uint8_t> f = read(size - kFooterSize, kFooterSize);
    if (f.size() != kFooterSize) return {};
    if (be(&f[140], 4) != kMagic || be(&f[136], 4) != kVersion1 || be(&f[128], 8) != size)
        return {};
    auto section = [&](int i, uint64_t* off, uint64_t* len) {
        *off = be(&f[i * 16], 8);
        *len = be(&f[i * 16 + 8], 8);
        return *off + *len <= size;
    };
    uint64_t dataOff, dataLen, recOff, recLen, namesOff, namesLen, treeOff, treeLen;
    if (!section(0, &dataOff, &dataLen) || !section(1, &recOff, &recLen) ||
        !section(2, &namesOff, &namesLen) || !section(3, &treeOff, &treeLen) ||
        namesLen > kMaxTable || treeLen > kMaxTable || treeLen < kEntrySize)
        return {};
    const std::vector<uint8_t> names = read(namesOff, namesLen);
    const std::vector<uint8_t> tree = read(treeOff, treeLen);
    if (names.size() != namesLen || tree.size() != treeLen) return {};
    std::vector<Entry> entries(treeLen / kEntrySize);
    for (size_t i = 0; i < entries.size(); ++i) {
        const uint8_t* p = &tree[i * kEntrySize];
        entries[i] = {static_cast<uint32_t>(be(p, 4)), static_cast<uint32_t>(be(p + 4, 4)),
                      static_cast<uint32_t>(be(p + 8, 4)), static_cast<uint32_t>(be(p + 12, 4))};
    }
    // The child of folder `dir` called `want` (any case), or -1.
    auto child = [&](size_t dir, const std::string& want) -> long {
        const Entry& d = entries[dir];
        if (d.isFile()) return -1;
        for (uint64_t k = d.a; k < uint64_t(d.a) + d.b && k < entries.size(); ++k)
            if (lower(nameAt(names, entries[k].nameAndType & 0x7FFFFFFF)) == want)
                return static_cast<long>(k);
        return -1;
    };
    // THE GAME'S OWN FOLDER: 00050000 is a game, 00050002 a demo. Not the
    // update's or the DLC's, whose meta.xml can name them differently.
    long meta = -1;
    const Entry& root = entries[0];
    for (uint64_t k = root.a; k < uint64_t(root.a) + root.b && k < entries.size() && meta < 0; ++k) {
        const std::string top = lower(nameAt(names, entries[k].nameAndType & 0x7FFFFFFF));
        if (top.compare(0, 8, "00050000") != 0 && top.compare(0, 8, "00050002") != 0) continue;
        const long metaDir = child(k, "meta");
        if (metaDir >= 0) meta = child(static_cast<size_t>(metaDir), "meta.xml");
    }
    if (meta < 0 || !entries[meta].isFile()) return {};
    const Entry& m = entries[meta];
    const uint64_t off = m.a | (uint64_t(m.c & 0xFFFF) << 32);
    const uint64_t len = m.b | (uint64_t(m.c & 0xFFFF0000u) << 16);
    if (len == 0 || len > kMaxTable) return {};

    // ITS BLOCKS. Every sixteenth block's compressed position is stored whole;
    // the ones between are the sizes before them added on. A block whose size
    // is the whole 64 KiB was stored as it is.
    std::vector<uint8_t> plain;
    for (uint64_t blk = off / kBlock; blk <= (off + len - 1) / kBlock; ++blk) {
        const std::vector<uint8_t> rec = read(recOff + (blk / 16) * kRecordSize, kRecordSize);
        if (rec.size() != kRecordSize || (blk / 16 + 1) * kRecordSize > recLen) return {};
        uint64_t pos = be(rec.data(), 8);
        for (uint64_t i = 0; i < blk % 16; ++i) pos += be(&rec[8 + i * 2], 2) + 1;
        const uint64_t csize = be(&rec[8 + (blk % 16) * 2], 2) + 1;
        if (pos + csize > dataLen) return {};
        std::vector<uint8_t> raw = read(dataOff + pos, csize);
        if (raw.size() != csize) return {};
        if (csize != kBlock) raw = romfile::unzstd(raw);
        if (raw.size() != kBlock && blk != (off + len - 1) / kBlock) return {};
        plain.insert(plain.end(), raw.begin(), raw.end());
    }
    const uint64_t start = off % kBlock;
    if (start + len > plain.size()) return {};
    return codeFromMeta(std::string(reinterpret_cast<const char*>(plain.data() + start), len));
}

std::string rememberedCode(int romId, bool* known) { return gCodes.remembered(romId, known); }

void rememberCode(int romId, const std::string& code) { gCodes.remember(romId, code); }

std::string controls(const std::string& code) { return gControls.controls(code); }

Needs needs(const std::string& code) {
    const std::string c = controls(code);
    if (c.find('p') != std::string::npos || c.find('c') != std::string::npos) return Needs::Nothing;
    if (c.find('w') != std::string::npos) return Needs::WiiRemote;
    return Needs::GamePad;
}

}  // namespace wiiu
