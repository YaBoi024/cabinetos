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
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace netplay {

const char* const kNotYet = "not yet";
const char* const kNobodyJoined = "nobody joined";

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kHandshakeMs = 5000;    // a guest must pass the handshake in this long
constexpr int kMaxDelay = 30;         // frames
constexpr size_t kMaxWaiting = 256;   // frames of buttons or fingerprints held for frames not yet played

int msLeft(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return left.count() < 0 ? 0 : static_cast<int>(left.count());
}

void noDelay(int fd) {
    const int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
}

bool waitFd(int fd, int timeoutMs) {
    if (fd < 0) return false;
    pollfd p{fd, POLLIN, 0};
    return ::poll(&p, 1, timeoutMs) > 0;
}

// The kernel's own timing of a connection, in milliseconds; false if unknown.
bool timing(int fd, double& rtt, double& var) {
    if (fd < 0) return false;
    tcp_info info{};
    socklen_t len = sizeof info;
    if (::getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len) != 0) return false;
    rtt = info.tcpi_rtt / 1000.0;
    var = info.tcpi_rttvar / 1000.0;
    return true;
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

void Session::closeLink(Link& l) {
    if (l.fd >= 0) ::close(l.fd);
    l.fd = -1;
    l.gone = true;
}

void Session::close_() {
    for (Link& l : links_) closeLink(l);
    gone_ = true;
}

bool Session::sendTo(Link& l, const std::vector<uint8_t>& bytes) {
    if (l.fd < 0 || l.gone || bytes.empty()) return false;
    size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = ::send(l.fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        // A failed send does not end the link: the reading side finds the
        // close or reset, in order, after whatever came before it, and handles
        // the departure in one place (guestWent, or gone_ on a guest).
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

void Session::sendToAll(const std::vector<uint8_t>& bytes, const Link* except) {
    for (Link& l : links_)
        if (&l != except) sendTo(l, bytes);
}

// Waits for any connection to have something to read.
bool Session::waitReadable(int timeoutMs) {
    std::vector<pollfd> p;
    for (const Link& l : links_)
        if (l.fd >= 0 && !l.gone) p.push_back({l.fd, POLLIN, 0});
    if (p.empty()) return false;
    return ::poll(p.data(), p.size(), timeoutMs) > 0;
}

// Handshake only: the next whole message from one connection, within timeoutMs.
bool Session::readMessage(Link& l, netplaywire::Message& out, int timeoutMs) {
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        if (l.parser.next(out)) return true;
        if (l.parser.failed()) {
            error_ = "the other side sent something that is not netplay";
            return false;
        }
        const int left = msLeft(deadline);
        if (left == 0 || !waitFd(l.fd, left)) {
            error_ = "the other side did not answer";
            return false;
        }
        uint8_t buf[512];
        const ssize_t n = ::recv(l.fd, buf, sizeof buf, 0);
        if (n <= 0) {
            error_ = "the other side closed the connection";
            return false;
        }
        l.parser.feed(buf, static_cast<size_t>(n));
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
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 ||
        ::listen(fd, netplaywire::kMaxPlayers) != 0) {
        const std::string why = std::string("cannot listen: ") + std::strerror(errno);
        ::close(fd);
        return fail(why);
    }
    socklen_t len = sizeof addr;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    listenFd_ = fd;
    host_ = true;
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
    double worst = -1.0;
    for (const Link& l : links_) {
        double rtt = 0.0, var = 0.0;
        if (!l.gone && timing(l.fd, rtt, var)) worst = std::max(worst, rtt);
    }
    return worst;
}

double Session::rttVarMs() const {
    double worst = 0.0;
    for (const Link& l : links_) {
        double rtt = 0.0, var = 0.0;
        if (!l.gone && timing(l.fd, rtt, var)) worst = std::max(worst, var);
    }
    return worst;
}

// ---- the host: gathering guests, and starting -------------------------------

void Session::announceRoster() {
    netplaywire::Message m;
    m.type = netplaywire::Type::Roster;
    m.players = static_cast<uint8_t>(links_.size() + 1);
    sendToAll(netplaywire::encode(m));
}

int Session::here() {
    if (!host_) return here_;
    if (started_) return playing();
    // The lobby: take in whatever the guests have said (only goodbyes are
    // expected) and let go of those who have gone.
    const size_t before = links_.size();
    for (Link& l : links_) {
        for (; !l.gone;) {
            netplaywire::Message m;
            if (l.parser.next(m) || l.parser.failed()) {
                closeLink(l);   // a guest may say goodbye, and nothing else, before the game
                break;
            }
            uint8_t buf[512];
            const ssize_t n = ::recv(l.fd, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                l.parser.feed(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                closeLink(l);
            break;
        }
    }
    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                [](const Link& l) { return l.gone; }),
                 links_.end());
    if (links_.size() != before) announceRoster();
    return static_cast<int>(links_.size()) + 1;
}

std::string Session::admit(uint16_t code, const Identity& me, int timeoutMs, int maxPlayers,
                           double fps, const std::vector<uint8_t>* savePack) {
    if (listenFd_ < 0) return fail("not listening");
    if (started_) return fail("already started");
    if (maxPlayers < 2 || maxPlayers > netplaywire::kMaxPlayers) return fail("bad number of players");
    maxPlayers_ = maxPlayers;
    fps_ = fps;
    here();   // let go of guests who have gone since last time
    pollfd p{listenFd_, POLLIN, 0};
    if (::poll(&p, 1, timeoutMs) <= 0) return fail(kNobodyJoined);
    const int fd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return fail(std::string("accept failed: ") + std::strerror(errno));
    noDelay(fd);
    Link g;
    g.fd = fd;

    netplaywire::Message hello;
    if (!readMessage(g, hello, kHandshakeMs)) {
        const std::string why = error_;
        closeLink(g);
        return fail(why);
    }
    // The code is checked before anything else is looked at.
    std::string why;
    if (hello.type != netplaywire::Type::Hello) why = "the first message was not Hello";
    else if (hello.code != code) why = "wrong code";
    else if (hello.version != netplaywire::kVersion) why = "different netplay version";
    else if (hello.players != 1) why = "one player from each console";
    else if (static_cast<int>(links_.size()) + 1 >= maxPlayers) why = "the game is full";
    else if (hello.core != me.core) why = "different core";
    else if (hello.coreVersion != me.coreVersion) why = "different core version";
    else if (hello.gameHash != me.gameHash) why = "different game file";
    if (!why.empty()) {
        netplaywire::Message no;
        no.type = netplaywire::Type::Refuse;
        no.reason = why;
        sendTo(g, netplaywire::encode(no));
        closeLink(g);
        return fail(why);
    }
    // The kernel timed the connection while the guest knocked.
    if (timing(g.fd, g.rtt, g.rttVar)) {
        worstRtt_ = std::max(worstRtt_, g.rtt);
        worstVar_ = std::max(worstVar_, g.rttVar);
    }
    // Different saved games: the host's is the one all play on.
    const bool sync = hello.saveHash != me.saveHash;
    const std::vector<uint8_t> noSave;
    const std::vector<uint8_t>& pack = savePack ? *savePack : noSave;
    if (sync && pack.size() > netplaywire::kMaxSave) {
        closeLink(g);
        return fail("the saved game is too big to share");
    }
    netplaywire::Message yes;
    yes.type = netplaywire::Type::Accept;
    yes.players = static_cast<uint8_t>(maxPlayers);
    yes.sync = sync ? 1 : 0;
    yes.saveSize = sync ? static_cast<uint32_t>(pack.size()) : 0;
    if (!sendTo(g, netplaywire::encode(yes))) {
        closeLink(g);
        return fail("could not answer the guest");
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
            if (!sendTo(g, netplaywire::encode(piece))) {
                closeLink(g);
                return fail("could not send the saved game");
            }
        }
        sentSave_ = true;
        std::fprintf(stderr, "[netplay] sent the host's saved game (%zu bytes)\n", pack.size());
    }
    links_.push_back(std::move(g));
    std::fprintf(stderr, "[netplay] player %zu is here\n", links_.size() + 1);
    announceRoster();
    return {};
}

std::string Session::start(int delay) {
    if (!host_ || started_) return fail("cannot start");
    if (delay != kAutoDelay && (delay < 0 || delay > kMaxDelay)) return fail("bad delay");
    here();   // let go of guests who left while the host was deciding
    if (links_.empty()) return fail("nobody to play with");
    // The door shuts: nobody joins a game that has begun.
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    players_ = static_cast<int>(links_.size()) + 1;
    delay_ = delay;
    if (delay == kAutoDelay) {
        delay_ = chooseDelay(worstRtt_, worstVar_, fps_);
        std::fprintf(stderr, "[netplay] slowest round trip %.1f ms (+/- %.1f), %d frames of delay\n",
                     worstRtt_, worstVar_, delay_);
    }
    for (size_t i = 0; i < links_.size(); ++i) {
        links_[i].slot = static_cast<int>(i) + 1;
        netplaywire::Message m;
        m.type = netplaywire::Type::Start;
        m.players = static_cast<uint8_t>(players_);
        m.player = static_cast<uint8_t>(links_[i].slot);
        m.delay = static_cast<uint8_t>(delay_);
        if (!sendTo(links_[i], netplaywire::encode(m))) {
            close_();
            return fail("a player dropped out as the game began");
        }
    }
    self_ = 0;
    started_ = true;
    std::fprintf(stderr, "[netplay] %d players, %d frames of delay\n", players_, delay_);
    return {};
}

std::string Session::accept(uint16_t code, const Identity& me, int delay, int timeoutMs,
                            double fps, const std::vector<uint8_t>* savePack) {
    if (delay != kAutoDelay && (delay < 0 || delay > kMaxDelay)) return fail("bad delay");
    if (std::string e = admit(code, me, timeoutMs, 2, fps, savePack); !e.empty()) return e;
    return start(delay);
}

// ---- a guest ----------------------------------------------------------------

std::string Session::join(const std::string& address, uint16_t port, uint16_t code,
                          const Identity& me, int timeoutMs) {
    if (!links_.empty()) return fail("already connected");
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
    links_.emplace_back();
    Link& up = links_.back();
    up.fd = fd;
    up.slot = 0;
    host_ = false;
    gone_ = false;

    netplaywire::Message hello;
    hello.type = netplaywire::Type::Hello;
    hello.code = code;
    hello.players = 1;
    hello.core = me.core;
    hello.coreVersion = me.coreVersion;
    hello.gameHash = me.gameHash;
    hello.saveHash = me.saveHash;
    const std::vector<uint8_t> bytes = netplaywire::encode(hello);
    if (bytes.empty() || !sendTo(up, bytes)) {
        close_();
        links_.clear();
        return fail("could not send Hello");
    }
    netplaywire::Message answer;
    if (!readMessage(up, answer, kHandshakeMs)) {
        const std::string why = error_;
        close_();
        links_.clear();
        return fail(why);
    }
    if (answer.type == netplaywire::Type::Refuse) {
        close_();
        links_.clear();
        return fail("the host said no: " + answer.reason);
    }
    if (answer.type != netplaywire::Type::Accept) {
        close_();
        links_.clear();
        return fail("the host's answer makes no sense");
    }
    maxPlayers_ = answer.players;
    here_ = 2;
    synced_ = false;
    received_.clear();
    if (answer.sync) {
        // The host's saved game follows, in pieces, in order.
        received_.reserve(answer.saveSize);
        while (received_.size() < answer.saveSize) {
            netplaywire::Message piece;
            if (!readMessage(up, piece, 30000)) {
                const std::string why = error_;
                close_();
                links_.clear();
                return fail("the saved game did not arrive: " + why);
            }
            if (piece.type != netplaywire::Type::Save || piece.saveSize != answer.saveSize ||
                piece.saveOffset != received_.size()) {
                close_();
                links_.clear();
                return fail("the saved game arrived out of order");
            }
            received_.insert(received_.end(), piece.saveBytes.begin(), piece.saveBytes.end());
        }
        synced_ = true;
    }
    return {};
}

std::string Session::awaitStart(int timeoutMs) {
    if (host_ || links_.empty()) return fail("not joined");
    if (started_) return {};
    Link& l = links_[0];
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        netplaywire::Message m;
        while (l.parser.next(m)) {
            switch (m.type) {
                case netplaywire::Type::Roster:
                    here_ = m.players;
                    break;
                case netplaywire::Type::Start:
                    if (m.delay > kMaxDelay) {
                        close_();
                        return fail("the host's answer makes no sense");
                    }
                    players_ = m.players;
                    self_ = m.player;
                    delay_ = m.delay;
                    started_ = true;
                    return {};
                case netplaywire::Type::Bye:
                case netplaywire::Type::Refuse:
                    close_();
                    return fail("the host closed the game");
                default:
                    close_();
                    return fail("the host sent something unexpected");
            }
        }
        if (l.parser.failed()) {
            close_();
            return fail("the other side sent something that is not netplay");
        }
        const int left = msLeft(deadline);
        if (left == 0 || !waitFd(l.fd, left)) return kNotYet;
        uint8_t buf[512];
        const ssize_t n = ::recv(l.fd, buf, sizeof buf, 0);
        if (n <= 0) {
            close_();
            return fail("the host closed the connection");
        }
        l.parser.feed(buf, static_cast<size_t>(n));
    }
}

// ---- the game: the delay, buttons and fingerprints --------------------------

// The host works out, for each guest, how far ahead they must send: the most
// any other player (the host included) asked for.
void Session::relayLeads() {
    leadsDirty_ = false;
    if (!host_ || !started_) return;
    for (Link& g : links_) {
        if (g.gone) continue;
        int lead = hostNeed_;
        for (const Link& h : links_)
            if (&h != &g && !h.gone) lead = std::max(lead, h.need);
        if (lead < 1 || lead == g.told) continue;
        netplaywire::Message m;
        m.type = netplaywire::Type::Lead;
        m.lead = static_cast<uint8_t>(std::min(lead, netplaywire::kMaxLead));
        if (sendTo(g, netplaywire::encode(m))) g.told = lead;
    }
}

void Session::sendLead(int frames) {
    const int want = std::max(1, std::min(frames, netplaywire::kMaxLead));
    if (host_) {
        hostNeed_ = want;
        relayLeads();
        return;
    }
    if (links_.empty()) return;
    netplaywire::Message m;
    m.type = netplaywire::Type::Lead;
    m.lead = static_cast<uint8_t>(want);
    sendTo(links_[0], netplaywire::encode(m));
}

int Session::takeLead() {
    if (!host_) {
        const int v = peerLead_;
        peerLead_ = 0;
        return v;
    }
    int lead = 0;
    for (const Link& g : links_)
        if (!g.gone) lead = std::max(lead, g.need);
    if (lead < 1 || lead == taken_) return 0;
    taken_ = lead;
    return lead;
}

void Session::sendInputs(uint32_t frame, const netplaywire::Pad& pad) {
    netplaywire::Message m;
    m.type = netplaywire::Type::Inputs;
    m.player = static_cast<uint8_t>(self_);
    m.frame = frame;
    m.pad = pad;
    sendToAll(netplaywire::encode(m));
}

void Session::sendDigest(uint32_t frame, uint64_t digest) {
    netplaywire::Message m;
    m.type = netplaywire::Type::Digest;
    m.player = static_cast<uint8_t>(self_);
    m.frame = frame;
    m.digest = digest;
    sendToAll(netplaywire::encode(m));
}

// A guest has gone (said goodbye, broke the rules or the connection). Before
// the game they are just dropped; during it everyone is told which frame their
// buttons stop at, so all play the same.
void Session::guestWent(Link& l) {
    if (l.fd < 0 && l.gone) return;
    closeLink(l);
    if (!started_ || l.slot < 1) return;
    left_[static_cast<size_t>(l.slot)] = true;
    leftFrom_[static_cast<size_t>(l.slot)] = lastFrame_[static_cast<size_t>(l.slot)];
    departures_.push_back(l.slot);
    std::fprintf(stderr, "[netplay] player %d left (their buttons stop at frame %u)\n",
                 l.slot + 1, leftFrom_[static_cast<size_t>(l.slot)]);
    netplaywire::Message m;
    m.type = netplaywire::Type::Left;
    m.player = static_cast<uint8_t>(l.slot);
    m.frame = leftFrom_[static_cast<size_t>(l.slot)];
    sendToAll(netplaywire::encode(m));
    leadsDirty_ = true;
}

// Keeps one message's buttons or fingerprint. `ok` goes false when too much is held.
void Session::store(Link&, const netplaywire::Message& m, bool& ok) {
    const size_t slot = m.player;
    if (m.type == netplaywire::Type::Inputs) {
        if (inputs_.size() >= kMaxWaiting && !inputs_.count(m.frame)) {
            ok = false;
            return;
        }
        PadsIn& f = inputs_[m.frame];
        f.pad[slot] = m.pad;
        f.have = static_cast<uint8_t>(f.have | (1u << slot));
        lastFrame_[slot] = std::max(lastFrame_[slot], m.frame + 1);
    } else {
        if (digests_.size() >= kMaxWaiting && !digests_.count(m.frame)) {
            ok = false;
            return;
        }
        DigestsIn& f = digests_[m.frame];
        f.digest[slot] = m.digest;
        f.have = static_cast<uint8_t>(f.have | (1u << slot));
    }
}

// Takes in everything that has arrived on one connection, without waiting.
void Session::drainLink(Link& l) {
    if (l.fd < 0 || l.gone) return;
    for (;;) {
        netplaywire::Message m;
        while (l.parser.next(m)) {
            bool ok = true;
            if (host_) {
                switch (m.type) {
                    case netplaywire::Type::Inputs:
                    case netplaywire::Type::Digest:
                        // A guest may speak only for itself.
                        if (m.player != l.slot) {
                            error_ = "a player sent another's buttons";
                            guestWent(l);
                            return;
                        }
                        store(l, m, ok);
                        if (!ok) {
                            error_ = "too many buttons ahead";
                            guestWent(l);
                            return;
                        }
                        sendToAll(netplaywire::encode(m), &l);   // the other guests need them too
                        break;
                    case netplaywire::Type::Lead:
                        l.need = m.lead;
                        leadsDirty_ = true;
                        break;
                    case netplaywire::Type::Bye:
                    case netplaywire::Type::Refuse:
                        guestWent(l);
                        return;
                    default:
                        error_ = "unexpected message in a session";
                        guestWent(l);
                        return;
                }
            } else {
                switch (m.type) {
                    case netplaywire::Type::Inputs:
                    case netplaywire::Type::Digest:
                        if (m.player == self_ || m.player >= players_) {
                            error_ = "the host sent buttons for a player that does not exist";
                            gone_ = true;
                            return;
                        }
                        store(l, m, ok);
                        if (!ok) {
                            error_ = "too many buttons ahead";
                            gone_ = true;
                            return;
                        }
                        break;
                    case netplaywire::Type::Lead:
                        peerLead_ = m.lead;
                        break;
                    case netplaywire::Type::Left:
                        if (m.player == self_ || m.player >= players_) {
                            error_ = "the host said a player left who is not there";
                            gone_ = true;
                            return;
                        }
                        if (!left_[m.player]) {
                            left_[m.player] = true;
                            leftFrom_[m.player] = m.frame;
                            departures_.push_back(m.player);
                        }
                        break;
                    case netplaywire::Type::Roster:
                        here_ = m.players;
                        break;
                    case netplaywire::Type::Bye:
                    case netplaywire::Type::Refuse:
                        closeLink(l);
                        gone_ = true;
                        return;
                    default:
                        error_ = "unexpected message in a session";
                        gone_ = true;
                        return;
                }
            }
        }
        if (l.parser.failed()) {
            error_ = "the other side sent something that is not netplay";
            if (host_) guestWent(l);
            else gone_ = true;
            return;
        }
        uint8_t buf[512];
        const ssize_t n = ::recv(l.fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            l.parser.feed(buf, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            if (host_) guestWent(l);
            else {
                closeLink(l);
                gone_ = true;
            }
        }
        return;   // nothing more for now
    }
}

void Session::drain() {
    // A guest that left can drop out of links_ only before the game; keep the
    // list steady here, as slots index into it.
    for (size_t i = 0; i < links_.size(); ++i) drainLink(links_[i]);
    if (leadsDirty_) relayLeads();
    if (!started_) return;
    if (host_) {
        if (playing() < 2) gone_ = true;
    } else if (links_.empty() || links_[0].gone) {
        gone_ = true;
    }
}

int Session::playing() const {
    int n = players_;
    for (int s = 0; s < players_ && s < netplaywire::kMaxPlayers; ++s)
        if (left_[static_cast<size_t>(s)]) --n;
    return n;
}

int Session::takeLeft() {
    if (departures_.empty()) return -1;
    const int s = departures_.front();
    departures_.erase(departures_.begin());
    return s;
}

Wait Session::remoteInputs(uint32_t frame, int slot, netplaywire::Pad& out, int timeoutMs) {
    out = {};
    if (slot < 0 || slot >= netplaywire::kMaxPlayers || slot == self_ || slot >= players_)
        return Wait::Absent;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        drain();
        if (auto it = inputs_.find(frame); it != inputs_.end() && ((it->second.have >> slot) & 1)) {
            out = it->second.pad[static_cast<size_t>(slot)];
            inputs_.erase(inputs_.begin(), it);   // older frames are done with
            return Wait::Got;
        }
        if (gone_) return Wait::Gone;
        if (left_[static_cast<size_t>(slot)] && frame >= leftFrom_[static_cast<size_t>(slot)])
            return Wait::Absent;
        const int left = msLeft(deadline);
        if (left == 0) return Wait::Late;
        waitReadable(left);
    }
}

Wait Session::remoteDigest(uint32_t frame, int slot, uint64_t& out, int timeoutMs) {
    out = 0;
    if (slot < 0 || slot >= netplaywire::kMaxPlayers || slot == self_ || slot >= players_)
        return Wait::Absent;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        drain();
        if (auto it = digests_.find(frame); it != digests_.end() && ((it->second.have >> slot) & 1)) {
            out = it->second.digest[static_cast<size_t>(slot)];
            digests_.erase(digests_.begin(), it);
            return Wait::Got;
        }
        if (gone_) return Wait::Gone;
        // Whatever a player sent before leaving has come already, in order.
        if (left_[static_cast<size_t>(slot)]) return Wait::Absent;
        const int left = msLeft(deadline);
        if (left == 0) return Wait::Late;
        waitReadable(left);
    }
}

void Session::leave() {
    netplaywire::Message bye;
    bye.type = netplaywire::Type::Bye;
    sendToAll(netplaywire::encode(bye));
    close_();
}

// ---- --netplay-session-test -------------------------------------------------
// Every end in one process, each on its own thread, over 127.0.0.1.

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

struct Run {
    std::string result;   // "" when it finished and every fingerprint matched, else why not
    uint64_t hash = 0;    // where its "game" ended
};

// One side of a lockstep run with no core: the "game" is a running hash of
// every player's buttons, in slot order. `flipAt` makes this side's state
// drift at that frame; `leaveAt` makes it walk out then.
Run playLockstep(Session& s, uint32_t frames, int flipAt, int leaveAt = -1) {
    const int me = s.localPlayer();
    const int n = s.players();
    const uint32_t d = static_cast<uint32_t>(s.delay());
    Run run;
    uint64_t hash = 1469598103934665603ull;
    std::map<uint32_t, uint64_t> mine;
    // Compare our fingerprints with every other player's, oldest first.
    auto compare = [&](bool last) -> std::string {
        for (auto it = mine.begin(); it != mine.end();) {
            for (int slot = 0; slot < n; ++slot) {
                if (slot == me) continue;
                uint64_t theirs = 0;
                const Wait w = s.remoteDigest(it->first, slot, theirs, last ? 3000 : 0);
                if (w == Wait::Late) return {};
                if (w == Wait::Gone) return last ? "the other side left before the end" : std::string();
                if (w == Wait::Absent) continue;
                if (theirs != it->second) return "DRIFT at frame " + std::to_string(it->first);
            }
            it = mine.erase(it);
        }
        return {};
    };
    for (uint32_t f = 0; f < frames; ++f) {
        if (leaveAt >= 0 && static_cast<int>(f) == leaveAt) {
            s.leave();
            run.result = "left";
            return run;
        }
        s.sendInputs(f + d, scripted(f, me));
        for (int slot = 0; slot < n; ++slot) {
            netplaywire::Pad pad;
            if (f >= d) {
                if (slot == me) {
                    pad = scripted(f - d, me);
                } else {
                    const Wait w = s.remoteInputs(f, slot, pad, 3000);
                    if (w != Wait::Got && w != Wait::Absent) {
                        s.leave();
                        run.result = "no buttons for frame " + std::to_string(f);
                        return run;
                    }
                }
            }
            hash = mixPad(hash, pad);
        }
        if (flipAt >= 0 && static_cast<int>(f) == flipAt) hash ^= 1;
        if ((f + 1) % 60 == 0) {
            s.sendDigest(f, hash);
            mine[f] = hash;
        }
        if (std::string why = compare(false); !why.empty()) {
            s.leave();   // stop the game, as a real session would
            run.result = why;
            return run;
        }
    }
    run.result = compare(true);
    run.hash = hash;
    return run;
}

// A host and some guests, gathered and started.
struct Group {
    Session host;
    std::vector<std::unique_ptr<Session>> guest;
    std::vector<std::string> guestErr;
    std::string hostErr;
};

constexpr uint16_t kCode = 4821;

// `guests` join one after the other (so their slots are in a known order),
// then the host starts. The result is in group.hostErr and group.guestErr.
void gather(Group& g, int guests, int maxPlayers, int delay, const Identity& id) {
    g.hostErr = g.host.listen("127.0.0.1", 0);
    if (!g.hostErr.empty()) return;
    g.guest.resize(static_cast<size_t>(guests));
    g.guestErr.assign(static_cast<size_t>(guests), "");
    std::vector<std::thread> threads;
    for (int i = 0; i < guests; ++i) {
        g.guest[static_cast<size_t>(i)] = std::make_unique<Session>();
        threads.emplace_back([&g, i, &id] {
            Session& s = *g.guest[static_cast<size_t>(i)];
            std::string e = s.join("127.0.0.1", g.host.port(), kCode, id, 3000);
            for (int k = 0; e.empty() && k < 40; ++k) {
                e = s.awaitStart(250);
                if (e != kNotYet) break;
            }
            if (e == kNotYet) e = "timed out waiting for the host to start";
            g.guestErr[static_cast<size_t>(i)] = e;
        });
        g.hostErr = g.host.admit(kCode, id, 3000, maxPlayers);
        if (!g.hostErr.empty()) break;
    }
    if (g.hostErr.empty()) g.hostErr = g.host.start(delay);
    for (std::thread& t : threads) t.join();
}

bool allWell(const Group& g) {
    if (!g.hostErr.empty()) return false;
    for (const std::string& e : g.guestErr)
        if (!e.empty()) return false;
    return true;
}

// Every player runs `frames` frames; results by slot (the host is 0).
std::vector<Run> runGroup(Group& g, uint32_t frames, int flipSlot = -1, int leaveSlot = -1,
                          int leaveAt = -1) {
    std::vector<Run> out(g.guest.size() + 1);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < g.guest.size(); ++i) {
        threads.emplace_back([&, i] {
            const int slot = static_cast<int>(i) + 1;
            out[i + 1] = playLockstep(*g.guest[i], frames, slot == flipSlot ? 130 : -1,
                                      slot == leaveSlot ? leaveAt : -1);
        });
    }
    out[0] = playLockstep(g.host, frames, flipSlot == 0 ? 130 : -1, leaveSlot == 0 ? leaveAt : -1);
    for (std::thread& t : threads) t.join();
    return out;
}

// Keeps a session reading what has arrived (a guest looks at the host's slot).
void pump(Session& s, int slot) {
    netplaywire::Pad pad;
    s.remoteInputs(1, slot, pad, 5);
}

// A guest that is not a Session: sends exactly what it is told to.
struct RawGuest {
    int fd = -1;
    netplaywire::Parser parser;
    bool open(uint16_t port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        return ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    }
    bool send(const netplaywire::Message& m) {
        const std::vector<uint8_t> b = netplaywire::encode(m);
        return ::send(fd, b.data(), b.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(b.size());
    }
    bool read(netplaywire::Message& m, int ms) {
        for (;;) {
            if (parser.next(m)) return true;
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, ms) <= 0) return false;
            uint8_t buf[512];
            const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) return false;
            parser.feed(buf, static_cast<size_t>(n));
        }
    }
    ~RawGuest() {
        if (fd >= 0) ::close(fd);
    }
};

netplaywire::Message helloFor(const Identity& id, uint16_t code, uint8_t players = 1) {
    netplaywire::Message h;
    h.type = netplaywire::Type::Hello;
    h.code = code;
    h.players = players;
    h.core = id.core;
    h.coreVersion = id.coreVersion;
    h.gameHash = id.gameHash;
    h.saveHash = id.saveHash;
    return h;
}

}  // namespace

int test() {
    int bad = 0;
    auto check = [&bad](bool ok, const char* what) {
        std::fprintf(stderr, "[session] %-62s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++bad;
    };
    const Identity id{"snes9x", "Snes9x 1.63", "00ff00ff00ff00ff", ""};

    check(chooseDelay(0, 0, 60) == 2, "no round trip: 2 frames, the least");
    check(chooseDelay(20, 2, 60) == 2, "20 ms round trip at 60 Hz: 2 frames");
    check(chooseDelay(60, 5, 60) == 3, "60 ms round trip at 60 Hz: 3 frames");
    check(chooseDelay(20, 2, 50) == 2, "20 ms round trip at 50 Hz: 2 frames, the least");
    check(chooseDelay(900, 50, 60) == 8, "a very slow link is held at 8 frames");

    // ---- two players, as before ----
    {
        Group g;
        gather(g, 1, 2, 3, id);
        check(allWell(g) && g.guest[0]->delay() == 3 && g.host.localPlayer() == 0 &&
                  g.guest[0]->localPlayer() == 1 && g.host.players() == 2 &&
                  g.guest[0]->players() == 2,
              "the right code and the same game connect, two players");
    }
    {
        Group g;
        gather(g, 1, 2, kAutoDelay, id);
        check(allWell(g) && g.guest[0]->delay() >= 2 && g.guest[0]->delay() <= 8 &&
                  g.guest[0]->delay() == g.host.delay(),
              "an automatic delay is chosen, and both sides agree on it");
    }
    {
        // Two saved games that differ: the host's is sent, whole, in pieces.
        Session host, guest;
        Identity hostId = id, guestId = id;
        hostId.saveHash = "aaaaaaaaaaaaaaaa";
        guestId.saveHash = "bbbbbbbbbbbbbbbb";
        std::vector<uint8_t> big(300000);
        for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 31 + i / 251);
        host.listen("127.0.0.1", 0);
        std::string guestErr;
        std::thread t([&] {
            guestErr = guest.join("127.0.0.1", host.port(), kCode, guestId, 3000);
            if (guestErr.empty()) guestErr = guest.awaitStart(3000);
        });
        const std::string hostErr = host.accept(kCode, hostId, 2, 3000, 60.0, &big);
        t.join();
        check(hostErr.empty() && guestErr.empty() && host.sentSave() && guest.synced() &&
                  guest.receivedSave() == big,
              "a different saved game is sent to the guest, byte for byte");
    }
    {
        // The same saved game on both sides: nothing is sent.
        Session host, guest;
        Identity hostId = id, guestId = id;
        hostId.saveHash = guestId.saveHash = "cccccccccccccccc";
        std::vector<uint8_t> pack(1000, 7);
        host.listen("127.0.0.1", 0);
        std::string guestErr;
        std::thread t([&] {
            guestErr = guest.join("127.0.0.1", host.port(), kCode, guestId, 3000);
            if (guestErr.empty()) guestErr = guest.awaitStart(3000);
        });
        const std::string hostErr = host.accept(kCode, hostId, 2, 3000, 60.0, &pack);
        t.join();
        check(hostErr.empty() && guestErr.empty() && !host.sentSave() && !guest.synced(),
              "the same saved game on both sides sends nothing");
    }
    {
        // The host has none and the guest has one: the guest is told to have none.
        Session host, guest;
        Identity guestId = id;
        guestId.saveHash = "dddddddddddddddd";
        host.listen("127.0.0.1", 0);
        std::string guestErr;
        std::thread t([&] {
            guestErr = guest.join("127.0.0.1", host.port(), kCode, guestId, 3000);
            if (guestErr.empty()) guestErr = guest.awaitStart(3000);
        });
        const std::string hostErr = host.accept(kCode, id, 2, 3000, 60.0, nullptr);
        t.join();
        check(hostErr.empty() && guestErr.empty() && guest.synced() && guest.receivedSave().empty(),
              "a host with no saved game tells the guest to start with none");
    }
    {
        Session host, guest;
        host.listen("127.0.0.1", 0);
        std::string guestErr;
        std::thread t([&] { guestErr = guest.join("127.0.0.1", host.port(), 1111, id, 3000); });
        const std::string hostErr = host.accept(kCode, id, 3, 3000);
        t.join();
        check(!hostErr.empty() && guestErr.find("wrong code") != std::string::npos,
              "a wrong code is refused, and the guest is told why");
    }
    {
        Session host, guest;
        Identity other = id;
        other.gameHash = "1111111111111111";
        host.listen("127.0.0.1", 0);
        std::string guestErr;
        std::thread t([&] { guestErr = guest.join("127.0.0.1", host.port(), kCode, other, 3000); });
        const std::string hostErr = host.accept(kCode, id, 3, 3000);
        t.join();
        check(!hostErr.empty() && guestErr.find("different game file") != std::string::npos,
              "a different game file is refused");
    }
    {
        Session host, guest;
        Identity other = id;
        other.coreVersion = "Snes9x 1.62";
        host.listen("127.0.0.1", 0);
        std::string guestErr;
        std::thread t([&] { guestErr = guest.join("127.0.0.1", host.port(), kCode, other, 3000); });
        const std::string hostErr = host.accept(kCode, id, 3, 3000);
        t.join();
        check(!hostErr.empty() && guestErr.find("different core version") != std::string::npos,
              "a different core version is refused");
    }
    {
        Session host;
        host.listen("127.0.0.1", 0);
        const std::string e = host.accept(kCode, id, 2, 150);
        check(e == kNobodyJoined, "a host nobody joins gives up");
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
        const std::string e = host.accept(kCode, id, 2, 3000);
        t.join();
        check(!e.empty(), "a web request sent to the port is turned away");
    }
    for (const int delay : {0, 2, 5}) {
        Group g;
        gather(g, 1, 2, delay, id);
        std::vector<Run> r;
        if (allWell(g)) r = runGroup(g, 600);
        char what[96];
        std::snprintf(what, sizeof what, "600 frames in lockstep, two players, delay %d", delay);
        check(allWell(g) && r[0].result.empty() && r[1].result.empty() && r[0].hash == r[1].hash,
              what);
    }
    {
        Group g;
        gather(g, 1, 2, 2, id);
        std::vector<Run> r;
        if (allWell(g)) r = runGroup(g, 300, 1);
        // The side that sees it first stops; the other may see the fingerprints too, or the stop.
        check(allWell(g) && !r[0].result.empty() && !r[1].result.empty() &&
                  (r[0].result.find("DRIFT") != std::string::npos ||
                   r[1].result.find("DRIFT") != std::string::npos),
              "a drifting side is caught at the fingerprint, and both stop");
    }
    {
        Group g;
        gather(g, 1, 2, 2, id);
        netplaywire::Pad got;
        check(g.host.remoteInputs(7, 1, got, 60) == Wait::Late, "buttons that have not come are Late");
        g.guest[0]->sendInputs(7, scripted(7, 1));
        check(g.host.remoteInputs(7, 1, got, 1000) == Wait::Got && got == scripted(7, 1),
              "buttons that come are Got, exactly");
        g.guest[0]->leave();
        check(g.host.remoteInputs(8, 1, got, 1000) == Wait::Gone, "the only other player leaving is Gone");
    }
    {
        Group g;
        gather(g, 1, 2, 2, id);
        netplaywire::Pad got;
        g.host.leave();
        check(g.guest[0]->remoteInputs(8, 0, got, 1000) == Wait::Gone, "the host leaving is Gone");
    }

    // ---- more than two ----
    {
        Group g;
        gather(g, 3, 4, 2, id);
        bool slots = allWell(g) && g.host.players() == 4;
        for (size_t i = 0; slots && i < g.guest.size(); ++i)
            slots = g.guest[i]->localPlayer() == static_cast<int>(i) + 1 &&
                    g.guest[i]->players() == 4 && g.guest[i]->delay() == 2;
        check(slots, "four players gather; each is told its slot, the count and the delay");
    }
    for (const int delay : {0, 2, 4}) {
        for (const int guests : {2, 3}) {
            Group g;
            gather(g, guests, guests + 1, delay, id);
            std::vector<Run> r;
            if (allWell(g)) r = runGroup(g, 600);
            bool same = allWell(g);
            for (size_t i = 0; same && i < r.size(); ++i)
                same = r[i].result.empty() && r[i].hash == r[0].hash;
            char what[96];
            std::snprintf(what, sizeof what, "600 frames in lockstep, %d players, delay %d, all agree",
                          guests + 1, delay);
            check(same, what);
        }
    }
    {
        Group g;
        gather(g, 3, 4, 2, id);
        std::vector<Run> r;
        if (allWell(g)) r = runGroup(g, 400, 2);
        bool all = allWell(g), drift = false;
        for (const Run& x : r) {
            all = all && !x.result.empty();
            drift = drift || x.result.find("DRIFT") != std::string::npos;
        }
        check(all && drift, "one of four drifting is caught, and the game stops for all");
    }
    {
        // Player 3 walks out at frame 200 of 600; the other three play on and agree.
        Group g;
        gather(g, 3, 4, 2, id);
        std::vector<Run> r;
        if (allWell(g)) r = runGroup(g, 600, -1, 2, 200);
        const bool played = allWell(g) && r[2].result == "left" && r[0].result.empty() &&
                            r[1].result.empty() && r[3].result.empty() &&
                            r[0].hash == r[1].hash && r[1].hash == r[3].hash;
        check(played, "a player leaving does not end the game; the others still agree");
        check(played && g.host.takeLeft() == 2 && g.guest[0]->takeLeft() == 2 &&
                  g.guest[2]->takeLeft() == 2 && g.host.takeLeft() == -1,
              "everyone is told which player left, once");
        check(played && g.host.playing() == 3 && g.guest[0]->playing() == 3,
              "three are still playing");
    }
    {
        // Everyone but the host walks out: the host's game is over.
        Group g;
        gather(g, 2, 3, 2, id);
        std::thread a([&] { g.guest[0]->leave(); });
        std::thread b([&] { g.guest[1]->leave(); });
        a.join();
        b.join();
        netplaywire::Pad got;
        Wait w = Wait::Late;
        for (int i = 0; i < 100 && w != Wait::Gone; ++i) w = g.host.remoteInputs(9, 1, got, 20);
        check(w == Wait::Gone && g.host.playing() < 2, "when every guest has left, the host's game is over");
    }
    {
        // The delay each player must send ahead is the most any OTHER player needs.
        Group g;
        gather(g, 2, 3, 2, id);
        Session& host = g.host;
        Session& a = *g.guest[0];
        Session& b = *g.guest[1];
        a.sendLead(6);
        pump(host, 1);
        pump(b, 0);
        const int hostLead = host.takeLead();
        const int bLead = b.takeLead();
        const int aLead = a.takeLead();
        check(hostLead == 6 && bLead == 6 && aLead == 0,
              "a guest needing 6 frames: the host and the other guest send 6 ahead, it does not");
        b.sendLead(3);
        pump(host, 2);
        pump(a, 0);
        check(a.takeLead() == 3 && host.takeLead() == 0,
              "a second guest asking for less changes nothing for the host");
        host.sendLead(4);
        pump(a, 0);
        pump(b, 0);
        check(a.takeLead() == 4 && b.takeLead() == 0,
              "the host asking: the guests send ahead by the most others need");
    }

    // ---- the lobby ----
    {
        // The door stays open after a wrong code and after a full house.
        Session host;
        host.listen("127.0.0.1", 0);
        const uint16_t port = host.port();
        std::string e1, e2, e3, e4;
        Session a, b, c;
        std::thread t([&] {
            e1 = a.join("127.0.0.1", port, 1111, id, 3000);   // wrong code
            e2 = a.join("127.0.0.1", port, kCode, id, 3000);
            e3 = b.join("127.0.0.1", port, kCode, id, 3000);
            e4 = c.join("127.0.0.1", port, kCode, id, 3000);   // one too many
        });
        const std::string h1 = host.admit(kCode, id, 3000, 3);
        const std::string h2 = host.admit(kCode, id, 3000, 3);
        const std::string h3 = host.admit(kCode, id, 3000, 3);
        const std::string h4 = host.admit(kCode, id, 3000, 3);
        t.join();
        check(h1 == "wrong code" && h2.empty() && h3.empty() && h4 == "the game is full" &&
                  e1.find("wrong code") != std::string::npos && e2.empty() && e3.empty() &&
                  e4.find("the game is full") != std::string::npos && host.here() == 3,
              "the lobby stays open after a wrong code, and turns away the one too many");
        const std::string wa = a.awaitStart(300), wb = b.awaitStart(300);
        check(wa == kNotYet && wb == kNotYet && a.lobbySize() == 3 && b.lobbySize() == 3,
              "guests waiting in the lobby are told how many are here");
        check(host.start(2).empty() && a.awaitStart(2000).empty() && b.awaitStart(2000).empty() &&
                  a.players() == 3 && b.localPlayer() == 2,
              "starting from the lobby tells every guest");
    }
    {
        // A guest who leaves the lobby is dropped; the rest start without them.
        Session host;
        host.listen("127.0.0.1", 0);
        const uint16_t port = host.port();
        Session a, b;
        std::string ea, eb;
        std::thread t([&] {
            ea = a.join("127.0.0.1", port, kCode, id, 3000);
            eb = b.join("127.0.0.1", port, kCode, id, 3000);
        });
        host.admit(kCode, id, 3000, 4);
        host.admit(kCode, id, 3000, 4);
        t.join();
        const int before = host.here();
        a.leave();
        int after = before;
        for (int i = 0; i < 100 && after == before; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            after = host.here();
        }
        const bool started = host.start(2).empty() && b.awaitStart(2000).empty();
        check(before == 3 && after == 2 && started && b.players() == 2 && b.localPlayer() == 1,
              "a guest leaving the lobby is dropped, and the rest start without them");
    }
    {
        Session host;
        host.listen("127.0.0.1", 0);
        check(!host.start(2).empty(), "a host cannot start with nobody to play with");
    }
    {
        // A guest that is not what it says.
        Session host;
        host.listen("127.0.0.1", 0);
        RawGuest two;
        two.open(host.port());
        two.send(helloFor(id, kCode, 2));
        const std::string e = host.admit(kCode, id, 3000, 4);
        netplaywire::Message answer;
        const bool read = two.read(answer, 1000);
        check(e == "one player from each console" && read &&
                  answer.type == netplaywire::Type::Refuse,
              "a console that says it brings two players is refused");
    }
    {
        // A guest that speaks for another player is dropped; the game goes on.
        Session host;
        host.listen("127.0.0.1", 0);
        RawGuest liar;
        Session honest;
        std::string honestErr;
        std::thread t([&] { honestErr = honest.join("127.0.0.1", host.port(), kCode, id, 3000); });
        host.admit(kCode, id, 3000, 3);
        t.join();
        liar.open(host.port());
        liar.send(helloFor(id, kCode));
        host.admit(kCode, id, 3000, 3);
        host.start(2);
        const std::string honestStart = honest.awaitStart(2000);
        netplaywire::Message start;
        for (int i = 0; i < 6 && start.type != netplaywire::Type::Start; ++i) liar.read(start, 1000);
        netplaywire::Message lie;
        lie.type = netplaywire::Type::Inputs;
        lie.player = 1;   // the honest guest's slot, not its own (2)
        lie.frame = 5;
        liar.send(lie);
        netplaywire::Pad got;
        for (int i = 0; i < 20; ++i) host.remoteInputs(5, 2, got, 10);
        check(honestErr.empty() && honestStart.empty() && start.type == netplaywire::Type::Start && start.player == 2 &&
                  host.takeLeft() == 2 && host.playing() == 2,
              "a guest sending another player's buttons is dropped, and the game goes on");
        netplaywire::Pad seen;
        check(host.remoteInputs(5, 1, seen, 50) == Wait::Late,
              "...and the buttons it forged were not kept");
    }
    {
        // Two guests, both given the host's saved game.
        Identity hostId = id;
        hostId.saveHash = "eeeeeeeeeeeeeeee";
        std::vector<uint8_t> pack(5000);
        for (size_t i = 0; i < pack.size(); ++i) pack[i] = static_cast<uint8_t>(i * 7);
        Session host;
        host.listen("127.0.0.1", 0);
        Session a, b;
        std::string ea, eb;
        std::thread t([&] {
            ea = a.join("127.0.0.1", host.port(), kCode, id, 3000);
            eb = b.join("127.0.0.1", host.port(), kCode, id, 3000);
        });
        host.admit(kCode, hostId, 3000, 3, 60.0, &pack);
        host.admit(kCode, hostId, 3000, 3, 60.0, &pack);
        t.join();
        check(ea.empty() && eb.empty() && a.receivedSave() == pack && b.receivedSave() == pack,
              "every guest is given the host's saved game");
    }
    std::fprintf(stderr, "[session] %s\n", bad == 0 ? "PASS" : "FAIL");
    return bad == 0 ? 0 : 1;
}

}  // namespace netplay
