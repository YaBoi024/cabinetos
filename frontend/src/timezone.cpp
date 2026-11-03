#include "timezone.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>

#include "unit.h"

namespace tz {
namespace {

constexpr const char* kZones = "/usr/share/zoneinfo";
constexpr const char* kLocaltime = "/etc/localtime";

// The database's first parts as a person says them; any other is shown as
// it is.
std::string regionName(const std::string& key) {
    static const std::map<std::string, std::string> kNames = {
        {"America", "Americas"},
        {"Atlantic", "Atlantic Ocean"},
        {"Indian", "Indian Ocean"},
        {"Pacific", "Pacific Ocean"},
    };
    const auto it = kNames.find(key);
    return it == kNames.end() ? key : it->second;
}

std::string spaced(std::string s) {
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}

std::vector<std::string> parts(const std::string& zone) {
    std::vector<std::string> out;
    size_t from = 0;
    for (;;) {
        const size_t slash = zone.find('/', from);
        out.push_back(zone.substr(from, slash - from));
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    return out;
}

// "America/Indiana/Knox" in the United States is "Knox, Indiana"; a middle
// part that is the country's own name ("America/Argentina/Buenos_Aires")
// says nothing the country beside it does not.
std::string cityOf(const std::string& zone, const std::string& country) {
    const std::vector<std::string> p = parts(zone);
    if (p.size() < 2) return spaced(zone);
    std::string name = spaced(p.back());
    if (p.size() == 3 && spaced(p[1]) != country) name += ", " + spaced(p[1]);
    return name;
}

std::map<std::string, std::string> countries() {
    std::map<std::string, std::string> out;
    std::ifstream in(std::string(kZones) + "/iso3166.tab");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) out[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return out;
}

// A unit's instance as systemd escapes one (systemd-escape): "/" is "-", and
// anything but a letter, a digit, "_" or ":" is "\xNN".
std::string escaped(const std::string& zone) {
    std::string out;
    for (const unsigned char c : zone) {
        if (c == '/') {
            out += '-';
        } else if (std::isalnum(c) || c == '_' || c == ':') {
            out += static_cast<char>(c);
        } else {
            char hex[8];
            std::snprintf(hex, sizeof hex, "\\x%02x", c);
            out += hex;
        }
    }
    return out;
}

}  // namespace

std::vector<Region> regions() {
    const std::map<std::string, std::string> names = countries();
    std::map<std::string, Region> byKey;
    std::set<std::string> seen;
    std::ifstream in(std::string(kZones) + "/zone.tab");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        // country code, coordinates, zone, comment; tab-separated
        std::vector<std::string> cols;
        size_t from = 0;
        for (;;) {
            const size_t tab = line.find('\t', from);
            cols.push_back(line.substr(from, tab - from));
            if (tab == std::string::npos) break;
            from = tab + 1;
        }
        if (cols.size() < 3 || cols[2].find('/') == std::string::npos) continue;
        const std::string& zone = cols[2];
        if (!seen.insert(zone).second) continue;
        const auto c = names.find(cols[0]);
        const std::string country = c == names.end() ? cols[0] : c->second;
        Region& r = byKey[regionKey(zone)];
        r.key = regionKey(zone);
        r.name = regionName(r.key);
        r.cities.push_back({zone, cityOf(zone, country), country});
    }
    auto byName = [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
                return std::tolower(x) < std::tolower(y);
            });
    };
    std::vector<Region> out;
    for (auto& [key, r] : byKey) {
        std::sort(r.cities.begin(), r.cities.end(),
                  [&](const City& a, const City& b) { return byName(a.name, b.name); });
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(),
              [&](const Region& a, const Region& b) { return byName(a.name, b.name); });
    return out;
}

std::string current() {
    char link[PATH_MAX];
    const ssize_t n = ::readlink(kLocaltime, link, sizeof link - 1);
    if (n <= 0) return {};
    const std::string target(link, static_cast<size_t>(n));
    const size_t at = target.find("zoneinfo/");
    return at == std::string::npos ? std::string() : target.substr(at + 9);
}

bool isSet() {
    // lstat, as the root helper looks: a link that leads nowhere is still a
    // zone somebody set, and not one first run may replace.
    struct stat st;
    return ::lstat(kLocaltime, &st) == 0;
}

std::string regionKey(const std::string& zone) { return zone.substr(0, zone.find('/')); }

bool choose(const std::string& zone, std::string* why) {
    const std::string name = "cabinetos-timezone@" + escaped(zone) + ".service";
    std::string err;
    if (!unit::start(name.c_str(), &err)) {
        std::fprintf(stderr, "[timezone] %s refused: %s\n", name.c_str(), err.c_str());
        if (why) *why = "Couldn't set the time zone.";
        return false;
    }
    const bool set = unit::awaitOneshot(name.c_str(), 10.0, [&] { return current() == zone; });
    std::fprintf(stderr, "[timezone] %s %s\n", zone.c_str(), set ? "set" : "not set");
    if (!set && why) *why = "Couldn't set the time zone.";
    return set;
}

void follow(const std::string& zone) {
    // ":" and a name: a file in the database, as POSIX spells it. A new name
    // is what makes glibc read the zone again.
    ::setenv("TZ", (":" + zone).c_str(), 1);
    ::tzset();
}

}  // namespace tz
