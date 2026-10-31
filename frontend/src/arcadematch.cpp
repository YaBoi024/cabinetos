// Which emulator plays an arcade game, from the files in its zip; the rule is
// in arcadematch.h.

#include "arcadematch.h"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

#include "catalog.h"
#include "standalone.h"

namespace arcadematch {

namespace {

// One set in one emulator's catalogue (cores/arcade-sets.sh's line).
struct Entry {
    bool works = true;
    char flag = '-';    // g a light gun, x made for anything but a controller
    int buttons = 0;    // player 1's, where the catalogue says (b<n>)
    std::vector<std::pair<uint32_t, int64_t>> files;   // CRC32 and size; size 0 = any
    std::vector<Disc> discs;                           // sha1 empty = by name only
};

struct Catalogue {
    std::unordered_map<std::string, Entry> sets;
    std::set<std::string> bios;   // Flycast's !bios line, in lower case
};

// THE ORDER, and it is the decision (arcadematch.h).
// sm2-emu and Supermodel are programs of their own (standalone.h), named as
// their catalog rows name them.
constexpr const char* kOrder[] = {"flycast",    "fbneo_libretro", "mame2003_plus", "sm2emu",
                                  "supermodel", "mame_libretro"};
constexpr size_t kCores = sizeof kOrder / sizeof kOrder[0];

std::string gDir = "cores/build";
std::once_flag gLoaded;
Catalogue gCat[kCores];

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The set's name and whether the file is a 7z, from `sf2.zip`.
void nameOf(const std::string& fileName, std::string* set, std::string* ext) {
    std::string base = fileName;
    if (const size_t slash = base.find_last_of('/'); slash != std::string::npos)
        base.erase(0, slash + 1);
    const size_t dot = base.find_last_of('.');
    *set = lower(dot == std::string::npos ? base : base.substr(0, dot));
    *ext = dot == std::string::npos ? std::string() : lower(base.substr(dot + 1));
}

Catalogue parse(const std::string& text) {
    Catalogue c;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> words;
        for (size_t i = 0; i < line.size();) {
            const size_t sp = line.find(' ', i);
            const size_t stop = sp == std::string::npos ? line.size() : sp;
            if (stop > i) words.push_back(line.substr(i, stop - i));
            i = stop + 1;
        }
        if (words.empty()) continue;
        if (words[0] == "!bios") {
            for (size_t i = 1; i < words.size(); ++i) c.bios.insert(lower(words[i]));
            continue;
        }
        if (words.size() < 3) continue;
        Entry e;
        e.works = words[1] == "w";
        e.flag = words[2][0];
        for (size_t i = 3; i < words.size(); ++i) {
            const std::string& w = words[i];
            const size_t colon = w.find(':');
            if (colon == std::string::npos) {
                if (w.size() > 1 && w[0] == 'b') e.buttons = std::atoi(w.c_str() + 1);
                continue;
            }
            if (w[0] == '@') {
                // A space in a disc's name is written %20, and % as %25.
                std::string disc;
                for (size_t k = 1; k < colon; ++k) {
                    if (w[k] == '%' && k + 2 < colon && w.compare(k, 3, "%20") == 0) {
                        disc += ' ';
                        k += 2;
                    } else if (w[k] == '%' && k + 2 < colon && w.compare(k, 3, "%25") == 0) {
                        disc += '%';
                        k += 2;
                    } else {
                        disc += w[k];
                    }
                }
                e.discs.push_back({lower(disc), lower(w.substr(colon + 1))});
            } else {
                e.files.emplace_back(static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, 16)),
                                     std::strtoll(w.c_str() + colon + 1, nullptr, 10));
            }
        }
        c.sets.emplace(words[0], std::move(e));
    }
    return c;
}

// Every emulator's catalogue in `dir`, in the order. A missing one is said
// and reads as empty.
void loadFrom(const std::string& dir, Catalogue* cats) {
    for (size_t i = 0; i < kCores; ++i) {
        // fbneo_libretro.so's is fbneo_libretro.catalogue; a program of its
        // own has no .so, and its catalogue is named for it (sm2emu.catalogue,
        // supermodel.catalogue).
        std::string file = catalog::coreFileName(kOrder[i]);
        file.replace(file.size() - 3, 3, ".catalogue");
        if (cab::standalone::find(kOrder[i])) file = std::string(kOrder[i]) + ".catalogue";
        const std::string path = dir + "/" + file;
        std::string text;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            char buf[65536];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        if (text.empty()) {
            std::fprintf(stderr, "[arcade] no catalogue at %s: %s plays nothing\n",
                         path.c_str(), kOrder[i]);
            continue;
        }
        cats[i] = parse(text);
        std::fprintf(stderr, "[arcade] %s: %zu sets\n", kOrder[i], cats[i].sets.size());
    }
}

void load() {
    std::call_once(gLoaded, [] { loadFrom(gDir, gCat); });
}

// The emulators this game is asked of, in order, with their entry for it.
struct Candidate {
    const char* core;
    const Entry* entry;
};
std::vector<Candidate> candidates(const Catalogue* cats, const std::string& fileName,
                                  bool dreamcast) {
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    std::vector<Candidate> out;
    for (size_t i = 0; i < kCores; ++i) {
        const std::string core = kOrder[i];
        if (dreamcast && core != "flycast") continue;
        // MAME 2003-Plus and Supermodel open a zip only.
        if (ext == "7z" && (core == "mame2003_plus" || core == "supermodel")) continue;
        const auto it = cats[i].sets.find(set);
        if (it == cats[i].sets.end() || !it->second.works) continue;
        // NOTHING LEFT TO CHECK is never a fit: an empty list fits any zip.
        if (it->second.files.empty() && it->second.discs.empty()) continue;
        out.push_back({kOrder[i], &it->second});
    }
    return out;
}

Answer notSupported() { return {Verdict::NotSupported, nullptr, "Not supported"}; }

// A GAME NOT MADE FOR A CONTROLLER, by any list that has it, is not
// supported, and a light-gun game, by any list, needs a light gun: what a game
// was made for does not change with the emulator, and an older list can call
// a gun a trackball (MAME 2003-Plus's Born to Fight).
bool marked(const std::vector<Candidate>& c, char flag) {
    for (const Candidate& k : c)
        if (k.entry->flag == flag) return true;
    return false;
}
bool noController(const std::vector<Candidate>& c) { return marked(c, 'x'); }
bool needsGun(const std::vector<Candidate>& c) { return marked(c, 'g'); }
Answer gunAnswer(const Candidate& c) { return {Verdict::NeedsGun, c.core, "Needs a light gun"}; }

Answer byNameIn(const Catalogue* cats, const std::string& fileName, bool dreamcast) {
    const std::vector<Candidate> c = candidates(cats, fileName, dreamcast);
    if (c.empty() || noController(c)) return notSupported();
    if (needsGun(c)) return gunAnswer(c.front());
    return {Verdict::Plays, c.front().core, {}};
}

Answer byFilesIn(const Catalogue* cats, const std::string& fileName,
                 const std::vector<Member>& members, const std::vector<Disc>& discs,
                 bool dreamcast) {
    const std::vector<Candidate> c = candidates(cats, fileName, dreamcast);
    if (c.empty() || noController(c)) return notSupported();
    if (needsGun(c)) return gunAnswer(c.front());
    std::set<std::pair<uint32_t, int64_t>> have;
    std::set<uint32_t> crcs;
    for (const Member& m : members) {
        have.emplace(m.crc, m.size);
        crcs.insert(m.crc);
    }
    std::string base = fileName;
    if (const size_t slash = base.find_last_of('/'); slash != std::string::npos)
        base.erase(0, slash + 1);
    // The first emulator that has the set working says why, when none fits.
    Answer first;
    bool failed = false;
    auto fail = [&](Answer a) {
        if (!failed) first = std::move(a);
        failed = true;
    };
    for (const Candidate& k : c) {
        bool fits = true;
        for (const auto& f : k.entry->files)
            if (f.second > 0 ? !have.count(f) : !crcs.count(f.first)) { fits = false; break; }
        if (!fits) {
            fail({Verdict::FilesDiffer, k.core, "The files in " + base + " don't match its set"});
            continue;
        }
        bool missing = false, differs = false;
        for (const Disc& want : k.entry->discs) {
            const auto d = std::find_if(discs.begin(), discs.end(), [&](const Disc& x) {
                return lower(x.name) == want.name;
            });
            if (d == discs.end()) missing = true;
            else if (!want.sha1.empty() && lower(d->sha1) != want.sha1) differs = true;
        }
        if (missing) {
            fail({Verdict::DiscMissing, k.core, "Its disc image is missing"});
            continue;
        }
        if (differs) {
            fail({Verdict::DiscDiffers, k.core, "Its disc image doesn't match its set"});
            continue;
        }
        return {Verdict::Plays, k.core, {}};
    }
    return first;
}

uint32_t le32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t le16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint64_t le64(const unsigned char* p) {
    return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32);
}

bool readAt(FILE* f, int64_t at, size_t n, std::vector<unsigned char>* out) {
    out->assign(n, 0);
    return fseeko(f, at, SEEK_SET) == 0 && std::fread(out->data(), 1, n, f) == n;
}

// A ZIP'S OWN DIRECTORY, at its end: each file's name, CRC32 and size,
// without decompressing anything. ZIP64 for a zip past 4 GB or 65,535 files.
bool zipMembers(FILE* f, std::vector<Member>* out, std::string* err) {
    if (fseeko(f, 0, SEEK_END) != 0) return false;
    const int64_t size = ftello(f);
    const int64_t tail = std::min<int64_t>(size, 22 + 65535);
    std::vector<unsigned char> t;
    if (size < 22 || !readAt(f, size - tail, static_cast<size_t>(tail), &t)) {
        if (err) *err = "too short for a zip";
        return false;
    }
    int64_t eocd = -1;
    for (int64_t i = tail - 22; i >= 0; --i)
        if (le32(&t[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { if (err) *err = "no end of the zip's directory"; return false; }
    uint64_t entries = le16(&t[eocd + 10]);
    uint64_t dirSize = le32(&t[eocd + 12]);
    uint64_t dirAt = le32(&t[eocd + 16]);
    if (dirAt == 0xffffffffu || entries == 0xffff) {
        std::vector<unsigned char> loc, z;
        const int64_t locAt = size - tail + eocd - 20;
        if (locAt < 0 || !readAt(f, locAt, 20, &loc) || le32(loc.data()) != 0x07064b50 ||
            !readAt(f, static_cast<int64_t>(le64(&loc[8])), 56, &z) ||
            le32(z.data()) != 0x06064b50) {
            if (err) *err = "a ZIP64 directory that does not read";
            return false;
        }
        entries = le64(&z[32]);
        dirSize = le64(&z[40]);
        dirAt = le64(&z[48]);
    }
    std::vector<unsigned char> d;
    if (dirSize > 64u * 1024 * 1024 || !readAt(f, static_cast<int64_t>(dirAt), dirSize, &d)) {
        if (err) *err = "the zip's directory does not read";
        return false;
    }
    size_t p = 0;
    for (uint64_t n = 0; n < entries; ++n) {
        if (p + 46 > d.size() || le32(&d[p]) != 0x02014b50) {
            if (err) *err = "the zip's directory is damaged";
            return false;
        }
        const uint16_t nameLen = le16(&d[p + 28]), extraLen = le16(&d[p + 30]),
                       commentLen = le16(&d[p + 32]);
        if (p + 46 + nameLen + extraLen + commentLen > d.size()) {
            if (err) *err = "the zip's directory is damaged";
            return false;
        }
        Member m;
        m.crc = le32(&d[p + 16]);
        uint64_t usize = le32(&d[p + 24]);
        m.name.assign(reinterpret_cast<const char*>(&d[p + 46]), nameLen);
        if (usize == 0xffffffffu) {
            // The real size is the first field of its ZIP64 extra.
            for (size_t x = p + 46 + nameLen; x + 4 <= p + 46 + nameLen + extraLen;) {
                const uint16_t id = le16(&d[x]), len = le16(&d[x + 2]);
                if (id == 0x0001 && len >= 8) { usize = le64(&d[x + 4]); break; }
                x += 4 + len;
            }
        }
        m.size = static_cast<int64_t>(usize);
        if (!m.name.empty() && m.name.back() != '/') out->push_back(std::move(m));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

// A 7z: every file decompressed through libarchive for its CRC32.
bool sevenZipMembers(const std::string& path, std::vector<Member>* out, std::string* err) {
    struct archive* a = archive_read_new();
    if (!a) return false;
    archive_read_support_format_7zip(a);
    if (archive_read_open_filename(a, path.c_str(), 1 << 16) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "does not open";
        archive_read_free(a);
        return false;
    }
    struct archive_entry* e = nullptr;
    std::vector<unsigned char> buf(1 << 16);
    bool ok = true;
    for (int r; (r = archive_read_next_header(a, &e)) != ARCHIVE_EOF;) {
        if (r != ARCHIVE_OK) { ok = false; break; }
        if (archive_entry_filetype(e) != AE_IFREG) continue;
        Member m;
        m.name = archive_entry_pathname(e) ? archive_entry_pathname(e) : "";
        uLong crc = crc32(0L, Z_NULL, 0);
        for (la_ssize_t n; (n = archive_read_data(a, buf.data(), buf.size())) != 0;) {
            if (n < 0) { ok = false; break; }
            crc = crc32(crc, buf.data(), static_cast<uInt>(n));
            m.size += n;
        }
        if (!ok) break;
        m.crc = static_cast<uint32_t>(crc);
        out->push_back(std::move(m));
    }
    if (!ok && err) *err = archive_error_string(a) ? archive_error_string(a) : "damaged";
    archive_read_free(a);
    return ok;
}

}  // namespace

bool isArcadeSlug(const std::string& slug) {
    for (const char* s : {"arcade", "mame", "mame-advmame", "fbneo", "fba", "naomi", "naomi2",
                          "naomigd", "atomiswave", "gaelco", "cps1", "cps2", "cps3", "neogeomvs",
                          "neogeoaes", "stv", "model2", "model3"})
        if (slug == s) return true;
    return false;
}

bool asksDreamcast(const std::string& fileName) {
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    return ext == "zip" || ext == "7z";
}

void setDirectory(const std::string& dir) { gDir = dir; }

Answer byName(const std::string& fileName, bool dreamcast) {
    load();
    return byNameIn(gCat, fileName, dreamcast);
}

Answer byFiles(const std::string& fileName, const std::vector<Member>& members,
               const std::vector<Disc>& discs, bool dreamcast) {
    load();
    return byFilesIn(gCat, fileName, members, discs, dreamcast);
}

bool readMembers(const std::string& path, std::vector<Member>* out, std::string* err) {
    out->clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "does not open"; return false; }
    unsigned char magic[6] = {};
    const size_t n = std::fread(magic, 1, sizeof magic, f);
    bool ok = false;
    if (n >= 4 && magic[0] == 'P' && magic[1] == 'K') {
        ok = zipMembers(f, out, err);
        std::fclose(f);
    } else {
        std::fclose(f);
        static const unsigned char k7z[6] = {'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};
        if (n == 6 && std::memcmp(magic, k7z, 6) == 0) ok = sevenZipMembers(path, out, err);
        else if (err) *err = "neither a zip nor a 7z";
    }
    return ok;
}

std::string chdSha1(const std::string& path) {
    unsigned char h[124] = {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    const size_t n = std::fread(h, 1, sizeof h, f);
    std::fclose(f);
    if (n < 100 || std::memcmp(h, "MComprHD", 8) != 0) return {};
    const uint32_t version = (h[12] << 24) | (h[13] << 16) | (h[14] << 8) | h[15];
    // MAME's chd.h: the SHA1 of the data and its metadata together.
    const size_t at = version == 3 ? 80 : version == 4 ? 48 : version == 5 ? 84 : 0;
    if (!at || at + 20 > n) return {};
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (size_t i = at; i < at + 20; ++i) {
        out += kHex[h[i] >> 4];
        out += kHex[h[i] & 15];
    }
    return out;
}

bool flycastBios(const std::string& fileName) {
    load();
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    return ext == "zip" && gCat[0].bios.count(set);
}

int buttons(const char* core, const std::string& fileName) {
    load();
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    for (size_t i = 0; i < kCores; ++i) {
        if (std::strcmp(kOrder[i], core) != 0) continue;
        const auto it = gCat[i].sets.find(set);
        return it == gCat[i].sets.end() ? 0 : it->second.buttons;
    }
    return 0;
}

// ---- --arcade-changes ------------------------------------------------------------

int changes(const std::string& oldDir, const std::string& newDir) {
    Catalogue before[kCores], after[kCores];
    loadFrom(oldDir, before);
    loadFrom(newDir, after);
    std::set<std::string> names;
    for (size_t i = 0; i < kCores; ++i) {
        for (const auto& [n, e] : before[i].sets) names.insert(n);
        for (const auto& [n, e] : after[i].sets) names.insert(n);
    }
    // What a game shows for an answer: its emulator, or why it does not play.
    auto said = [](const Answer& a) {
        return a.verdict == Verdict::Plays ? std::string(a.core) : "(" + a.why + ")";
    };
    auto entryIn = [](const Catalogue* cats, const char* core, const std::string& n)
        -> const Entry* {
        for (size_t i = 0; i < kCores; ++i) {
            if (!core || std::strcmp(kOrder[i], core) != 0) continue;
            const auto it = cats[i].sets.find(n);
            return it == cats[i].sets.end() ? nullptr : &it->second;
        }
        return nullptr;
    };
    // The emulators that have the set working, in order: the ones its files
    // and its disc are tried on at Play.
    auto tried = [](const Catalogue* cats, const std::string& n) {
        std::string out;
        for (const Candidate& k : candidates(cats, n + ".zip", false))
            out += (out.empty() ? "" : ", ") + std::string(k.core);
        return out;
    };
    size_t moved = 0, refiled = 0, retried = 0;
    for (const std::string& n : names) {
        const Answer a = byNameIn(before, n + ".zip", false);
        const Answer b = byNameIn(after, n + ".zip", false);
        if (said(a) != said(b)) {
            std::printf("%s: %s -> %s\n", n.c_str(), said(a).c_str(), said(b).c_str());
            ++moved;
            continue;
        }
        if (b.verdict != Verdict::Plays) continue;
        // THE SAME BY NAME, BUT TRIED ON OTHERS AT PLAY: a zip or a disc the
        // first does not fit falls to the next (MAME 2003-Plus's Killer
        // Instinct with MAME 0.289's CHD).
        if (const std::string x = tried(before, n), y = tried(after, n); x != y) {
            std::printf("%s: %s by name; at Play tried on %s, was %s\n", n.c_str(), b.core,
                        y.c_str(), x.empty() ? "none" : x.c_str());
            ++retried;
            continue;
        }
        const Entry* x = entryIn(before, b.core, n);
        const Entry* y = entryIn(after, b.core, n);
        if (x && y && (x->files != y->files || x->discs.size() != y->discs.size() ||
                       !std::equal(x->discs.begin(), x->discs.end(), y->discs.begin(),
                                   [](const Disc& p, const Disc& q) {
                                       return p.name == q.name && p.sha1 == q.sha1;
                                   }))) {
            std::printf("%s: %s, its files changed\n", n.c_str(), b.core);
            ++refiled;
        }
    }
    std::printf("arcade changes: %zu sets change emulator, %zu may at Play, %zu keep theirs "
                "with other files, of %zu\n",
                moved, retried, refiled, names.size());
    return 0;
}

// ---- --arcade-match-test ----------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    // Six catalogues in the order Flycast, FinalBurn Neo, MAME 2003-Plus,
    // sm2-emu, Supermodel, MAME 0.289.
    Catalogue cats[kCores] = {
        parse("# flycast\n!bios naomi awbios\n"
              "azumanga w - b71ede16:0 @gdl-0018:\n"
              "hotd2 w g b23d1a0c:0\n"
              "tduno2 w x 1b58f24a:0\n"),
        parse("# fbneo\n"
              "sf2 w - 00000001:100 00000002:200\n"
              "gollygho n - 00000009:10\n"
              "opwolf w g 0000000a:10\n"
              "kinst w - 00000020:10\n"
              "vliner w x 00000040:10\n"
              "zombraid w - 00000060:10\n"),
        parse("# mame2003_plus\n"
              "sf2 w - 00000001:100 00000003:300\n"
              "gollygho w - 00000009:10\n"
              "kinst w - 00000020:10 @kinst:aaaa\n"
              "saturn w -\n"
              "blockgal n - 00000030:10\n"
              "vliner w - 00000040:10\n"
              "mjhaihai w x 00000050:10\n"
              "zombraid w g 00000060:10\n"
              "borntofi w - 00000080:10\n"),
        parse("# sm2emu\n"
              "daytona w - 00000101:0\n"
              "doa n - b3 00000102:0\n"
              "vf2 w - b3 00000103:0\n"
              "vcop n g 00000104:0\n"
              "lastbrnx n - b3 00000106:0\n"),
        parse("# supermodel\n"
              "scud w - 00000201:0\n"
              "vf3 w - b4 00000202:0\n"
              "lostwsga w g 00000203:0\n"),
        parse("# mame_libretro\n"
              "sf2 w - b6 00000001:100 00000003:300\n"
              "gollygho w - b2 00000009:10\n"
              "kinst w - b6 00000020:10 @kinst:cccc\n"
              "raystorm w - b2 00000070:10 @raystorm:dddd\n"
              "roadburn w - b4 000000a0:10 @road%20burners%20v1.04:eeee\n"
              "borntofi w g b2 00000080:10\n"
              "megat w x 00000090:10\n"
              "daytona w - b9 00000101:10\n"
              "doa w - b3 00000102:10\n"
              "vcop w g b1 00000104:10\n"
              "lastbrnx n - b3 00000106:10\n"
              "scud n - b2 00000201:10\n"
              "vf3 n - b4 00000202:10\n"
              "lostwsga n g b2 00000203:10\n"),
    };
    auto name = [&](const char* f, bool dc = false) { return byNameIn(cats, f, dc); };
    auto files = [&](const char* f, std::vector<Member> m, std::vector<Disc> d = {},
                     bool dc = false) { return byFilesIn(cats, f, m, d, dc); };
    auto core = [](const Answer& a) { return std::string(a.core ? a.core : ""); };

    check(name("sf2.zip").verdict == Verdict::Plays && core(name("sf2.zip")) == "fbneo_libretro",
          "by name: sf2 is FinalBurn Neo's, the first in the order that has it");
    check(name("SF2.ZIP").verdict == Verdict::Plays, "the name is read in any case");
    check(name("nothere.zip").verdict == Verdict::NotSupported &&
              name("nothere.zip").why == "Not supported",
          "a set no emulator has is not supported");
    check(core(name("gollygho.zip")) == "mame2003_plus",
          "an emulator that marks the set not working is skipped");
    check(name("blockgal.zip").verdict == Verdict::NotSupported,
          "a set every emulator marks not working is not supported");
    check(name("saturn.zip").verdict == Verdict::NotSupported,
          "an entry with nothing to check never fits");
    check(name("opwolf.zip").verdict == Verdict::NeedsGun &&
              name("opwolf.zip").why == "Needs a light gun",
          "a light-gun game needs a light gun");
    check(name("tduno2.zip").verdict == Verdict::NotSupported &&
              name("tduno2.zip").why == "Not supported",
          "a game played on its screen is not supported: no controller plays it");
    check(name("mjhaihai.zip").verdict == Verdict::NotSupported,
          "nor is a mahjong panel's game");
    check(name("vliner.zip").verdict == Verdict::NotSupported,
          "one list marking a game no controller's is enough, though another plays it");
    check(name("zombraid.zip").verdict == Verdict::NeedsGun,
          "and one list marking a light-gun game is enough, though the first plays it");
    check(core(name("gollygho.7z")) == "mame_libretro",
          "a 7z is never MAME 2003-Plus's, which cannot open one: the next that has it");
    check(core(name("raystorm.zip")) == "mame_libretro",
          "MAME 0.289 plays what nothing older has");
    check(core(name("gollygho.zip")) == "mame2003_plus",
          "and MAME 2003-Plus comes before it, where both have the set working");
    check(name("borntofi.zip").verdict == Verdict::NeedsGun,
          "a game MAME 0.289's list makes a light-gun game needs one, though an older list "
          "plays it");
    check(name("megat.zip").verdict == Verdict::NotSupported,
          "a touchscreen machine in MAME 0.289's list is not supported");
    check(cats[5].sets.at("raystorm").buttons == 2 && cats[1].sets.at("sf2").buttons == 0,
          "MAME 0.289's catalogue says how many buttons a game has; the others say none");
    check(core(name("daytona.zip")) == "sm2emu",
          "sm2-emu comes before MAME 0.289, where both have a Model 2 game working");
    check(core(name("doa.zip")) == "mame_libretro",
          "a game sm2-emu's list marks not working goes to the next that has it working");
    check(name("lastbrnx.zip").verdict == Verdict::NotSupported,
          "and one both mark not working is not supported");
    check(name("vcop.zip").verdict == Verdict::NeedsGun,
          "a light-gun game in sm2-emu's list needs a light gun");
    check(cats[3].sets.at("vf2").buttons == 3 && cats[3].sets.at("daytona").buttons == 0,
          "sm2-emu's catalogue gives a three-button game's buttons, and a driving game none");
    check(core(name("scud.zip")) == "supermodel",
          "Supermodel comes before MAME 0.289, which marks every Model 3 game not working");
    check(core(name("scud.7z")) == "",
          "a Model 3 7z is never Supermodel's, which opens a zip only, and MAME 0.289's is "
          "not working");
    check(name("lostwsga.zip").verdict == Verdict::NeedsGun,
          "a light-gun game in Supermodel's catalogue needs a light gun");
    check(cats[4].sets.at("vf3").buttons == 4 && cats[4].sets.at("scud").buttons == 0,
          "Supermodel's catalogue gives a fighting game's four buttons, and a driving game none");
    check(core(name("azumanga.zip", true)) == "flycast",
          "in the Dreamcast folder, a Flycast set is Flycast's");
    check(name("sf2.zip", true).verdict == Verdict::NotSupported,
          "and any other zip there is not supported, never handed to Flycast");

    const std::vector<Member> fb = {{"a", 1, 100}, {"b", 2, 200}, {"extra", 7, 7}};
    const std::vector<Member> mame = {{"a", 1, 100}, {"c", 3, 300}};
    check(core(files("sf2.zip", fb)) == "fbneo_libretro",
          "by files: FinalBurn Neo's version, with an extra file, is FinalBurn Neo's");
    check(core(files("sf2.zip", mame)) == "mame2003_plus",
          "MAME 2003-Plus's version is MAME 2003-Plus's, though FinalBurn Neo comes first");
    check(files("sf2.zip", {{"a", 1, 100}, {"b", 2, 201}}).verdict == Verdict::FilesDiffer,
          "a file of the wrong size does not fit");
    {
        const Answer a = files("sf2.zip", {{"a", 1, 100}});
        check(a.verdict == Verdict::FilesDiffer && core(a) == "fbneo_libretro" &&
                  a.why == "The files in sf2.zip don't match its set",
              "fitting none, the first emulator that has the set says why");
    }
    check(core(files("azumanga.zip", {{"key", 0xb71ede16, 4096}}, {{"gdl-0018", "1234"}})) ==
              "flycast",
          "Flycast lists no sizes or disc SHA1: a CRC and a CHD of the name fit");
    check(files("azumanga.zip", {{"key", 0xb71ede16, 4096}}).verdict == Verdict::DiscMissing,
          "a set with a disc and no CHD beside it says its disc image is missing");
    check(core(files("kinst.zip", {{"k", 0x20, 10}}, {{"kinst", "AAAA"}})) == "fbneo_libretro",
          "a set FinalBurn Neo has with no disc plays there");
    {
        Catalogue noFb[kCores] = {cats[0], Catalogue{}, cats[2], cats[3], cats[4], cats[5]};
        check(core(byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"KINST", "AAAA"}}, false)) ==
                  "mame2003_plus",
              "a disc of the listed name and SHA1 fits, in any case");
        const Answer d =
            byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"kinst", "bbbb"}}, false);
        check(d.verdict == Verdict::DiscDiffers && d.why == "Its disc image doesn't match its set",
              "a disc of another SHA1 does not fit");
        check(core(byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"kinst", "cccc"}}, false)) ==
                  "mame_libretro",
              "a disc made for MAME 0.289 plays there, though MAME 2003-Plus comes first");
    }
    check(core(files("raystorm.zip", {{"r", 0x70, 10}}, {{"raystorm", "dddd"}})) ==
              "mame_libretro",
          "by files, MAME 0.289's disc game with its CHD plays there");
    check(core(files("roadburn.zip", {{"r", 0xa0, 10}}, {{"Road Burners v1.04", "EEEE"}})) ==
              "mame_libretro",
          "and a disc whose name has spaces, written %20 in the catalogue");
    check(core(files("daytona.zip", {{"d", 0x101, 10}})) == "sm2emu",
          "by files, a Model 2 zip with the files both lists give plays in sm2-emu");
    check(core(files("vf3.zip", {{"v", 0x202, 10}})) == "supermodel",
          "by files, a Model 3 zip with the files its list gives plays in Supermodel");
    check(files("hotd2.zip", {{"r", 0xb23d1a0c, 1}}).verdict == Verdict::NeedsGun,
          "by files too, a light-gun game needs a light gun");
    check(files("vliner.zip", {{"v", 0x40, 10}}).verdict == Verdict::NotSupported,
          "and a game no controller plays is not supported, whatever its files");
    check(cats[0].bios.count("naomi") && cats[0].bios.count("awbios"),
          "Flycast's BIOS zips are read from its !bios line");
    check(isArcadeSlug("arcade") && isArcadeSlug("cps2") && isArcadeSlug("neogeomvs"),
          "RomM 5.4's arcade labels are arcade folders");
    check(isArcadeSlug("mame") && isArcadeSlug("fbneo") && isArcadeSlug("naomi") &&
              isArcadeSlug("naomigd") && isArcadeSlug("atomiswave") && isArcadeSlug("gaelco"),
          "and so are the labels RomM gives each emulator's folder after rommapp/romm#5273");
    check(!isArcadeSlug("triforce") && !isArcadeSlug("lindbergh") && !isArcadeSlug("dc"),
          "a board no emulator here plays is not, nor is Dreamcast");

    std::printf("%s\n", failures ? "arcade match test: FAILED" : "arcade match test: passed");
    return failures ? 1 : 0;
}

}  // namespace arcadematch
