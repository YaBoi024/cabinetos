#include "timewords.h"

#include <json-c/json.h>

#include <cstdio>

#include "storage.h"

// A file time the way RomM writes one, so the two sort and read alike.
std::string isoUtc(time_t t) {
    struct tm g;
    gmtime_r(&t, &g);
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S+00:00", &g);
    return buf;
}

int64_t isoMicros(const std::string& iso) {
    int Y, M, D, h, m, sec, read = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d%n", &Y, &M, &D, &h, &m, &sec, &read) != 6)
        return 0;
    struct tm utc = {};
    utc.tm_year = Y - 1900;
    utc.tm_mon = M - 1;
    utc.tm_mday = D;
    utc.tm_hour = h;
    utc.tm_min = m;
    utc.tm_sec = sec;
    int64_t micros = static_cast<int64_t>(timegm(&utc)) * 1000000;
    size_t at = static_cast<size_t>(read);
    if (at < iso.size() && iso[at] == '.') {
        int64_t unit = 100000;
        for (++at; at < iso.size() && iso[at] >= '0' && iso[at] <= '9' && unit > 0; ++at) {
            micros += (iso[at] - '0') * unit;
            unit /= 10;
        }
    }
    return micros;
}

// A GAME'S YEAR, MAKER AND PLAYERS, kept for the page offline (#88), beside
// the game's record: `games/<romId>.facts.json`.
void rememberFacts(int romId, const romm::Client::Facts& f) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "year", json_object_new_int(f.year));
    json_object_object_add(o, "maker", json_object_new_string(f.maker.c_str()));
    json_object_object_add(o, "players", json_object_new_string(f.players.c_str()));
    storage::makeDirs(storage::gamesDir());
    const std::string path = storage::gamesDir() + "/" + std::to_string(romId) + ".facts.json";
    const std::string tmp = path + ".part";
    if (json_object_to_file(tmp.c_str(), o) == 0) ::rename(tmp.c_str(), path.c_str());
    json_object_put(o);
}

bool knownFacts(int romId, romm::Client::Facts* f) {
    json_object* o = json_object_from_file(
        (storage::gamesDir() + "/" + std::to_string(romId) + ".facts.json").c_str());
    if (!o) return false;
    json_object* v = nullptr;
    if (json_object_object_get_ex(o, "year", &v)) f->year = json_object_get_int(v);
    if (json_object_object_get_ex(o, "maker", &v)) f->maker = json_object_get_string(v);
    if (json_object_object_get_ex(o, "players", &v)) f->players = json_object_get_string(v);
    json_object_put(o);
    return true;
}

// WHEN A STATE WAS SAVED, as the launch screen says it: "Today, 8:13 PM",
// "Yesterday, 8:13 PM", "Sep 23, 8:13 PM", with the year only when it is not
// this one. From RomM's `updated_at`, which is UTC, into this console's time.
std::string stateWhen(const std::string& iso) {
    const int64_t micros = isoMicros(iso);
    if (micros == 0) return "";
    const time_t t = static_cast<time_t>(micros / 1000000);
    struct tm at, now;
    localtime_r(&t, &at);
    const time_t n = time(nullptr);
    localtime_r(&n, &now);
    char clock[32];
    std::strftime(clock, sizeof clock, "%l:%M %p", &at);
    const char* c = clock;
    while (*c == ' ') ++c;
    struct tm yday = now;
    yday.tm_mday -= 1;
    mktime(&yday);
    auto same = [](const struct tm& a, const struct tm& b) {
        return a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
    };
    char day[32];
    if (same(at, now)) std::snprintf(day, sizeof day, "Today");
    else if (same(at, yday)) std::snprintf(day, sizeof day, "Yesterday");
    else if (at.tm_year == now.tm_year) std::strftime(day, sizeof day, "%b %e", &at);
    else std::strftime(day, sizeof day, "%b %e %Y", &at);
    std::string d = day;
    for (size_t i; (i = d.find("  ")) != std::string::npos;) d.erase(i, 1);
    return d + ", " + c;
}
