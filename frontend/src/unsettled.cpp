#include "unsettled.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include "fileio.h"
#include "storage.h"

namespace unsettled {
namespace {

// NEVER DESTROYED: a settle thread can still be waiting on a drive while the
// program exits and its statics go.
std::mutex& lock() {
    static auto* m = new std::mutex;
    return *m;
}

// How many lists each game has begun. A settle deletes its list only while
// no later begin has taken it over.
std::map<int, unsigned>& generations() {
    static auto* g = new std::map<int, unsigned>;
    return *g;
}

struct List {
    int romId = 0;   // 0: none begun
    unsigned generation = 0;
    std::string file;
    std::string boot;
    std::vector<std::string> paths;
};

List& current() {
    static auto* l = new List;
    return *l;
}

std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

// The lines a write finished. A power cut can leave a last line cut short,
// and a cut path could name some other file, so a line counts only with its
// newline.
std::vector<std::string> wholeLines(const std::string& file) {
    const std::vector<uint8_t> bytes = cab::readBytes(file);
    std::vector<std::string> out;
    std::string line;
    for (uint8_t b : bytes) {
        if (b != '\n') {
            line += static_cast<char>(b);
            continue;
        }
        out.push_back(line);
        line.clear();
    }
    return out;
}

// Adds lines to the end of the list and returns once they are on the drive.
// Appended rather than rewritten: a folder game of thousands of files on an
// exFAT drive notes each file as it lands. A new list's folder is synced too,
// so the list is found after a power cut.
bool append(const std::string& file, const std::string& text, bool created) {
    const int fd = ::open(file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    const bool ok = done == text.size() && ::fdatasync(fd) == 0;
    ::close(fd);
    if (!ok || !created) return ok;
    const int dir = ::open(folderOf(file).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return false;
    const bool synced = ::fsync(dir) == 0;
    ::close(dir);
    return synced;
}

}  // namespace

void begin(int romId, const std::string& boot) {
    std::lock_guard<std::mutex> g(lock());
    List l;
    l.romId = romId;
    l.generation = ++generations()[romId];
    l.file = storage::configDir() + "/unsettled/" + std::to_string(romId);
    l.boot = boot;
    const std::vector<std::string> lines = wholeLines(l.file);
    if (!lines.empty() && lines[0] == boot) {
        l.paths.assign(lines.begin() + 1, lines.end());
    } else {
        for (size_t i = 1; i < lines.size(); ++i)
            if (::unlink(lines[i].c_str()) == 0)
                std::fprintf(stderr, "[unsettled] %s may not be whole after a power cut; "
                                     "removed\n", lines[i].c_str());
        cab::removeFile(l.file);
    }
    current() = std::move(l);
}

bool note(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> g(lock());
    List& l = current();
    if (l.romId == 0) return false;
    std::string text;
    const bool created = l.paths.empty();
    if (created) {
        if (!storage::makeDirs(folderOf(l.file))) return false;
        cab::removeFile(l.file);   // one of this boot with no path in it yet
        text = l.boot + "\n";
    }
    std::vector<std::string> added;
    for (const std::string& p : paths) {
        if (std::find(l.paths.begin(), l.paths.end(), p) != l.paths.end()) continue;
        if (std::find(added.begin(), added.end(), p) != added.end()) continue;
        added.push_back(p);
        text += p + "\n";
    }
    if (added.empty()) return true;
    if (!append(l.file, text, created)) return false;
    l.paths.insert(l.paths.end(), added.begin(), added.end());
    return true;
}

void settle() {
    List l;
    {
        std::lock_guard<std::mutex> g(lock());
        l = std::move(current());
        current() = List{};
    }
    if (l.romId == 0 || l.paths.empty()) return;
    std::thread([l = std::move(l)] {
        // Each folder's drive once; a second folder on a drive already
        // synced costs nothing.
        std::set<std::string> folders;
        for (const std::string& p : l.paths) folders.insert(folderOf(p));
        bool all = true;
        for (const std::string& f : folders) {
            struct stat st;
            if (::stat(f.c_str(), &st) != 0) continue;   // removed since: nothing to wait for
            all = cab::syncDrive(f) && all;
        }
        std::lock_guard<std::mutex> g(lock());
        if (!all || generations()[l.romId] != l.generation) return;
        cab::removeFile(l.file);
    }).detach();
}

}  // namespace unsettled
