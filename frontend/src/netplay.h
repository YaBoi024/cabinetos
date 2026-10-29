// FORK (handheld): one netplay session between consoles (NETPLAY-PLAN.md,
// 6.1, 6.4 and 6.5): the connections, the handshake, and the per-frame
// exchange of buttons and fingerprints. Plain sockets, no SDL and no core, so
// --netplay-session-test can run every end in one process.
//
// Two to eight players. The host is player 1 (slot 0) and the guests follow in
// the order they joined. The host is the hub: every guest connects to it, and
// it passes each player's buttons and fingerprints on to the others. A frame
// is played only when every player's buttons for it are in hand; each side
// sends its own buttons `lead` frames ahead, so by the time a frame comes
// round the others' are usually here already.
//
// Nothing listens until listen() is called, and only on the address it is
// given. The first thing a guest must send is the host's code; nothing else
// is read before the handshake has passed. The host keeps the door open while
// guests gather (admit) and shuts it when it starts the game (start).
//
// A guest who leaves once the game is on does not end it: their buttons are
// neutral from the frame the host saw them go, the same for everyone. The game
// ends when the host leaves or when no guest is left.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "netplaywire.h"

namespace netplay {

// What each side is running. All must be equal for a session to start.
struct Identity {
    std::string core, coreVersion, gameHash;
    // A fingerprint of the shared saved game this side holds ("" for none).
    // Not required to match: when it differs, the host's saved game is sent.
    std::string saveHash;
};

// Got: here. Late: not yet. Gone: the session is over. Absent: that player has
// left, so there will be nothing from them (their buttons are neutral).
enum class Wait { Got, Late, Gone, Absent };

// Ask start() to choose the input delay itself.
constexpr int kAutoDelay = -1;

// What awaitStart() and admit() answer when the time is up and nothing is wrong.
extern const char* const kNotYet;       // "not yet"
extern const char* const kNobodyJoined;   // "nobody joined"

// Frames of delay that keep the other side's buttons from arriving late: the
// one-way trip, a margin for jitter, and a few milliseconds for the two
// consoles' refreshes not lining up, over the length of a frame. At least 2 and
// at most 8; a link that needs more is too slow to play well and is logged.
int chooseDelay(double rttMs, double rttVarMs, double fps);

class Session {
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // ---- the host ----

    // Open a port (0 picks a free one; port() says which). "" on success, or a plain reason.
    std::string listen(const std::string& bindAddress, uint16_t port);
    uint16_t port() const { return port_; }

    // Wait up to timeoutMs for one guest and take it through the handshake.
    // "" when a guest was admitted; kNobodyJoined when the time ran out; any
    // other answer is the reason a guest was turned away (the door stays open).
    // `maxPlayers` is the most the game can have, the host included; `fps`
    // and `savePack` as for start() and the shared saved game (netplaysave.h),
    // sent to the guest when the two fingerprints differ.
    std::string admit(uint16_t code, const Identity& me, int timeoutMs, int maxPlayers,
                      double fps = 60.0, const std::vector<uint8_t>* savePack = nullptr);
    // Players here now, the host included. Drops guests who have gone since.
    int here();
    // Shut the door and begin: every guest is told how many play, which one they
    // are, and the delay. `delay` is in frames; kAutoDelay picks it from the
    // slowest round trip the kernel measured while the guests knocked.
    std::string start(int delay);
    // Two players in one call: admit one guest, then start.
    std::string accept(uint16_t code, const Identity& me, int delay, int timeoutMs,
                       double fps = 60.0, const std::vector<uint8_t>* savePack = nullptr);

    // ---- a guest ----

    // Connect to a host and pass the handshake, and take the saved game if one is sent.
    std::string join(const std::string& address, uint16_t port, uint16_t code,
                     const Identity& me, int timeoutMs);
    // Wait for the host to begin. "" once it has; kNotYet when the time ran out
    // (ask again); otherwise why the game will not start.
    std::string awaitStart(int timeoutMs);

    // ---- both ----

    int delay() const { return delay_; }
    int players() const { return players_; }          // after start; at the start
    int localPlayer() const { return self_; }          // 0 is the host
    int maxPlayers() const { return maxPlayers_; }
    // Players waiting in the lobby, the host included, as the host last said.
    int lobbySize() const { return host_ ? static_cast<int>(links_.size()) + 1 : here_; }
    // The connection's smoothed round trip in milliseconds, as the kernel sees it
    // now; the slowest guest's for a host. -1 if unknown.
    double rttMs() const;
    double rttVarMs() const;

    // The host's saved game, as a guest received it: synced() says the host
    // sent one (possibly empty, meaning the host has none), receivedSave() is it.
    // On the host, sentSave() says one was sent to somebody.
    bool synced() const { return synced_; }
    bool sentSave() const { return sentSave_; }
    const std::vector<uint8_t>& receivedSave() const { return received_; }

    // The delay, kept up to date. sendLead(n) says how many frames ahead we need
    // the others' buttons (1 to 8). takeLead() is how many frames ahead we must
    // send ours, when that has changed (0 when it has not): the most any other
    // player asked for.
    void sendLead(int frames);
    int takeLead();

    // Our buttons for `frame`, and a fingerprint of a frame we have played.
    void sendInputs(uint32_t frame, const netplaywire::Pad& pad);
    void sendDigest(uint32_t frame, uint64_t digest);

    // Another player's buttons for `frame`, waiting up to timeoutMs (0 looks
    // and returns). Absent (and a neutral pad) once they have left.
    Wait remoteInputs(uint32_t frame, int slot, netplaywire::Pad& out, int timeoutMs);
    Wait remoteDigest(uint32_t frame, int slot, uint64_t& out, int timeoutMs);

    // The slot of a player who has left since the last call, or -1.
    int takeLeft();
    // How many are still playing, ourselves included.
    int playing() const;

    void leave();   // says goodbye and closes
    const std::string& error() const { return error_; }

private:
    // One connection: a guest on the host, the host on a guest.
    struct Link {
        int fd = -1;
        netplaywire::Parser parser;
        bool gone = false;
        int slot = -1;       // the player at the other end; a guest's is set by start()
        int need = 0;        // host: frames ahead this guest last asked for
        int told = 0;        // host: frames ahead we last told this guest to send
        double rtt = -1.0, rttVar = 0.0;   // host: as the kernel saw it at the handshake
    };
    struct PadsIn {          // the buttons that have come in for one frame
        std::array<netplaywire::Pad, netplaywire::kMaxPlayers> pad{};
        uint8_t have = 0;    // a bit for each slot
    };
    struct DigestsIn {       // the fingerprints that have come in for one frame
        std::array<uint64_t, netplaywire::kMaxPlayers> digest{};
        uint8_t have = 0;
    };

    bool sendTo(Link& l, const std::vector<uint8_t>& bytes);
    void sendToAll(const std::vector<uint8_t>& bytes, const Link* except = nullptr);
    bool readMessage(Link& l, netplaywire::Message& out, int timeoutMs);   // handshake only
    bool waitReadable(int timeoutMs);
    void drain();
    void drainLink(Link& l);
    void store(Link& l, const netplaywire::Message& m, bool& ok);
    void guestWent(Link& l);
    void relayLeads();
    void announceRoster();
    void close_();
    void closeLink(Link& l);
    std::string fail(const std::string& why);

    int listenFd_ = -1;
    uint16_t port_ = 0;
    bool host_ = false;
    bool started_ = false;
    bool gone_ = false;
    int delay_ = 0;
    int players_ = 0, maxPlayers_ = 2, self_ = 0, here_ = 1;
    double fps_ = 60.0, worstRtt_ = 0.0, worstVar_ = 0.0;
    std::string error_;
    std::vector<Link> links_;                        // host: the guests; guest: the host
    std::map<uint32_t, PadsIn> inputs_;
    std::map<uint32_t, DigestsIn> digests_;
    std::array<uint32_t, netplaywire::kMaxPlayers> lastFrame_{};   // host: newest buttons seen from each
    std::array<bool, netplaywire::kMaxPlayers> left_{};
    std::array<uint32_t, netplaywire::kMaxPlayers> leftFrom_{};
    std::vector<int> departures_;
    int hostNeed_ = 0;       // host: what the host itself asked for
    bool leadsDirty_ = false;   // host: somebody's ask changed
    int taken_ = 0;          // host: the lead last handed out by takeLead
    int peerLead_ = 0;       // guest: the newest ask from the host
    bool synced_ = false;
    bool sentSave_ = false;
    std::vector<uint8_t> received_;
};

// --netplay-session-test: every end in one process over 127.0.0.1. 0 when all pass.
int test();

}  // namespace netplay
