#include "dirsave.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
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
// A NAME OUTSIDE ASCII IS MARKED UTF-8 (#346). RomM names a zip's files when
// it hashes one (romm::contentHash), and reads a name the zip does not mark
// as CP437, so an unmarked name with an accent in it hashed to another value
// there than here, and RomM refused the push. libarchive sets the mark when the
// names it is given are UTF-8, which they are while Utf8Names stands; a name
// that is not valid UTF-8 goes in as its bytes, unmarked.
struct ZipOut {
    Utf8Names names;
    struct archive* a = nullptr;
    std::vector<uint8_t> buf;
    size_t used = 0;

    ~ZipOut() {
        if (a) archive_write_free(a);
    }
    static std::string why(struct archive* a, const char* fallback) {
        return archive_error_string(a) ? archive_error_string(a) : fallback;
    }
    bool open(size_t room, std::string* err) {
        a = archive_write_new();
        if (!a) { if (err) *err = "archive_write_new failed"; return false; }
        archive_write_set_format_zip(a);
        archive_write_set_options(a, "zip:hdrcharset=UTF-8");
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
        // A warning is a name libarchive could not read as UTF-8: it goes in
        // as its bytes.
        if (archive_write_header(a, e) < ARCHIVE_WARN) {
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

}  // namespace cab
