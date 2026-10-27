// FORK (handheld): see netplaylive.h.

#include "netplaylive.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <mutex>
#include <thread>
#include <vector>

#include "netplay.h"

namespace netplaylive {
namespace {

enum class State { Off, Connecting, Playing };

struct Live {
    std::atomic<State> state{State::Off};
    std::atomic<bool> cancel{false};
    std::thread connector;
    // A new one for every game: the old one's buttons, fingerprints and
    // listening door must not reach the next.
    std::unique_ptr<netplay::Session> session;
    std::string why;   // set by the connector before state becomes Playing/Off
    std::atomic<bool> connectFailed{false};
    std::atomic<bool> together{false};   // the game on screen was started to be played together

    std::mutex lock;
    std::string message;   // waiting to be taken
    Mood mood = Mood::Info;

    netplaywire::Pad local;   // latest buttons, main thread only
    int delay = 0;
    int self = 0;
    uint32_t frame = 0;
    uint32_t sentUpTo = 0;    // buttons sent for frames < sentUpTo
    std::map<uint32_t, netplaywire::Pad> sent;     // what we sent, by frame
    std::map<uint32_t, uint64_t> mine;              // our digests, to compare
    std::chrono::steady_clock::time_point lateSince{};
    bool late = false;
    bool saidWaiting = false;
};

std::string waitingLine;   // main thread and connector; guarded by Live::lock

Live& live() {
    static Live l;
    return l;
}

void say(const std::string& m, Mood mood = Mood::Info) {
    Live& l = live();
    std::lock_guard<std::mutex> g(l.lock);
    l.message = m;
    l.mood = mood;
}

// A hash of the start of the file and its size: enough to tell two different
// games apart without reading a whole disc image.
std::string hashOfFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    constexpr uint64_t kBasis = 1469598103934665603ull;
    uint64_t h = kBasis;
    std::vector<char> buf(1 << 20);
    for (int i = 0; i < 8 && f; ++i) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        for (std::streamsize k = 0; k < n; ++k) {
            h ^= static_cast<uint8_t>(buf[static_cast<size_t>(k)]);
            h *= 1099511628211ull;
        }
    }
    f.clear();
    f.seekg(0, std::ios::end);
    const auto size = static_cast<uint64_t>(f.tellg());
    h ^= size;
    h *= 1099511628211ull;
    char out[40];
    std::snprintf(out, sizeof out, "%016llx-%llu", static_cast<unsigned long long>(h),
                  static_cast<unsigned long long>(size));
    return out;
}

netplaywire::Pad toWire(const cab::PadState& s) {
    auto axis = [](float v) {
        return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    };
    auto trig = [](float v) {
        return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    };
    netplaywire::Pad p;
    p.buttons = static_cast<uint16_t>(s.buttons & 0xFFFF);
    p.leftX = axis(s.leftX);
    p.leftY = axis(s.leftY);
    p.rightX = axis(s.rightX);
    p.rightY = axis(s.rightY);
    p.leftTrigger = trig(s.leftTrigger);
    p.rightTrigger = trig(s.rightTrigger);
    return p;
}

// Both sides turn the wire form into the core's the same way, their own
// buttons as well as the other's, so the core sees identical numbers.
cab::PadState toCore(const netplaywire::Pad& p) {
    cab::PadState s;
    s.buttons = p.buttons;
    s.leftX = static_cast<float>(p.leftX) / 32767.0f;
    s.leftY = static_cast<float>(p.leftY) / 32767.0f;
    s.rightX = static_cast<float>(p.rightX) / 32767.0f;
    s.rightY = static_cast<float>(p.rightY) / 32767.0f;
    s.leftTrigger = static_cast<float>(p.leftTrigger) / 255.0f;
    s.rightTrigger = static_cast<float>(p.rightTrigger) / 255.0f;
    return s;
}

void connect(Request r, netplay::Identity me) {
    Live& l = live();
    std::string why;
    using Clock = std::chrono::steady_clock;
    if (r.host) {
        why = l.session->listen("0.0.0.0", static_cast<uint16_t>(r.port));
        if (why.empty()) {
            std::fprintf(stderr, "[netplay] hosting on port %u (code %d)\n",
                         static_cast<unsigned>(l.session->port()), r.code);
            const auto stop = Clock::now() + std::chrono::seconds(300);
            for (;;) {
                why = l.session->accept(static_cast<uint16_t>(r.code), me, r.delay, 500);
                if (why.empty() || l.cancel || Clock::now() > stop) break;
                if (why == "nobody joined") continue;
                // Somebody knocked and was turned away (a wrong code, a
                // different game). The door closed behind them: open it again.
                std::fprintf(stderr, "[netplay] turned a joiner away: %s\n", why.c_str());
                say(why == "wrong code" ? "Someone tried the wrong code"
                                        : "Someone tried to join: " + why,
                    Mood::Problem);
                why = l.session->listen("0.0.0.0", static_cast<uint16_t>(r.port));
                if (!why.empty()) break;
            }
        }
    } else {
        const auto stop = Clock::now() + std::chrono::seconds(120);
        for (;;) {
            why = l.session->join(r.address, static_cast<uint16_t>(r.port),
                                 static_cast<uint16_t>(r.code), me, 3000);
            if (why.empty() || why.find("the host said no") == 0 || l.cancel ||
                Clock::now() > stop)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    if (!why.empty()) {
        l.why = why;
        l.connectFailed = true;
    }
    l.state = why.empty() ? State::Playing : State::Off;
}

void fail(const std::string& message) {
    Live& l = live();
    std::fprintf(stderr, "[netplay] %s\n", message.c_str());
    say(message, Mood::Problem);
    l.session->leave();
    l.state = State::Off;
    pending() = {};
}

}  // namespace

Request& pending() {
    static Request r;
    return r;
}

void begin(cab::Core& core, const std::string& romPath, int romId) {
    Live& l = live();
    const Request r = pending();
    if (!r.wanted || l.state != State::Off) return;
    // Asked for on another game's page and never started: not this one's.
    if (r.romId != 0 && r.romId != romId) {
        pending() = {};
        return;
    }
    l.together = true;
    if (!supportsCore(core.coreName())) {
        std::fprintf(stderr, "[netplay] no Play together for the core \"%s\"\n",
                     core.coreName().c_str());
        say("Play together isn't ready for this system yet", Mood::Problem);
        pending() = {};
        return;
    }
    netplay::Identity me;
    me.core = core.coreName();
    me.coreVersion = core.coreVersion();
    me.gameHash = hashOfFile(romPath);
    if (me.gameHash.empty()) {
        say("Can't read the game for Play together", Mood::Problem);
        pending() = {};
        return;
    }
    if (l.connector.joinable()) l.connector.join();
    l.cancel = false;
    l.connectFailed = false;
    l.local = {};
    l.sent.clear();
    l.mine.clear();
    l.frame = 0;
    l.sentUpTo = 0;
    l.late = false;
    l.saidWaiting = false;
    l.delay = r.delay;
    l.session = std::make_unique<netplay::Session>();
    l.state = State::Connecting;
    {
        std::lock_guard<std::mutex> g(l.lock);
        char line[96];
        if (r.host)
            std::snprintf(line, sizeof line, "Waiting for player 2 \xC2\xB7 code %04d", r.code);
        else
            std::snprintf(line, sizeof line, "Joining player 1\xE2\x80\xA6");
        waitingLine = line;
        l.message = line;
        l.mood = Mood::Busy;
    }
    l.connector = std::thread(connect, r, me);
}

bool active() {
    Live& l = live();
    if (l.state == State::Connecting) return true;
    if (l.state == State::Playing) return true;
    // The connector gave up: tell the player once, from the main thread.
    if (l.connectFailed.exchange(false)) {
        if (l.connector.joinable()) l.connector.join();
        std::fprintf(stderr, "[netplay] %s\n", l.why.c_str());
        {
            std::lock_guard<std::mutex> g(l.lock);
            waitingLine.clear();
        }
        say("Play together: " + l.why, Mood::Problem);
        pending() = {};
    }
    return false;
}

void setLocal(const cab::PadState& pad) {
    live().local = toWire(pad);
}

int step(cab::Core& core, int maxFrames) {
    Live& l = live();
    if (l.state == State::Connecting) return 0;
    if (l.state != State::Playing) return 0;
    if (l.connector.joinable()) {
        l.connector.join();
        l.delay = l.session->delay();
        l.self = l.session->localPlayer();
        std::fprintf(stderr, "[netplay] connected, player %d, delay %d frames\n", l.self + 1,
                     l.delay);
        {
            std::lock_guard<std::mutex> g(l.lock);
            waitingLine.clear();
        }
        say("Connected as player " + std::to_string(l.self + 1), Mood::Done);
    }
    core.setPlayers(2);
    const uint32_t d = static_cast<uint32_t>(l.delay);
    int ran = 0;
    for (int i = 0; i < maxFrames; ++i) {
        const uint32_t f = l.frame;
        // Our buttons for f + d go out once, whether or not frame f can run yet.
        if (l.sentUpTo <= f) {
            l.session->sendInputs(f + d, l.local);
            l.sent[f + d] = l.local;
            l.sentUpTo = f + 1;
        }
        netplaywire::Pad mine, theirs;
        if (f >= d) {
            mine = l.sent[f];
            const netplay::Wait w = l.session->remoteInputs(f, theirs, 0);
            if (w == netplay::Wait::Gone) {
                fail("The other player left");
                return ran;
            }
            if (w == netplay::Wait::Late) {
                const auto now = std::chrono::steady_clock::now();
                if (!l.late) {
                    l.late = true;
                    l.lateSince = now;
                } else if (!l.saidWaiting && now - l.lateSince > std::chrono::seconds(1)) {
                    l.saidWaiting = true;
                    say("Waiting for the other player", Mood::Busy);
                } else if (now - l.lateSince > std::chrono::seconds(20)) {
                    fail("Lost the other player");
                    return ran;
                }
                return ran;
            }
            l.sent.erase(l.sent.begin(), l.sent.find(f));
        }
        l.late = false;
        l.saidWaiting = false;
        core.setPad(0, toCore(l.self == 0 ? mine : theirs));
        core.setPad(1, toCore(l.self == 0 ? theirs : mine));
        ran += core.runFrame();
        ++l.frame;
        // A fingerprint of the picture each second. A mismatch ends the
        // session with a message instead of letting the two games drift.
        if (l.frame % 60 == 0) {
            const uint64_t v = core.frameDigest();
            l.session->sendDigest(f, v);
            l.mine[f] = v;
        }
        for (auto it = l.mine.begin(); it != l.mine.end();) {
            uint64_t theirDigest = 0;
            const netplay::Wait w = l.session->remoteDigest(it->first, theirDigest, 0);
            if (w == netplay::Wait::Late) break;
            if (w == netplay::Wait::Gone) break;   // the input path reports it
            if (theirDigest != it->second) {
                fail("The two games have drifted apart");
                return ran;
            }
            it = l.mine.erase(it);
        }
    }
    return ran;
}

Message takeMessage() {
    Live& l = live();
    std::lock_guard<std::mutex> g(l.lock);
    Message m;
    m.text.swap(l.message);
    m.mood = l.mood;
    return m;
}

void stop() {
    Live& l = live();
    l.cancel = true;
    if (l.connector.joinable()) l.connector.join();
    if (l.session) l.session->leave();
    l.session.reset();
    l.state = State::Off;
    l.sent.clear();
    l.mine.clear();
    {
        std::lock_guard<std::mutex> g(l.lock);
        waitingLine.clear();
    }
    pending() = {};
    l.together = false;
}

bool blankSave(int romId) {
    const Request& r = pending();
    return (r.wanted && (r.romId == 0 || r.romId == romId)) || live().together;
}

bool supportsPlatform(const std::string& slug) {
    for (const char* s : {"nes", "snes", "genesis", "sms", "segacd", "sega32", "tg16",
                          "turbografx-cd", "arcade", "atari2600", "atari7800", "psx"})
        if (slug == s) return true;
    return false;
}

bool supportsCore(const std::string& coreName) {
    // The core's own name for itself ("Snes9x", "PCSX-ReARMed", "Genesis Plus
    // GX"), which is not its file's: capitals, spaces and dashes are ignored.
    std::string n;
    for (char c : coreName)
        if (std::isalnum(static_cast<unsigned char>(c)))
            n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* s : {"fceumm", "snes9x", "genesisplusgx", "beetlepcefast", "finalburnneo",
                          "fbneo", "mame2003", "stella", "prosystem", "picodrive",
                          "pcsxrearmed"})
        if (n.find(s) != std::string::npos) return true;
    return false;
}

int newCode() {
    static std::mt19937 rng{std::random_device{}()};
    return 1000 + static_cast<int>(rng() % 9000);
}

std::string connecting() {
    Live& l = live();
    std::lock_guard<std::mutex> g(l.lock);
    return l.state == State::Connecting ? waitingLine : std::string();
}

}  // namespace netplaylive
