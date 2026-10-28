// FORK (handheld): see netplay.h.

#include "netplay.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <thread>
#include <vector>

namespace netplay {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kHandshakeMs = 5000;    // the joiner must pass the handshake in this long
constexpr int kMaxDelay = 30;         // frames
constexpr size_t kMaxWaiting = 256;   // buttons or fingerprints held for frames not yet played

int msLeft(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return left.count() < 0 ? 0 : static_cast<int>(left.count());
}

void noDelay(int fd) {
    const int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
}

}  // namespace

Session::~Session() {
    leave();
    if (listenFd_ >= 0) ::close(listenFd_);
}

std::string Session::fail(const std::string& why) {
    error_ = why;
    return why;
}

void Session::close_() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    gone_ = true;
}

bool Session::sendAll(const std::vector<uint8_t>& bytes) {
    if (fd_ < 0 || gone_ || bytes.empty()) return false;
    size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = ::send(fd_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            gone_ = true;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool Session::waitReadable(int timeoutMs) {
    if (fd_ < 0) return false;
    pollfd p{fd_, POLLIN, 0};
    const int r = ::poll(&p, 1, timeoutMs);
    return r > 0;
}

// Handshake only: the next whole message, within timeoutMs.
bool Session::readMessage(netplaywire::Message& out, int timeoutMs) {
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        if (parser_.next(out)) return true;
        if (parser_.failed()) {
            error_ = "the other side sent something that is not netplay";
            return false;
        }
        const int left = msLeft(deadline);
        if (left == 0 || !waitReadable(left)) {
            error_ = "the other side did not answer";
            return false;
        }
        uint8_t buf[512];
        const ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
        if (n <= 0) {
            error_ = "the other side closed the connection";
            return false;
        }
        parser_.feed(buf, static_cast<size_t>(n));
    }
}

std::string Session::listen(const std::string& bindAddress, uint16_t port) {
    if (listenFd_ >= 0) return fail("already listening");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1)
        return fail("not an address: " + bindAddress);
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return fail(std::string("no socket: ") + std::strerror(errno));
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(fd, 1) != 0) {
        const std::string why = std::string("cannot listen: ") + std::strerror(errno);
        ::close(fd);
        return fail(why);
    }
    socklen_t len = sizeof addr;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    listenFd_ = fd;
    return {};
}

int chooseDelay(double rttMs, double rttVarMs, double fps) {
    if (fps < 1.0) fps = 60.0;
    const double frameMs = 1000.0 / fps;
    const double needMs = rttMs / 2.0 + 2.0 * rttVarMs + 4.0;
    int frames = static_cast<int>(std::ceil(needMs / frameMs));
    // Never below 2: that is the delay every session was played at, flawless,
    // before it was chosen from the round trip. One frame stalls whenever the
    // two screens' refreshes fall apart.
    if (frames < 2) frames = 2;
    if (frames > 8) frames = 8;
    return frames;
}

double Session::rttMs() const {
    if (fd_ < 0) return -1.0;
    tcp_info info{};
    socklen_t len = sizeof info;
    if (::getsockopt(fd_, IPPROTO_TCP, TCP_INFO, &info, &len) != 0) return -1.0;
    return info.tcpi_rtt / 1000.0;
}

double Session::rttVarMs() const {
    if (fd_ < 0) return 0.0;
    tcp_info info{};
    socklen_t len = sizeof info;
    if (::getsockopt(fd_, IPPROTO_TCP, TCP_INFO, &info, &len) != 0) return 0.0;
    return info.tcpi_rttvar / 1000.0;
}

void Session::sendLead(int frames) {
    netplaywire::Message m;
    m.type = netplaywire::Type::Lead;
    m.lead = static_cast<uint8_t>(std::max(1, std::min(frames, netplaywire::kMaxLead)));
    sendAll(netplaywire::encode(m));
}

int Session::takeLead() {
    const int v = peerLead_;
    peerLead_ = 0;
    return v;
}

std::string Session::accept(uint16_t code, const Identity& me, int delay, int timeoutMs,
                            double fps, const std::vector<uint8_t>* savePack) {
    if (listenFd_ < 0) return fail("not listening");
    if (delay != kAutoDelay && (delay < 0 || delay > kMaxDelay)) return fail("bad delay");
    pollfd p{listenFd_, POLLIN, 0};
    if (::poll(&p, 1, timeoutMs) <= 0) return fail("nobody joined");
    fd_ = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
    // One joiner only: nothing listens from here on.
    ::close(listenFd_);
    listenFd_ = -1;
    if (fd_ < 0) return fail(std::string("accept failed: ") + std::strerror(errno));
    noDelay(fd_);
    host_ = true;
    delay_ = delay == kAutoDelay ? 1 : delay;   // chosen below, once the joiner has passed
    gone_ = false;

    netplaywire::Message hello;
    if (!readMessage(hello, kHandshakeMs)) {
        const std::string why = error_;
        close_();
        return fail(why);
    }
    // The code is checked before anything else is looked at.
    std::string why;
    if (hello.type != netplaywire::Type::Hello) why = "the first message was not Hello";
    else if (hello.code != code) why = "wrong code";
    else if (hello.version != netplaywire::kVersion) why = "different netplay version";
    else if (hello.players != 2) why = "only two players are supported";
    else if (hello.core != me.core) why = "different core";
    else if (hello.coreVersion != me.coreVersion) why = "different core version";
    else if (hello.gameHash != me.gameHash) why = "different game file";
    if (!why.empty()) {
        netplaywire::Message no;
        no.type = netplaywire::Type::Refuse;
        no.reason = why;
        sendAll(netplaywire::encode(no));
        close_();
        return fail(why);
    }
    if (delay == kAutoDelay) {
        // The kernel timed the connection while the joiner knocked.
        tcp_info info{};
        socklen_t len = sizeof info;
        double rtt = 0.0, var = 0.0;
        if (::getsockopt(fd_, IPPROTO_TCP, TCP_INFO, &info, &len) == 0) {
            rtt = info.tcpi_rtt / 1000.0;
            var = info.tcpi_rttvar / 1000.0;
        }
        delay_ = chooseDelay(rtt, var, fps);
        std::fprintf(stderr, "[netplay] round trip %.1f ms (+/- %.1f), %d frames of delay\n", rtt,
                     var, delay_);
    }
    // Different saved games: the host's is the one both play on.
    const bool sync = hello.saveHash != me.saveHash;
    const std::vector<uint8_t> noSave;
    const std::vector<uint8_t>& pack = savePack ? *savePack : noSave;
    netplaywire::Message yes;
    yes.type = netplaywire::Type::Accept;
    yes.delay = static_cast<uint8_t>(delay_);
    yes.players = 2;
    yes.sync = sync ? 1 : 0;
    yes.saveSize = sync ? static_cast<uint32_t>(pack.size()) : 0;
    if (pack.size() > netplaywire::kMaxSave) {
        close_();
        return fail("the saved game is too big to share");
    }
    if (!sendAll(netplaywire::encode(yes))) {
        close_();
        return fail("could not answer the joiner");
    }
    if (sync) {
        for (size_t at = 0; at < pack.size(); at += netplaywire::kMaxSaveChunk) {
            netplaywire::Message piece;
            piece.type = netplaywire::Type::Save;
            piece.saveSize = static_cast<uint32_t>(pack.size());
            piece.saveOffset = static_cast<uint32_t>(at);
            const size_t n = std::min(netplaywire::kMaxSaveChunk, pack.size() - at);
            piece.saveBytes.assign(pack.begin() + static_cast<long>(at),
                                   pack.begin() + static_cast<long>(at + n));
            if (!sendAll(netplaywire::encode(piece))) {
                close_();
                return fail("could not send the saved game");
            }
        }
        sentSave_ = true;
        std::fprintf(stderr, "[netplay] sent the host's saved game (%zu bytes)\n", pack.size());
    }
    return {};
}

std::string Session::join(const std::string& address, uint16_t port, uint16_t code,
                          const Identity& me, int timeoutMs) {
    if (fd_ >= 0) return fail("already connected");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1)
        return fail("not an address: " + address);
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return fail(std::string("no socket: ") + std::strerror(errno));
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 &&
        errno != EINPROGRESS) {
        const std::string why = std::string("cannot connect: ") + std::strerror(errno);
        ::close(fd);
        return fail(why);
    }
    pollfd p{fd, POLLOUT, 0};
    int soError = 0;
    socklen_t soLen = sizeof soError;
    if (::poll(&p, 1, timeoutMs) <= 0 ||
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &soLen) != 0 || soError != 0) {
        ::close(fd);
        return fail("cannot reach the host");
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    noDelay(fd);
    fd_ = fd;
    host_ = false;
    gone_ = false;

    netplaywire::Message hello;
    hello.type = netplaywire::Type::Hello;
    hello.code = code;
    hello.players = 2;
    hello.core = me.core;
    hello.coreVersion = me.coreVersion;
    hello.gameHash = me.gameHash;
    hello.saveHash = me.saveHash;
    const std::vector<uint8_t> bytes = netplaywire::encode(hello);
    if (bytes.empty() || !sendAll(bytes)) {
        close_();
        return fail("could not send Hello");
    }
    netplaywire::Message answer;
    if (!readMessage(answer, kHandshakeMs)) {
        const std::string why = error_;
        close_();
        return fail(why);
    }
    if (answer.type == netplaywire::Type::Refuse) {
        close_();
        return fail("the host said no: " + answer.reason);
    }
    if (answer.type != netplaywire::Type::Accept || answer.delay > kMaxDelay ||
        answer.players != 2) {
        close_();
        return fail("the host's answer makes no sense");
    }
    delay_ = answer.delay;
    synced_ = false;
    received_.clear();
    if (answer.sync) {
        // The host's saved game follows, in pieces, in order.
        received_.reserve(answer.saveSize);
        while (received_.size() < answer.saveSize) {
            netplaywire::Message piece;
            if (!readMessage(piece, 30000)) {
                const std::string why = error_;
                close_();
                return fail("the saved game did not arrive: " + why);
            }
            if (piece.type != netplaywire::Type::Save || piece.saveSize != answer.saveSize ||
                piece.saveOffset != received_.size()) {
                close_();
                return fail("the saved game arrived out of order");
            }
            received_.insert(received_.end(), piece.saveBytes.begin(), piece.saveBytes.end());
        }
        synced_ = true;
    }
    return {};
}

void Session::sendInputs(uint32_t frame, const netplaywire::Pad& pad) {
    netplaywire::Message m;
    m.type = netplaywire::Type::Inputs;
    m.frame = frame;
    m.pad = pad;
    sendAll(netplaywire::encode(m));
}

void Session::sendDigest(uint32_t frame, uint64_t digest) {
    netplaywire::Message m;
    m.type = netplaywire::Type::Digest;
    m.frame = frame;
    m.digest = digest;
    sendAll(netplaywire::encode(m));
}

// Takes in everything that has arrived, without waiting.
void Session::drain() {
    if (fd_ < 0) return;
    for (;;) {
        netplaywire::Message m;
        while (parser_.next(m)) {
            switch (m.type) {
                case netplaywire::Type::Inputs:
                    if (inputs_.size() >= kMaxWaiting) {
                        error_ = "too many buttons ahead";
                        gone_ = true;
                        return;
                    }
                    inputs_[m.frame] = m.pad;
                    break;
                case netplaywire::Type::Digest:
                    if (digests_.size() >= kMaxWaiting) {
                        error_ = "too many fingerprints ahead";
                        gone_ = true;
                        return;
                    }
                    digests_[m.frame] = m.digest;
                    break;
                case netplaywire::Type::Lead:
                    peerLead_ = m.lead;
                    break;
                case netplaywire::Type::Bye:
                case netplaywire::Type::Refuse:
                    gone_ = true;
                    break;
                default:
                    error_ = "unexpected message in a session";
                    gone_ = true;
                    return;
            }
        }
        if (parser_.failed()) {
            error_ = "the other side sent something that is not netplay";
            gone_ = true;
            return;
        }
        uint8_t buf[512];
        const ssize_t n = ::recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            parser_.feed(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) gone_ = true;
        if (n < 0 && errno == EINTR) continue;
        return;   // nothing more for now
    }
}

Wait Session::remoteInputs(uint32_t frame, netplaywire::Pad& out, int timeoutMs) {
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        drain();
        if (auto it = inputs_.find(frame); it != inputs_.end()) {
            out = it->second;
            inputs_.erase(inputs_.begin(), std::next(it));   // this frame and any older
            return Wait::Got;
        }
        if (gone_ || fd_ < 0) return Wait::Gone;
        const int left = msLeft(deadline);
        if (left == 0) return Wait::Late;
        waitReadable(left);
    }
}

Wait Session::remoteDigest(uint32_t frame, uint64_t& out, int timeoutMs) {
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        drain();
        if (auto it = digests_.find(frame); it != digests_.end()) {
            out = it->second;
            digests_.erase(digests_.begin(), std::next(it));
            return Wait::Got;
        }
        if (gone_ || fd_ < 0) return Wait::Gone;
        const int left = msLeft(deadline);
        if (left == 0) return Wait::Late;
        waitReadable(left);
    }
}

void Session::leave() {
    if (fd_ >= 0 && !gone_) {
        netplaywire::Message bye;
        bye.type = netplaywire::Type::Bye;
        sendAll(netplaywire::encode(bye));
    }
    close_();
}

// ---- --netplay-session-test -------------------------------------------------
// Both ends in one process, each on its own thread, over 127.0.0.1.

namespace {

netplaywire::Pad scripted(uint32_t frame, int player) {
    const uint32_t x = (frame * 2654435761u) ^ (static_cast<uint32_t>(player + 1) * 40503u);
    netplaywire::Pad p;
    p.buttons = static_cast<uint16_t>(x >> 8);
    p.leftX = static_cast<int16_t>(x >> 3);
    p.leftTrigger = static_cast<uint8_t>(x >> 5);
    return p;
}

uint64_t mixPad(uint64_t h, const netplaywire::Pad& p) {
    const uint8_t b[] = {static_cast<uint8_t>(p.buttons >> 8), static_cast<uint8_t>(p.buttons),
                         static_cast<uint8_t>(p.leftX >> 8), static_cast<uint8_t>(p.leftX),
                         p.leftTrigger};
    for (uint8_t v : b) {
        h ^= v;
        h *= 1099511628211ull;
    }
    return h;
}

// One side of a lockstep run with no core: the "game" is a running hash of
// both players' buttons. Returns "" when it finished and every fingerprint
// matched, else why not. `flipAt` makes this side's state drift at that frame.
std::string playLockstep(Session& s, uint32_t frames, int flipAt) {
    const int me = s.localPlayer();
    const uint32_t d = static_cast<uint32_t>(s.delay());
    uint64_t hash = 1469598103934665603ull;
    std::map<uint32_t, uint64_t> mine;
    auto compare = [&](bool last) -> std::string {
        for (auto it = mine.begin(); it != mine.end();) {
            uint64_t theirs = 0;
            const Wait w = s.remoteDigest(it->first, theirs, last ? 3000 : 0);
            if (w == Wait::Late) break;
            if (w == Wait::Gone) return last ? "the other side left before the end" : std::string();
            if (theirs != it->second) return "DRIFT at frame " + std::to_string(it->first);
            it = mine.erase(it);
        }
        return {};
    };
    for (uint32_t f = 0; f < frames; ++f) {
        s.sendInputs(f + d, scripted(f, me));
        netplaywire::Pad local, remote;
        if (f >= d) {
            local = scripted(f - d, me);
            const Wait w = s.remoteInputs(f, remote, 3000);
            if (w != Wait::Got) {
                s.leave();
                return "no buttons for frame " + std::to_string(f);
            }
        }
        hash = me == 0 ? mixPad(mixPad(hash, local), remote) : mixPad(mixPad(hash, remote), local);
        if (flipAt >= 0 && static_cast<int>(f) == flipAt) hash ^= 1;
        if ((f + 1) % 60 == 0) {
            s.sendDigest(f, hash);
            mine[f] = hash;
        }
        if (std::string why = compare(false); !why.empty()) {
            s.leave();   // stop the game, as a real session would
            return why;
        }
    }
    return compare(true);
}

}  // namespace

int test() {
    int bad = 0;
    auto check = [&bad](bool ok, const char* what) {
        std::fprintf(stderr, "[session] %-58s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++bad;
    };
    const Identity id{"snes9x", "Snes9x 1.63", "00ff00ff00ff00ff", ""};

    check(chooseDelay(0, 0, 60) == 2, "no round trip: 2 frames, the least");
    check(chooseDelay(20, 2, 60) == 2, "20 ms round trip at 60 Hz: 2 frames");
    check(chooseDelay(60, 5, 60) == 3, "60 ms round trip at 60 Hz: 3 frames");
    check(chooseDelay(20, 2, 50) == 2, "20 ms round trip at 50 Hz: 2 frames, the least");
    check(chooseDelay(900, 50, 60) == 8, "a very slow link is held at 8 frames");

    // A host and a joiner, each on a thread. Returns the host's and the joiner's answers.
    struct Pair {
        Session host, join;
        std::string hostErr, joinErr;
    };
    auto connect = [&](Pair& p, uint16_t hostCode, uint16_t joinCode, const Identity& hostId,
                       const Identity& joinId, int delay) {
        const std::string e = p.host.listen("127.0.0.1", 0);
        if (!e.empty()) {
            p.hostErr = e;
            return;
        }
        std::thread t([&] { p.joinErr = p.join.join("127.0.0.1", p.host.port(), joinCode, joinId, 3000); });
        p.hostErr = p.host.accept(hostCode, hostId, delay, 3000);
        t.join();
    };

    {
        Pair p;
        connect(p, 4821, 4821, id, id, 3);
        check(p.hostErr.empty() && p.joinErr.empty() && p.join.delay() == 3 &&
                  p.host.localPlayer() == 0 && p.join.localPlayer() == 1,
              "the right code and the same game connect");
    }
    {
        Pair p;
        connect(p, 4821, 4821, id, id, kAutoDelay);
        check(p.hostErr.empty() && p.joinErr.empty() && p.join.delay() >= 2 &&
                  p.join.delay() <= 8 && p.join.delay() == p.host.delay(),
              "an automatic delay is chosen, and both sides agree on it");
    }
    {
        // Two saved games that differ: the host's is sent, whole, in pieces.
        Pair p;
        Identity hostId = id, joinId = id;
        hostId.saveHash = "aaaaaaaaaaaaaaaa";
        joinId.saveHash = "bbbbbbbbbbbbbbbb";
        std::vector<uint8_t> big(300000);
        for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 31 + i / 251);
        p.host.listen("127.0.0.1", 0);
        std::thread t([&] { p.joinErr = p.join.join("127.0.0.1", p.host.port(), 4821, joinId, 3000); });
        p.hostErr = p.host.accept(4821, hostId, 2, 3000, 60.0, &big);
        t.join();
        check(p.hostErr.empty() && p.joinErr.empty() && p.host.sentSave() && p.join.synced() &&
                  p.join.receivedSave() == big,
              "a different saved game is sent to the joiner, byte for byte");
    }
    {
        // The same saved game on both sides: nothing is sent.
        Pair p;
        Identity hostId = id, joinId = id;
        hostId.saveHash = joinId.saveHash = "cccccccccccccccc";
        std::vector<uint8_t> pack(1000, 7);
        p.host.listen("127.0.0.1", 0);
        std::thread t([&] { p.joinErr = p.join.join("127.0.0.1", p.host.port(), 4821, joinId, 3000); });
        p.hostErr = p.host.accept(4821, hostId, 2, 3000, 60.0, &pack);
        t.join();
        check(p.hostErr.empty() && p.joinErr.empty() && !p.host.sentSave() && !p.join.synced(),
              "the same saved game on both sides sends nothing");
    }
    {
        // The host has none and the joiner has one: the joiner is told to have none.
        Pair p;
        Identity joinId = id;
        joinId.saveHash = "dddddddddddddddd";
        p.host.listen("127.0.0.1", 0);
        std::thread t([&] { p.joinErr = p.join.join("127.0.0.1", p.host.port(), 4821, joinId, 3000); });
        p.hostErr = p.host.accept(4821, id, 2, 3000, 60.0, nullptr);
        t.join();
        check(p.hostErr.empty() && p.joinErr.empty() && p.join.synced() &&
                  p.join.receivedSave().empty(),
              "a host with no saved game tells the joiner to start with none");
        p.host.sendLead(5);
        int got = 0;
        for (int i = 0; i < 100 && !got; ++i) {
            netplaywire::Pad pad;
            p.join.remoteInputs(1, pad, 5);   // reads what has arrived
            got = p.join.takeLead();
        }
        check(got == 5, "a Lead request reaches the other side");
    }
    {
        Pair p;
        connect(p, 4821, 1111, id, id, 3);
        check(!p.hostErr.empty() && p.joinErr.find("wrong code") != std::string::npos,
              "a wrong code is refused, and the joiner is told why");
    }
    {
        Pair p;
        Identity other = id;
        other.gameHash = "1111111111111111";
        connect(p, 4821, 4821, id, other, 3);
        check(!p.hostErr.empty() && p.joinErr.find("different game file") != std::string::npos,
              "a different game file is refused");
    }
    {
        Pair p;
        Identity other = id;
        other.coreVersion = "Snes9x 1.62";
        connect(p, 4821, 4821, id, other, 3);
        check(!p.hostErr.empty() && p.joinErr.find("different core version") != std::string::npos,
              "a different core version is refused");
    }
    {
        Session host;
        host.listen("127.0.0.1", 0);
        const std::string e = host.accept(4821, id, 2, 150);
        check(!e.empty(), "a host nobody joins gives up");
    }
    {
        // Something that is not netplay, sent to a host.
        Session host;
        host.listen("127.0.0.1", 0);
        const uint16_t port = host.port();
        std::thread t([port] {
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port);
            ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
            if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
                const char junk[] = "GET / HTTP/1.1\r\n\r\n";
                ::send(fd, junk, sizeof junk, MSG_NOSIGNAL);
                uint8_t sink[64];
                ::recv(fd, sink, sizeof sink, 0);
            }
            ::close(fd);
        });
        const std::string e = host.accept(4821, id, 2, 3000);
        t.join();
        check(!e.empty(), "a web request sent to the port is turned away");
    }
    for (const int delay : {0, 2, 5}) {
        Pair p;
        connect(p, 4821, 4821, id, id, delay);
        std::string hostEnd, joinEnd;
        std::thread t([&] { joinEnd = playLockstep(p.join, 600, -1); });
        hostEnd = playLockstep(p.host, 600, -1);
        t.join();
        char what[96];
        std::snprintf(what, sizeof what, "600 frames in lockstep with a delay of %d", delay);
        check(hostEnd.empty() && joinEnd.empty(), what);
    }
    {
        Pair p;
        connect(p, 4821, 4821, id, id, 2);
        std::string hostEnd, joinEnd;
        std::thread t([&] { joinEnd = playLockstep(p.join, 300, 130); });
        hostEnd = playLockstep(p.host, 300, -1);
        t.join();
        // The side that sees it first stops; the other may see the fingerprints too, or the stop.
        check((hostEnd.find("DRIFT") != std::string::npos ||
               joinEnd.find("DRIFT") != std::string::npos) &&
                  !hostEnd.empty() && !joinEnd.empty(),
              "a drifting side is caught at the fingerprint, and both stop");
    }
    {
        Pair p;
        connect(p, 4821, 4821, id, id, 2);
        netplaywire::Pad got;
        check(p.host.remoteInputs(7, got, 60) == Wait::Late, "buttons that have not come are Late");
        p.join.sendInputs(7, scripted(7, 1));
        check(p.host.remoteInputs(7, got, 1000) == Wait::Got && got == scripted(7, 1),
              "buttons that come are Got, exactly");
        p.join.leave();
        check(p.host.remoteInputs(8, got, 1000) == Wait::Gone, "a side that leaves is Gone");
    }
    std::fprintf(stderr, "[session] %s\n", bad == 0 ? "PASS" : "FAIL");
    return bad == 0 ? 0 : 1;
}

}  // namespace netplay
