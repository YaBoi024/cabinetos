#include "dirsave.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace cab {
namespace {

void walk(const std::string& root, const std::string& rel,
          std::vector<DirEntry>* out) {
    const std::string dir = rel.empty() ? root : root + "/" + rel;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string childRel = rel.empty() ? name : rel + "/" + name;
        const std::string childAbs = root + "/" + childRel;
        struct stat st;
        // lstat, not stat: a symlink inside a save directory is not something
        // to follow into, and archiving what it points at could reach anywhere.
        if (::lstat(childAbs.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk(root, childRel, out);
        } else if (S_ISREG(st.st_mode)) {
            out->push_back(DirEntry{childRel,
                                    static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 +
                                        static_cast<int64_t>(st.st_mtim.tv_nsec),
                                    static_cast<int64_t>(st.st_size)});
        }
    }
    ::closedir(d);
}

bool readFile(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out->resize(static_cast<size_t>(n));
    const bool ok = n == 0 || std::fread(out->data(), 1, out->size(), f) == out->size();
    std::fclose(f);
    return ok;
}

// mkdir -p, for the parent of a file that is about to be written.
bool makeParents(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        const std::string part = path.substr(0, i);
        if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

}  // namespace

bool safeRelPath(const std::string& p) {
    if (p.empty() || p.front() == '/') return false;
    size_t start = 0;
    while (start <= p.size()) {
        const size_t slash = p.find('/', start);
        const std::string part =
            p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

std::vector<DirEntry> listTree(const std::string& root) {
    std::vector<DirEntry> out;
    walk(root, "", &out);
    std::sort(out.begin(), out.end(),
              [](const DirEntry& a, const DirEntry& b) { return a.relPath < b.relPath; });
    return out;
}

bool looksLikeZip(const std::vector<uint8_t>& d) {
    return d.size() >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 0x03 && d[3] == 0x04;
}

Utf8Names::Utf8Names()
    : utf8_(newlocale(LC_CTYPE_MASK, "C.UTF-8", static_cast<locale_t>(nullptr))),
      was_(utf8_ ? uselocale(utf8_) : static_cast<locale_t>(nullptr)) {}

Utf8Names::~Utf8Names() {
    if (!utf8_) return;
    uselocale(was_);
    freelocale(utf8_);
}

namespace {

// One zip being written into memory. archive_write_open_memory is a single
// fixed buffer, so `room` is sized before anything is written: the stored
// bytes plus generous room for each entry's header and the central directory
// at the end. Zip only ever shrinks these files, so that cannot be too small.
//
// Whether `s` is UTF-8 as Python's strict decoder takes it, which is how RomM
// reads a name a zip marks as UTF-8: no overlong form, no surrogate, nothing
// past U+10FFFF. ui::nextCodepoint (text.h) passes all of those, as drawing a
// name should; marking one of them would make the zip one RomM cannot read.
bool isUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) { ++i; continue; }
        int more = 0;
        unsigned char lo = 0x80, hi = 0xBF;   // the byte after the lead
        if (b >= 0xC2 && b <= 0xDF) more = 1;
        else if (b == 0xE0) { more = 2; lo = 0xA0; }
        else if (b == 0xED) { more = 2; hi = 0x9F; }
        else if (b >= 0xE1 && b <= 0xEF) more = 2;
        else if (b == 0xF0) { more = 3; lo = 0x90; }
        else if (b == 0xF4) { more = 3; hi = 0x8F; }
        else if (b >= 0xF1 && b <= 0xF3) more = 3;
        else return false;
        if (i + static_cast<size_t>(more) >= s.size()) return false;
        for (int k = 1; k <= more; ++k) {
            const auto c = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if (k == 1 ? (c < lo || c > hi) : (c < 0x80 || c > 0xBF)) return false;
        }
        i += static_cast<size_t>(more) + 1;
    }
    return true;
}

// EACH NAME GOES IN AS ITS BYTES, MARKED UTF-8 ONLY WHEN IT IS (#346). RomM
// names a zip's files when it hashes one (romm::contentHash) and when it
// restores one: a name the zip marks it reads as UTF-8, any other as CP437.
// So a name outside ASCII that is UTF-8 is marked, or RomM would hash it as
// other text and refuse the push; and a name that is not UTF-8 (a GameCube
// save named in Shift-JIS) is left unmarked, as its bytes, which RomM reads
// as CP437 and another console unpacks as the same bytes. libarchive marks
// a name outside ASCII exactly when the thread's locale is UTF-8, and with
// no `hdrcharset` it converts no name, so each name is written in the locale
// that marks it rightly. Asked to convert every name to UTF-8
// (`hdrcharset=UTF-8`), libarchive marks a Shift-JIS name too, and writes
// it with each byte it cannot read replaced and a 0x5C as "/"
// (docs/lessons/emulators.md).
struct ZipOut {
    locale_t utf8 = newlocale(LC_CTYPE_MASK, "C.UTF-8", static_cast<locale_t>(nullptr));
    locale_t plain = newlocale(LC_CTYPE_MASK, "C", static_cast<locale_t>(nullptr));
    struct archive* a = nullptr;
    std::vector<uint8_t> buf;
    size_t used = 0;

    ~ZipOut() {
        if (a) archive_write_free(a);
        if (utf8) freelocale(utf8);
        if (plain) freelocale(plain);
    }
    static std::string why(struct archive* a, const char* fallback) {
        return archive_error_string(a) ? archive_error_string(a) : fallback;
    }
    bool open(size_t room, std::string* err) {
        if (!utf8 || !plain) { if (err) *err = "no locale for a zip's names"; return false; }
        a = archive_write_new();
        if (!a) { if (err) *err = "archive_write_new failed"; return false; }
        archive_write_set_format_zip(a);
        buf.assign(room, 0);
        if (archive_write_open_memory(a, buf.data(), buf.size(), &used) != ARCHIVE_OK) {
            if (err) *err = why(a, "cannot open");
            return false;
        }
        return true;
    }
    bool add(const std::string& name, const std::vector<uint8_t>& data, std::string* err) {
        struct archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, name.c_str());
        archive_entry_set_size(e, static_cast<int64_t>(data.size()));
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        const locale_t was = uselocale(isUtf8(name) ? utf8 : plain);
        const int header = archive_write_header(a, e);
        uselocale(was);
        if (header != ARCHIVE_OK) {
            if (err) *err = why(a, "header");
            archive_entry_free(e);
            return false;
        }
        if (!data.empty() && archive_write_data(a, data.data(), data.size()) < 0) {
            if (err) *err = why(a, "write");
            archive_entry_free(e);
            return false;
        }
        archive_entry_free(e);
        return true;
    }
    bool close(std::vector<uint8_t>* out, std::string* err) {
        if (archive_write_close(a) != ARCHIVE_OK) {
            if (err) *err = why(a, "close");
            return false;
        }
        out->assign(buf.begin(), buf.begin() + static_cast<long>(used));
        return true;
    }
};

size_t roomFor(const std::string& name, size_t bytes) { return bytes + name.size() * 2 + 512; }

}  // namespace

bool zipTree(const std::string& root, const std::vector<std::string>& relPaths,
             std::vector<uint8_t>* out, std::string* err) {
    if (relPaths.empty()) {
        if (err) *err = "nothing to archive";
        return false;
    }
    size_t total = 4096;
    for (const std::string& rel : relPaths) {
        struct stat st;
        total += roomFor(rel, ::stat((root + "/" + rel).c_str(), &st) == 0
                                  ? static_cast<size_t>(st.st_size) : 0);
    }
    ZipOut zip;
    if (!zip.open(total, err)) return false;
    for (const std::string& rel : relPaths) {
        std::vector<uint8_t> data;
        if (!readFile(root + "/" + rel, &data)) {
            if (err) *err = "cannot read " + rel;
            return false;
        }
        if (!zip.add(rel, data, err)) return false;
    }
    return zip.close(out, err);
}

bool zipFiles(const std::vector<ZipFile>& files, std::vector<uint8_t>* out, std::string* err) {
    if (files.empty()) {
        if (err) *err = "nothing to archive";
        return false;
    }
    size_t total = 4096;
    for (const ZipFile& f : files) total += roomFor(f.relPath, f.bytes.size());
    ZipOut zip;
    if (!zip.open(total, err)) return false;
    for (const ZipFile& f : files)
        if (!zip.add(f.relPath, f.bytes, err)) return false;
    return zip.close(out, err);
}

namespace {

// One archive being read from memory, its names as UTF-8 (Utf8Names): a name
// a zip marks as UTF-8, as this console's own zips and Python's do, cannot
// be read in the C locale the program runs in, and the entry carrying it
// read as the end of the archive, so a save folder with an accent in a file
// name came back as "no files". `next` steps to the next entry: false at the
// end, and false with `failed` set when libarchive could not go on, which is
// said and never taken for the end. A name libarchive only warned about is
// still an entry.
struct ZipIn {
    Utf8Names names;
    struct archive* a = nullptr;
    bool failed = false;

    ~ZipIn() {
        if (a) archive_read_free(a);
    }
    std::string why(const char* fallback) const {
        return archive_error_string(a) ? archive_error_string(a) : fallback;
    }
    bool open(const std::vector<uint8_t>& data, std::string* err) {
        a = archive_read_new();
        if (!a) { if (err) *err = "archive_read_new failed"; return false; }
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
        if (archive_read_open_memory(a, const_cast<uint8_t*>(data.data()), data.size()) !=
            ARCHIVE_OK) {
            if (err) *err = why("cannot open");
            return false;
        }
        return true;
    }
    bool next(struct archive_entry** entry, std::string* err) {
        const int rc = archive_read_next_header(a, entry);
        if (rc == ARCHIVE_OK || rc == ARCHIVE_WARN) return true;
        if (rc != ARCHIVE_EOF) {
            failed = true;
            if (err) *err = why("cannot read the next entry");
        }
        return false;
    }
    // The entry's name when it is a file that may be written under a folder;
    // null for a folder or anything else that is no file. False, with a
    // reason, for a name unzipTree refuses.
    bool fileName(struct archive_entry* entry, const char** name, std::string* err) {
        *name = nullptr;
        if (archive_entry_filetype(entry) != AE_IFREG) return true;
        const char* n = archive_entry_pathname(entry);
        if (!n || !safeRelPath(n)) {
            if (err) *err = std::string("refusing entry ") + (n ? n : "(unnamed)");
            return false;
        }
        *name = n;
        return true;
    }
};

}  // namespace

bool readZip(const std::vector<uint8_t>& data, std::vector<ZipFile>* out, std::string* err) {
    out->clear();
    ZipIn zip;
    if (!zip.open(data, err)) return false;
    struct archive_entry* entry = nullptr;
    while (zip.next(&entry, err)) {
        const char* name = nullptr;
        if (!zip.fileName(entry, &name, err)) return false;
        if (!name) continue;
        ZipFile f;
        f.relPath = name;
        char chunk[64 * 1024];
        for (;;) {
            const ssize_t n = archive_read_data(zip.a, chunk, sizeof chunk);
            if (n == 0) break;
            if (n < 0) {
                if (err) *err = zip.why("read");
                return false;
            }
            f.bytes.insert(f.bytes.end(), chunk, chunk + n);
        }
        out->push_back(std::move(f));
    }
    return !zip.failed;
}

bool unzipTree(const std::vector<uint8_t>& data, const std::string& root,
               std::string* err) {
    ZipIn zip;
    if (!zip.open(data, err)) return false;
    int files = 0;
    struct archive_entry* entry = nullptr;
    while (zip.next(&entry, err)) {
        const char* name = nullptr;
        if (!zip.fileName(entry, &name, err)) return false;
        if (!name) continue;
        const std::string dest = root + "/" + name;
        if (!makeParents(dest)) {
            if (err) *err = "cannot create directories for " + std::string(name);
            return false;
        }
        FILE* f = std::fopen(dest.c_str(), "wb");
        if (!f) {
            if (err) *err = "cannot write " + dest;
            return false;
        }
        char chunk[64 * 1024];
        for (;;) {
            const ssize_t n = archive_read_data(zip.a, chunk, sizeof chunk);
            if (n == 0) break;
            if (n < 0) {
                if (err) *err = zip.why("read");
                std::fclose(f);
                return false;
            }
            if (std::fwrite(chunk, 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) {
                if (err) *err = "short write to " + dest;
                std::fclose(f);
                return false;
            }
        }
        std::fclose(f);
        ++files;
    }
    if (zip.failed) return false;
    if (files == 0) {
        if (err) *err = "archive held no files";
        return false;
    }
    return true;
}

namespace {

uint32_t le16(const std::vector<uint8_t>& d, size_t at) {
    return static_cast<uint32_t>(d[at]) | static_cast<uint32_t>(d[at + 1]) << 8;
}
uint32_t le32(const std::vector<uint8_t>& d, size_t at) {
    return le16(d, at) | le16(d, at + 2) << 16;
}
uint64_t le64(const std::vector<uint8_t>& d, size_t at) {
    return static_cast<uint64_t>(le32(d, at)) | static_cast<uint64_t>(le32(d, at + 4)) << 32;
}

}  // namespace

bool zipNameMarks(const std::vector<uint8_t>& zip, std::map<std::string, bool>* marked) {
    marked->clear();
    // The end record: the last "PK\5\6" whose 22 bytes fit, within the 64 KiB
    // a comment after it may take, as Python's zipfile looks for it.
    constexpr size_t kEnd = 22;
    if (zip.size() < kEnd) return false;
    const size_t lowest = zip.size() > kEnd + 0xFFFF ? zip.size() - kEnd - 0xFFFF : 0;
    size_t end = zip.size() - kEnd + 1;
    do {
        if (end-- == lowest) return false;
    } while (le32(zip, end) != 0x06054B50);
    // The central directory ends where the end record starts, or where the
    // zip64 end record (56 bytes, before its 20-byte locator) does, and is
    // as long as the end record says: zipfile's own reckoning, which holds
    // for a zip with bytes in front of it too.
    uint64_t size = le32(zip, end + 12);
    size_t tail = end;
    if (end >= 76 && le32(zip, end - 20) == 0x07064B50) {
        tail = end - 76;
        if (le32(zip, tail) != 0x06064B50) return false;
        size = le64(zip, tail + 40);
    }
    if (size > tail) return false;
    size_t at = tail - static_cast<size_t>(size);
    while (at < tail) {
        if (at + 46 > tail || le32(zip, at) != 0x02014B50) return false;
        const size_t nameLen = le16(zip, at + 28);
        const size_t next = at + 46 + nameLen + le16(zip, at + 30) + le16(zip, at + 32);
        if (next > tail) return false;
        std::string name(reinterpret_cast<const char*>(zip.data() + at + 46), nameLen);
        // zipfile ends a name at a NUL, as a C string does.
        name.resize(std::strlen(name.c_str()));
        const bool utf8 = (le16(zip, at + 8) & 0x0800) != 0;   // general purpose bit 11
        const auto [it, added] = marked->emplace(name, utf8);
        if (!added && it->second != utf8) return false;
        at = next;
    }
    return true;
}

namespace {

bool hexRun(const std::string& s, size_t from, size_t n) {
    if (s.size() < from + n) return false;
    for (size_t i = from; i < from + n; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// The SD card's `<id0>/<id1>/`: two runs of 32 hex digits. This console's
// Azahar has one SD card, all zeros.
bool sdCardIds(const std::string& p) {
    return hexRun(p, 0, 32) && p.size() > 66 && p[32] == '/' && hexRun(p, 33, 32) && p[65] == '/';
}

// The folder every profile on a Switch shares, a device's saves.
constexpr char kSwitchDevice[] = "00000000000000000000000000000000";

}  // namespace

bool inSwitchUnit(const std::string& profile, const std::string& relPath) {
    const std::string& p = relPath;
    if (profile.size() != 32 || p.size() <= 50 || p[32] != '/' || p[49] != '/') return false;
    if (p.compare(0, 32, profile) != 0 && p.compare(0, 32, kSwitchDevice) != 0) return false;
    if (!hexRun(p, 33, 16)) return false;
    // `.yuzu_save_size`, `.citron_save_size`, `.sudachi_save_size`,
    // `.suyu_save_size`: never collected, written or removed (Sigil's Switch
    // page).
    const std::string base = p.substr(p.find_last_of('/') + 1);
    const std::string end = "_save_size";
    return !(base.size() > end.size() + 1 && base[0] == '.' &&
             base.compare(base.size() - end.size(), end.size(), end) == 0);
}

std::vector<ZipFile> toZipShape(ZipShape shape, std::vector<ZipFile> files,
                                const std::string& place) {
    if (shape == ZipShape::Folder) return files;
    std::vector<ZipFile> out;
    if (shape == ZipShape::Switch) {
        for (ZipFile& f : files) {
            if (!inSwitchUnit(place, f.relPath)) continue;
            const bool device = f.relPath.compare(0, 32, kSwitchDevice) == 0;
            out.push_back({(device ? "device/" : "") + f.relPath.substr(33), std::move(f.bytes)});
        }
        return out;
    }
    if (shape == ZipShape::Wii) {
        // `<kind>/<title>/data/...` to `<title>/data/...`.
        for (const ZipFile& f : files) {
            if (!hexRun(f.relPath, 0, 8) || f.relPath.size() < 23 || f.relPath[8] != '/' ||
                !hexRun(f.relPath, 9, 8) || f.relPath.compare(17, 6, "/data/") != 0)
                return files;
            out.push_back({f.relPath.substr(9), f.bytes});
        }
        return out;
    }
    // 3DS: the title the save's data belongs to, then each file under it.
    std::string high, low;
    for (const ZipFile& f : files)
        if (sdCardIds(f.relPath) && f.relPath.compare(66, 6, "title/") == 0 &&
            hexRun(f.relPath, 72, 8) && hexRun(f.relPath, 81, 8)) {
            if (!high.empty() && (high != f.relPath.substr(72, 8) || low != f.relPath.substr(81, 8)))
                return files;   // two titles: Sigil's unit holds one
            high = f.relPath.substr(72, 8);
            low = f.relPath.substr(81, 8);
        }
    if (high.empty()) return files;
    char ext[17];
    std::snprintf(ext, sizeof ext, "%08lx", std::strtoul(low.c_str(), nullptr, 16) >> 8);
    const std::string titleAt = "title/" + high + "/" + low + "/data/";
    const std::string extAt = "extdata/00000000/" + std::string(ext) + "/";
    for (const ZipFile& f : files) {
        if (!sdCardIds(f.relPath)) return files;
        const std::string rest = f.relPath.substr(66);
        if (rest.compare(0, titleAt.size(), titleAt) == 0)
            out.push_back({high + "/" + low + "/data/" + rest.substr(titleAt.size()), f.bytes});
        else if (rest.compare(0, extAt.size(), extAt) == 0)
            out.push_back({high + "/" + low + "/extdata/" + rest.substr(extAt.size()), f.bytes});
        else
            return files;
    }
    return out;
}

std::vector<ZipFile> fromZipShape(ZipShape shape, const std::string& place,
                                  std::vector<ZipFile> files) {
    if (shape == ZipShape::Folder || files.empty()) return files;
    std::vector<ZipFile> out;
    if (shape == ZipShape::Switch) {
        // `device/<title>/...` into the device's folder; everything else into
        // the profile's, where a zip from before was rooted, whatever it
        // holds beside its title folders.
        if (place.size() != 32) return files;
        for (const ZipFile& f : files) {
            const std::string& p = f.relPath;
            if (p.compare(0, 7, "device/") == 0 && hexRun(p, 7, 16) && p.size() > 24 && p[23] == '/')
                out.push_back({std::string(kSwitchDevice) + "/" + p.substr(7), f.bytes});
            else
                out.push_back({place + "/" + p, f.bytes});
        }
        return out;
    }
    if (shape == ZipShape::Wii) {
        // `<title>/data/...` is Sigil's; `<kind>/<title>/data/...` the folder's.
        for (const ZipFile& f : files) {
            const bool sigil = hexRun(f.relPath, 0, 8) && f.relPath.compare(8, 6, "/data/") == 0;
            if (!sigil) return files;
            out.push_back({place + "/" + f.relPath, f.bytes});
        }
        return out;
    }
    // 3DS: `<high>/<low>/data|extdata/...` is Sigil's; the folder's starts
    // with the SD card's ids.
    const std::string sd(32, '0');
    for (const ZipFile& f : files) {
        const std::string& p = f.relPath;
        if (p.size() < 18) return files;
        const std::string high = p.substr(0, 8), low = p.substr(9, 8), rest = p.substr(18);
        if (rest.compare(0, 5, "data/") == 0) {
            out.push_back({sd + "/" + sd + "/title/" + high + "/" + low + "/" + rest, f.bytes});
        } else if (rest.compare(0, 8, "extdata/") == 0) {
            char ext[17];
            std::snprintf(ext, sizeof ext, "%08lx", std::strtoul(low.c_str(), nullptr, 16) >> 8);
            out.push_back({sd + "/" + sd + "/extdata/00000000/" + ext + "/" + rest.substr(8),
                           f.bytes});
        } else {
            return files;
        }
    }
    return out;
}

}  // namespace cab
