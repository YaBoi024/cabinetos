// Every request to the LinkUp server (server_link.h).

#include "server_link.h"

#include <atomic>

#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>

#include <curl/curl.h>

#include "hexutil.h"
#include "jsonutil.h"

namespace {

// PROTOCOL.md: an answer is at most 256 KiB, and a reader stops reading past it.
constexpr size_t kMaxAnswer = 256 * 1024;

constexpr uint8_t kKindOpen = 3;
constexpr uint8_t kKindSealed = 4;

size_t keepAnswer(char* data, size_t size, size_t count, void* to) {
    auto* out = static_cast<std::vector<uint8_t>*>(to);
    const size_t n = size * count;
    if (out->size() + n > kMaxAnswer) return 0;  // ends the transfer
    out->insert(out->end(), data, data + n);
    return n;
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

// Whether `host` already says its port: `name:7845`, `1.2.3.4:7845`, `[::1]:7845`.
bool hasPort(const std::string& host) {
    if (!host.empty() && host[0] == '[') {
        const size_t close = host.find(']');
        return close != std::string::npos && close + 1 < host.size() && host[close + 1] == ':' &&
               allDigits(host.substr(close + 2));
    }
    const size_t colon = host.find(':');
    return colon != std::string::npos && host.find(':', colon + 1) == std::string::npos &&
           allDigits(host.substr(colon + 1));
}

}  // namespace

ServerLink::ServerLink(Keys& keys) : keys_(keys) {}

std::vector<std::string> ServerLink::bases(const std::string& typed) {
    std::string a = typed;
    while (!a.empty() && std::isspace(static_cast<unsigned char>(a.front()))) a.erase(a.begin());
    while (!a.empty() && (std::isspace(static_cast<unsigned char>(a.back())) || a.back() == '/')) a.pop_back();
    if (a.rfind("http://", 0) == 0 || a.rfind("https://", 0) == 0) return {a};
    // No address is no place: "http://" + "" + "/linkup/v1" would ask the network for a machine
    // called linkup.
    if (a.empty() || a[0] == '/') return {};
    std::string host = a;
    std::string path;
    const size_t slash = a.find('/');
    if (slash != std::string::npos) {
        host = a.substr(0, slash);
        path = a.substr(slash);
    }
    if (hasPort(host)) return {"http://" + host + path};
    return {"https://" + host + path, "http://" + host + path};
}

namespace {
std::atomic<bool> gAbort{false};
int onProgress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return gAbort.load() ? 1 : 0; }
}  // namespace

void ServerLink::abortRequests(bool on) { gAbort = on; }

ServerLink::Post ServerLink::post(const std::string& base, const std::vector<uint8_t>& body, long seconds) {
    Post out;
    CURL* curl = curl_easy_init();
    if (!curl) return out;
    const std::string url = base + "/linkup/v1";
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, keepAnswer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, seconds < 10 ? seconds : 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, seconds);
    // What PROTOCOL.md asks of a reader.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // Nothing from the environment decides where this goes.
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cabinetos-linkup");
    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        out.answered = true;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return out;
}

bool ServerLink::firstPlace(const std::vector<uint8_t>& body, long seconds, Post& out) {
    const auto server = keys_.server();
    if (!server) return false;
    std::vector<std::string> places;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!base_.empty() && baseAddress_ == server->address) places = {base_};
    }
    if (places.empty()) places = bases(server->address);
    for (const std::string& place : places) {
        out = post(place, body, seconds);
        if (!out.answered) continue;
        std::lock_guard<std::mutex> lk(mu_);
        base_ = place;
        baseAddress_ = server->address;
        return true;
    }
    return false;
}

// One Open under the remembered key. `send` posts it and says whether anything answered.
ServerLink::Outcome ServerLink::openBy(const Send& send, std::shared_ptr<Session>& out, bool& answered) {
    answered = false;
    const auto server = keys_.server();
    if (!server) return Outcome::Unreachable;

    json_object* hello = json_object_new_object();
    jsonutil::set(hello, "protocol", kProtocol);
    jsonutil::set(hello, "oldest", kOldest);
    jsonutil::set(hello, "stream", kStream);
    jsonutil::set(hello, "t", keys_.nextT());
    const std::string payload = jsonutil::text(hello);
    json_object_put(hello);

    noise::Handshake::Config c;
    c.pattern = noise::Pattern::IK;
    c.initiator = true;
    c.prologue = "cabinet-linkup session 1";
    c.s = keys_.mine();
    c.rs = server->key;
    noise::Handshake h(c);
    noise::Bytes message;
    if (!h.write(noise::Bytes(payload.begin(), payload.end()), message)) return Outcome::Unreachable;

    std::vector<uint8_t> body{kKindOpen};
    body.insert(body.end(), message.begin(), message.end());
    Post answer;
    answered = send(body, answer);
    if (!answered || answer.status != 200) return Outcome::Unreachable;

    // A server without the remembered key cannot make one that opens, so a fake server at a
    // typed address stops here.
    noise::Bytes plain;
    if (!h.read(answer.body, plain) || !h.finished()) return Outcome::Unreachable;
    json_object* theirs = jsonutil::parse(std::string(plain.begin(), plain.end()));
    if (!theirs) return Outcome::Unreachable;
    Outcome result = Outcome::Ok;
    if (jsonutil::flag(theirs, "removed")) {
        result = Outcome::Removed;
    } else if (kProtocol < jsonutil::num(theirs, "oldest", 1)) {
        result = Outcome::ConsoleBehind;
    } else if (jsonutil::num(theirs, "protocol", kProtocol) < kOldest) {
        result = Outcome::ServerBehind;
    } else {
        auto session = std::make_shared<Session>();
        if (!hexutil::decode(jsonutil::str(theirs, "session"), session->id)) {
            result = Outcome::Unreachable;
        } else {
            session->send = h.send1();
            session->recv = h.send2();
            out = std::move(session);
        }
    }
    json_object_put(theirs);
    return result;
}

ServerLink::Outcome ServerLink::open(std::shared_ptr<Session>& out, long seconds) {
    bool answered = false;
    return openBy([&](const std::vector<uint8_t>& body, Post& answer) { return firstPlace(body, seconds, answer); },
                  out, answered);
}

ServerLink::Place ServerLink::whatAnswers(const std::string& address, long seconds) {
    const std::vector<std::string> places = bases(address);
    std::shared_ptr<Session> unused;
    bool answered = false;
    const Outcome outcome = openBy(
        [&](const std::vector<uint8_t>& body, Post& answer) {
            for (const std::string& place : places) {
                answer = post(place, body, seconds);
                if (answer.answered) return true;
            }
            return false;
        },
        unused, answered);
    if (outcome != Outcome::Unreachable) return Place::ThisServer;
    return answered ? Place::Another : Place::Nothing;
}

ServerLink::Outcome ServerLink::sessionFor(std::shared_ptr<Session>& out, long seconds) {
    std::lock_guard<std::mutex> opening(opening_);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (session_) {
            out = session_;
            return Outcome::Ok;
        }
        if (std::chrono::steady_clock::now() < openAfter_) return lastOpen_;
    }
    std::shared_ptr<Session> fresh;
    const Outcome result = open(fresh, seconds);
    std::lock_guard<std::mutex> lk(mu_);
    lastOpen_ = result;
    if (result == Outcome::Ok) {
        session_ = fresh;
        out = fresh;
        waitSeconds_ = 0;
        openAfter_ = {};
    } else {
        waitSeconds_ = waitSeconds_ == 0 ? 1 : (waitSeconds_ * 2 > 60 ? 60 : waitSeconds_ * 2);
        openAfter_ = std::chrono::steady_clock::now() + std::chrono::seconds(waitSeconds_);
    }
    return result;
}

ServerLink::Outcome ServerLink::call(const std::string& request, std::string& answer, long seconds) {
    const noise::Bytes plain(request.begin(), request.end());
    Outcome last = Outcome::Unreachable;
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::shared_ptr<Session> session;
        last = sessionFor(session, seconds);
        if (last != Outcome::Ok) {
            if (last == Outcome::Unreachable && attempt == 0) continue;
            return last;
        }
        uint64_t counter = 0;
        std::string base;
        {
            std::lock_guard<std::mutex> lk(mu_);
            counter = session->next++;
            base = base_;
        }
        const noise::Bytes sealed = noise::sealRequest(session->send, session->id, counter, plain);
        std::vector<uint8_t> body{kKindSealed};
        body.insert(body.end(), sealed.begin(), sealed.end());
        const Post sent = post(base, body, seconds);
        noise::Bytes opened;
        if (sent.answered && sent.status == 200 &&
            noise::openAnswer(session->recv, session->id, counter, sent.body, opened)) {
            answer.assign(opened.begin(), opened.end());
            json_object* o = jsonutil::parse(answer);
            const std::string error = jsonutil::str(o, "error");
            if (o) json_object_put(o);
            return error == "removed" ? Outcome::Removed : Outcome::Ok;
        }
        // Any answer that is not 200, or none: a new session. This request, retried, goes under a
        // new counter in that session.
        last = Outcome::Unreachable;
        std::lock_guard<std::mutex> lk(mu_);
        if (session_ == session) session_.reset();
    }
    return last;
}

void ServerLink::waitBeforeRetry(const std::function<bool()>& stop) {
    std::chrono::steady_clock::time_point until;
    {
        std::lock_guard<std::mutex> lk(mu_);
        until = openAfter_;
    }
    const auto floor = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    if (until < floor) until = floor;
    while (std::chrono::steady_clock::now() < until && !stop())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void ServerLink::reset() {
    std::lock_guard<std::mutex> opening(opening_);
    std::lock_guard<std::mutex> lk(mu_);
    session_.reset();
    base_.clear();
    baseAddress_.clear();
    waitSeconds_ = 0;
    openAfter_ = {};
    lastOpen_ = Outcome::Ok;
}
