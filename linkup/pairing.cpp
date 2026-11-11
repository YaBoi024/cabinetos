// Pairing (pairing.h).

#include "pairing.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <openssl/crypto.h>

#include "box.h"
#include "jsonutil.h"
#include "server_link.h"

namespace pairing {

namespace {

constexpr uint8_t kKindPairFirst = 1;
constexpr uint8_t kKindPairSecond = 2;
constexpr size_t kPairIdLen = 16;
constexpr long kSeconds = 15;

}  // namespace

std::string tidy(const std::string& typed) {
    static const char kAccepts[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    std::string out;
    for (char ch : typed) {
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (up != '\0' && std::strchr(kAccepts, up)) out.push_back(up);
    }
    return out.size() == 12 ? out : std::string();
}

Result pair(Keys& keys, const std::string& address, const std::string& code, const std::string& joining,
            std::string& serverName) {
    const std::string twelve = tidy(code);
    if (twelve.empty()) {
        std::fprintf(stderr, "[linkup] pairing: what was typed is not 12 characters of the code's alphabet (%zu typed)\n", code.size());
        return Result::WrongCode;
    }
    noise::Handshake::Config c;
    c.pattern = noise::Pattern::XXpsk0;
    c.initiator = true;
    c.prologue = "cabinet-linkup pair 1";
    c.s = keys.mine();
    noise::Key k{};
    if (!noise::slowHash(twelve, k)) return Result::Failed;
    c.psk = k;
    OPENSSL_cleanse(k.data(), k.size());
    noise::Handshake h(c);
    if (c.psk) OPENSSL_cleanse(c.psk->data(), c.psk->size());

    noise::Bytes first;
    if (!h.write({}, first) || first.size() != 48) return Result::Failed;
    std::vector<uint8_t> body{kKindPairFirst};
    body.insert(body.end(), first.begin(), first.end());

    std::string base;
    ServerLink::Post answer;
    for (const std::string& place : ServerLink::bases(address)) {
        answer = ServerLink::post(place, body, kSeconds);
        if (answer.answered) {
            base = place;
            break;
        }
    }
    if (base.empty()) return Result::Unreachable;
    if (answer.status == 403) {
        std::fprintf(stderr, "[linkup] pairing: the server refused the first message (403): the code it holds is not this one\n");
        return Result::WrongCode;
    }
    if (answer.status == 429) return Result::TooManyTries;
    if (answer.status != 200 || answer.body.size() <= kPairIdLen) {
        std::fprintf(stderr, "[linkup] pairing: the first answer was status %ld, %zu bytes\n", static_cast<long>(answer.status), answer.body.size());
        return Result::Unreachable;
    }

    // Message 2 is the server's proof. Something that cannot make one that opens does not hold
    // the code: a typo that happened to fit nothing there, or a fake. It stops here, keeping nothing.
    const std::vector<uint8_t> pairId(answer.body.begin(), answer.body.begin() + kPairIdLen);
    const noise::Bytes second(answer.body.begin() + kPairIdLen, answer.body.end());
    noise::Bytes helloPlain;
    if (!h.read(second, helloPlain) || !h.remoteStatic()) return Result::NotThatServer;
    json_object* hello = jsonutil::parse(std::string(helloPlain.begin(), helloPlain.end()));
    const std::string name = box::cleanText(jsonutil::str(hello, "name"), box::kMaxUsername);
    if (hello) json_object_put(hello);

    noise::Bytes third;
    if (!h.write(noise::Bytes(joining.begin(), joining.end()), third) || !h.finished()) return Result::Failed;
    body = {kKindPairSecond};
    body.insert(body.end(), pairId.begin(), pairId.end());
    body.insert(body.end(), third.begin(), third.end());

    // The server cannot know its reply arrived, so a lost one is asked for with the same bytes.
    ServerLink::Post reply;
    for (int attempt = 0; attempt < 3; ++attempt) {
        reply = ServerLink::post(base, body, kSeconds);
        if (reply.answered) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (!reply.answered) return Result::Unreachable;
    if (reply.status == 403) {
        std::fprintf(stderr, "[linkup] pairing: the server refused the second message (403)\n");
        return Result::WrongCode;
    }
    if (reply.status == 429) return Result::TooManyTries;
    if (reply.status != 200) return Result::Unreachable;

    noise::Bytes plain;
    if (!noise::open(h.send2(), 0, nullptr, 0, reply.body.data(), reply.body.size(), plain)) return Result::NotThatServer;
    json_object* result = jsonutil::parse(std::string(plain.begin(), plain.end()));
    const bool ok = jsonutil::flag(result, "ok");
    const std::string error = jsonutil::str(result, "error");
    if (result) json_object_put(result);
    if (!ok) {
        std::fprintf(stderr, "[linkup] pairing: the server said %s\n", error.c_str());
        return error == "wrong_code" ? Result::WrongCode : Result::Refused;
    }

    // Only now is the server's key kept (PROTOCOL.md).
    Keys::Server server;
    server.address = address;
    server.key = *h.remoteStatic();
    server.name = name;
    if (!keys.remember(server)) return Result::Failed;
    serverName = name;
    return Result::Paired;
}

}  // namespace pairing
