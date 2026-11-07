// Every request to the LinkUp server: libcurl, and PROTOCOL.md's "Moving bytes" and "Sessions".
// This, pairing.cpp and the stream's own connection are the only internet this program makes.
//
// No redirects, http and https only, a cap on what it reads, timeouts, and no certificate
// check: a certificate is not what makes this safe, so a self-signed proxy works, and what comes
// back is proved by its seal.
//
// ONE SESSION, ANY NUMBER OF REQUESTS AT ONCE. The long `wait` and everything else run on
// different threads and share one session. One Open at a time, because a lower `t` arriving
// second is refused as a replay. A counter is used up when it is sealed.

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "keys.h"
#include "noise.h"

class ServerLink {
public:
    // The numbers stated on connect. Only a real break moves one (PROTOCOL.md).
    static constexpr int kProtocol = 1;
    static constexpr int kOldest = 1;
    static constexpr int kStream = 1;

    enum class Outcome {
        Ok,
        Unreachable,     // no answer, or one that did not open, or one that was not 200
        Removed,         // an owner took this console off the server
        ConsoleBehind,   // this console is too old for the server
        ServerBehind,    // the server is too old for this console
    };

    explicit ServerLink(Keys& keys);

    // One operation, opening a session if there is none and once more if the one it had has gone.
    // `answer` is the JSON that came back, which may hold an "error" word. Blocks: never on the
    // main loop.
    Outcome call(const std::string& request, std::string& answer, long seconds = 15);

    // Waits as PROTOCOL.md asks between tries at opening: growing, up to 60 seconds. For the long
    // request's loop. `stop` ends the wait early.
    void waitBeforeRetry(const std::function<bool()>& stop);

    // Drops the session, after the address changed or the console left.
    void reset();

    // The places an address could mean, PROTOCOL.md's table. Exposed for the test.
    static std::vector<std::string> bases(const std::string& address);

    struct Post {
        bool answered = false;
        long status = 0;
        std::vector<uint8_t> body;
    };
    // One POST of sealed bytes to `<base>/linkup/v1`. Blocks for at most `seconds`.
    static Post post(const std::string& base, const std::vector<uint8_t>& body, long seconds);

private:
    struct Session {
        noise::SessionId id{};
        noise::Key send{};
        noise::Key recv{};
        uint64_t next = 1;
    };
    Outcome open(std::shared_ptr<Session>& out, long seconds);
    Outcome sessionFor(std::shared_ptr<Session>& out, long seconds);
    bool firstPlace(const std::vector<uint8_t>& body, long seconds, Post& out);

    Keys& keys_;
    std::mutex mu_;          // session_, base_, the wait
    std::mutex opening_;     // one Open at a time
    std::shared_ptr<Session> session_;
    std::string base_;
    std::string baseAddress_;
    Outcome lastOpen_ = Outcome::Ok;
    int waitSeconds_ = 0;
    std::chrono::steady_clock::time_point openAfter_{};
};
