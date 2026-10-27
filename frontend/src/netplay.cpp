// FORK (handheld): see netplay.h.

#include "netplay.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
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

std::string Session::accept(uint16_t code, const Identity& me, int delay, int timeoutMs) {
    if (listenFd_ < 0) return fail("not listening");
    if (delay < 0 || delay > kMaxDelay) return fail("bad delay");
    pollfd p{listenFd_, POLLIN, 0};
    if (::poll(&p, 1, timeoutMs) <= 0) return fail("nobody joined");
    fd_ = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
    // One joiner only: nothing listens from here on.
    ::close(listenFd_);
    listenFd_ = -1;
    if (fd_ < 0) return fail(std::string("accept failed: ") + std::strerror(errno));
    noDelay(fd_);
    host_ = true;
    delay_ = delay;
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
    netplaywire::Message yes;
    yes.type = netplaywire::Type::Accept;
    yes.delay = static_cast<uint8_t>(delay);
    yes.players = 2;
    if (!sendAll(netplaywire::encode(yes))) {
        close_();
        return fail("could not answer the joiner");
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
    const Identity id{"snes9x", "Snes9x 1.63", "00ff00ff00ff00ff"};

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
