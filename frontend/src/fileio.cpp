#include "fileio.h"

#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>

namespace cab {
namespace {

bool makeParents(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        const std::string part = path.substr(0, i);
        if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

}  // namespace

std::vector<uint8_t> readBytes(const std::string& path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

bool writeBytes(const std::string& path, const void* data, size_t size, mode_t mode) {
    if (!makeParents(path)) return false;
    const std::string tmp = path + ".placing";
    // A temporary left by a run that died keeps its own mode through O_TRUNC,
    // so it goes first and the new one is made with `mode`.
    ::unlink(tmp.c_str());
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;
    const char* p = static_cast<const char*>(data);
    size_t left = size;
    bool ok = true;
    while (ok && left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    ok = ok && ::fsync(fd) == 0;
    ok = ::close(fd) == 0 && ok;
    if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool writeBytes(const std::string& path, const std::vector<uint8_t>& data, mode_t mode) {
    return writeBytes(path, data.data(), data.size(), mode);
}

bool writeBytes(const std::string& path, const std::string& text, mode_t mode) {
    return writeBytes(path, text.data(), text.size(), mode);
}

bool removeFile(const std::string& path) {
    if (::unlink(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

bool syncDrive(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool ok = ::syncfs(fd) == 0;
    ::close(fd);
    return ok;
}

bool keepsSizeWithBytes(const std::string& path) {
    struct statfs s;
    if (::statfs(path.c_str(), &s) != 0) return false;
    return s.f_type == BTRFS_SUPER_MAGIC || s.f_type == EXT4_SUPER_MAGIC;
}

}  // namespace cab

std::string sanitisedStem(const std::string& title) {
    std::string out;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Anything above ASCII is passed through untouched. This is a UTF-8
        // string and those bytes are the tail of a multi-byte character: the
        // first version tested ASCII-only and turned "Pokémon" into "Pok__mon",
        // which is merely ugly — a Japanese title would have come out as
        // nothing but underscores, and a ROM library is full of those.
        if (u >= 0x80 || std::isalnum(u) || c == '-' || c == ' ' || c == '_' ||
            c == '(' || c == ')' || c == '.' || c == ',' || c == '\'') {
            out += c;
        } else {
            out += '_';
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "game" : out;
}

std::string rommKeptStem(const std::string& stem) {
    std::string out;
    for (char c : stem) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '\\' || c == '/' || c == ':' || c == '|') out += '-';
        else if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '+' ||
                 u < 0x20 || u == 0x7f)
            continue;
        else out += c;
    }
    const size_t start = out.find_first_not_of(' ');
    return start == std::string::npos ? "game" : out.substr(start);
}
