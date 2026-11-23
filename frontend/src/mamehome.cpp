// MAME 0.289's home folder; what each part is for is in mamehome.h.

#include "mamehome.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fileio.h"
#include "storage.h"

namespace mamehome {

namespace {

// `link` made to point at `target`, unless it already does. A link elsewhere
// is replaced; a real file or folder there is nobody's link, and is left as
// it is rather than lose what is in it.
bool pointAt(const std::string& link, const std::string& target) {
    struct stat st;
    if (::lstat(link.c_str(), &st) == 0) {
        if (!S_ISLNK(st.st_mode)) {
            std::fprintf(stderr, "[mame] %s is not a link, left as it is\n", link.c_str());
            return false;
        }
        char was[4096];
        const ssize_t n = ::readlink(link.c_str(), was, sizeof was - 1);
        if (n > 0 && std::string(was, static_cast<size_t>(n)) == target) return true;
        ::unlink(link.c_str());
    }
    if (::symlink(target.c_str(), link.c_str()) == 0) return true;
    std::fprintf(stderr, "[mame] could not link %s to %s: %s\n", link.c_str(), target.c_str(),
                 std::strerror(errno));
    return false;
}

}  // namespace

void prepare(const std::string& saveDir, const std::string& coreDir, const std::string& biosDir) {
    const std::string home = biosDir + "/mame";
    storage::makeDirs(home + "/ini");
    const std::string plugins = coreDir + "/mame/plugins";
    if (!storage::exists(plugins + "/hiscore/init.lua"))
        std::fprintf(stderr, "[mame] no high-score plugin at %s\n", plugins.c_str());
    pointAt(home + "/plugins", plugins);

    const std::string ini = home + "/ini/mame.ini";
    static const std::string kIni = "plugin hiscore\n";
    const std::vector<uint8_t> now = cab::readBytes(ini);
    if (std::string(now.begin(), now.end()) != kIni && !cab::writeBytes(ini, kIni))
        std::fprintf(stderr, "[mame] could not write %s\n", ini.c_str());

    const std::string scores = saveDir + "/mame/hiscore";
    storage::makeDirs(scores);
    if (pointAt(home + "/hiscore", scores))
        std::fprintf(stderr, "[mame] high scores in %s\n", scores.c_str());
}

}  // namespace mamehome
