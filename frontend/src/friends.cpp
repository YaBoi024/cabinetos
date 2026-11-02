// FORK (handheld): the friends a player joins (friends.h).

#include "friends.h"

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>

#include "fileio.h"
#include "moonlight.h"
#include "storage.h"

namespace friends {

namespace {

std::string filePath() { return storage::configDir() + "/friends"; }

// An address is kept only as moonlight::cleanHost would leave it, so a file
// edited by hand, or damaged, can never put a second argument or an option
// into the Moonlight command line.
bool goodHost(const std::string& host) {
    return !host.empty() && moonlight::cleanHost(host) == host;
}

bool parsePort(const std::string& s, int* port) {
    if (s.empty() || s.size() > 5) return false;
    int v = 0;
    for (const char ch : s) {
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (ch - '0');
    }
    if (v < 1 || v > 65535) return false;
    *port = v;
    return true;
}

}  // namespace

std::string cleanName(const std::string& typed) {
    std::string s;
    for (const char ch : typed) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (u >= 0x20 && u != 0x7f) s += ch;
    }
    auto trim = [](std::string& t) {
        while (!t.empty() && t.back() == ' ') t.pop_back();
        size_t a = 0;
        while (a < t.size() && t[a] == ' ') ++a;
        t.erase(0, a);
    };
    trim(s);
    if (s.size() > kNameMax) {
        size_t cut = kNameMax;
        // Back off a cut that would split a multi-byte character.
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
        trim(s);
    }
    return s;
}

std::vector<Friend> parse(const std::string& text) {
    std::vector<Friend> out;
    size_t pos = 0;
    while (pos < text.size() && out.size() < kMax) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t tab = line.find('\t');
        const std::string host = tab == std::string::npos ? line : line.substr(0, tab);
        if (!goodHost(host)) continue;
        if (std::any_of(out.begin(), out.end(), [&](const Friend& f) { return f.host == host; }))
            continue;
        std::string name = tab == std::string::npos ? "" : cleanName(line.substr(tab + 1));
        if (name.empty()) name = cleanName(host);
        out.push_back({name, host});
    }
    return out;
}

std::string serialize(const std::vector<Friend>& v) {
    std::string text;
    for (const Friend& f : v) {
        if (!goodHost(f.host)) continue;
        text += f.host + "\t" + cleanName(f.name) + "\n";
    }
    return text;
}

std::vector<Friend> list() {
    const std::vector<uint8_t> b = cab::readBytes(filePath());
    return parse(std::string(b.begin(), b.end()));
}

bool has(const std::string& host) {
    const std::vector<Friend> v = list();
    return std::any_of(v.begin(), v.end(), [&](const Friend& f) { return f.host == host; });
}

namespace {
bool save(const std::vector<Friend>& v) {
    storage::makeDirs(storage::configDir());
    // 0600: who a player plays with, and where, is theirs.
    return cab::writeBytes(filePath(), serialize(v), 0600);
}
}  // namespace

bool add(const Friend& f) {
    if (!goodHost(f.host)) return false;
    Friend e{cleanName(f.name), f.host};
    if (e.name.empty()) e.name = cleanName(f.host);
    if (e.name.empty()) return false;
    std::vector<Friend> v = list();
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const Friend& x) { return x.host == e.host; }),
            v.end());
    v.insert(v.begin(), e);
    if (v.size() > kMax) v.resize(kMax);
    return save(v);
}

bool rename(const std::string& host, const std::string& name) {
    const std::string n = cleanName(name);
    if (n.empty()) return false;
    std::vector<Friend> v = list();
    bool found = false;
    for (Friend& f : v)
        if (f.host == host) {
            f.name = n;
            found = true;
        }
    return found && save(v);
}

bool remove(const std::string& host) {
    std::vector<Friend> v = list();
    const size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Friend& x) { return x.host == host; }),
            v.end());
    return v.size() != before && save(v);
}

bool splitHostPort(const std::string& typed, int fallback, std::string* host, int* port) {
    std::string s;
    for (const char ch : typed)
        if (ch != ' ') s += ch;
    if (s.empty()) return false;
    *port = fallback;
    if (s.front() == '[') {
        const size_t close = s.find(']');
        if (close == std::string::npos || close == 1) return false;
        *host = s.substr(1, close - 1);
        if (close + 1 < s.size()) {
            if (s[close + 1] != ':' || !parsePort(s.substr(close + 2), port)) return false;
        }
        return true;
    }
    const size_t colons = static_cast<size_t>(std::count(s.begin(), s.end(), ':'));
    if (colons == 1) {
        const size_t c = s.find(':');
        if (c == 0 || !parsePort(s.substr(c + 1), port)) return false;
        *host = s.substr(0, c);
        return true;
    }
    *host = s;   // a name, an IPv4 address, or a bare IPv6 literal
    return true;
}

bool reachable(const std::string& typed, int timeoutMs) {
    std::string host;
    int port = kProbePort;
    if (!splitHostPort(typed, kProbePort, &host, &port)) return false;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
        return false;

    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    bool ok = false;
    for (const addrinfo* ai = res; ai && !ok; ai = ai->ai_next) {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                ai->ai_protocol);
        if (fd < 0) continue;
        int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - Clock::now()).count();
            pollfd p{fd, POLLOUT, 0};
            if (left > 0 && ::poll(&p, 1, static_cast<int>(left)) == 1) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) rc = 0;
            }
        }
        ok = rc == 0;
        ::close(fd);
    }
    ::freeaddrinfo(res);
    return ok;
}

}  // namespace friends
