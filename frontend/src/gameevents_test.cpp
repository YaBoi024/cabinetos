// `--game-events-test` (#716): Game events with no network but the
// container's loopback. Every message with every field; the home-address
// rule on a table of addresses; and a receiver on loopback inside the test:
// messages arrive whole and in order, the knock gates them, a redirect is
// not followed, a slow receiver hits the time limit without holding the
// caller, and with no address nothing is sent. The code is gameevents.cpp
// and gameevents_wire.cpp.

#include "gameevents.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "gameevents_internal.h"

namespace gameevents {
namespace {

// A receiver on loopback: each request recorded whole, and answered as the
// test says, each on a thread of its own so a slow answer holds no other.
struct Receiver {
    struct Got {
        std::string path, body, event;
    };
    int fd = -1;
    int port = 0;
    std::thread th;
    std::vector<std::thread> answering;
    std::atomic<bool> stop{false};
    std::mutex m;
    std::vector<Got> got;
    int knockStatus = 200;   // what a `test` message is answered with

    bool open() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        const int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof a;
        if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(fd, 16) != 0 ||
            getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len) != 0)
            return false;
        port = ntohs(a.sin_port);
        th = std::thread([this] { serve(); });
        return true;
    }

    void serve() {
        while (!stop) {
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 50) <= 0) continue;
            const int c = accept(fd, nullptr, nullptr);
            if (c >= 0) answering.emplace_back([this, c] { answer(c); });
        }
    }

    void answer(int c) {
        std::string in;
        char buf[4096];
        size_t head = std::string::npos, want = 0;
        while (!stop) {
            pollfd p{c, POLLIN, 0};
            if (poll(&p, 1, 50) <= 0) continue;
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            in.append(buf, static_cast<size_t>(n));
            if (head == std::string::npos && (head = in.find("\r\n\r\n")) != std::string::npos) {
                std::string lower = in.substr(0, head);
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                const size_t cl = lower.find("content-length:");
                if (cl != std::string::npos) want = std::strtoul(lower.c_str() + cl + 15, nullptr, 10);
            }
            if (head != std::string::npos && in.size() >= head + 4 + want) break;
        }
        Got g;
        const size_t sp = in.find(' '), sp2 = in.find(' ', sp + 1);
        if (sp != std::string::npos && sp2 != std::string::npos) g.path = in.substr(sp + 1, sp2 - sp - 1);
        if (head != std::string::npos) g.body = in.substr(head + 4);
        if (json_object* o = json_tokener_parse(g.body.c_str())) {
            json_object* e = nullptr;
            if (json_object_object_get_ex(o, "event", &e)) g.event = json_object_get_string(e);
            json_object_put(o);
        }
        int status = 200;
        {
            std::lock_guard<std::mutex> lk(m);
            got.push_back(g);
            if (g.event == "test") status = knockStatus;
        }
        std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
        if (g.path == "/moved")
            reply = "HTTP/1.1 302 Found\r\nLocation: /elsewhere\r\nContent-Length: 0\r\n"
                    "Connection: close\r\n\r\n";
        else if (g.path == "/missing" || status == 404)
            reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (g.path == "/slow")
            for (int i = 0; i < 140 && !stop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ::send(c, reply.data(), reply.size(), MSG_NOSIGNAL);
        ::close(c);
    }

    std::vector<Got> take() {
        std::lock_guard<std::mutex> lk(m);
        std::vector<Got> out;
        out.swap(got);
        return out;
    }

    void close() {
        stop = true;
        if (th.joinable()) th.join();
        for (std::thread& t : answering)
            if (t.joinable()) t.join();
        if (fd >= 0) ::close(fd);
    }
};

std::string field(const std::string& body, const char* key) {
    std::string out;
    if (json_object* o = json_tokener_parse(body.c_str())) {
        json_object* v = nullptr;
        if (json_object_object_get_ex(o, key, &v)) out = json_object_get_string(v);
        json_object_put(o);
    }
    return out;
}

bool hasField(const std::string& body, const char* key) {
    bool has = false;
    if (json_object* o = json_tokener_parse(body.c_str())) {
        has = json_object_object_get_ex(o, key, nullptr);
        json_object_put(o);
    }
    return has;
}

sockaddr_storage addr(const char* ip) {
    sockaddr_storage s{};
    if (inet_pton(AF_INET, ip, &reinterpret_cast<sockaddr_in*>(&s)->sin_addr) == 1)
        s.ss_family = AF_INET;
    else if (inet_pton(AF_INET6, ip, &reinterpret_cast<sockaddr_in6*>(&s)->sin6_addr) == 1)
        s.ss_family = AF_INET6;
    return s;
}

bool homeText(const char* ip) {
    const sockaddr_storage s = addr(ip);
    return isHome(reinterpret_cast<const sockaddr*>(&s));
}

}  // namespace

int test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    };
    curl_global_init(CURL_GLOBAL_DEFAULT);
    using Clock = std::chrono::steady_clock;
    auto msSince = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };

    std::printf("The parts of a message\n");
    check(base64({}).empty() && base64({'f'}) == "Zg==" && base64({'f', 'o'}) == "Zm8=" &&
              base64({'f', 'o', 'o'}) == "Zm9v" && base64({'f', 'o', 'o', 'b', 'a', 'r'}) == "Zm9vYmFy",
          "base64 as RFC 4648's vectors");
    const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0};
    const std::vector<uint8_t> webp = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
    check(imageType(png) == "image/png" && imageType(webp) == "image/webp" &&
              imageType({0xFF, 0xD8, 0xFF, 0xE0}) == "image/jpeg" &&
              imageType({1, 2, 3}) == "application/octet-stream",
          "a cover is named by its own first bytes");
    {
        const std::string t = localTime(std::time(nullptr));
        check(t.size() == 25 && t[4] == '-' && t[10] == 'T' && (t[19] == '+' || t[19] == '-') && t[22] == ':',
              "the time is local, with its offset: " + t);
    }

    std::printf("Home addresses\n");
    // Each range's first and last address, and the documented examples
    // (ci/check-code-and-docs-agree.py, ADDRESSES).
    for (const char* ip : {"127.0.0.1", "127.255.255.254", "10.0.0.0", "10.255.255.255", "172.16.0.0",
                           "172.31.255.255", "192.168.0.0", "192.168.255.255", "100.64.0.1",
                           "100.127.255.255", "::1", "fc00::1", "fd7a:115c:a1e0::1", "fe80::1",
                           "febf::1", "::ffff:192.168.0.20"})
        check(homeText(ip), std::string(ip) + " is home");
    for (const char* ip : {"8.8.8.8", "1.1.1.1", "172.15.255.255", "172.32.0.1", "192.169.0.1",
                           "100.63.255.255", "100.128.0.1", "11.0.0.1", "169.254.1.1", "0.0.0.0",
                           "2001:db8::1", "fec0::1", "fe00::1", "::", "::2", "::ffff:8.8.8.8",
                           "2606:4700::1111"})
        check(!homeText(ip), std::string(ip) + " is not home");
    check(!allHome({addr("192.168.0.20"), addr("8.8.8.8")}),
          "a name that gives one public address among private ones is not home");
    check(allHome({addr("192.168.0.20"), addr("fd00::1")}), "a name that gives only home addresses is");
    check(!allHome({}), "a name that gives nothing is not home");
    {
        const auto t0 = Clock::now();
        const Tried a = tryAddress("http://8.8.8.8:9/hook");
        const Tried b = tryAddress("http://[2001:db8::1]:9/hook");
        check(a.answer == Answer::NotHome && b.answer == Answer::NotHome && msSince(t0) < 500,
              "a public address is refused before anything connects: " + why(a));
        check(tryAddress("ftp://192.168.0.20/hook").answer == Answer::CantConnect, "only http and https");
    }

    Receiver rx;
    if (!rx.open()) {
        check(false, "a receiver on loopback");
        return failures;
    }
    const std::string base = "http://127.0.0.1:" + std::to_string(rx.port);
    auto waitFor = [&](size_t n, int ms) {
        std::vector<Receiver::Got> all;
        const auto t0 = Clock::now();
        while (msSince(t0) < ms) {
            for (Receiver::Got& g : rx.take()) all.push_back(std::move(g));
            if (all.size() >= n) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return all;
    };
    auto events = [](const std::vector<Receiver::Got>& v) {
        std::string s;
        for (const Receiver::Got& g : v) s += (s.empty() ? "" : " ") + g.event;
        return s;
    };
    auto drained = [&](int ms) {
        const auto t0 = Clock::now();
        while (pending() > 0 && msSince(t0) < ms) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return pending() == 0;
    };
    auto knockWith = [&](int status) {
        std::lock_guard<std::mutex> lk(rx.m);
        rx.knockStatus = status;
    };
    std::mutex netMutex;
    std::string net = "net-a";
    auto setNet = [&](const std::string& n) {
        std::lock_guard<std::mutex> lk(netMutex);
        net = n;
    };
    standIns(
        [&] {
            std::lock_guard<std::mutex> lk(netMutex);
            return net;
        },
        [&](const std::string& key) { return key == "big" ? png : std::vector<uint8_t>(); });
    const Who who{"Sam", "unused"};
    Game contra;
    contra.title = "Contra III: The Alien Wars";
    contra.system = "Super Nintendo";
    contra.romId = 1234;
    contra.covers = {"big", "small"};

    std::printf("Off is off\n");
    started(contra, who);
    paused(true, who);
    ended(60000, who);
    check(!on() && !senderJoinable(), "with no address the sender is never started");
    check(waitFor(1, 300).empty(), "and nothing is sent");

    std::printf("The keyboard's Done\n");
    {
        const Tried t = tryAddress(base + "/hook");
        const std::vector<Receiver::Got> got = waitFor(1, 1000);
        check(t.answer == Answer::Ok && why(t).empty(), "an app at home answers the test message");
        check(got.size() == 1 && got[0].event == "test" && got[0].path == "/hook" &&
                  field(got[0].body, "format") == "1" && hasField(got[0].body, "time") &&
                  !hasField(got[0].body, "person") && !hasField(got[0].body, "game"),
              "the test message names nobody and no game");
        const Tried missing = tryAddress(base + "/missing");
        check(missing.answer == Answer::Error && why(missing) == "It answered with an error (404)",
              "an error says its number: " + why(missing));
        waitFor(1, 500);
        const Tried local = tryAddress("http://localhost:" + std::to_string(rx.port) + "/hook");
        check(local.answer == Answer::Ok, "a name is looked up, checked, and connected to as checked");
        waitFor(1, 500);
    }

    std::printf("Every message, whole and in order\n");
    startSending(base + "/hook", /*knocked=*/true);
    check(on(), "an address saved starts the sender");
    {
        const auto t0 = Clock::now();
        started(contra, who);
        paused(true, who);
        paused(true, who);
        paused(false, who);
        unlocked("Clean Sweep", 10, who);
        completed(who);
        ended(125000, who);
        check(msSince(t0) < 50, "the calls return at once");
        const std::vector<Receiver::Got> got = waitFor(6, 3000);
        check(events(got) == "game_started game_paused game_resumed achievement_unlocked set_completed "
                             "game_ended",
              "in order, a pause said once, and no knock after Done's test: " + events(got));
        bool whole = got.size() == 6;
        for (const Receiver::Got& g : got)
            whole = whole && field(g.body, "format") == "1" && field(g.body, "person") == "Sam" &&
                    field(g.body, "game") == contra.title && field(g.body, "system") == "Super Nintendo" &&
                    field(g.body, "rom_id") == "1234" && field(g.body, "linkup") == "unused" &&
                    field(g.body, "time").size() == 25;
        check(whole, "each with format, time, person, game, system, rom_id and linkup");
        if (got.size() == 6) {
            check(field(got[0].body, "cover_type") == "image/png" && field(got[0].body, "cover") == base64(png),
                  "started carries the cover the console keeps, as base64");
            check(!hasField(got[1].body, "cover"), "paused carries none");
            check(field(got[3].body, "achievement") == "Clean Sweep" && field(got[3].body, "points") == "10",
                  "an unlock names the achievement and its points");
            check(field(got[4].body, "cover_type") == "image/png", "set completed carries the cover");
            check(field(got[5].body, "minutes_played") == "2", "ended says the minutes played");
        }
    }

    std::printf("A LinkUp friend's console\n");
    {
        const Who alex{"Alex", "online"};
        Game friendGame;
        friendGame.title = contra.title;
        friendGame.friendSide = true;
        started(friendGame, alex);
        paused(true, alex);
        ended(0, alex);
        const std::vector<Receiver::Got> got = waitFor(2, 3000);
        check(events(got) == "game_started game_ended", "started and ended only: " + events(got));
        bool lean = got.size() == 2;
        for (const Receiver::Got& g : got)
            lean = lean && field(g.body, "person") == "Alex" && field(g.body, "game") == contra.title &&
                   !hasField(g.body, "rom_id") && !hasField(g.body, "system") && !hasField(g.body, "cover");
        check(lean, "under the friend's name, without rom_id, system and cover");
        check(waitFor(1, 300).empty(), "nothing more");
    }

    std::printf("The knock on each network\n");
    {
        setNet("net-b");
        started(contra, who);
        const std::vector<Receiver::Got> got = waitFor(2, 3000);
        check(events(got) == "test game_started", "a new network: the knock first: " + events(got));
        setNet("net-c");
        check(events(waitFor(1, 7000)) == "test", "and it goes as the console joins one, with nothing to send");
        knockWith(404);
        setNet("net-d");
        paused(true, who);
        const std::vector<Receiver::Got> gated = waitFor(2, 1500);
        check(events(gated) == "test" && drained(2000),
              "a knock not answered: nothing else goes on that network: " + events(gated));
        knockWith(200);
        paused(false, who);
        check(waitFor(1, 1000).empty() && drained(2000), "not even once it would answer");
        setNet("net-e");
        ended(60000, who);
        check(events(waitFor(2, 3000)) == "test game_ended", "the next network knocks again");
        setNet("");
        started(contra, who);
        check(waitFor(1, 1000).empty() && drained(2000), "no network: nothing sent, nothing kept");
        setNet("net-f");
        check(events(waitFor(1, 7000)) == "test", "back on a network, the knock");
        ended(0, who);
        waitFor(1, 2000);
    }

    std::printf("No redirect followed\n");
    {
        const Tried t = tryAddress(base + "/moved");
        check(t.answer == Answer::Error && t.status == 302, "a redirect is an answer, not a 2xx");
        startSending(base + "/moved", /*knocked=*/false);
        started(contra, who);
        const std::vector<Receiver::Got> got = waitFor(3, 1500);
        bool elsewhere = false;
        for (const Receiver::Got& g : got) elsewhere = elsewhere || g.path == "/elsewhere";
        check(!elsewhere, "and where it points is never asked");
        check(events(got) == "test test", "a knock answered with one sends nothing after: " + events(got));
        ended(0, who);
        drained(2000);
    }

    std::printf("A slow receiver\n");
    {
        const auto t0 = Clock::now();
        const Tried t = tryAddress(base + "/slow");
        const long took = static_cast<long>(msSince(t0));
        check(t.answer == Answer::CantConnect && took >= 4500 && took < 6500,
              "the test message gives up at the time limit (" + std::to_string(took) + " ms)");
        startSending(base + "/slow", /*knocked=*/true);
        const auto t1 = Clock::now();
        started(contra, who);
        ended(0, who);
        check(msSince(t1) < 50, "the game is not held");
        check(drained(13000), "each message dropped at the limit, and the line empties");
        waitFor(3, 300);
    }

    std::printf("Stop sending, and the exit\n");
    {
        stopRunning();
        started(contra, who);
        check(!on() && pending() == 0, "stopped: nothing goes in the line");
        check(waitFor(1, 500).empty(), "and nothing is sent");
        startSending(base + "/hook", /*knocked=*/true);
        started(contra, who);
        shutdown();
        check(events(waitFor(1, 1000)) == "game_started" && !senderJoinable(),
              "the exit lets what is waiting go, then joins the sender");
    }

    rx.close();
    standIns(nullptr, nullptr);
    std::printf("%s: %d failed\n", failures ? "FAILED" : "passed", failures);
    return failures;
}

}  // namespace gameevents
