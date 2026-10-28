// FORK (handheld): one netplay session between two consoles (NETPLAY-PLAN.md,
// 6.1, 6.4 and 6.5): the connection, the handshake, and the per-frame
// exchange of buttons and fingerprints. Plain sockets, no SDL and no core, so
// --netplay-session-test can run both ends in one process.
//
// Two players. The host is player 1 (port 0), the joiner player 2 (port 1).
// A frame is played only when both players' buttons for it are in hand; the
// caller sends its own buttons `delay` frames ahead, so by the time a frame
// comes round the other side's are usually here already.
//
// Nothing listens until listen() is called, and only on the address it is
// given. The first thing a joiner must send is the host's code; nothing else
// is read before the handshake has passed.

#pragma once

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

enum class Wait { Got, Late, Gone };

// Ask accept() to choose the input delay itself.
constexpr int kAutoDelay = -1;

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

    // Host: open a port (0 picks a free one; port() says which), then accept.
    // Each returns "" on success, or a plain reason.
    std::string listen(const std::string& bindAddress, uint16_t port);
    // `delay` is in frames; kAutoDelay picks it from the round trip the kernel
    // measured while the joiner connected (chooseDelay), for a core running at `fps`.
    //
    // `savePack` is the host's shared saved game (netplaysave.h), sent to the joiner
    // when the two fingerprints differ.
    std::string accept(uint16_t code, const Identity& me, int delay, int timeoutMs,
                       double fps = 60.0, const std::vector<uint8_t>* savePack = nullptr);
    uint16_t port() const { return port_; }

    // Joiner: connect to a host and pass the handshake.
    std::string join(const std::string& address, uint16_t port, uint16_t code,
                     const Identity& me, int timeoutMs);

    int delay() const { return delay_; }
    // The connection's smoothed round trip in milliseconds, as the kernel sees it
    // now (it keeps measuring from the acknowledgements of the buttons); -1 if unknown.
    double rttMs() const;
    double rttVarMs() const;

    // The host's saved game, as the joiner received it: synced() says the host
    // sent one (possibly empty, meaning the host has none), receivedSave() is it.
    // On the host, sentSave() says it was sent.
    bool synced() const { return synced_; }
    bool sentSave() const { return sentSave_; }
    const std::vector<uint8_t>& receivedSave() const { return received_; }

    // Ask the other side to send its buttons at least `frames` ahead (1 to 8),
    // and the newest such ask from them (0 when there is none new).
    void sendLead(int frames);
    int takeLead();
    int localPlayer() const { return host_ ? 0 : 1; }

    // Our buttons for `frame`, and a fingerprint of a frame we have played.
    void sendInputs(uint32_t frame, const netplaywire::Pad& pad);
    void sendDigest(uint32_t frame, uint64_t digest);

    // The other side's buttons for `frame`, waiting up to timeoutMs (0 looks
    // and returns). Gone when they left or the connection broke.
    Wait remoteInputs(uint32_t frame, netplaywire::Pad& out, int timeoutMs);
    Wait remoteDigest(uint32_t frame, uint64_t& out, int timeoutMs);

    void leave();   // says goodbye and closes
    const std::string& error() const { return error_; }

private:
    bool sendAll(const std::vector<uint8_t>& bytes);
    bool readMessage(netplaywire::Message& out, int timeoutMs);   // handshake only
    void drain();
    bool waitReadable(int timeoutMs);
    void close_();
    std::string fail(const std::string& why);

    int listenFd_ = -1;
    int fd_ = -1;
    uint16_t port_ = 0;
    bool host_ = false;
    bool gone_ = false;
    int delay_ = 0;
    std::string error_;
    netplaywire::Parser parser_;
    std::map<uint32_t, netplaywire::Pad> inputs_;
    std::map<uint32_t, uint64_t> digests_;
    bool synced_ = false;
    bool sentSave_ = false;
    std::vector<uint8_t> received_;
    int peerLead_ = 0;
};

// --netplay-session-test: both ends in one process over 127.0.0.1. 0 when all pass.
int test();

}  // namespace netplay
