// FORK (handheld): see netplayrun.h.

#include "netplayrun.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>
#include <vector>

#include "core.h"
#include "netplay.h"

namespace netplayrun {
namespace {

constexpr uint64_t kBasis = 1469598103934665603ull;

void mix(uint64_t& h, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
}

// What this player presses on `frame`: something new every 6 frames, from a
// fixed sequence, so both copies and both runs agree. Select, Start and the
// stick clicks are left out: they pause or reset games.
netplaywire::Pad scripted(uint32_t frame, int player) {
    uint32_t x = (frame / 6 + 1) * 2654435761u ^ (static_cast<uint32_t>(player) + 1) * 40503u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    netplaywire::Pad p;
    p.buttons = static_cast<uint16_t>(x & 0x3FF3);
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

std::string hashOfFile(const std::string& path) {
    size_t n = 0;
    void* bytes = SDL_LoadFile(path.c_str(), &n);
    if (!bytes) return {};
    uint64_t h = kBasis;
    mix(h, bytes, n);
    SDL_free(bytes);
    char out[24];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
    return out;
}

int envInt(const char* name, int fallback) {
    const char* v = SDL_getenv(name);
    return v ? SDL_atoi(v) : fallback;
}

}  // namespace

int run(cab::Core& core, const Options& o) {
    const char* role = o.host ? "host" : "joiner";
    const int frames = envInt("CABINETOS_NETPLAY_FRAMES", 3600);
    const int delay = envInt("CABINETOS_NETPLAY_DELAY", 2);
    const uint16_t code = static_cast<uint16_t>(envInt("CABINETOS_NETPLAY_CODE", 4821));

    netplay::Identity me;
    me.core = core.coreName();
    me.coreVersion = core.coreVersion();
    me.gameHash = hashOfFile(o.romPath);
    if (me.gameHash.empty()) {
        std::fprintf(stderr, "[netplay] cannot read %s\n", o.romPath.c_str());
        return 1;
    }

    netplay::Session s;
    std::string why;
    if (o.host) {
        why = s.listen(o.address, static_cast<uint16_t>(o.port));
        if (!why.empty()) {
            std::fprintf(stderr, "[netplay] host: %s\n", why.c_str());
            return 1;
        }
        std::fprintf(stderr, "[netplay] host: waiting on %s port %u (code %u)\n", o.address.c_str(),
                     static_cast<unsigned>(s.port()), static_cast<unsigned>(code));
        why = s.accept(code, me, delay, 120000);
    } else {
        // The host may start a moment after us: try for a minute.
        const auto stop = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (;;) {
            why = s.join(o.address, static_cast<uint16_t>(o.port), code, me, 3000);
            if (why.empty() || why.find("the host said no") == 0 ||
                std::chrono::steady_clock::now() > stop)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        // Joined; now the host begins the game.
        while (why.empty()) {
            why = s.awaitStart(500);
            if (why != netplay::kNotYet || std::chrono::steady_clock::now() > stop) break;
            why.clear();
        }
        if (why == netplay::kNotYet) why = "the host did not start the game";
    }
    if (!why.empty()) {
        std::fprintf(stderr, "[netplay] %s: %s\n", role, why.c_str());
        return 1;
    }
    const int d = s.delay();
    const int self = s.localPlayer();
    std::fprintf(stderr, "[netplay] %s: connected, player %d, delay %d frames, %d frames to play\n",
                 role, self + 1, d, frames);

    uint64_t run = kBasis;
    std::map<uint32_t, uint64_t> mine;
    auto compare = [&](bool last) -> std::string {
        for (auto it = mine.begin(); it != mine.end();) {
            uint64_t theirs = 0;
            const netplay::Wait w = s.remoteDigest(it->first, 1 - self, theirs, last ? 10000 : 0);
            if (w == netplay::Wait::Late) break;
            if (w == netplay::Wait::Gone) return last ? "the other side left before the end" : std::string();
            if (theirs != it->second) return "DRIFT at frame " + std::to_string(it->first);
            it = mine.erase(it);
        }
        return {};
    };

    core.drainAudio();
    for (int f = 0; f < frames; ++f) {
        const uint32_t frame = static_cast<uint32_t>(f);
        s.sendInputs(frame + static_cast<uint32_t>(d), scripted(frame, self));
        netplaywire::Pad local, remote;
        if (f >= d) {
            local = scripted(frame - static_cast<uint32_t>(d), self);
            if (s.remoteInputs(frame, 1 - self, remote, 10000) != netplay::Wait::Got) {
                std::fprintf(stderr, "[netplay] %s: no buttons for frame %d (%s)\n", role, f,
                             s.error().empty() ? "the other side stopped" : s.error().c_str());
                s.leave();
                return 1;
            }
        }
        core.setPad(0, toCore(self == 0 ? local : remote));
        core.setPad(1, toCore(self == 0 ? remote : local));
        core.runFrame();
        const uint64_t v = core.frameDigest();
        const std::vector<int16_t>& audio = core.drainAudio();
        uint64_t a = kBasis;
        mix(a, audio.data(), audio.size() * sizeof(int16_t));
        mix(run, &v, sizeof v);
        mix(run, &a, sizeof a);
        if ((f + 1) % 60 == 0) {
            s.sendDigest(frame, run);
            mine[frame] = run;
        }
        if (const std::string bad = compare(false); !bad.empty()) {
            std::fprintf(stderr, "[netplay] %s: %s\n", role, bad.c_str());
            s.leave();
            return 1;
        }
    }
    // The last stretch: a fingerprint of the very end, and every one still owed.
    s.sendDigest(static_cast<uint32_t>(frames), run);
    mine[static_cast<uint32_t>(frames)] = run;
    const std::string bad = compare(true);
    std::fprintf(stderr, "[netplay] %s: %d frames, digest %016llx, %s\n", role, frames,
                 static_cast<unsigned long long>(run), bad.empty() ? "IN STEP" : bad.c_str());
    s.leave();
    return bad.empty() ? 0 : 1;
}

}  // namespace netplayrun
