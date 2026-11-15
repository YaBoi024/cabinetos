// Game events (#716) on the wire: one message to the owner's app. The address
// read as curl reads it, its name looked up and every address it gives held
// to the home rule, and the POST sent to those addresses and no others, with
// no redirect and no proxy, 2 s to connect and 5 s in all. gameevents.h says
// why each rule is there; gameevents.cpp makes the messages and sends them in
// order.

#include "gameevents.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <netdb.h>
#include <netinet/in.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "gameevents_internal.h"

namespace gameevents {
namespace {

bool homeV4(uint32_t a) {
    return (a >> 24) == 127         // 127/8
           || (a >> 24) == 10       // 10/8
           || (a >> 20) == 0xAC1    // 172.16/12
           || (a >> 16) == 0xC0A8   // 192.168/16
           || (a >> 22) == 0x191;   // 100.64/10, Tailscale's
}

struct Parts {
    std::string scheme, host, port;
};

// curl's own reading of the address; only http and https.
bool partsOf(const std::string& url, Parts* out) {
    CURLU* u = curl_url();
    if (!u) return false;
    bool ok = curl_url_set(u, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK;
    char *scheme = nullptr, *host = nullptr, *port = nullptr;
    ok = ok && curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
         curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
         curl_url_get(u, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) == CURLUE_OK;
    if (ok) *out = Parts{scheme, host, port};
    curl_free(scheme);
    curl_free(host);
    curl_free(port);
    curl_url_cleanup(u);
    return ok && !out->host.empty() && (out->scheme == "http" || out->scheme == "https");
}

struct Wire {
    const std::atomic<bool>* stop = nullptr;
    bool refused = false;   // curl was about to connect somewhere not home
};

// Whatever curl connects to is checked once more, at the socket.
curl_socket_t openHomeSocket(void* p, curlsocktype purpose, curl_sockaddr* a) {
    if (purpose != CURLSOCKTYPE_IPCXN || !isHome(&a->addr)) {
        static_cast<Wire*>(p)->refused = true;
        return CURL_SOCKET_BAD;
    }
    return ::socket(a->family, a->socktype, a->protocol);
}

int stopWhenAsked(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const Wire* w = static_cast<const Wire*>(p);
    return w->stop && w->stop->load() ? 1 : 0;
}

size_t discard(char*, size_t size, size_t n, void*) { return size * n; }

}  // namespace

// HOME: the ranges romm::looksLocal holds a RomM address to (#479), on an
// address a name was looked up to. looksLocal reads a typed host, and calls a
// name with no dot local without asking, which says nothing about where it
// goes.
bool isHome(const sockaddr* sa) {
    if (!sa) return false;
    if (sa->sa_family == AF_INET)
        return homeV4(ntohl(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr.s_addr));
    if (sa->sa_family != AF_INET6) return false;
    const in6_addr& a = reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr;
    const uint8_t* b = a.s6_addr;
    if (IN6_IS_ADDR_LOOPBACK(&a)) return true;                // ::1
    if ((b[0] & 0xFE) == 0xFC) return true;                   // fc00::/7
    if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;   // fe80::/10
    // An IPv4 address written as IPv6 is that IPv4 address.
    if (IN6_IS_ADDR_V4MAPPED(&a))
        return homeV4(static_cast<uint32_t>(b[12]) << 24 | b[13] << 16 | b[14] << 8 | b[15]);
    return false;
}

// Every address a name gave is home, and it gave one.
bool allHome(const std::vector<sockaddr_storage>& addrs) {
    if (addrs.empty()) return false;
    for (const sockaddr_storage& s : addrs)
        if (!isHome(reinterpret_cast<const sockaddr*>(&s))) return false;
    return true;
}

std::string ipText(const sockaddr* sa) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (sa->sa_family == AF_INET)
        inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr, buf, sizeof buf);
    else
        inet_ntop(AF_INET6, &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr, buf, sizeof buf);
    return buf;
}

std::string hostOf(const std::string& url) {
    Parts p;
    return partsOf(url, &p) ? p.host : std::string();
}

Tried post(const std::string& url, const std::string& body, const std::atomic<bool>* stop) {
    const auto began = std::chrono::steady_clock::now();
    Parts p;
    if (!partsOf(url, &p)) return {Answer::CantConnect, 0};
    // The name looked up, and every address it gives must be home.
    std::string bare = p.host;
    if (bare.size() > 2 && bare.front() == '[' && bare.back() == ']') bare = bare.substr(1, bare.size() - 2);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(bare.c_str(), p.port.c_str(), &hints, &found) != 0 || !found)
        return {Answer::CantConnect, 0};
    std::vector<sockaddr_storage> addrs;
    for (addrinfo* r = found; r; r = r->ai_next) {
        sockaddr_storage s{};
        std::memcpy(&s, r->ai_addr, std::min<size_t>(r->ai_addrlen, sizeof s));
        addrs.push_back(s);
    }
    freeaddrinfo(found);
    if (!allHome(addrs)) return {Answer::NotHome, 0};
    const long spent = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - began)
                                             .count());
    if (spent >= kMessageMs) return {Answer::CantConnect, 0};

    // THE CONNECTION GOES TO THE ADDRESSES CHECKED: curl is handed them for
    // the name, so it looks nothing up itself. A number needs no lookup.
    curl_slist* resolve = nullptr;
    in6_addr scratch{};
    if (inet_pton(AF_INET, bare.c_str(), &scratch) != 1 && inet_pton(AF_INET6, bare.c_str(), &scratch) != 1) {
        std::string list;
        for (const sockaddr_storage& s : addrs) {
            const sockaddr* sa = reinterpret_cast<const sockaddr*>(&s);
            const std::string ip = sa->sa_family == AF_INET6 ? "[" + ipText(sa) + "]" : ipText(sa);
            if (list.find(ip) == std::string::npos) list += (list.empty() ? "" : ",") + ip;
        }
        resolve = curl_slist_append(nullptr, (bare + ":" + p.port + ":" + list).c_str());
    }

    CURL* c = curl_easy_init();
    if (!c) {
        curl_slist_free_all(resolve);
        return {Answer::CantConnect, 0};
    }
    Wire wire;
    wire.stop = stop;
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_PROXY, "");
    if (resolve) curl_easy_setopt(c, CURLOPT_RESOLVE, resolve);
    curl_easy_setopt(c, CURLOPT_OPENSOCKETFUNCTION, openHomeSocket);
    curl_easy_setopt(c, CURLOPT_OPENSOCKETDATA, &wire);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, kConnectMs);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, kMessageMs - spent);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "CabinetOS");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, stopWhenAsked);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &wire);
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    curl_slist_free_all(headers);
    curl_slist_free_all(resolve);
    if (wire.refused) return {Answer::NotHome, 0};
    if (rc == CURLE_PEER_FAILED_VERIFICATION) return {Answer::Certificate, 0};
    if (rc != CURLE_OK || status == 0) return {Answer::CantConnect, 0};
    if (status / 100 == 2) return {Answer::Ok, status};
    return {Answer::Error, status};
}

}  // namespace gameevents
