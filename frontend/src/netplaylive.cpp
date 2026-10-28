// FORK (handheld): see netplaylive.h.

#include "netplaylive.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <mutex>
#include <thread>
#include <vector>

#include "netplay.h"
#include "netplaysave.h"

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
    uint32_t nextSend = 0;    // the first frame whose buttons have not been sent
    double fps = 60.0;

    // THE DELAY, KEPT UP TO DATE. `lead` is how far ahead we send our buttons:
    // what the other side last asked for. `need` is what we ask of them: the
    // round trip the kernel measures, raised a frame each second the game had
    // to wait for their buttons, and lowered a frame at a time once it has not
    // waited for a while.
    int lead = 0;
    int need = 0;
    int stallBoost = 0;
    uint32_t stalls = 0;      // waits begun since the last look
    std::chrono::steady_clock::time_point lastLook{}, lastStall{}, lastSlowSay{};

    // The shared saved game (netplaysave.h): the folder, the GameCube card
    // folder in it, and the host's copy to send.
    std::string saveDir, cardRoot;
    std::vector<uint8_t> pack;
    bool ramCheck = false;    // fingerprint the console's memory, not the picture
    std::map<uint32_t, netplaywire::Pad> sent;     // what we sent, by frame
    std::map<uint32_t, uint64_t> mine;              // our digests, to compare
    std::chrono::steady_clock::time_point lateSince{};
    bool late = false;
    bool saidWaiting = false;
    // For the [netplay] line every 600 frames: refreshes that had to wait for
    // the other side's buttons, and the longest wait.
    uint32_t heldTicks = 0;
    double longestHoldMs = 0.0;
};

// Whether a drift ends the session. CABINETOS_NETPLAY_DRIFT=log keeps playing
// and only logs it, for testing whether a new system stays in step.
bool driftEnds() {
    const char* e = std::getenv("CABINETOS_NETPLAY_DRIFT");
    return !(e && std::strcmp(e, "log") == 0);
}

// A packet often lands a few milliseconds after the check. Waiting this long
// for it costs less than giving up the whole refresh and trying on the next.
constexpr int kShortWaitMs = 3;

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

void connect(Request r, netplay::Identity me, double fps) {
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
                why = l.session->accept(static_cast<uint16_t>(r.code), me, r.delay, 500, fps, &l.pack);
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

void fail(const std::string& message, Mood mood = Mood::Problem) {
    Live& l = live();
    std::fprintf(stderr, "[netplay] %s\n", message.c_str());
    say(message, mood);
    l.session->leave();
    l.state = State::Off;
    pending() = {};
}

}  // namespace

Request& pending() {
    static Request r;
    return r;
}

void begin(cab::Core& core, const std::string& romPath, int romId, const std::string& saveDir,
           const std::string& cardRoot) {
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
    l.saveDir = saveDir;
    l.cardRoot = cardRoot;
    me.saveHash = netplaysave::fingerprint(saveDir, cardRoot);
    l.pack = r.host ? netplaysave::pack(saveDir, cardRoot) : std::vector<uint8_t>{};
    l.fps = core.avInfo().fps > 1.0 ? core.avInfo().fps : 60.0;
    {
        std::string n;
        for (char c : core.coreName())
            if (std::isalnum(static_cast<unsigned char>(c)))
                n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        // The two systems drawn by the graphics card: the picture can differ
        // between two machines while the game does not.
        l.ramCheck = n.find("mupen64plus") != std::string::npos || n.find("dolphin") != std::string::npos;
    }
    std::fprintf(stderr, "[netplay] shared saved game: %s\n",
                 me.saveHash.empty() ? "none yet" : me.saveHash.c_str());
    if (l.connector.joinable()) l.connector.join();
    l.cancel = false;
    l.connectFailed = false;
    l.local = {};
    l.sent.clear();
    l.mine.clear();
    l.frame = 0;
    l.nextSend = 0;
    l.lead = l.need = l.stallBoost = 0;
    l.stalls = 0;
    l.heldTicks = 0;
    l.longestHoldMs = 0.0;
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
    l.connector = std::thread(connect, r, me, l.fps);
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

namespace {

// Once a second: what we ask of the other side, from the round trip and from
// whether the game had to wait for their buttons.
void look(Live& l) {
    const auto now = std::chrono::steady_clock::now();
    if (now - l.lastLook < std::chrono::seconds(1)) return;
    l.lastLook = now;
    const double rtt = l.session->rttMs();
    const int fromRtt =
        rtt >= 0.0 ? netplay::chooseDelay(rtt, l.session->rttVarMs(), l.fps) : l.need;
    if (l.stalls > 0) {
        l.stallBoost = std::min(netplaywire::kMaxLead, std::max(l.stallBoost, l.need) + 1);
        l.lastStall = now;
    } else if (l.stallBoost > 0 && now - l.lastStall > std::chrono::seconds(10)) {
        --l.stallBoost;   // a frame back each 4 seconds once it has been quiet for 10
        l.lastStall = now - std::chrono::seconds(6);
    }
    l.stalls = 0;
    int want = std::clamp(std::max(fromRtt, l.stallBoost), 2, netplaywire::kMaxLead);
    if (want < l.need) want = l.need - 1;   // down gently: each step down drops a frame of buttons
    if (want == l.need) return;
    std::fprintf(stderr, "[netplay] asking for %d frames of delay (was %d; round trip %.1f ms)\n",
                 want, l.need, rtt);
    if (want >= 4 && l.need < 4 && now - l.lastSlowSay > std::chrono::seconds(30)) {
        l.lastSlowSay = now;
        say("The connection is slow, so there's a little more delay", Mood::Info);
    }
    l.need = want;
    l.session->sendLead(want);
}

uint64_t digestOf(const Live& l, cab::Core& core) {
    if (l.ramCheck)
        if (const uint64_t v = core.ramDigest(); v != 0) return v;
    return core.frameDigest();
}

// Just connected: the joiner takes the host's saved game, if the host sent
// one. False when the session has to end (said on screen) because the game
// already running cannot take it in: a memory card, which a GameCube reads
// only at power on, or a battery the host does not have.
bool takeSavedGame(Live& l, cab::Core& core) {
    if (!l.session->synced()) return true;
    const bool hadBattery = netplaysave::hasBattery(l.saveDir);
    netplaysave::Unpacked got;
    std::string why;
    if (!netplaysave::unpack(l.saveDir, l.cardRoot, l.session->receivedSave(), &why, &got)) {
        fail("Couldn't use the host's saved game: " + why);
        return false;
    }
    std::fprintf(stderr, "[netplay] took the host's saved game (%zu bytes)\n",
                 l.session->receivedSave().size());
    if (got.card || (got.battery.empty() && hadBattery)) {
        fail("Copied the host's saved game. Start Play together again", Mood::Done);
        return false;
    }
    if (!got.battery.empty()) {
        std::ifstream f(l.saveDir + "/" + got.battery, std::ios::binary);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
        if (data.empty() || !core.writeSaveRam(data)) {
            fail("Copied the host's saved game. Start Play together again", Mood::Done);
            return false;
        }
    }
    return true;
}

}  // namespace

int step(cab::Core& core, int maxFrames) {
    Live& l = live();
    if (l.state == State::Connecting) return 0;
    if (l.state != State::Playing) return 0;
    if (l.connector.joinable()) {
        l.connector.join();
        l.delay = l.session->delay();
        l.self = l.session->localPlayer();
        l.lead = l.need = l.delay;
        l.nextSend = static_cast<uint32_t>(l.delay);
        l.lastLook = l.lastStall = std::chrono::steady_clock::now();
        std::fprintf(stderr, "[netplay] connected, player %d, delay %d frames\n", l.self + 1,
                     l.delay);
        {
            std::lock_guard<std::mutex> g(l.lock);
            waitingLine.clear();
        }
        std::fprintf(stderr, "[netplay] drift check compares the %s\n",
                     l.ramCheck && core.ramDigest() != 0 ? "console's memory" : "picture");
        if (!takeSavedGame(l, core)) return 0;
        std::string hello = "Connected as player " + std::to_string(l.self + 1);
        if (l.session->synced() && !l.session->receivedSave().empty())
            hello += " \xC2\xB7 on the host's saved game";
        say(hello, Mood::Done);
    }
    core.setPlayers(2);
    look(l);
    const uint32_t d = static_cast<uint32_t>(l.delay);   // the first frames, played with no buttons
    int ran = 0;
    for (int i = 0; i < maxFrames; ++i) {
        const uint32_t f = l.frame;
        if (const int ask = l.session->takeLead(); ask > 0) {
            if (ask != l.lead)
                std::fprintf(stderr, "[netplay] sending %d frames ahead (was %d)\n", ask, l.lead);
            l.lead = ask;
        }
        // Our buttons go out as far ahead as the other side asked. A bigger
        // ask fills the frames between with the buttons held now; a smaller
        // one sends nothing until the frames catch up, so no frame is ever
        // sent twice.
        const uint32_t upTo = f + static_cast<uint32_t>(l.lead);
        while (l.nextSend <= upTo) {
            l.session->sendInputs(l.nextSend, l.local);
            l.sent[l.nextSend] = l.local;
            ++l.nextSend;
        }
        netplaywire::Pad mine, theirs;
        if (f >= d) {
            mine = l.sent[f];
            netplay::Wait w = l.session->remoteInputs(f, theirs, 0);
            if (w == netplay::Wait::Late) {
                const auto t0 = std::chrono::steady_clock::now();
                w = l.session->remoteInputs(f, theirs, kShortWaitMs);
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0).count();
                ++l.heldTicks;
                if (ms > l.longestHoldMs) l.longestHoldMs = ms;
            }
            if (w == netplay::Wait::Gone) {
                if (l.session->sentSave() && l.frame < 120)
                    fail("Sent your saved game. Start Play together again", Mood::Done);
                else
                    fail("The other player left");
                return ran;
            }
            if (w == netplay::Wait::Late) {
                const auto now = std::chrono::steady_clock::now();
                if (!l.late) {
                    l.late = true;
                    l.lateSince = now;
                    ++l.stalls;
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
        if (l.frame % 600 == 0) {
            std::fprintf(stderr,
                         "[netplay] %u frames: round trip %.1f ms, asking %d, sending %d ahead, "
                         "waited on the other side %u times (longest %.1f ms)\n",
                         l.frame, l.session->rttMs(), l.need, l.lead, l.heldTicks,
                         l.longestHoldMs);
            l.heldTicks = 0;
            l.longestHoldMs = 0.0;
        }
        // A fingerprint each second: of the console's memory for N64 and
        // GameCube, of the picture for the rest. A mismatch ends the session
        // with a message instead of letting the two games drift.
        if (l.frame % 60 == 0) {
            const uint64_t v = digestOf(l, core);
            if (!driftEnds())
                std::fprintf(stderr, "[netplay] fingerprint at frame %u: %016llx\n", f,
                             static_cast<unsigned long long>(v));
            l.session->sendDigest(f, v);
            l.mine[f] = v;
        }
        for (auto it = l.mine.begin(); it != l.mine.end();) {
            uint64_t theirDigest = 0;
            const netplay::Wait w = l.session->remoteDigest(it->first, theirDigest, 0);
            if (w == netplay::Wait::Late) break;
            if (w == netplay::Wait::Gone) break;   // the input path reports it
            if (theirDigest != it->second) {
                std::fprintf(stderr, "[netplay] fingerprint at frame %u: mine %016llx, theirs %016llx\n",
                             it->first, static_cast<unsigned long long>(it->second),
                             static_cast<unsigned long long>(theirDigest));
                if (driftEnds()) {
                    fail("The two games have drifted apart");
                    return ran;
                }
                std::fprintf(stderr, "[netplay] the two games differ at frame %u (playing on: "
                             "CABINETOS_NETPLAY_DRIFT=log)\n", it->first);
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

bool playingTogether(int romId) {
    const Request& r = pending();
    return (r.wanted && (r.romId == 0 || r.romId == romId)) || live().together;
}

bool supportsPlatform(const std::string& slug) {
    for (const char* s : {"nes", "snes", "genesis", "sms", "segacd", "sega32", "tg16",
                          "turbografx-cd", "arcade", "atari2600", "atari7800", "psx", "n64",
                          "ngc"})
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
                          "pcsxrearmed", "mupen64plus", "dolphin"})
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
