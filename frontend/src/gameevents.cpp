// Game events (#716): the messages, the knock on each network, and the one
// thread that sends them in order. gameevents.h says what each rule is for.
// One message on the wire, with the home-address rule, is
// gameevents_wire.cpp; the Settings row and the App's side of each message
// are gameeventsflow.cpp; the self-test is gameevents_test.cpp.

#include "gameevents.h"

#include <ifaddrs.h>
#include <json-c/json.h>
#include <net/if.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

#include "covercache.h"
#include "gameevents_internal.h"
#include "prefs.h"

namespace gameevents {
namespace {

constexpr const char* kPref = "game_events_address";
// While the line is empty the sender looks at the network this often, so the
// knock goes as the console joins one rather than with the next game.
constexpr auto kLookEvery = std::chrono::seconds(5);
// Past this many waiting, a new one is dropped: it would go late anyway.
constexpr size_t kLongestLine = 20;

using Json = std::unique_ptr<json_object, int (*)(json_object*)>;

Json newObject() { return Json(json_object_new_object(), json_object_put); }

void addText(json_object* o, const char* key, const std::string& value) {
    json_object_object_add(o, key, json_object_new_string(value.c_str()));
}

std::string text(json_object* o) {
    return json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
}

int64_t wallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The `test` message: Done's, and the knock on each network. It names nobody
// and no game.
std::string testMessage() {
    Json o = newObject();
    json_object_object_add(o.get(), "format", json_object_new_int(1));
    addText(o.get(), "event", "test");
    addText(o.get(), "time", localTime(std::time(nullptr)));
    return text(o.get());
}

void addCover(json_object* o, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return;
    addText(o, "cover_type", imageType(bytes));
    addText(o, "cover", base64(bytes));
}

// --- The network the console is on --------------------------------------------
//
// Its addresses, by interface: joining a network, leaving it, or Tailscale
// coming up changes them. IPv6 from the kernel's own list, which marks a
// temporary address (IFA_F_TEMPORARY): those change every day or so on the
// same network. Link-local addresses say nothing about which network it is.
std::string networkNow() {
    std::vector<std::string> parts;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
            if (!(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_RUNNING) ||
                (a->ifa_flags & IFF_LOOPBACK))
                continue;
            parts.push_back(std::string(a->ifa_name) + " " + ipText(a->ifa_addr));
        }
        freeifaddrs(list);
    }
    std::ifstream f("/proc/net/if_inet6");
    std::string hex, index, length, scope, flags, name;
    while (f >> hex >> index >> length >> scope >> flags >> name) {
        if (std::strtoul(scope.c_str(), nullptr, 16) != 0) continue;   // not global
        if (std::strtoul(flags.c_str(), nullptr, 16) & 0x01) continue;   // temporary
        parts.push_back(name + " " + hex);
    }
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (const std::string& s : parts) out += s + ",";
    return out;
}

// --- The line and its sender ------------------------------------------------------

struct Message {
    std::string event;
    Json body{nullptr, json_object_put};
    std::vector<std::string> covers;   // read and added on the sender's thread
};

struct Sender {
    std::mutex m;
    std::condition_variable wake;
    std::condition_variable idle;   // the line is empty and nothing is on the wire
    std::deque<Message> line;
    std::thread th;
    bool running = false;           // th started and not told to stop
    bool stop = false;
    std::atomic<bool> stopNow{false};
    int onWire = 0;
    std::string url;
    std::string knockedOn;          // the network the last knock was on
    bool knockOk = false;           // and it was answered
    std::function<std::string()> network = networkNow;
    std::function<std::vector<uint8_t>(const std::string&)> cover = covercache::read;

    // JOINED AT EXIT HOWEVER THE PROGRAM ENDS: a std::thread still joinable
    // when it is destroyed ends the program in std::terminate.
    ~Sender() {
        {
            std::lock_guard<std::mutex> lk(m);
            stop = true;
            stopNow = true;
        }
        wake.notify_all();
        if (th.joinable()) th.join();
    }
};

Sender& sender() {
    static Sender s;
    return s;
}

std::atomic<bool> gOn{false};

void dropAll(Sender& s, const char* why) {
    for (const Message& m : s.line)
        std::fprintf(stderr, "[gameevents] %s not sent: %s\n", m.event.c_str(), why);
    s.line.clear();
}

void run(Sender& s) {
    std::unique_lock<std::mutex> lk(s.m);
    while (!s.stop) {
        if (s.line.empty() && s.onWire == 0) s.idle.notify_all();
        s.wake.wait_for(lk, kLookEvery, [&] { return s.stop || !s.line.empty(); });
        if (s.stop) break;
        const std::function<std::string()> network = s.network;
        lk.unlock();
        const std::string here = network();
        lk.lock();
        if (s.stop) break;
        // NO NETWORK, NOTHING SENT, and nothing kept for later.
        if (here.empty()) {
            if (!s.knockedOn.empty()) std::fprintf(stderr, "[gameevents] no network: nothing is sent\n");
            s.knockedOn.clear();
            s.knockOk = false;
            dropAll(s, "no network");
            continue;
        }
        // A NEW NETWORK: THE KNOCK FIRST.
        if (here != s.knockedOn) {
            const std::string url = s.url;
            s.knockedOn = here;
            s.knockOk = false;
            ++s.onWire;
            lk.unlock();
            const Tried t = post(url, testMessage(), &s.stopNow);
            lk.lock();
            --s.onWire;
            // An address changed meanwhile was tested by its own Done.
            if (s.knockedOn == here && s.url == url) s.knockOk = t.answer == Answer::Ok;
            if (t.answer == Answer::Ok)
                std::fprintf(stderr, "[gameevents] a new network: the knock was answered (%ld)\n", t.status);
            else
                std::fprintf(stderr, "[gameevents] a new network: the knock was not answered (%s); "
                                     "nothing is sent on this network\n", why(t).c_str());
            continue;
        }
        if (s.line.empty()) continue;
        Message msg = std::move(s.line.front());
        s.line.pop_front();
        if (!s.knockOk) {
            std::fprintf(stderr, "[gameevents] %s not sent: this network did not answer the knock\n",
                         msg.event.c_str());
            continue;
        }
        const std::string url = s.url;
        const auto cover = s.cover;
        ++s.onWire;
        lk.unlock();
        for (const std::string& key : msg.covers) {
            const std::vector<uint8_t> bytes = key.empty() ? std::vector<uint8_t>() : cover(key);
            if (bytes.empty()) continue;
            addCover(msg.body.get(), bytes);
            break;
        }
        const Tried t = post(url, text(msg.body.get()), &s.stopNow);
        if (t.answer == Answer::Ok)
            std::fprintf(stderr, "[gameevents] %s sent (%ld)\n", msg.event.c_str(), t.status);
        else
            std::fprintf(stderr, "[gameevents] %s dropped: %s\n", msg.event.c_str(), why(t).c_str());
        lk.lock();
        --s.onWire;
    }
    s.line.clear();
    s.idle.notify_all();
}

void put(Message m) {
    Sender& s = sender();
    {
        std::lock_guard<std::mutex> lk(s.m);
        if (!s.running) return;
        if (s.line.size() >= kLongestLine) {
            std::fprintf(stderr, "[gameevents] %s not sent: %zu still waiting\n", m.event.c_str(),
                         s.line.size());
            return;
        }
        s.line.push_back(std::move(m));
    }
    s.wake.notify_all();
}

// --- The game in play -------------------------------------------------------------
//
// Frame thread only. Kept after its end, for an unlock that lands as the game
// is left (achievements_ui.cpp drains those).
struct Current {
    bool in = false;
    bool paused = false;
    Game game;
    int64_t startedMs = 0;
};
Current gGame;

void putGame(const char* event, const Who& w, bool withCover,
             const std::function<void(json_object*)>& extra = nullptr) {
    Message m;
    m.event = event;
    m.body = newObject();
    json_object* o = m.body.get();
    json_object_object_add(o, "format", json_object_new_int(1));
    addText(o, "event", event);
    addText(o, "time", localTime(std::time(nullptr)));
    addText(o, "person", w.person);
    addText(o, "game", gGame.game.title);
    // A LinkUp friend's console has only the game's name: it is on the
    // host's server.
    if (!gGame.game.friendSide) {
        addText(o, "system", gGame.game.system);
        json_object_object_add(o, "rom_id", json_object_new_int(gGame.game.romId));
        if (withCover) m.covers = gGame.game.covers;
    }
    addText(o, "linkup", w.linkup);
    addText(o, "linkup_game", gGame.game.friendSide   ? "joined"
                              : gGame.game.linkupHost ? "hosting"
                                                      : "none");
    if (extra) extra(o);
    put(std::move(m));
}

}  // namespace

// --- Shared with the self-test (gameevents_internal.h) ------------------------------

std::string base64(const std::vector<uint8_t>& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out += {k[(v >> 18) & 63], k[(v >> 12) & 63], k[(v >> 6) & 63], k[v & 63]};
    }
    if (i < in.size()) {
        const bool two = i + 1 < in.size();
        const uint32_t v = (in[i] << 16) | (two ? in[i + 1] << 8 : 0);
        out += {k[(v >> 18) & 63], k[(v >> 12) & 63], two ? k[(v >> 6) & 63] : '=', '='};
    }
    return out;
}

// What the cover is, from its first bytes: RomM keeps whatever format it was
// given, and the cache keeps RomM's bytes. Any other is still sent, as bytes.
std::string imageType(const std::vector<uint8_t>& b) {
    auto at = [&](size_t i, const char* s) {
        const size_t n = std::strlen(s);
        return b.size() >= i + n && std::memcmp(b.data() + i, s, n) == 0;
    };
    if (at(0, "\x89PNG\r\n\x1a\n")) return "image/png";
    if (at(0, "\xFF\xD8\xFF")) return "image/jpeg";
    if (at(0, "RIFF") && at(8, "WEBP")) return "image/webp";
    return "application/octet-stream";
}

// "2026-11-15T19:32:00-05:00": the console's own time, with its offset.
std::string localTime(std::time_t t) {
    std::tm lt{};
    localtime_r(&t, &lt);
    char day[32];
    std::strftime(day, sizeof day, "%Y-%m-%dT%H:%M:%S", &lt);
    // An offset is under a day, so two digits each.
    const long off = lt.tm_gmtoff, a = (off < 0 ? -off : off) % 86400;
    char zone[8];
    std::snprintf(zone, sizeof zone, "%c%02d:%02d", off < 0 ? '-' : '+', static_cast<int>(a / 3600),
                  static_cast<int>(a % 3600 / 60));
    return std::string(day) + zone;
}

void startSending(const std::string& url, bool knocked) {
    Sender& s = sender();
    const std::string here = knocked ? s.network() : std::string();
    std::unique_lock<std::mutex> lk(s.m);
    // One told to stop is joined before a new one starts: at its next look
    // at the flag, or after a name's lookup.
    if (!s.running && s.th.joinable()) {
        lk.unlock();
        s.th.join();
        lk.lock();
    }
    s.url = url;
    s.line.clear();
    s.knockedOn = here;
    s.knockOk = knocked && !here.empty();
    if (!s.running) {
        s.stop = false;
        s.stopNow = false;
        s.running = true;
        s.th = std::thread(run, std::ref(s));
        std::fprintf(stderr, "[gameevents] sending\n");
    }
    gOn = true;
}

void stopRunning() {
    Sender& s = sender();
    gGame = Current{};
    {
        std::lock_guard<std::mutex> lk(s.m);
        if (!s.running) return;
        s.running = false;
        s.stop = true;
        s.stopNow = true;
        s.url.clear();
        dropAll(s, "Game events was stopped");
    }
    s.wake.notify_all();
    gOn = false;
    std::fprintf(stderr, "[gameevents] stopped\n");
}

bool senderJoinable() { return sender().th.joinable(); }

void standIns(std::function<std::string()> network,
              std::function<std::vector<uint8_t>(const std::string&)> cover) {
    Sender& s = sender();
    std::lock_guard<std::mutex> lk(s.m);
    s.network = network ? std::move(network) : networkNow;
    s.cover = cover ? std::move(cover) : covercache::read;
}

// --- The address --------------------------------------------------------------

std::string address() { return prefs::get(kPref, ""); }

void start() {
    const std::string url = address();
    if (!url.empty()) startSending(url, /*knocked=*/false);
}

void shutdown() {
    Sender& s = sender();
    {
        std::unique_lock<std::mutex> lk(s.m);
        if (s.running)
            s.idle.wait_for(lk, std::chrono::milliseconds(kMessageMs),
                            [&] { return s.line.empty() && s.onWire == 0; });
        dropAll(s, "the console is going down");
        s.running = false;
        s.stop = true;
        s.stopNow = true;
    }
    s.wake.notify_all();
    if (s.th.joinable()) s.th.join();
    gOn = false;
}

int pending() {
    Sender& s = sender();
    std::lock_guard<std::mutex> lk(s.m);
    return static_cast<int>(s.line.size()) + s.onWire;
}

Tried tryAddress(const std::string& url) {
    const Tried t = post(url, testMessage(), nullptr);
    std::fprintf(stderr, "[gameevents] the test message: %s\n",
                 t.answer == Answer::Ok ? "answered" : why(t).c_str());
    return t;
}

std::string why(const Tried& t) {
    switch (t.answer) {
        case Answer::Ok: return "";
        case Answer::NotHome: return "Not on your home network";
        case Answer::Error: return "It answered with an error (" + std::to_string(t.status) + ")";
        case Answer::Certificate: return "Its certificate isn't trusted. Use http:// at home.";
        case Answer::CantConnect: break;
    }
    return "Can't connect";
}

void setAddress(const std::string& url) {
    prefs::set(kPref, url);
    startSending(url, /*knocked=*/true);
}

void stopSending() {
    prefs::set(kPref, "");
    stopRunning();
}

// --- The messages -------------------------------------------------------------

bool on() { return gOn.load(); }

bool inGame(bool friendSide) { return gGame.in && gGame.game.friendSide == friendSide; }

bool pausedNow() { return gGame.paused; }

void started(const Game& g, const Who& w) {
    if (!on()) return;
    gGame = Current{true, false, g, wallMs()};
    putGame("game_started", w, /*withCover=*/true);
}

void paused(bool isPaused, const Who& w) {
    if (!on() || !gGame.in || gGame.game.friendSide || gGame.paused == isPaused) return;
    gGame.paused = isPaused;
    putGame(isPaused ? "game_paused" : "game_resumed", w, false);
}

void ended(int64_t playedMs, const Who& w) {
    if (!on() || !gGame.in) return;
    // A LinkUp friend's console keeps no play clock (the game runs on the
    // host's), so its minutes are the wall clock's.
    const int64_t ms = gGame.game.friendSide ? wallMs() - gGame.startedMs : playedMs;
    putGame("game_ended", w, false, [ms](json_object* o) {
        json_object_object_add(o, "minutes_played", json_object_new_int64(std::max<int64_t>(0, ms) / 60000));
    });
    gGame.in = false;
    gGame.paused = false;
}

void unlocked(const std::string& achievement, uint32_t points, const Who& w) {
    if (!on() || gGame.game.title.empty() || gGame.game.friendSide) return;
    putGame("achievement_unlocked", w, false, [&](json_object* o) {
        addText(o, "achievement", achievement);
        json_object_object_add(o, "points", json_object_new_int64(points));
    });
}

void completed(const Who& w) {
    if (!on() || gGame.game.title.empty() || gGame.game.friendSide) return;
    putGame("set_completed", w, /*withCover=*/true);
}

}  // namespace gameevents
