#include "xboxhdd.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace cab::xboxhdd {
namespace {

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }
void putBe64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) { p[i] = uint8_t(v); v >>= 8; }
}
uint32_t le32(const uint8_t* p) {
    return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
void putLe32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) { p[i] = uint8_t(v); v >>= 8; }
}
void putLe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }

// --- qcow2 ------------------------------------------------------------------
//
// The layout is qemu's docs/interop/qcow2.txt. A guest offset goes through the
// L1 table to an L2 table to a host cluster; a cluster may be unallocated or
// "all zeros" (reads as zeros), compressed (deflate, read-only here), or
// ordinary. Writing replaces anything that is not an ordinary cluster owned by
// this image alone with a fresh one appended to the file, and counts it in the
// refcount table so qemu never hands the same bytes out twice.
class Qcow2 {
public:
    // Read-only unless asked (#488, EMU-14): listing a drive or reading its
    // saves off must work on a copy nobody may write, and must not write.
    bool open(const std::string& path, bool writable, std::string* err) {
        f_ = std::fopen(path.c_str(), writable ? "r+b" : "rb");
        if (!f_) return fail(err, "cannot open " + path + ": " + std::strerror(errno));
        uint8_t h[112] = {};
        if (!readAt(0, h, sizeof h)) return fail(err, "cannot read the header");
        if (be32(h) != 0x514649fb) return fail(err, "not a qcow2 file");
        const uint32_t version = be32(h + 4);
        if (version != 2 && version != 3) return fail(err, "unsupported qcow2 version");
        if (be64(h + 8) != 0) return fail(err, "has a backing file");
        clusterBits_ = be32(h + 20);
        if (clusterBits_ < 9 || clusterBits_ > 21) return fail(err, "odd cluster size");
        cs_ = uint64_t(1) << clusterBits_;
        size_ = be64(h + 24);
        if (be32(h + 32) != 0) return fail(err, "is encrypted");
        l1Size_ = be32(h + 36);
        l1Off_ = be64(h + 40);
        rtOff_ = be64(h + 48);
        rtClusters_ = be32(h + 56);
        if (be32(h + 60) != 0) return fail(err, "holds snapshots");
        refBits_ = 16;
        if (version == 3) {
            const uint64_t incompatible = be64(h + 72);
            const uint32_t headerLen = be32(h + 100);
            // Bit 3 only says a compression type field is present; it must
            // still be zlib. Anything else (dirty, corrupt, external data,
            // extended L2) is outside what this code can keep consistent.
            if (incompatible & ~uint64_t(1 << 3)) return fail(err, "has features this cannot write");
            if ((incompatible & (1 << 3)) && headerLen > 104 && h[104] != 0)
                return fail(err, "is not zlib-compressed");
            refBits_ = 1u << be32(h + 96);
            if (refBits_ != 16) return fail(err, "has an unusual refcount width");
            // Autoclear bits describe extensions a writer that does not know
            // them must declare stale. None matter to xemu. A reader leaves
            // them: it changes nothing they describe.
            if (writable && be64(h + 88) != 0) {
                uint8_t zero[8] = {};
                writeAt(88, zero, 8);
            }
        }
        l1_.resize(l1Size_);
        std::vector<uint8_t> raw(size_t(l1Size_) * 8);
        if (!raw.empty() && !readAt(l1Off_, raw.data(), raw.size()))
            return fail(err, "cannot read the L1 table");
        for (uint32_t i = 0; i < l1Size_; ++i) l1_[i] = be64(&raw[i * 8]);
        std::fseek(f_, 0, SEEK_END);
        eof_ = (uint64_t(std::ftell(f_)) + cs_ - 1) & ~(cs_ - 1);
        return true;
    }
    ~Qcow2() { if (f_) std::fclose(f_); }
    bool ok() const { return !broken_; }

    bool read(uint64_t off, uint8_t* buf, size_t len) {
        while (len > 0) {
            const uint64_t at = off & (cs_ - 1);
            const size_t n = size_t(std::min<uint64_t>(len, cs_ - at));
            if (!readCluster(off >> clusterBits_, at, buf, n)) return false;
            off += n; buf += n; len -= n;
        }
        return true;
    }
    bool write(uint64_t off, const uint8_t* buf, size_t len) {
        while (len > 0) {
            const uint64_t at = off & (cs_ - 1);
            const size_t n = size_t(std::min<uint64_t>(len, cs_ - at));
            if (!writeCluster(off >> clusterBits_, at, buf, n)) return false;
            off += n; buf += n; len -= n;
        }
        return true;
    }
    bool flush() { return std::fflush(f_) == 0 && !broken_; }

private:
    static bool fail(std::string* err, const std::string& why) {
        if (err) *err = why;
        return false;
    }
    bool readAt(uint64_t off, void* p, size_t n) {
        if (fseeko(f_, off_t(off), SEEK_SET) != 0) return false;
        return std::fread(p, 1, n, f_) == n;
    }
    bool writeAt(uint64_t off, const void* p, size_t n) {
        if (fseeko(f_, off_t(off), SEEK_SET) != 0 || std::fwrite(p, 1, n, f_) != n) {
            broken_ = true;
            return false;
        }
        return true;
    }

    static constexpr uint64_t kOffsetMask = 0x00fffffffffffe00ull;
    static constexpr uint64_t kCopied = 1ull << 63;
    static constexpr uint64_t kCompressed = 1ull << 62;

    std::vector<uint64_t>* l2(uint64_t l2Off) {
        auto it = l2Cache_.find(l2Off);
        if (it != l2Cache_.end()) return &it->second;
        std::vector<uint8_t> raw(cs_);
        if (!readAt(l2Off, raw.data(), raw.size())) return nullptr;
        std::vector<uint64_t> t(cs_ / 8);
        for (size_t i = 0; i < t.size(); ++i) t[i] = be64(&raw[i * 8]);
        return &(l2Cache_[l2Off] = std::move(t));
    }

    // The whole guest cluster's bytes, whatever kind of cluster it is.
    bool clusterBytes(uint64_t entry, std::vector<uint8_t>* out) {
        out->assign(cs_, 0);
        if (entry & kCompressed) {
            const unsigned x = 62 - (clusterBits_ - 8);
            const uint64_t host = entry & ((uint64_t(1) << x) - 1);
            const uint64_t sectors = ((entry >> x) & ((uint64_t(1) << (clusterBits_ - 8)) - 1)) + 1;
            const size_t clen = size_t(sectors * 512 - (host & 511));
            std::vector<uint8_t> comp(clen);
            // The last compressed cluster may end short of its sector count.
            if (fseeko(f_, off_t(host), SEEK_SET) != 0) return false;
            const size_t got = std::fread(comp.data(), 1, clen, f_);
            z_stream z{};
            if (inflateInit2(&z, -12) != Z_OK) return false;
            z.next_in = comp.data();
            z.avail_in = unsigned(got);
            z.next_out = out->data();
            z.avail_out = unsigned(cs_);
            const int r = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            return r == Z_STREAM_END || (r == Z_BUF_ERROR && z.avail_out == 0) || r == Z_OK;
        }
        const uint64_t host = entry & kOffsetMask;
        if (host == 0 || (entry & 1)) return true;   // unallocated, or all zeros
        return readAt(host, out->data(), cs_);
    }

    bool readCluster(uint64_t idx, uint64_t at, uint8_t* buf, size_t n) {
        const uint64_t l2Entries = cs_ / 8;
        const uint64_t i1 = idx / l2Entries;
        if (i1 >= l1_.size() || (l1_[i1] & kOffsetMask) == 0) {
            std::memset(buf, 0, n);
            return true;
        }
        std::vector<uint64_t>* t = l2(l1_[i1] & kOffsetMask);
        if (!t) return false;
        const uint64_t e = (*t)[idx % l2Entries];
        if (!(e & kCompressed) && ((e & kOffsetMask) == 0 || (e & 1))) {
            std::memset(buf, 0, n);
            return true;
        }
        if (!(e & kCompressed)) return readAt((e & kOffsetMask) + at, buf, n);
        std::vector<uint8_t> whole;
        if (!clusterBytes(e, &whole)) return false;
        std::memcpy(buf, whole.data() + at, n);
        return true;
    }

    // A new host cluster at the end of the file, zeroed and counted.
    uint64_t allocate() {
        const uint64_t off = eof_;
        eof_ += cs_;
        std::vector<uint8_t> zero(cs_, 0);
        if (!writeAt(off, zero.data(), zero.size())) return 0;
        if (!setRefcount(off >> clusterBits_)) return 0;
        return off;
    }

    bool setRefcount(uint64_t cluster) {
        const uint64_t perBlock = cs_ * 8 / refBits_;
        const uint64_t ti = cluster / perBlock;
        if (ti >= rtClusters_ * cs_ / 8) { broken_ = true; return false; }
        uint8_t raw[8];
        if (!readAt(rtOff_ + ti * 8, raw, 8)) return false;
        uint64_t block = be64(raw) & ~uint64_t(511);
        if (block == 0) {
            // A refcount block of its own, which then has to be counted too.
            block = eof_;
            eof_ += cs_;
            std::vector<uint8_t> zero(cs_, 0);
            if (!writeAt(block, zero.data(), zero.size())) return false;
            putBe64(raw, block);
            if (!writeAt(rtOff_ + ti * 8, raw, 8)) return false;
            if (!setRefcount(block >> clusterBits_)) return false;
        }
        const uint8_t one[2] = {0, 1};
        return writeAt(block + (cluster % perBlock) * 2, one, 2);
    }

    bool writeCluster(uint64_t idx, uint64_t at, const uint8_t* buf, size_t n) {
        const uint64_t l2Entries = cs_ / 8;
        const uint64_t i1 = idx / l2Entries;
        if (i1 >= l1_.size()) { broken_ = true; return false; }
        if ((l1_[i1] & kOffsetMask) == 0) {
            const uint64_t t = allocate();
            if (!t) return false;
            l1_[i1] = t | kCopied;
            uint8_t raw[8];
            putBe64(raw, l1_[i1]);
            if (!writeAt(l1Off_ + i1 * 8, raw, 8)) return false;
        }
        const uint64_t l2Off = l1_[i1] & kOffsetMask;
        std::vector<uint64_t>* t = l2(l2Off);
        if (!t) return false;
        uint64_t& e = (*t)[idx % l2Entries];
        const bool inPlace = !(e & kCompressed) && (e & kOffsetMask) != 0 && (e & kCopied);
        if (inPlace && !(e & 1)) return writeAt((e & kOffsetMask) + at, buf, n);

        // Anything else becomes a whole new cluster: its old bytes, then ours.
        std::vector<uint8_t> whole;
        if (!clusterBytes(e, &whole)) return false;
        std::memcpy(whole.data() + at, buf, n);
        uint64_t host = inPlace ? (e & kOffsetMask) : allocate();
        if (!host) return false;
        if (!writeAt(host, whole.data(), whole.size())) return false;
        e = host | kCopied;
        uint8_t raw[8];
        putBe64(raw, e);
        return writeAt(l2Off + (idx % l2Entries) * 8, raw, 8);
    }

    std::FILE* f_ = nullptr;
    unsigned clusterBits_ = 16;
    uint64_t cs_ = 65536, size_ = 0;
    uint32_t l1Size_ = 0, rtClusters_ = 0, refBits_ = 16;
    uint64_t l1Off_ = 0, rtOff_ = 0, eof_ = 0;
    std::vector<uint64_t> l1_;
    std::map<uint64_t, std::vector<uint64_t>> l2Cache_;
    bool broken_ = false;
};

// --- FATX -------------------------------------------------------------------
//
// A retail 8 GB drive's E: partition, at its fixed place and size (libfatx
// v0.0.8 fatx_disk.c and X1 BOX's xemu_fatx_import.c agree on both; checked
// against xemu's published drive). The superblock is 4 KiB, the FAT follows
// it, one entry per cluster plus one reserved, rounded up to 4 KiB; cluster 1
// is the first after it. 16-bit entries below 0xfff0 clusters, 32-bit above;
// E: is 32-bit.
constexpr uint64_t kEOffset = 0xABE80000ull;
constexpr uint64_t kESize = 0x1312D6000ull;
constexpr uint8_t kDeleted = 0xE5;
constexpr uint8_t kAttrDir = 0x10;
constexpr size_t kMaxName = 42;

struct Entry {
    std::string name;
    uint8_t attr = 0;
    uint32_t first = 0;
    uint32_t size = 0;
    uint64_t at = 0;   // where its 64 bytes are on the drive
};

class Fatx {
public:
    explicit Fatx(Qcow2& d) : d_(d) {}

    bool open(std::string* err) {
        uint8_t sb[16];
        if (!d_.read(kEOffset, sb, sizeof sb)) return fail(err, "cannot read E:");
        if (std::memcmp(sb, "FATX", 4) != 0) return fail(err, "E: is not formatted");
        const uint32_t spc = le32(sb + 8);
        root_ = le32(sb + 12);
        if (spc == 0 || spc > 1024 || (spc & (spc - 1))) return fail(err, "odd cluster size on E:");
        csz_ = uint64_t(spc) * 512;
        uint64_t entries = kESize / csz_ + 1;
        fat32_ = entries >= 0xfff0;
        fatBytes_ = entries * (fat32_ ? 4 : 2);
        fatBytes_ = (fatBytes_ + 4095) & ~uint64_t(4095);
        dataOff_ = kEOffset + 0x1000 + fatBytes_;
        clusters_ = (kESize - fatBytes_ - 0x1000) / csz_ + 1;
        std::vector<uint8_t> raw(size_t(clusters_) * (fat32_ ? 4 : 2));
        if (!d_.read(kEOffset + 0x1000, raw.data(), raw.size()))
            return fail(err, "cannot read E:'s FAT");
        fat_.resize(clusters_);
        for (uint64_t i = 0; i < clusters_; ++i) {
            uint32_t v = fat32_ ? le32(&raw[i * 4]) : le16(&raw[i * 2]);
            // 16-bit markers read the same as 32-bit ones from here on.
            if (!fat32_ && v >= 0xfff0) v |= 0xffff0000u;
            fat_[i] = v;
        }
        return true;
    }

    // The directory at `path` ("", "UDATA"), or false.
    bool find(const std::string& path, uint32_t* cluster, Entry* self = nullptr) {
        uint32_t c = root_;
        std::istringstream parts(path);
        for (std::string part; std::getline(parts, part, '/');) {
            if (part.empty()) continue;
            std::vector<Entry> list;
            if (!entries(c, &list)) return false;
            bool found = false;
            for (const Entry& e : list)
                if ((e.attr & kAttrDir) && strcasecmp(e.name.c_str(), part.c_str()) == 0) {
                    c = e.first;
                    if (self) *self = e;
                    found = true;
                    break;
                }
            if (!found) return false;
        }
        *cluster = c;
        return true;
    }

    bool entries(uint32_t dir, std::vector<Entry>* out) {
        out->clear();
        std::vector<uint8_t> buf(csz_);
        for (uint32_t c : chain(dir)) {
            const uint64_t base = offsetOf(c);
            if (!d_.read(base, buf.data(), buf.size())) return false;
            for (size_t i = 0; i + 64 <= csz_; i += 64) {
                const uint8_t nl = buf[i];
                if (nl == 0x00 || nl == 0xFF) return true;
                if (nl == kDeleted || nl > kMaxName) continue;
                Entry e;
                e.name.assign(reinterpret_cast<char*>(&buf[i + 2]), nl);
                e.attr = buf[i + 1];
                e.first = le32(&buf[i + 44]);
                e.size = le32(&buf[i + 48]);
                e.at = base + i;
                out->push_back(e);
            }
        }
        return true;
    }

    bool readFile(const Entry& e, std::vector<uint8_t>* out) {
        out->assign(e.size, 0);
        uint64_t done = 0;
        for (uint32_t c : chain(e.first)) {
            if (done >= e.size) break;
            const size_t n = size_t(std::min<uint64_t>(csz_, e.size - done));
            if (!d_.read(offsetOf(c), out->data() + done, n)) return false;
            done += n;
        }
        return done >= e.size;
    }

    // Everything inside `dir`, recursively, then its entry in the parent.
    bool removeTree(const Entry& e) {
        if (e.attr & kAttrDir) {
            std::vector<Entry> kids;
            if (!entries(e.first, &kids)) return false;
            for (const Entry& k : kids)
                if (!removeTree(k)) return false;
        }
        freeChain(e.first);
        const uint8_t mark = kDeleted;
        return d_.write(e.at, &mark, 1);
    }

    // A new entry in directory `dir`. A directory gets one cluster of 0xFF,
    // which reads as empty; a file gets its bytes. False when E: is full.
    bool add(uint32_t dir, const std::string& name, bool isDir, const std::vector<uint8_t>& data,
             time_t when, uint32_t* made) {
        if (name.empty() || name.size() > kMaxName) return false;
        uint32_t first = 0;
        const uint64_t need = isDir ? 1 : (data.size() + csz_ - 1) / csz_;
        std::vector<uint32_t> got;
        for (uint64_t i = 0; i < need; ++i) {
            const uint32_t c = alloc();
            if (!c) return false;
            if (!got.empty()) {
                fat_[got.back()] = c;
                dirty(got.back());
            }
            got.push_back(c);
        }
        if (!got.empty()) first = got.front();
        if (isDir) {
            std::vector<uint8_t> ff(csz_, 0xFF);
            if (!d_.write(offsetOf(first), ff.data(), ff.size())) return false;
        } else {
            for (size_t i = 0; i < got.size(); ++i) {
                std::vector<uint8_t> chunk(csz_, 0);
                const size_t from = i * csz_;
                std::memcpy(chunk.data(), data.data() + from,
                            std::min<size_t>(csz_, data.size() - from));
                if (!d_.write(offsetOf(got[i]), chunk.data(), chunk.size())) return false;
            }
        }
        uint64_t slot = 0;
        bool wasEnd = false;
        if (!freeSlot(dir, &slot, &wasEnd)) return false;
        uint8_t raw[64];
        std::memset(raw, 0xFF, sizeof raw);
        raw[0] = uint8_t(name.size());
        raw[1] = isDir ? kAttrDir : 0;
        std::memcpy(raw + 2, name.data(), name.size());
        putLe32(raw + 44, first);
        putLe32(raw + 48, isDir ? 0 : uint32_t(data.size()));
        // Packed as FAT packs a time: five bits of hour, six of minutes.
        // libfatx v0.0.8's FATX_TIME keeps one bit fewer of each, which put a
        // save 16 hours or 32 minutes back (#728). IN UTC, because the Xbox's
        // own clock is: xemu starts it from the PC's time in UTC, so what a
        // game writes is stamped in UTC, and a save put back in the player's
        // zone sorted wrongly among them.
        struct tm t{};
        gmtime_r(&when, &t);
        const uint16_t tm = uint16_t(((t.tm_hour & 0x1f) << 11) | ((t.tm_min & 0x3f) << 5) |
                                     ((t.tm_sec / 2) & 0x1f));
        const uint16_t dt = uint16_t((t.tm_mday & 0x1f) | (((t.tm_mon + 1) & 0xf) << 5) |
                                     (((t.tm_year + 1900 - 2000) & 0x7f) << 9));
        for (int i = 0; i < 3; ++i) {
            putLe16(raw + 52 + i * 4, tm);
            putLe16(raw + 54 + i * 4, dt);
        }
        if (!d_.write(slot, raw, sizeof raw)) return false;
        // The slot after one that ended the directory ends it now, if it is
        // in the same cluster (a cluster boundary is the chain's to end).
        if (wasEnd && ((slot + 64 - dataOff_) % csz_) != 0) {
            const uint8_t end = 0xFF;
            if (!d_.write(slot + 64, &end, 1)) return false;
        }
        if (made) *made = first;
        return true;
    }

    bool flush() {
        for (uint64_t page : dirty_) {
            const size_t width = fat32_ ? 4 : 2;
            const uint64_t firstEntry = page * 4096 / width;
            std::vector<uint8_t> raw(4096, 0);
            for (uint64_t i = 0; i < 4096 / width && firstEntry + i < clusters_; ++i) {
                const uint32_t v = fat_[firstEntry + i];
                if (fat32_) putLe32(&raw[i * 4], v);
                else putLe16(&raw[i * 2], uint16_t(v));
            }
            const uint64_t bytes = std::min<uint64_t>(4096, clusters_ * width - page * 4096);
            if (!d_.write(kEOffset + 0x1000 + page * 4096, raw.data(), size_t(bytes))) return false;
        }
        dirty_.clear();
        return true;
    }

private:
    static bool fail(std::string* err, const std::string& why) {
        if (err) *err = why;
        return false;
    }
    uint64_t offsetOf(uint32_t c) const { return dataOff_ + uint64_t(c - 1) * csz_; }
    bool isEnd(uint32_t v) const { return v >= 0xfffffff0u; }
    std::vector<uint32_t> chain(uint32_t c) const {
        std::vector<uint32_t> out;
        while (c >= 1 && c < clusters_ && !isEnd(c) && out.size() < clusters_) {
            out.push_back(c);
            const uint32_t next = fat_[c];
            if (next == 0 || isEnd(next)) break;
            c = next;
        }
        return out;
    }
    void dirty(uint32_t c) { dirty_.insert(uint64_t(c) * (fat32_ ? 4 : 2) / 4096); }
    void freeChain(uint32_t c) {
        for (uint32_t x : chain(c)) { fat_[x] = 0; dirty(x); }
    }
    uint32_t alloc() {
        for (uint64_t i = 0; i < clusters_ - 2; ++i) {
            const uint32_t c = uint32_t(2 + (next_ + i) % (clusters_ - 2));
            if (fat_[c] == 0) {
                fat_[c] = 0xffffffffu;
                dirty(c);
                next_ = c - 1;
                return c;
            }
        }
        return 0;
    }
    // A deleted or end slot in `dir`, growing it by a cluster when full.
    bool freeSlot(uint32_t dir, uint64_t* slot, bool* wasEnd) {
        std::vector<uint8_t> buf(csz_);
        const std::vector<uint32_t> cs = chain(dir);
        for (uint32_t c : cs) {
            if (!d_.read(offsetOf(c), buf.data(), buf.size())) return false;
            for (size_t i = 0; i + 64 <= csz_; i += 64) {
                const uint8_t nl = buf[i];
                if (nl == kDeleted || nl == 0x00 || nl == 0xFF) {
                    *slot = offsetOf(c) + i;
                    *wasEnd = nl != kDeleted;
                    return true;
                }
            }
        }
        if (cs.empty()) return false;
        const uint32_t grow = alloc();
        if (!grow) return false;
        fat_[cs.back()] = grow;
        dirty(cs.back());
        std::vector<uint8_t> ff(csz_, 0xFF);
        if (!d_.write(offsetOf(grow), ff.data(), ff.size())) return false;
        *slot = offsetOf(grow);
        *wasEnd = true;
        return true;
    }

    Qcow2& d_;
    uint64_t csz_ = 16384, fatBytes_ = 0, dataOff_ = 0, clusters_ = 0;
    uint32_t root_ = 1;
    uint32_t next_ = 0;
    bool fat32_ = true;
    std::vector<uint32_t> fat_;
    std::set<uint64_t> dirty_;
};

// --- The host side ------------------------------------------------------------

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back(n);
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}
bool readHost(const std::string& p, std::vector<uint8_t>* out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), {});
    return true;
}
void removeHost(const std::string& p) {
    if (isDir(p)) {
        for (const std::string& n : listDir(p)) removeHost(p + "/" + n);
        ::rmdir(p.c_str());
    } else {
        ::unlink(p.c_str());
    }
}

bool copyIn(Fatx& fs, uint32_t dir, const std::string& host, int* files, std::string* err) {
    for (const std::string& n : listDir(host)) {
        const std::string p = host + "/" + n;
        struct stat st;
        if (::stat(p.c_str(), &st) != 0) continue;
        if (n.size() > kMaxName) {
            std::fprintf(stderr, "[xbox] %s: name too long for the Xbox; left off\n", p.c_str());
            continue;
        }
        uint32_t made = 0;
        if (S_ISDIR(st.st_mode)) {
            if (!fs.add(dir, n, true, {}, st.st_mtime, &made) ||
                !copyIn(fs, made, p, files, err)) {
                if (err->empty()) *err = "E: is full";
                return false;
            }
        } else if (S_ISREG(st.st_mode)) {
            std::vector<uint8_t> data;
            if (!readHost(p, &data) || !fs.add(dir, n, false, data, st.st_mtime, &made)) {
                *err = "could not put " + p + " on E:";
                return false;
            }
            ++*files;
        }
    }
    return true;
}

// Brings `host` into line with the drive's `dir`, rewriting only what
// differs, so a file the game did not touch keeps its time and the save sync
// (saves.cpp, syncDirSave) sees no change in it.
bool copyOut(Fatx& fs, uint32_t dir, const std::string& host, int* changed, std::string* err) {
    ::mkdir(host.c_str(), 0755);
    std::vector<Entry> list;
    if (!fs.entries(dir, &list)) { *err = "cannot read a folder on E:"; return false; }
    std::set<std::string> keep;
    for (const Entry& e : list) {
        const std::string p = host + "/" + e.name;
        keep.insert(e.name);
        if (e.attr & kAttrDir) {
            if (!isDir(p)) removeHost(p);
            if (!copyOut(fs, e.first, p, changed, err)) return false;
            continue;
        }
        std::vector<uint8_t> data, old;
        if (!fs.readFile(e, &data)) { *err = "cannot read " + e.name + " on E:"; return false; }
        if (isDir(p)) removeHost(p);
        if (readHost(p, &old) && old == data) continue;
        const std::string tmp = p + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
            if (!out) { *err = "cannot write " + p; return false; }
        }
        if (std::rename(tmp.c_str(), p.c_str()) != 0) { *err = "cannot write " + p; return false; }
        ++*changed;
    }
    for (const std::string& n : listDir(host))
        if (!keep.count(n)) { removeHost(host + "/" + n); ++*changed; }
    return true;
}

constexpr const char* kFolders[] = {"UDATA", "TDATA"};

}  // namespace

bool writeSaves(const std::string& drive, const std::string& root, std::string* err) {
    Qcow2 q;
    if (!q.open(drive, /*writable=*/true, err)) return false;
    Fatx fs(q);
    if (!fs.open(err)) return false;
    uint32_t top = 0;
    fs.find("", &top);
    int files = 0;
    for (const char* folder : kFolders) {
        std::vector<Entry> list;
        if (!fs.entries(top, &list)) { *err = "cannot read E:"; return false; }
        for (const Entry& e : list)
            if (strcasecmp(e.name.c_str(), folder) == 0 && !fs.removeTree(e)) {
                *err = std::string("cannot clear E:\\") + folder;
                return false;
            }
        uint32_t made = 0;
        if (!fs.add(top, folder, true, {}, std::time(nullptr), &made)) {
            *err = std::string("cannot make E:\\") + folder;
            return false;
        }
        const std::string host = root + "/" + folder;
        if (isDir(host) && !copyIn(fs, made, host, &files, err)) return false;
    }
    if (!fs.flush() || !q.flush()) {
        if (err->empty()) *err = "could not finish writing the drive";
        return false;
    }
    std::fprintf(stderr, "[xbox] %d save file(s) onto %s\n", files, drive.c_str());
    return true;
}

bool readSaves(const std::string& drive, const std::string& root, std::string* err) {
    Qcow2 q;
    if (!q.open(drive, /*writable=*/false, err)) return false;
    Fatx fs(q);
    if (!fs.open(err)) return false;
    ::mkdir(root.c_str(), 0755);
    int changed = 0;
    for (const char* folder : kFolders) {
        uint32_t c = 0;
        const std::string host = root + "/" + folder;
        if (!fs.find(folder, &c)) {
            if (isDir(host)) { removeHost(host); ++changed; }
            continue;
        }
        if (!copyOut(fs, c, host, &changed, err)) return false;
    }
    std::fprintf(stderr, "[xbox] %d save file(s) changed coming off %s\n", changed,
                 drive.c_str());
    return true;
}

bool list(const std::string& drive, std::string* out, std::string* err) {
    Qcow2 q;
    if (!q.open(drive, /*writable=*/false, err)) return false;
    Fatx fs(q);
    if (!fs.open(err)) return false;
    std::ostringstream s;
    for (const char* folder : kFolders) {
        uint32_t c = 0;
        if (!fs.find(folder, &c)) continue;
        std::vector<std::pair<uint32_t, std::string>> todo{{c, folder}};
        while (!todo.empty()) {
            auto [dir, path] = todo.back();
            todo.pop_back();
            std::vector<Entry> list;
            if (!fs.entries(dir, &list)) { *err = "cannot read " + path; return false; }
            for (const Entry& e : list) {
                if (e.attr & kAttrDir) todo.push_back({e.first, path + "/" + e.name});
                else s << path << "/" << e.name << " " << e.size << "\n";
            }
        }
    }
    *out = s.str();
    return true;
}

}  // namespace cab::xboxhdd
