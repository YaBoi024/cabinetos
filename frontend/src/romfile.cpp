#include "romfile.h"

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "dirsave.h"
#include "storage.h"

namespace romfile {
namespace {

bool starts(const std::vector<uint8_t>& b, const char* magic, size_t n, size_t at = 0) {
    if (b.size() < at + n) return false;
    return std::memcmp(b.data() + at, magic, n) == 0;
}

// Lowercased text after the last dot, or empty. Used ONLY to match a member of
// an archive against a core's valid_extensions — never to decide what the
// downloaded payload is.
std::string extensionOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return {};
    std::string e = name.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

bool coreAccepts(const std::string& validExtensions, const std::string& ext) {
    if (ext.empty() || validExtensions.empty()) return false;
    size_t start = 0;
    while (start <= validExtensions.size()) {
        const size_t bar = validExtensions.find('|', start);
        const size_t end = (bar == std::string::npos) ? validExtensions.size() : bar;
        if (validExtensions.compare(start, end - start, ext) == 0) return true;
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return false;
}

}  // namespace

Kind sniff(const std::vector<uint8_t>& b) {
    // Order matters only in that the native-container checks come first: being
    // wrong about those is the expensive mistake, because unpacking one
    // produces something no core can open.
    if (starts(b, "MComprHD", 8)) return Kind::Chd;
    if (starts(b, "RVZ\x01", 4)) return Kind::Rvz;

    // "PK\x03\x04" is a normal archive; the other two are an empty archive and
    // a spanned one, and neither is something to try to read a ROM out of.
    if (starts(b, "PK\x03\x04", 4)) return Kind::Zip;
    if (starts(b, "PK\x05\x06", 4) || starts(b, "PK\x07\x08", 4)) return Kind::Zip;
    if (starts(b, "7z\xBC\xAF\x27\x1C", 6)) return Kind::SevenZip;
    if (starts(b, "Rar!\x1A\x07", 6)) return Kind::Rar;
    if (starts(b, "\x1F\x8B", 2)) return Kind::Gzip;
    // tar keeps its magic 257 bytes in, which is why a tar cannot be
    // recognised from a short prefix.
    if (starts(b, "ustar", 5, 257)) return Kind::Tar;

    // A web page where a ROM should be. Checked LAST, so nothing that is a
    // real container can fall into it, and matched only on the two openings
    // that cannot be anything else — a ROM starting with `<!DOCTYPE` or
    // `<html` is not a file anyone has. Leading whitespace and a UTF-8 byte
    // order mark are skipped, because a served error page often carries them.
    {
        size_t i = 0;
        if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) i = 3;
        while (i < b.size() && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n'))
            ++i;
        auto at = [&](const char* lit) {
            const size_t n = std::strlen(lit);
            if (i + n > b.size()) return false;
            for (size_t k = 0; k < n; ++k) {
                const unsigned char c = b[i + k];
                const char want = lit[k];
                const char got = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                                                        : static_cast<char>(c);
                if (got != want) return false;
            }
            return true;
        };
        if (at("<!doctype") || at("<html")) return Kind::NotAGame;
    }
    return Kind::Plain;
}

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Zip: return "zip";
        case Kind::SevenZip: return "7z";
        case Kind::Rar: return "rar";
        case Kind::Tar: return "tar";
        case Kind::Gzip: return "gzip";
        case Kind::Chd: return "chd";
        case Kind::Rvz: return "rvz";
        case Kind::NotAGame: return "a web page";
        default: return "plain";
    }
}

bool isContainer(Kind k) {
    return k == Kind::Zip || k == Kind::SevenZip || k == Kind::Rar ||
           k == Kind::Tar || k == Kind::Gzip;
}

bool extractAll(const std::vector<uint8_t>& in, std::vector<Member>* out,
                std::string* err) {
    out->clear();
    struct archive* a = archive_read_new();
    if (!a) { if (err) *err = "archive_read_new failed"; return false; }
    // Everything libarchive knows, rather than a list of formats we guessed
    // people would have. The point of the library is not having that list.
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_memory(a, const_cast<uint8_t*>(in.data()), in.size()) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "cannot open";
        archive_read_free(a);
        return false;
    }

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;   // skip directories
        const char* name = archive_entry_pathname(entry);
        Member m;
        m.name = name ? name : "";

        // la_int64 size can be -1 when the format does not record it up front,
        // so this reads until EOF rather than trusting it.
        for (;;) {
            char buf[64 * 1024];
            const ssize_t n = archive_read_data(a, buf, sizeof buf);
            if (n < 0) {
                if (err) *err = archive_error_string(a) ? archive_error_string(a)
                                                       : "read failed";
                archive_read_free(a);
                return false;
            }
            if (n == 0) break;
            m.bytes.insert(m.bytes.end(), buf, buf + n);
        }
        out->push_back(std::move(m));
    }

    archive_read_free(a);
    return true;
}

Kind sniffFile(const std::string& path, std::string* err) {
    // 512 is enough for every signature here, including tar's, which sits 257
    // bytes in. Reading more of a multi-gigabyte file to identify it would
    // defeat the point of not holding it.
    std::vector<uint8_t> head(512);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "cannot open " + path; return Kind::Plain; }
    const size_t n = std::fread(head.data(), 1, head.size(), f);
    std::fclose(f);
    head.resize(n);
    return sniff(head);
}

namespace {

// Hand it over as it stands: the core asked to open its own archives, or it
// is not a container, or it is one this core reads natively — a .chd being
// the case that matters, since unpacking one produces something no core can
// load.
bool handsOver(Kind k, const std::string& validExtensions, bool blockExtract) {
    return blockExtract || !isContainer(k) || coreAccepts(validExtensions, kindName(k));
}

// AN ARCHIVE'S MEMBERS BY prepareFile's RULES, in one walk that
// alreadyUnpacked takes too, so the check looks for exactly the files the
// unpacking writes and points the core at the same one (#530). `member(a,
// entry, out)` is given each file to write, at `out` inside outDir, and
// returns the size that file is, or -1 to stop the walk as a failure with its
// reason in *err. Returns the file the core is pointed at: the first member
// whose name has an extension the core lists, else the largest. Empty on a
// failure.
template <typename Member>
std::string eachMember(const std::string& path, Kind k, const std::string& outDir,
                       const std::string& validExtensions, std::string* err,
                       Member member) {
    struct archive* a = archive_read_new();
    if (!a) { if (err) *err = "archive_read_new failed"; return {}; }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path.c_str(), 256 * 1024) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "cannot open";
        archive_read_free(a);
        return {};
    }

    std::string best;       // the member the core named
    std::string biggest;    // fallback, for a set whose members have no extension
    int64_t biggestSize = -1;
    bool any = false;

    // Every name written so far, lowercased, so a name that comes twice fails
    // the unpacking rather than writing over the first: a games drive is
    // usually exFAT, where two names differing only in case are one file. The
    // archive itself is in the list when it sits in outDir, so no member can
    // be written over the file being read.
    auto folded = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    std::set<std::string> names;
    if (path.compare(0, outDir.size() + 1, outDir + "/") == 0)
        names.insert(folded(path.substr(outDir.size() + 1)));

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        const char* raw = archive_entry_pathname(entry);
        if (!raw) continue;

        // EACH MEMBER KEEPS ITS FOLDERS (#482, FILES-07). Only the file name
        // was kept until 2026-10-19, so two discs' `track 1.bin` in two
        // folders overwrote each other, and a cue sheet naming its tracks in
        // a folder beside it found none. An archive is untrusted input, so a
        // name that is absolute or has a ".." part refuses the whole archive
        // (cab::safeRelPath, the rule a folder save is unpacked by), and
        // "a//x" and "./a/x" are written as the "a/x" they mean.
        if (!cab::safeRelPath(raw)) {
            if (err) *err = std::string("refusing member ") + raw;
            archive_read_free(a);
            return {};
        }
        const std::string name = raw;
        std::string rel;
        for (size_t start = 0; start <= name.size();) {
            const size_t slash = name.find('/', start);
            const size_t end = slash == std::string::npos ? name.size() : slash;
            const std::string part = name.substr(start, end - start);
            if (!part.empty() && part != ".") rel += (rel.empty() ? "" : "/") + part;
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        if (rel.empty()) continue;
        if (!names.insert(folded(rel)).second) {
            if (err) *err = std::string("the ") + kindName(k) + " holds " + rel + " twice";
            archive_read_free(a);
            return {};
        }

        const std::string out = outDir + "/" + rel;
        const int64_t size = member(a, entry, out);
        if (size < 0) {
            archive_read_free(a);
            return {};
        }
        any = true;

        // By the file's own name: a dot in a folder's name is not an extension.
        if (best.empty() &&
            coreAccepts(validExtensions, extensionOf(rel.substr(rel.find_last_of('/') + 1))))
            best = out;
        if (size > biggestSize) { biggestSize = size; biggest = out; }
    }
    archive_read_free(a);

    if (!any) {
        if (err) *err = std::string("the ") + kindName(k) + " held no files";
        return {};
    }
    return best.empty() ? biggest : best;
}

}  // namespace

int64_t unpackedSize(const std::string& path, const std::string& validExtensions,
                     bool blockExtract) {
    // Nothing will be written, so nothing is needed. The same three conditions
    // prepareFile uses to hand a file over untouched.
    const Kind k = sniffFile(path, nullptr);
    if (handsOver(k, validExtensions, blockExtract)) return 0;

    struct archive* a = archive_read_new();
    if (!a) return 0;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path.c_str(), 256 * 1024) != ARCHIVE_OK) {
        archive_read_free(a);
        return 0;
    }

    // Reading HEADERS only, never data — the size is in the archive's own index
    // and costs a seek rather than a decompression.
    int64_t total = 0;
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        // A format that streams may not declare a size. Zero then means "I do
        // not know" rather than "empty", and the honest answer is to say so
        // rather than to invent a ratio: the caller extracts into whatever room
        // there is and fails if it runs out.
        if (!archive_entry_size_is_set(entry)) {
            archive_read_free(a);
            return 0;
        }
        total += archive_entry_size(entry);
    }
    archive_read_free(a);
    return total;
}

bool prepareFile(const std::string& downloadedPath, const std::string& outDir,
                 const std::string& validExtensions, bool blockExtract,
                 std::string* primaryPath, Kind* kindOut, std::string* err,
                 const std::atomic<bool>& cancel) {
    const Kind k = sniffFile(downloadedPath, err);
    if (kindOut) *kindOut = k;
    if (handsOver(k, validExtensions, blockExtract)) {
        *primaryPath = downloadedPath;
        return true;
    }

    // EACH FILE WRITTEN TAKES THE ARCHIVE'S OWN TIME (#530), after it is
    // closed, because a buffered write at the close sets the time again. That
    // time and the file's size are what alreadyUnpacked looks for. A file cut
    // off (a cancel, a failed read or write) or whose close failed is never
    // stamped, so the next start unpacks it again.
    struct stat from;
    const bool stamp = ::stat(downloadedPath.c_str(), &from) == 0;
    const std::string primary = eachMember(downloadedPath, k, outDir, validExtensions, err,
        [&](struct archive* a, struct archive_entry*, const std::string& out) -> int64_t {
            if (!storage::makeDirs(out.substr(0, out.find_last_of('/')))) {
                if (err) *err = "cannot make the folder for " + out;
                return -1;
            }
            FILE* f = std::fopen(out.c_str(), "wb");
            if (!f) { if (err) *err = "cannot write " + out; return -1; }

            int64_t written = 0;
            std::vector<char> buf(256 * 1024);
            for (;;) {
                if (cancel.load()) {
                    std::fclose(f);
                    if (err) *err = "cancelled";
                    return -1;
                }
                const ssize_t got = archive_read_data(a, buf.data(), buf.size());
                if (got < 0) {
                    std::fclose(f);
                    if (err)
                        *err = archive_error_string(a) ? archive_error_string(a) : "read failed";
                    return -1;
                }
                if (got == 0) break;
                if (std::fwrite(buf.data(), 1, static_cast<size_t>(got), f) !=
                    static_cast<size_t>(got)) {
                    std::fclose(f);
                    if (err) *err = "short write to " + out;
                    return -1;
                }
                written += got;
            }
            if (std::fclose(f) == 0 && stamp) {
                const struct timespec when[2] = {{0, UTIME_OMIT}, from.st_mtim};
                ::utimensat(AT_FDCWD, out.c_str(), when, 0);
            }
            return written;
        });
    if (primary.empty()) return false;
    *primaryPath = primary;
    return true;
}

bool alreadyUnpacked(const std::string& downloadedPath, const std::string& outDir,
                     const std::string& validExtensions, bool blockExtract,
                     std::string* primaryPath) {
    struct stat from;
    if (::stat(downloadedPath.c_str(), &from) != 0) return false;
    const Kind k = sniffFile(downloadedPath, nullptr);
    if (handsOver(k, validExtensions, blockExtract)) return false;
    // The first member not as the unpacking left it ends the walk, so a game
    // never unpacked costs one header.
    const std::string primary = eachMember(downloadedPath, k, outDir, validExtensions, nullptr,
        [&from](struct archive*, struct archive_entry* e, const std::string& out) -> int64_t {
            struct stat st;
            if (!archive_entry_size_is_set(e) || ::lstat(out.c_str(), &st) != 0 ||
                !S_ISREG(st.st_mode) || st.st_size != archive_entry_size(e) ||
                st.st_mtim.tv_sec != from.st_mtim.tv_sec ||
                st.st_mtim.tv_nsec != from.st_mtim.tv_nsec)
                return -1;
            return st.st_size;
        });
    if (primary.empty()) return false;
    *primaryPath = primary;
    return true;
}

bool accepts(const std::string& validExtensions, const std::string& fileName) {
    return coreAccepts(validExtensions, extensionOf(fileName));
}

bool prepare(const std::vector<uint8_t>& downloaded, const std::string& validExtensions,
             bool blockExtract, Prepared* out, std::string* err) {
    out->members.clear();
    out->primary = -1;
    out->kind = sniff(downloaded);

    // The core asked for the archive itself. Some read their own, and a core
    // knows better than we do.
    if (blockExtract) { out->passThrough = true; return true; }

    // Not a container at all — a plain ROM, a .chd, a .rvz.
    if (!isContainer(out->kind)) { out->passThrough = true; return true; }

    // A container the core reads natively. genesis_plus_gx lists `chd`; some
    // cores list `zip`. Unpacking it would be actively wrong.
    if (coreAccepts(validExtensions, kindName(out->kind))) {
        out->passThrough = true;
        return true;
    }

    if (!extractAll(downloaded, &out->members, err)) return false;
    if (out->members.empty()) {
        if (err) *err = std::string("the ") + kindName(out->kind) + " held no files";
        return false;
    }

    // The member the core said it wants.
    for (size_t i = 0; i < out->members.size(); ++i) {
        if (coreAccepts(validExtensions, extensionOf(out->members[i].name))) {
            out->primary = static_cast<int>(i);
            break;
        }
    }
    // Nothing matched. Rather than fail, take the largest file: a set whose
    // members have no extension is a real thing — 32 of them in the reference
    // library — and the ROM is reliably the biggest thing in the archive
    // beside its own metadata. Reported by the caller, never silent.
    if (out->primary < 0) {
        size_t best = 0;
        for (size_t i = 1; i < out->members.size(); ++i)
            if (out->members[i].bytes.size() > out->members[best].bytes.size()) best = i;
        out->primary = static_cast<int>(best);
    }
    out->passThrough = false;
    return true;
}

std::vector<uint8_t> unzstd(const std::vector<uint8_t>& in, size_t limit) {
    std::vector<uint8_t> out;
    archive* a = archive_read_new();
    archive_read_support_filter_zstd(a);
    archive_read_support_format_raw(a);
    archive_entry* e = nullptr;
    if (archive_read_open_memory(a, in.data(), in.size()) == ARCHIVE_OK &&
        archive_read_next_header(a, &e) == ARCHIVE_OK) {
        uint8_t buf[16384];
        for (la_ssize_t n; out.size() < limit && (n = archive_read_data(a, buf, sizeof buf)) > 0;)
            out.insert(out.end(), buf, buf + n);
    }
    archive_read_free(a);
    return out;
}

}  // namespace romfile
