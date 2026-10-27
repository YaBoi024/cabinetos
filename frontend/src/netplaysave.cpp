// FORK (handheld): see netplaysave.h.

#include "netplaysave.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace netplaysave {
namespace {

constexpr size_t kMaxFiles = 64;
constexpr uint64_t kMaxTotal = 16ull * 1024 * 1024;
const char* const kBackup = "previous-together";

bool endsWith(const std::string& s, const char* tail) {
    const size_t n = std::char_traits<char>::length(tail);
    return s.size() > n && s.compare(s.size() - n, n, tail) == 0;
}

// A name this module would have listed: one plain file name at the top that
// ends .srm or .rtc, or one under cardRoot that ends .gci.
bool allowed(const std::string& name, const std::string& cardRoot) {
    if (name.empty() || name.size() > 200) return false;
    if (name.find("..") != std::string::npos || name.find('\0') != std::string::npos ||
        name[0] == '/' || name.find('\\') != std::string::npos)
        return false;
    const size_t slash = name.find('/');
    if (slash == std::string::npos) return endsWith(name, ".srm") || endsWith(name, ".rtc");
    if (cardRoot.empty()) return false;
    const std::string prefix = cardRoot + "/";
    return name.compare(0, prefix.size(), prefix) == 0 &&
           name.find('/', prefix.size()) == std::string::npos && endsWith(name, ".gci");
}

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool writeAll(const fs::path& p, const uint8_t* data, size_t n) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}

// name -> path, sorted by name.
std::map<std::string, fs::path> listFiles(const std::string& dir, const std::string& cardRoot) {
    std::map<std::string, fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (allowed(name, cardRoot)) out[name] = e.path();
    }
    if (!cardRoot.empty()) {
        for (const auto& e : fs::directory_iterator(fs::path(dir) / cardRoot, ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string name = cardRoot + "/" + e.path().filename().string();
            if (allowed(name, cardRoot)) out[name] = e.path();
        }
    }
    return out;
}

void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<uint8_t>(v >> s));
}

}  // namespace

std::string fingerprint(const std::string& dir, const std::string& cardRoot) {
    const auto files = listFiles(dir, cardRoot);
    if (files.empty()) return {};
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    };
    for (const auto& [name, path] : files) {
        const std::vector<uint8_t> data = readAll(path);
        const uint64_t size = data.size();
        mix(name.data(), name.size());
        mix(&size, sizeof size);
        mix(data.data(), data.size());
    }
    char out[24];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
    return out;
}

std::vector<uint8_t> pack(const std::string& dir, const std::string& cardRoot) {
    const auto files = listFiles(dir, cardRoot);
    std::vector<uint8_t> out;
    if (files.empty() || files.size() > kMaxFiles) return out;
    out.push_back(static_cast<uint8_t>(files.size()));
    for (const auto& [name, path] : files) {
        const std::vector<uint8_t> data = readAll(path);
        out.push_back(static_cast<uint8_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        put32(out, static_cast<uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        if (out.size() > kMaxTotal) return {};
    }
    return out;
}

bool hasBattery(const std::string& dir) {
    for (const auto& [name, path] : listFiles(dir, "")) {
        (void)path;
        if (endsWith(name, ".srm")) return true;
    }
    return false;
}

bool unpack(const std::string& dir, const std::string& cardRoot, const std::vector<uint8_t>& blob,
            std::string* why, Unpacked* what) {
    auto bad = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    // Read the whole blob before changing anything.
    std::map<std::string, std::vector<uint8_t>> incoming;
    if (!blob.empty()) {
        size_t at = 0;
        auto left = [&] { return blob.size() - at; };
        const size_t count = blob[at++];
        if (count == 0 || count > kMaxFiles) return bad("the saved game has a bad file count");
        for (size_t i = 0; i < count; ++i) {
            if (left() < 1) return bad("the saved game is cut short");
            const size_t n = blob[at++];
            if (left() < n + 4) return bad("the saved game is cut short");
            const std::string name(reinterpret_cast<const char*>(&blob[at]), n);
            at += n;
            const uint32_t size = (static_cast<uint32_t>(blob[at]) << 24) | (blob[at + 1] << 16) |
                                  (blob[at + 2] << 8) | blob[at + 3];
            at += 4;
            if (!allowed(name, cardRoot)) return bad("the saved game names a file it should not");
            if (size > left()) return bad("the saved game is cut short");
            if (incoming.count(name)) return bad("the saved game names a file twice");
            incoming[name].assign(blob.begin() + static_cast<long>(at),
                                  blob.begin() + static_cast<long>(at + size));
            at += size;
        }
        if (left() != 0) return bad("the saved game has bytes left over");
    }
    // The joiner's own set moves aside, then the new one goes in.
    const fs::path backup = fs::path(dir) / kBackup;
    std::error_code ec;
    const auto current = listFiles(dir, cardRoot);
    if (!current.empty()) {
        fs::remove_all(backup, ec);
        for (const auto& [name, path] : current) {
            fs::create_directories((backup / name).parent_path(), ec);
            fs::rename(path, backup / name, ec);
            if (ec) return bad("could not set the old saved game aside");
        }
    }
    Unpacked got;
    for (const auto& [name, data] : incoming) {
        if (!writeAll(fs::path(dir) / name, data.data(), data.size()))
            return bad("could not write the saved game");
        if (endsWith(name, ".srm") && got.battery.empty()) got.battery = name;
        if (endsWith(name, ".gci")) got.card = true;
    }
    if (what) *what = got;
    return true;
}

// ---- --netplay-save-test ----------------------------------------------------

int test() {
    int bad = 0;
    auto check = [&bad](bool ok, const char* what) {
        std::fprintf(stderr, "[save-test] %-62s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++bad;
    };
    const fs::path root = fs::temp_directory_path() / "cabinetos-netplaysave-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::string card = "User/GC/USA/Card A";
    const std::string a = (root / "a").string(), b = (root / "b").string();
    fs::create_directories(a, ec);
    fs::create_directories(b, ec);

    check(fingerprint(a, card).empty() && pack(a, card).empty(), "a folder with no saved game has no fingerprint");
    const std::vector<uint8_t> sram(2000, 0x7E);
    writeAll(fs::path(a) / "Game.srm", sram.data(), sram.size());
    writeAll(fs::path(a) / "Dolphin.ini", sram.data(), 10);            // not part of it
    writeAll(fs::path(a) / card / "01-GALE-x.gci", sram.data(), 300);
    const std::string fa = fingerprint(a, card);
    check(!fa.empty(), "a saved game has a fingerprint");
    writeAll(fs::path(a) / "Other.ini", sram.data(), 5);
    check(fingerprint(a, card) == fa, "files that are not saves do not change it");

    const std::vector<uint8_t> blob = pack(a, card);
    check(!blob.empty(), "a saved game packs");
    writeAll(fs::path(b) / "Old.srm", sram.data(), 50);
    std::string why;
    check(unpack(b, card, blob, &why), "the other side unpacks it");
    check(fingerprint(b, card) == fa, "both folders then have the same fingerprint");
    check(fs::exists(fs::path(b) / kBackup / "Old.srm") && !fs::exists(fs::path(b) / "Old.srm"),
          "the joiner's own saved game is set aside, not lost");

    auto refuse = [&](std::vector<uint8_t> evil, const char* what) {
        std::string w;
        const std::string before = fingerprint(b, card);
        check(!unpack(b, card, evil, &w) && fingerprint(b, card) == before, what);
    };
    auto make = [](const std::string& name, uint32_t size) {
        std::vector<uint8_t> v{1, static_cast<uint8_t>(name.size())};
        v.insert(v.end(), name.begin(), name.end());
        put32(v, size);
        v.resize(v.size() + size, 1);
        return v;
    };
    refuse(make("../../etc/x.srm", 4), "a name that climbs out of the folder is refused");
    refuse(make("/tmp/x.srm", 4), "an absolute name is refused");
    refuse(make("x.sh", 4), "a name that is not a saved game is refused");
    refuse(make("sub/x.srm", 4), "a folder that is not the card folder is refused");
    {
        std::vector<uint8_t> cut = make("Game.srm", 40);
        cut.resize(cut.size() - 10);
        refuse(cut, "a blob cut short is refused");
        std::vector<uint8_t> extra = make("Game.srm", 4);
        extra.push_back(9);
        refuse(extra, "a blob with bytes left over is refused");
    }
    check(unpack(b, card, {}, &why) && fingerprint(b, card).empty(),
          "an empty blob leaves the joiner with no saved game, the old one set aside");
    fs::remove_all(root, ec);
    std::fprintf(stderr, "[save-test] %s\n", bad == 0 ? "PASS" : "FAIL");
    return bad == 0 ? 0 : 1;
}

}  // namespace netplaysave
