#include "playtime.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <mutex>

#include <unistd.h>

#include <json-c/json.h>

#include "fileio.h"

namespace playtime {

// ---- The clock ------------------------------------------------------------

void Clock::begin(int romId, int64_t nowMs) {
    romId_ = romId;
    startMs_ = nowMs;
    kept_ = 0.0;
    sinceTouch_ = 0.0;
}

void Clock::tick(float dt, bool running) {
    if (active() && running) sinceTouch_ += dt;
}

void Clock::touch() {
    if (!active()) return;
    // A press after a long absence keeps what came before the absence and
    // drops the absence itself.
    if (sinceTouch_ < kUnwatchedAfter) kept_ += sinceTouch_;
    sinceTouch_ = 0.0;
}

double Clock::counted() const {
    return kept_ + (sinceTouch_ < kUnwatchedAfter ? sinceTouch_ : 0.0);
}

Session Clock::now(int64_t nowMs) const {
    Session s;
    s.romId = romId_;
    s.startMs = startMs_;
    s.endMs = nowMs;
    s.playedMs = static_cast<int64_t>(counted() * 1000.0);
    // The clock can only ever be slower than the wall; a wall clock set back
    // mid-game must not make RomM refuse the session.
    if (s.endMs < s.startMs + s.playedMs) s.endMs = s.startMs + s.playedMs;
    return s;
}

Session Clock::finish(int64_t nowMs) {
    const Session s = now(nowMs);
    romId_ = 0;
    kept_ = sinceTouch_ = 0.0;
    return s;
}

int64_t wallMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string iso(int64_t ms) {
    const time_t t = static_cast<time_t>(ms / 1000);
    struct tm g;
    if (!gmtime_r(&t, &g)) return {};
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &g);
    return buf;
}

std::string describe(int64_t ms) {
    if (ms < kShortestMs) return {};
    const int64_t minutes = ms / 60000;
    if (minutes < 60)
        return std::to_string(minutes) + (minutes == 1 ? " minute played" : " minutes played");
    const int64_t hours = minutes / 60;
    return std::to_string(hours) + (hours == 1 ? " hour played" : " hours played");
}

// ---- The record on disk ---------------------------------------------------
//
//   {"current": {session}, "owed": [{session}, ...], "known": {"604": ms}}
//
// Small, and read and written whole. One lock for every person's file: the
// frame thread checkpoints and the upload worker sends, and neither does it
// often.

namespace {

std::mutex gLock;

struct Record {
    bool hasCurrent = false;
    Session current;
    std::vector<Session> owed;
    std::map<int, int64_t> known;
};

std::string pathFor(const storage::User& u) { return storage::userDir(u) + "/playtime.json"; }

int64_t jint64(json_object* o, const char* key) {
    json_object* v = nullptr;
    return json_object_object_get_ex(o, key, &v) ? json_object_get_int64(v) : 0;
}

Session readSession(json_object* o) {
    Session s;
    s.romId = static_cast<int>(jint64(o, "rom_id"));
    s.startMs = jint64(o, "start_ms");
    s.endMs = jint64(o, "end_ms");
    s.playedMs = jint64(o, "played_ms");
    return s;
}

json_object* writeSession(const Session& s) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "rom_id", json_object_new_int(s.romId));
    json_object_object_add(o, "start_ms", json_object_new_int64(s.startMs));
    json_object_object_add(o, "end_ms", json_object_new_int64(s.endMs));
    json_object_object_add(o, "played_ms", json_object_new_int64(s.playedMs));
    return o;
}

Record load(const storage::User& u) {
    Record r;
    const std::vector<uint8_t> bytes = cab::readBytes(pathFor(u));
    if (bytes.empty()) return r;
    const std::string text(bytes.begin(), bytes.end());
    json_object* root = json_tokener_parse(text.c_str());
    if (!root) {
        std::fprintf(stderr, "[playtime] %s is not JSON; starting it again\n",
                     pathFor(u).c_str());
        return r;
    }
    json_object* v = nullptr;
    if (json_object_object_get_ex(root, "current", &v) &&
        json_object_get_type(v) == json_type_object) {
        r.current = readSession(v);
        r.hasCurrent = r.current.romId > 0;
    }
    if (json_object_object_get_ex(root, "owed", &v) &&
        json_object_get_type(v) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(v); ++i) {
            const Session s = readSession(json_object_array_get_idx(v, i));
            if (s.romId > 0) r.owed.push_back(s);
        }
    }
    if (json_object_object_get_ex(root, "known", &v) &&
        json_object_get_type(v) == json_type_object) {
        json_object_object_foreach(v, key, val) {
            r.known[std::atoi(key)] = json_object_get_int64(val);
        }
    }
    json_object_put(root);
    return r;
}

void save(const storage::User& u, const Record& r) {
    json_object* root = json_object_new_object();
    if (r.hasCurrent) json_object_object_add(root, "current", writeSession(r.current));
    json_object* list = json_object_new_array();
    for (const Session& s : r.owed) json_object_array_add(list, writeSession(s));
    json_object_object_add(root, "owed", list);
    json_object* known = json_object_new_object();
    for (const auto& [rom, ms] : r.known)
        json_object_object_add(known, std::to_string(rom).c_str(), json_object_new_int64(ms));
    json_object_object_add(root, "known", known);
    const std::string text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);
    if (!cab::writeBytes(pathFor(u), std::vector<uint8_t>(text.begin(), text.end())))
        std::fprintf(stderr, "[playtime] could not write %s\n", pathFor(u).c_str());
}

void owe(Record& r, const Session& s) {
    if (s.romId <= 0 || s.playedMs < kShortestMs) return;
    r.owed.push_back(s);
}

}  // namespace

void checkpoint(const storage::User& u, const Session& s) {
    if (!u.valid() || s.romId <= 0) return;
    std::lock_guard<std::mutex> lk(gLock);
    Record r = load(u);
    r.hasCurrent = true;
    r.current = s;
    save(u, r);
}

void close(const storage::User& u, const Session& s) {
    if (!u.valid()) return;
    std::lock_guard<std::mutex> lk(gLock);
    Record r = load(u);
    r.hasCurrent = false;
    owe(r, s);
    save(u, r);
    std::fprintf(stderr, "[playtime] rom %d: %lld s counted of %lld s%s\n", s.romId,
                 static_cast<long long>(s.playedMs / 1000),
                 static_cast<long long>((s.endMs - s.startMs) / 1000),
                 s.playedMs < kShortestMs ? ", under a minute, not recorded" : "");
}

void recover(const storage::User& u) {
    if (!u.valid()) return;
    std::lock_guard<std::mutex> lk(gLock);
    Record r = load(u);
    if (!r.hasCurrent) return;
    std::fprintf(stderr, "[playtime] rom %d: a session left open, %lld s, is owed now\n",
                 r.current.romId, static_cast<long long>(r.current.playedMs / 1000));
    owe(r, r.current);
    r.hasCurrent = false;
    save(u, r);
}

std::vector<Session> owed(const storage::User& u) {
    if (!u.valid()) return {};
    std::lock_guard<std::mutex> lk(gLock);
    return load(u).owed;
}

void sent(const storage::User& u, const std::vector<Session>& done) {
    if (!u.valid() || done.empty()) return;
    std::lock_guard<std::mutex> lk(gLock);
    Record r = load(u);
    for (const Session& d : done) {
        for (auto it = r.owed.begin(); it != r.owed.end(); ++it) {
            if (it->romId == d.romId && it->startMs == d.startMs) {
                r.owed.erase(it);
                break;
            }
        }
        // Only onto a total RomM has given; added to nothing, it would be a
        // total of this console's sessions posing as RomM's.
        if (auto k = r.known.find(d.romId); k != r.known.end()) k->second += d.playedMs;
    }
    save(u, r);
}

int64_t owedMs(const storage::User& u, int romId) {
    int64_t ms = 0;
    for (const Session& s : owed(u))
        if (s.romId == romId) ms += s.playedMs;
    return ms;
}

void remember(const storage::User& u, int romId, int64_t ms) {
    if (!u.valid() || romId <= 0) return;
    std::lock_guard<std::mutex> lk(gLock);
    Record r = load(u);
    if (auto k = r.known.find(romId); k != r.known.end() && k->second == ms) return;
    r.known[romId] = ms;
    save(u, r);
}

bool known(const storage::User& u, int romId, int64_t* ms) {
    if (!u.valid()) return false;
    std::lock_guard<std::mutex> lk(gLock);
    const Record r = load(u);
    const auto k = r.known.find(romId);
    if (k == r.known.end()) return false;
    *ms = k->second;
    return true;
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto frames = [](Clock& c, double seconds, bool running) {
        for (double t = 0; t < seconds; t += 0.5) c.tick(0.5f, running);
    };
    constexpr int64_t t0 = 1790000000000LL;   // 2026-09-21

    {
        Clock c;
        c.begin(604, t0);
        frames(c, 600, true);
        c.touch();
        const Session s = c.finish(t0 + 600000);
        check(s.playedMs == 600000, "ten minutes played with presses counts ten minutes");
        check(!c.active(), "finishing leaves the clock idle");
    }
    {
        Clock c;
        c.begin(604, t0);
        frames(c, 300, true);
        c.touch();
        frames(c, 900, false);   // the pause menu, fifteen minutes
        c.touch();
        frames(c, 300, true);
        c.touch();
        check(c.now(t0 + 1500000).playedMs == 600000,
              "fifteen minutes on the pause menu do not count");
    }
    {
        Clock c;
        c.begin(604, t0);
        frames(c, 600, true);
        c.touch();
        frames(c, 3 * 3600, true);   // left running all afternoon
        check(c.now(t0).playedMs == 600000,
              "a game left running counts up to the last press");
        c.touch();   // someone comes back
        frames(c, 60, true);
        check(c.now(t0).playedMs == 660000, "and counts again from the next press");
    }
    {
        Clock c;
        c.begin(604, t0);
        frames(c, 19 * 60, true);   // a long cutscene, no press
        check(c.now(t0).playedMs == 19 * 60000, "nineteen minutes without a press still count");
    }
    {
        Clock c;
        c.begin(604, t0);
        frames(c, 120, true);
        const Session s = c.now(t0 - 3600000);   // the wall clock went back an hour
        check(s.endMs >= s.startMs + s.playedMs, "a clock set back never ends before it began");
    }

    check(describe(59000).empty(), "under a minute says nothing");
    check(describe(60000) == "1 minute played", "one minute");
    check(describe(40 * 60000) == "40 minutes played", "forty minutes");
    check(describe(119 * 60000) == "1 hour played", "an hour and fifty-nine is one hour");
    check(describe(12 * 3600000LL + 59 * 60000) == "12 hours played", "twelve hours");
    check(iso(t0) == "2026-09-21T14:13:20Z", "the time RomM is sent");

    // The record, in a scratch directory.
    char dir[] = "/tmp/playtime-test-XXXXXX";
    if (!mkdtemp(dir)) {
        check(false, "a scratch directory");
        return failures;
    }
    storage::setRoot(dir);
    storage::User u;
    u.id = 7;
    u.name = "test";
    {
        Session s{604, t0, t0 + 1200000, 1200000};
        checkpoint(u, s);
        recover(u);
        check(owed(u).size() == 1 && owed(u)[0].playedMs == 1200000,
              "a session a power cut left open is owed at its last checkpoint");
        recover(u);
        check(owed(u).size() == 1, "and only once");
        close(u, Session{604, t0 + 5000000, t0 + 5030000, 30000});
        check(owed(u).size() == 1, "a session under a minute is not owed");
        close(u, Session{604, t0 + 6000000, t0 + 6600000, 600000});
        close(u, Session{305, t0 + 7000000, t0 + 7600000, 600000});
        check(owedMs(u, 604) == 1800000, "what one game is owed adds up");
        int64_t k = 0;
        check(!known(u, 604, &k), "no total is known before RomM gives one");
        remember(u, 604, 3600000);
        sent(u, {owed(u)[0]});
        check(known(u, 604, &k) && k == 3600000 + 1200000,
              "a session RomM took is added to the total it gave");
        check(owedMs(u, 604) == 600000 && owedMs(u, 305) == 600000,
              "and is no longer owed, and nothing else moved");
        sent(u, owed(u));
        check(owed(u).empty(), "everything sent, nothing owed");
        check(known(u, 305, &k) == false, "a total RomM never gave is not invented");
    }
    // Removed in-process, not with a shell: an iPhone app cannot start one.
    std::error_code gone;
    std::filesystem::remove_all(dir, gone);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures;
}

}  // namespace playtime
