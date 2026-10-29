// FORK (handheld): see netplaylive.h.

#include "netplaylive.h"

#include <algorithm>
#include <array>
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
    int players = 2;          // how many play, once the game has begun
    // The lobby, for a host that waits for more than one friend: Start pressed
    // (set on the main thread, read by the connector) and the last press seen.
    std::atomic<bool> startNow{false};
    bool startHeld = false;
    std::atomic<bool> hosting{false};
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

    // The shared saved game (netplaysave.h): the folder, and the host's copy
    // to send.
    std::string saveDir;
    std::vector<uint8_t> pack;
    std::map<uint32_t, netplaywire::Pad> sent;     // what we sent, by frame
    std::map<uint32_t, uint64_t> mine;              // our digests, to compare
    std::chrono::steady_clock::time_point lateSince{};
    bool late = false;
    bool saidWaiting = false;
    // For the [netplay] line every 600 frames: waits for the other side's buttons.
    uint32_t heldTicks = 0;
};

// Whether a drift ends the session. CABINETOS_NETPLAY_DRIFT=log keeps playing
// and only logs it, for testing whether a new system stays in step.
bool driftEnds() {
    const char* e = std::getenv("CABINETOS_NETPLAY_DRIFT");
    return !(e && std::strcmp(e, "log") == 0);
}


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

// What to keep on screen while the lobby fills.
void lobbyLine(const Request& r, int here) {
    Live& l = live();
    char line[128];
    if (r.host && r.players > 2)
        std::snprintf(line, sizeof line, "%d of %d here \xC2\xB7 press Start to begin \xC2\xB7 code %04d",
                      here, r.players, r.code);
    else if (r.host)
        std::snprintf(line, sizeof line, "Waiting for player 2 \xC2\xB7 code %04d", r.code);
    else if (here > 1)
        std::snprintf(line, sizeof line, "In the game \xC2\xB7 %d here \xC2\xB7 waiting for the host to start",
                      here);
    else
        std::snprintf(line, sizeof line, "Joining player 1\xE2\x80\xA6");
    std::lock_guard<std::mutex> g(l.lock);
    waitingLine = line;
    l.message = line;
    l.mood = Mood::Busy;
}

void connect(Request r, netplay::Identity me, double fps) {
    Live& l = live();
    std::string why;
    using Clock = std::chrono::steady_clock;
    if (r.host) {
        why = l.session->listen("0.0.0.0", static_cast<uint16_t>(r.port));
        if (why.empty()) {
            std::fprintf(stderr, "[netplay] hosting on port %u (code %d), up to %d players\n",
                         static_cast<unsigned>(l.session->port()), r.code, r.players);
            const auto stop = Clock::now() + std::chrono::seconds(300);
            int shown = 1;
            for (;;) {
                // The door stays open while friends gather: a wrong code or a
                // different game is turned away and the next one is let in.
                why = l.session->admit(static_cast<uint16_t>(r.code), me, 500, r.players, fps,
                                       &l.pack);
                if (l.cancel || Clock::now() > stop) {
                    if (!l.cancel) why = "nobody started the game";
                    break;
                }
                if (!why.empty() && why != netplay::kNobodyJoined) {
                    std::fprintf(stderr, "[netplay] turned a joiner away: %s\n", why.c_str());
                    say(why == "wrong code" ? "Someone tried the wrong code"
                                            : "Someone tried to join: " + why,
                        Mood::Problem);
                }
                const int here = l.session->here();   // also drops anyone who left
                if (here != shown) {
                    shown = here;
                    lobbyLine(r, here);
                }
                if (here < 2) {
                    l.startNow = false;   // a Start pressed with nobody here does not count
                    continue;
                }
                // Start with the game full, or when the host presses Start.
                // Two players start as soon as the friend is in.
                if (here >= r.players || r.players <= 2 || l.startNow.exchange(false)) {
                    why = l.session->start(r.delay);
                    break;
                }
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
        // In: now wait, in the host's lobby, for the game to begin.
        if (why.empty()) {
            const auto wait = Clock::now() + std::chrono::seconds(300);
            int shown = 0;
            for (;;) {
                why = l.session->awaitStart(500);
                if (why != netplay::kNotYet) break;
                if (l.cancel) break;
                if (Clock::now() > wait) {
                    why = "the host did not start the game";
                    break;
                }
                if (const int here = l.session->lobbySize(); here != shown) {
                    shown = here;
                    lobbyLine(r, here);
                }
            }
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

void begin(cab::Core& core, const std::string& romPath, int romId, const std::string& saveDir) {
    Live& l = live();
    Request r = pending();
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
    // The most this system can take; a joiner's own count is the host's.
    r.players = std::clamp(r.players, 2, std::min(maxPlayersForCore(core.coreName()), netplaywire::kMaxPlayers));
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
    me.saveHash = netplaysave::fingerprint(saveDir);
    l.pack = r.host ? netplaysave::pack(saveDir) : std::vector<uint8_t>{};
    l.fps = core.avInfo().fps > 1.0 ? core.avInfo().fps : 60.0;
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
    l.late = false;
    l.saidWaiting = false;
    l.delay = r.delay;
    l.session = std::make_unique<netplay::Session>();
    l.state = State::Connecting;
    l.startNow = false;
    l.startHeld = false;
    l.hosting = r.host && r.players > 2;
    l.players = 2;
    lobbyLine(r, 1);
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
    Live& l = live();
    l.local = toWire(pad);
    // The host's Start, pressed while friends gather, begins the game. Only a
    // fresh press counts: a Start held from the menu does not.
    const bool start = (pad.buttons & cab::bit(cab::Start)) != 0;
    if (l.hosting && l.state == State::Connecting && start && !l.startHeld) l.startNow = true;
    l.startHeld = start;
}

namespace {

// A single wait is a one-frame hitch, as it always was; the delay, which is
// felt on every button, is raised only when waits are frequent.
constexpr uint32_t kStallsToRaise = 6;   // waits in one second

// Once a second: what we ask of the other side. The delay stays at the 2
// frames every session was first played at, which felt right on a good link,
// and goes up only when the game really had to wait for the other side's
// buttons (a frame each second it did), and back down one frame at a time
// after ten quiet seconds. The round trip is logged to explain a rise, but it
// does not choose the delay: a figure worked out from it came out higher than
// 2 on links that played perfectly at 2, and the delay could be seen.
void look(Live& l) {
    const auto now = std::chrono::steady_clock::now();
    if (now - l.lastLook < std::chrono::seconds(1)) return;
    l.lastLook = now;
    if (l.stalls >= kStallsToRaise) {
        l.stallBoost = std::min(netplaywire::kMaxLead, std::max(l.stallBoost, l.need) + 1);
        l.lastStall = now;
    } else if (l.stallBoost > 0 && now - l.lastStall > std::chrono::seconds(10)) {
        --l.stallBoost;   // a frame back each 4 seconds once it has been quiet for 10
        l.lastStall = now - std::chrono::seconds(6);
    }
    l.stalls = 0;
    int want = std::clamp(l.stallBoost, l.delay, netplaywire::kMaxLead);
    if (want < l.need) want = l.need - 1;   // down gently: each step down drops a frame of buttons
    if (want == l.need) return;
    std::fprintf(stderr,
                 "[netplay] asking for %d frames of delay (was %d; the game waited; round trip "
                 "%.1f ms)\n",
                 want, l.need, l.session->rttMs());
    if (want >= 4 && l.need < 4 && now - l.lastSlowSay > std::chrono::seconds(30)) {
        l.lastSlowSay = now;
        say("The connection is slow, so there's a little more delay", Mood::Info);
    }
    l.need = want;
    l.session->sendLead(want);
}

// Just connected: the joiner takes the host's saved game, if the host sent
// one. False when the session has to end (said on screen) because the game
// already running cannot take it in: a battery the host does not have.
bool takeSavedGame(Live& l, cab::Core& core) {
    if (!l.session->synced()) return true;
    const bool hadBattery = netplaysave::hasBattery(l.saveDir);
    netplaysave::Unpacked got;
    std::string why;
    if (!netplaysave::unpack(l.saveDir, l.session->receivedSave(), &why, &got)) {
        fail("Couldn't use the host's saved game: " + why);
        return false;
    }
    std::fprintf(stderr, "[netplay] took the host's saved game (%zu bytes)\n",
                 l.session->receivedSave().size());
    if (got.battery.empty() && hadBattery) {
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
        l.players = l.session->players();
        l.lead = l.need = l.delay;
        l.nextSend = static_cast<uint32_t>(l.delay);
        l.lastLook = l.lastStall = std::chrono::steady_clock::now();
        std::fprintf(stderr, "[netplay] connected, player %d of %d, delay %d frames\n", l.self + 1,
                     l.players, l.delay);
        {
            std::lock_guard<std::mutex> g(l.lock);
            waitingLine.clear();
        }
        if (!takeSavedGame(l, core)) return 0;
        std::string hello = "Connected as player " + std::to_string(l.self + 1);
        if (l.session->synced() && !l.session->receivedSave().empty())
            hello += " \xC2\xB7 on the host's saved game";
        say(hello, Mood::Done);
    }
    core.setPlayers(l.players);
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
        // Everyone's buttons for this frame, by player: ours as we sent them
        // `delay` frames ago, the others' as they came. A player who has left
        // is still, for everyone from the same frame.
        std::array<netplaywire::Pad, netplaywire::kMaxPlayers> pads{};
        if (f >= d) {
            pads[static_cast<size_t>(l.self)] = l.sent[f];
            for (int slot = 0; slot < l.players; ++slot) {
                if (slot == l.self) continue;
                const netplay::Wait w =
                    l.session->remoteInputs(f, slot, pads[static_cast<size_t>(slot)], 0);
                if (w == netplay::Wait::Gone) {
                    if (l.session->sentSave() && l.frame < 120)
                        fail("Sent your saved game. Start Play together again", Mood::Done);
                    else if (l.players <= 2)
                        fail("The other player left");
                    else
                        fail(l.self == 0 ? "Everyone else left" : "The host left");
                    return ran;
                }
                if (w == netplay::Wait::Late) {
                    const auto now = std::chrono::steady_clock::now();
                    if (!l.late) {
                        l.late = true;
                        l.lateSince = now;
                        ++l.stalls;
                        ++l.heldTicks;
                    } else if (!l.saidWaiting && now - l.lateSince > std::chrono::seconds(1)) {
                        l.saidWaiting = true;
                        say(l.players <= 2 ? "Waiting for the other player" : "Waiting for a player",
                            Mood::Busy);
                    } else if (now - l.lateSince > std::chrono::seconds(20)) {
                        fail(l.players <= 2 ? "Lost the other player" : "Lost a player's connection");
                        return ran;
                    }
                    return ran;
                }
            }
            l.sent.erase(l.sent.begin(), l.sent.find(f));
        }
        l.late = false;
        l.saidWaiting = false;
        for (int slot = 0; slot < l.players; ++slot)
            core.setPad(slot, toCore(pads[static_cast<size_t>(slot)]));
        ran += core.runFrame();
        ++l.frame;
        for (int gone; (gone = l.session->takeLeft()) >= 0;) {
            std::fprintf(stderr, "[netplay] player %d left\n", gone + 1);
            say("Player " + std::to_string(gone + 1) + " left", Mood::Info);
        }
        if (l.frame % 600 == 0) {
            std::fprintf(stderr,
                         "[netplay] %u frames: round trip %.1f ms, asking %d, sending %d ahead, "
                         "waited on the other side %u times\n",
                         l.frame, l.session->rttMs(), l.need, l.lead, l.heldTicks);
            l.heldTicks = 0;
        }
        // A fingerprint of the picture each second. A mismatch ends the
        // session with a message instead of letting the two games drift.
        if (l.frame % 60 == 0) {
            const uint64_t v = core.frameDigest();
            if (!driftEnds())
                std::fprintf(stderr, "[netplay] fingerprint at frame %u: %016llx\n", f,
                             static_cast<unsigned long long>(v));
            l.session->sendDigest(f, v);
            l.mine[f] = v;
        }
        // Every other player's fingerprint, oldest frame first. One still on its
        // way waits for the next frame; a player who has left has none owed.
        for (auto it = l.mine.begin(); it != l.mine.end();) {
            bool owed = false, gone = false;
            for (int slot = 0; slot < l.players && !owed && !gone; ++slot) {
                if (slot == l.self) continue;
                uint64_t theirDigest = 0;
                const netplay::Wait w = l.session->remoteDigest(it->first, slot, theirDigest, 0);
                if (w == netplay::Wait::Late) owed = true;
                else if (w == netplay::Wait::Gone) gone = true;   // the input path reports it
                else if (w == netplay::Wait::Got && theirDigest != it->second) {
                    std::fprintf(stderr,
                                 "[netplay] fingerprint at frame %u: mine %016llx, player %d's "
                                 "%016llx\n",
                                 it->first, static_cast<unsigned long long>(it->second), slot + 1,
                                 static_cast<unsigned long long>(theirDigest));
                    if (driftEnds()) {
                        fail("The games have drifted apart");
                        return ran;
                    }
                    std::fprintf(stderr, "[netplay] the games differ at frame %u (playing on: "
                                 "CABINETOS_NETPLAY_DRIFT=log)\n", it->first);
                }
            }
            if (owed || gone) break;
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
    l.hosting = false;
    l.startNow = false;
}

bool playingTogether(int romId) {
    const Request& r = pending();
    return (r.wanted && (r.romId == 0 || r.romId == romId)) || live().together;
}

bool supportsPlatform(const std::string& slug) {
    for (const char* s : {"snes", "genesis", "psx"})
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
    for (const char* s : {"snes9x", "genesisplusgx", "picodrive", "pcsxrearmed"})
        if (n.find(s) != std::string::npos) return true;
    return false;
}

// Two on every system that can be played together now. A system whose core
// gives every player a port of its own with no setting to change can be given
// more here, one at a time, once it has been played; a SNES or Genesis
// multitap needs a core option first. The lobby and the "How many can play?"
// question already follow what these return.
int maxPlayersForPlatform(const std::string&) { return 2; }

int maxPlayersForCore(const std::string&) { return 2; }

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
