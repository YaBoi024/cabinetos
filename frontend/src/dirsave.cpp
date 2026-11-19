#include "dirsave.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
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

bool zipTree(const std::string& root, const std::vector<std::string>& relPaths,
             std::vector<uint8_t>* out, std::string* err) {
    if (relPaths.empty()) {
        if (err) *err = "nothing to archive";
        return false;
    }
    // Sized before anything is written, because archive_write_open_memory is a
    // single fixed buffer: the stored bytes plus generous room for each entry's
    // header and the central directory at the end. Zip only ever shrinks these
    // files, so this cannot be too small.
    size_t total = 4096;
    for (const std::string& rel : relPaths) {
        struct stat st;
        if (::stat((root + "/" + rel).c_str(), &st) == 0)
            total += static_cast<size_t>(st.st_size);
        total += rel.size() * 2 + 512;
    }

    struct archive* a = archive_write_new();
    if (!a) { if (err) *err = "archive_write_new failed"; return false; }
    archive_write_set_format_zip(a);
    std::vector<uint8_t> buf(total, 0);
    size_t used = 0;
    if (archive_write_open_memory(a, buf.data(), buf.size(), &used) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "cannot open";
        archive_write_free(a);
        return false;
    }

    for (const std::string& rel : relPaths) {
        std::vector<uint8_t> data;
        if (!readFile(root + "/" + rel, &data)) {
            if (err) *err = "cannot read " + rel;
            archive_write_free(a);
            return false;
        }
        struct archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, rel.c_str());
        archive_entry_set_size(e, static_cast<int64_t>(data.size()));
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        if (archive_write_header(a, e) != ARCHIVE_OK) {
            if (err) *err = archive_error_string(a) ? archive_error_string(a) : "header";
            archive_entry_free(e);
            archive_write_free(a);
            return false;
        }
        if (!data.empty() &&
            archive_write_data(a, data.data(), data.size()) < 0) {
            if (err) *err = archive_error_string(a) ? archive_error_string(a) : "write";
            archive_entry_free(e);
            archive_write_free(a);
            return false;
        }
        archive_entry_free(e);
    }
    if (archive_write_close(a) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "close";
        archive_write_free(a);
        return false;
    }
    archive_write_free(a);
    out->assign(buf.begin(), buf.begin() + static_cast<long>(used));
    return true;
}

bool readZip(const std::vector<uint8_t>& data, std::vector<ZipFile>* out, std::string* err) {
    out->clear();
    struct archive* a = archive_read_new();
    if (!a) { if (err) *err = "archive_read_new failed"; return false; }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_memory(a, const_cast<uint8_t*>(data.data()), data.size()) !=
        ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "cannot open";
        archive_read_free(a);
        return false;
    }
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        const char* name = archive_entry_pathname(entry);
        if (!name || !safeRelPath(name)) {
            if (err) *err = std::string("refusing entry ") + (name ? name : "(unnamed)");
            archive_read_free(a);
            return false;
        }
        ZipFile f;
        f.relPath = name;
        char chunk[64 * 1024];
        for (;;) {
            const ssize_t n = archive_read_data(a, chunk, sizeof chunk);
            if (n == 0) break;
            if (n < 0) {
                if (err) *err = archive_error_string(a) ? archive_error_string(a) : "read";
                archive_read_free(a);
                return false;
            }
            f.bytes.insert(f.bytes.end(), chunk, chunk + n);
        }
        out->push_back(std::move(f));
    }
    archive_read_free(a);
    return true;
}

bool unzipTree(const std::vector<uint8_t>& data, const std::string& root,
               std::string* err) {
    struct archive* a = archive_read_new();
    if (!a) { if (err) *err = "archive_read_new failed"; return false; }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_memory(a, const_cast<uint8_t*>(data.data()), data.size()) !=
        ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "cannot open";
        archive_read_free(a);
        return false;
    }
    int files = 0;
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        const char* name = archive_entry_pathname(entry);
        if (!name || !safeRelPath(name)) {
            if (err) *err = std::string("refusing entry ") + (name ? name : "(unnamed)");
            archive_read_free(a);
            return false;
        }
        const std::string dest = root + "/" + name;
        if (!makeParents(dest)) {
            if (err) *err = "cannot create directories for " + std::string(name);
            archive_read_free(a);
            return false;
        }
        FILE* f = std::fopen(dest.c_str(), "wb");
        if (!f) {
            if (err) *err = "cannot write " + dest;
            archive_read_free(a);
            return false;
        }
        char chunk[64 * 1024];
        for (;;) {
            const ssize_t n = archive_read_data(a, chunk, sizeof chunk);
            if (n == 0) break;
            if (n < 0) {
                if (err) *err = archive_error_string(a) ? archive_error_string(a) : "read";
                std::fclose(f);
                archive_read_free(a);
                return false;
            }
            if (std::fwrite(chunk, 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) {
                if (err) *err = "short write to " + dest;
                std::fclose(f);
                archive_read_free(a);
                return false;
            }
        }
        std::fclose(f);
        ++files;
    }
    archive_read_free(a);
    if (files == 0) {
        if (err) *err = "archive held no files";
        return false;
    }
    return true;
}

}  // namespace cab
