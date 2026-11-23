// A console on the command line, for trying this program's server code against a real server by
// hand and for tools/linkup-interop.sh. Not in the image: `make probe` builds it. It holds no
// lock check and no stream, only what it says on the line, and one folder per console.
//
//   linkup-probe <folder> pair <address> <code> <account> <username>
//   linkup-probe <folder> address <address>
//   linkup-probe <folder> op '<json>'
//   linkup-probe <folder> seal <friend id> <invite> <invite|signal> <text>
//   linkup-probe <folder> open <friend id> <invite> <base64>
//   linkup-probe <folder> key
//   linkup-probe <folder> remember <address> <hex key>   as a pairing with a fake server would
//   linkup-probe <folder> fake <port> <mode>             a fake server: msg2, redirect, huge,
//                                                        console-behind, server-behind, removed

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <openssl/rand.h>

#include "box.h"
#include "hexutil.h"
#include "jsonutil.h"
#include "keys.h"
#include "pairing.h"
#include "server_link.h"

namespace {

const char* wordOf(ServerLink::Outcome o) {
    switch (o) {
        case ServerLink::Outcome::Ok: return "ok";
        case ServerLink::Outcome::Unreachable: return "unreachable";
        case ServerLink::Outcome::Removed: return "removed";
        case ServerLink::Outcome::ConsoleBehind: return "console_behind";
        case ServerLink::Outcome::ServerBehind: return "server_behind";
    }
    return "?";
}

const char* wordOf(pairing::Result r) {
    switch (r) {
        case pairing::Result::Paired: return "paired";
        case pairing::Result::WrongCode: return "wrong_code";
        case pairing::Result::TooManyTries: return "too_many_tries";
        case pairing::Result::Unreachable: return "unreachable";
        case pairing::Result::NotThatServer: return "not_that_server";
        case pairing::Result::Refused: return "refused";
        case pairing::Result::Failed: return "failed";
    }
    return "?";
}

bool friendKey(ServerLink& link, const std::string& id, noise::Key& key, std::string& name) {
    std::string answer;
    if (link.call("{\"op\":\"friends\"}", answer) != ServerLink::Outcome::Ok) return false;
    json_object* o = jsonutil::parse(answer);
    json_object* list = jsonutil::get(o, "friends");
    bool found = false;
    for (size_t i = 0; list && i < json_object_array_length(list); ++i) {
        json_object* f = json_object_array_get_idx(list, i);
        if (jsonutil::str(f, "id") != id) continue;
        std::vector<uint8_t> raw;
        if (hexutil::fromBase64(jsonutil::str(f, "key"), raw) && raw.size() == key.size()) {
            std::copy(raw.begin(), raw.end(), key.begin());
            name = jsonutil::str(f, "username");
            found = true;
        }
    }
    if (o) json_object_put(o);
    return found;
}

void sendAll(int fd, const std::string& text) {
    size_t at = 0;
    while (at < text.size()) {
        const ssize_t n = write(fd, text.data() + at, text.size() - at);
        if (n <= 0) return;
        at += static_cast<size_t>(n);
    }
}

void answerWith(int fd, const char* status, const std::string& body, const std::string& extra = "") {
    sendAll(fd, std::string("HTTP/1.1 ") + status + "\r\n" + extra + "Content-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
}

// One request read off a socket: enough of HTTP for a console's POST.
std::string readBody(int fd) {
    std::string in;
    char buf[4096];
    size_t head = std::string::npos;
    size_t want = 0;
    while (true) {
        const ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) return {};
        in.append(buf, static_cast<size_t>(n));
        if (head == std::string::npos) {
            head = in.find("\r\n\r\n");
            if (head == std::string::npos) continue;
            std::string lower = in.substr(0, head);
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const size_t cl = lower.find("content-length:");
            want = cl == std::string::npos ? 0 : std::strtoul(lower.c_str() + cl + 15, nullptr, 10);
        }
        if (in.size() >= head + 4 + want) return in.substr(head + 4, want);
    }
}

// What a console must not believe: answers that look like a server's and do not hold up.
int fakeServer(const Keys& keys, int port, const std::string& mode) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    const int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(listener, 8) != 0) return 2;
    while (true) {
        const int fd = accept(listener, nullptr, nullptr);
        if (fd < 0) return 2;
        const std::string body = readBody(fd);
        if (mode == "msg2") {
            std::string junk(16 + 96, '\0');
            RAND_bytes(reinterpret_cast<unsigned char*>(junk.data()), static_cast<int>(junk.size()));
            answerWith(fd, "200 OK", junk);
        } else if (mode == "redirect") {
            answerWith(fd, "307 Temporary Redirect", "", "Location: http://127.0.0.1:" + std::to_string(port + 1) + "/linkup/v1\r\n");
        } else if (mode == "huge") {
            answerWith(fd, "200 OK", std::string(300 * 1024, 'x'));
        } else if (!body.empty() && body[0] == 3) {
            // An Open, under the key this fake holds.
            noise::Handshake::Config c;
            c.pattern = noise::Pattern::IK;
            c.initiator = false;
            c.prologue = "cabinet-linkup session 1";
            c.s = keys.mine();
            noise::Handshake h(c);
            noise::Bytes plain;
            noise::Bytes reply;
            const std::string payload = mode == "console-behind"  ? "{\"protocol\":9,\"oldest\":9}"
                                        : mode == "server-behind" ? "{\"protocol\":0,\"oldest\":0}"
                                                                  : "{\"removed\":true}";
            if (h.read(noise::Bytes(body.begin() + 1, body.end()), plain) &&
                h.write(noise::Bytes(payload.begin(), payload.end()), reply)) {
                answerWith(fd, "200 OK", std::string(reply.begin(), reply.end()));
            } else {
                answerWith(fd, "404 Not Found", "");
            }
        } else {
            answerWith(fd, "404 Not Found", "");
        }
        close(fd);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: linkup-probe <folder> <action> ...\n");
        return 2;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Keys keys(argv[1]);
    if (!keys.load()) {
        std::fprintf(stderr, "cannot use that folder\n");
        return 2;
    }
    ServerLink link(keys);
    const std::string action = argv[2];
    if (action == "pair" && argc == 7) {
        json_object* j = json_object_new_object();
        jsonutil::set(j, "kind", "console");
        jsonutil::set(j, "protocol", ServerLink::kProtocol);
        jsonutil::set(j, "oldest", ServerLink::kOldest);
        jsonutil::set(j, "stream", ServerLink::kStream);
        jsonutil::set(j, "account", argv[5]);
        jsonutil::set(j, "username", argv[6]);
        std::string name;
        const pairing::Result r = pairing::pair(keys, argv[3], argv[4], jsonutil::text(j), name);
        json_object_put(j);
        std::printf("%s %s\n", wordOf(r), name.c_str());
        return r == pairing::Result::Paired ? 0 : 1;
    }
    if (action == "address" && argc == 4) {
        // As Change server does it (conductor.cpp, cmdRetype): another server's address is not kept.
        if (link.whatAnswers(argv[3]) == ServerLink::Place::Another) {
            std::printf("another\n");
            return 1;
        }
        keys.setAddress(argv[3]);
        std::string answer;
        const auto o = link.call("{\"op\":\"config\"}", answer);
        std::printf("%s %s\n", wordOf(o), answer.c_str());
        return o == ServerLink::Outcome::Ok ? 0 : 1;
    }
    if (action == "op" && argc == 4) {
        std::string answer;
        const auto o = link.call(argv[3], answer, 40);
        std::printf("%s %s\n", wordOf(o), answer.c_str());
        return o == ServerLink::Outcome::Ok ? 0 : 1;
    }
    if (action == "seal" && argc == 7) {
        // seal <friend id> <invite id> <type> <text>: a box of that type, printed in base64.
        noise::Key peer{};
        std::string peerName;
        if (!friendKey(link, argv[3], peer, peerName)) return 1;
        box::Contents c;
        c.type = argv[5];
        c.n = 1;
        c.expires = std::time(nullptr) + 600;
        c.from = "probe";
        c.to = peerName;
        c.stream = ServerLink::kStream;
        if (c.type == "invite") c.game = argv[6];
        else c.details = argv[6];
        const std::string text = box::write(c);
        noise::Bytes sealed;
        if (!box::seal(keys.mine(), peer, argv[4], noise::Bytes(text.begin(), text.end()), sealed)) return 1;
        std::printf("%s\n", hexutil::toBase64(sealed).c_str());
        return 0;
    }
    if (action == "open" && argc == 6) {
        noise::Key peer{};
        std::string peerName;
        if (!friendKey(link, argv[3], peer, peerName)) return 1;
        std::vector<uint8_t> raw;
        if (!hexutil::fromBase64(argv[5], raw)) return 1;
        noise::Bytes plain;
        if (!box::open(keys.mine(), peer, argv[4], raw, plain)) {
            std::printf("refused\n");
            return 1;
        }
        std::printf("%s\n", std::string(plain.begin(), plain.end()).c_str());
        return 0;
    }
    if (action == "remember" && argc == 5) {
        Keys::Server server;
        server.address = argv[3];
        if (!hexutil::decode(argv[4], server.key)) return 2;
        return keys.remember(server) ? 0 : 1;
    }
    if (action == "fake" && argc == 5) return fakeServer(keys, std::atoi(argv[3]), argv[4]);
    if (action == "key") {
        std::printf("%s\n", hexutil::encode(keys.mine().pub.data(), keys.mine().pub.size()).c_str());
        return 0;
    }
    std::fprintf(stderr, "unknown action\n");
    return 2;
}
