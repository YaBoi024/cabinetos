// `--token-revoke-test` (#545): the tokens this console is done with, against
// a stand-in RomM on loopback inside the test and no other network. The
// stand-in answers as RomM 5.5.0-beta.1 does where it matters here: a token
// revokes itself and nothing else (403), a token it does not know is refused
// (401), a pairing's answer carries the token's number; and, told to, as a
// RomM before 5.5, as one that is failing, or as one that takes a connection
// and never answers. The code is tokenrevoke.cpp, accounts.cpp, server.cpp
// and the client's own part in romm_http.cpp and romm_pairing.cpp.

#include "tokenrevoke.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "firstrun.h"
#include "romm.h"
#include "server.h"
#include "storage.h"

namespace tokenrevoke {
namespace {

using Clock = std::chrono::steady_clock;

long msSince(Clock::time_point t) {
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count());
}

struct StandIn {
    enum class As { Romm55, Romm54, Failing, Mute };

    int fd = -1;
    int port = 0;
    std::thread th;
    std::vector<std::thread> answering;
    std::atomic<bool> stop{false};
    std::atomic<bool> letSlowGo{false};
    std::atomic<int> slowHere{0};

    std::mutex m;   // guards the rest
    As as = As::Romm55;
    std::map<std::string, int> tokens;     // what it would accept, with its number
    std::map<std::string, int> person;     // whose each is
    std::string pairingAnswer;
    std::vector<std::string> deletes;      // "<number> by <token>: <status>"
    std::vector<bool> loginsThere;         // whether the first login's file was, at each
    int heartbeats = 0;

    std::string address() const { return "127.0.0.1:" + std::to_string(port); }

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
        th = std::thread([this] {
            while (!stop) {
                pollfd p{fd, POLLIN, 0};
                if (poll(&p, 1, 20) <= 0) continue;
                const int c = accept(fd, nullptr, nullptr);
                if (c >= 0) answering.emplace_back([this, c] { answer(c); });
            }
        });
        return true;
    }

    void answer(int c) {
        std::string in;
        char buf[4096];
        while (!stop && in.find("\r\n\r\n") == std::string::npos) {
            pollfd p{c, POLLIN, 0};
            if (poll(&p, 1, 20) <= 0) continue;
            const ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            in.append(buf, static_cast<size_t>(n));
        }
        const size_t sp = in.find(' '), sp2 = in.find(' ', sp + 1);
        const std::string method = sp == std::string::npos ? "" : in.substr(0, sp);
        const std::string path =
            sp2 == std::string::npos ? "" : in.substr(sp + 1, sp2 - sp - 1);
        std::string token;
        if (const size_t b = in.find("Bearer "); b != std::string::npos)
            token = in.substr(b + 7, in.find("\r\n", b) - b - 7);

        As now;
        {
            std::lock_guard<std::mutex> lk(m);
            now = as;
        }
        if (now == As::Mute) {
            while (!stop && !letSlowGo) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            ::close(c);
            return;
        }
        int status = 200;
        std::string body = "null";
        if (path == "/api/heartbeat") {
            std::lock_guard<std::mutex> lk(m);
            ++heartbeats;
            body = R"({"SYSTEM":{"VERSION":"5.5.0-beta.1"},"SAVE_SYNC":{}})";
        } else if (path == "/api/auth/device/token") {
            std::lock_guard<std::mutex> lk(m);
            body = pairingAnswer;
        } else if (path == "/slow") {
            ++slowHere;
            while (!stop && !letSlowGo) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            body = "slow";
        } else if (path == "/api/users/me") {
            std::lock_guard<std::mutex> lk(m);
            if (tokens.count(token))
                body = "{\"id\":" + std::to_string(person[token]) + ",\"username\":\"person" +
                       std::to_string(person[token]) + "\",\"current_device_id\":\"one\"}";
            else
                status = 401;
        } else if (method == "DELETE" && path.rfind("/api/client-tokens/", 0) == 0) {
            std::lock_guard<std::mutex> lk(m);
            const int id = std::atoi(path.c_str() + 19);
            const auto it = tokens.find(token);
            if (now == As::Failing) status = 500;
            else if (it == tokens.end()) status = 401;
            else if (now == As::Romm54 || it->second != id) status = 403;
            else tokens.erase(it);
            deletes.push_back(std::to_string(id) + " by " + token + ": " + std::to_string(status));
            loginsThere.push_back(storage::exists(accounts::tokenPath(1)));
        } else {
            status = 404;
        }
        const std::string reply = "HTTP/1.1 " + std::to_string(status) +
                                  " x\r\nContent-Type: application/json\r\nContent-Length: " +
                                  std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        ::send(c, reply.data(), reply.size(), MSG_NOSIGNAL);
        ::close(c);
    }

    void behaveAs(As what) {
        std::lock_guard<std::mutex> lk(m);
        as = what;
    }
    // A token of `who`'s that it accepts, under `number`.
    void issue(const std::string& token, int number, int who) {
        std::lock_guard<std::mutex> lk(m);
        tokens[token] = number;
        person[token] = who;
    }
    // The next pairing's answer, as RomM 5.5 gives it (recorded 2026-11-22 from
    // the write-test RomM, 5.5.0-beta.1) or as 5.4.1 does, without the number.
    void nextPairing(const std::string& token, int number, int who, bool withNumber = true) {
        issue(token, number, who);
        std::lock_guard<std::mutex> lk(m);
        pairingAnswer = "{";
        if (withNumber) pairingAnswer += "\"id\":" + std::to_string(number) + ",";
        pairingAnswer += "\"access_token\":\"" + token +
                         "\",\"device_id\":\"one\",\"scopes\":[\"me.read\"],\"expires_at\":null}";
    }
    bool accepts(const std::string& token) {
        std::lock_guard<std::mutex> lk(m);
        return tokens.count(token) > 0;
    }
    std::string deleted() {
        std::lock_guard<std::mutex> lk(m);
        std::string all;
        for (const std::string& d : deletes) all += (all.empty() ? "" : ", ") + d;
        deletes.clear();
        return all;
    }
    void close() {
        stop = true;
        if (th.joinable()) th.join();
        for (std::thread& t : answering)
            if (t.joinable()) t.join();
        if (fd >= 0) ::close(fd);
    }
};

// A pairing with the stand-in, as the console's screens do one.
bool pair(const StandIn& romm, accounts::Paired* who) {
    romm::Client c;
    std::string err;
    romm::Pairing p;
    p.deviceCode = "code";
    const bool ok = c.setAddress(romm.address(), &err) && c.pollPairing(p, &err) == 1 &&
                    accounts::recordPairing(c, who, &err);
    if (!ok) std::printf("        %s\n", err.c_str());
    return ok;
}

}  // namespace

int test() {
    int failures = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    };
    auto same = [&](const std::string& got, const std::string& want, const std::string& what) {
        check(got == want, what + (got == want ? "" : " (got \"" + got + "\")"));
    };

    char root[] = "/tmp/cabinetos-revoke-XXXXXX";
    if (!mkdtemp(root)) return 1;
    // The list follows the storage root, the logins HOME, and the drive search
    // and the server's address the environment: none may be the console's own.
    const std::string r = root;
    storage::setRoot(r);
    setenv("HOME", (r + "/home").c_str(), 1);
    setenv("CABINETOS_DRIVES", (r + "/drives").c_str(), 1);
    std::string err;
    if (!storage::ensureTree(&err)) {
        std::fprintf(stderr, "[revoke] scratch root unusable: %s\n", err.c_str());
        return 1;
    }
    StandIn romm;
    if (!romm.open()) {
        check(false, "a stand-in RomM on loopback");
        return failures;
    }
    setenv("CABINETOS_ROMM", romm.address().c_str(), 1);
    auto owedFile = [] { return storage::exists(accounts::revokePath()); };
    auto mode = [](const std::string& path) {
        struct stat st {};
        return ::stat(path.c_str(), &st) == 0 ? static_cast<int>(st.st_mode & 0777) : 0;
    };
    accounts::Paired who;

    std::printf("Pairing\n");
    romm.nextPairing("one-a", 11, 1);
    check(pair(romm, &who) && who.isNew && who.id == 1, "a new person pairs");
    check(accounts::login(1).token == "one-a" && accounts::login(1).tokenId == 11,
          "their token is kept with the number RomM's answer gave it");
    check(mode(accounts::tokenPath(1)) == 0600, "in a file only the console reads (0600)");
    check(!owedFile(), "and nothing is owed");
    check(sendOwed(romm.address()), "nothing to send is not a server that does not answer");
    same(romm.deleted(), "", "and nothing is asked of RomM");

    std::printf("Paired again, the person playing\n");
    romm::Client live;   // stands in for the app's own client
    live.setAddress(romm.address(), &err);
    live.loadToken(accounts::tokenPath(1));
    {
        romm::Client another;
        another.loadToken(accounts::tokenPath(1));
    }
    check(romm::tokenHeld("one-a"), "a token two clients had is in use while one still has it");
    std::thread slow([&] { live.fetchBytes("/slow"); });
    for (int i = 0; i < 200 && romm.slowHere == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(romm.slowHere == 1, "a request is on its way with the old token");
    romm.nextPairing("one-b", 12, 1);
    check(pair(romm, &who) && !who.isNew, "they pair again");
    check(accounts::login(1).token == "one-b" && accounts::login(1).tokenId == 12,
          "the new token and its number are the login now");
    check(owedFile() && mode(accounts::revokePath()) == 0600,
          "the old one is owed a revoke, in a file as private");
    sendOwed(romm.address());
    same(romm.deleted(), "", "not revoked while the app's client still has it");
    live.loadToken(accounts::tokenPath(1));
    check(romm::tokenHeld("one-a"), "the client has the new one, the request still the old");
    sendOwed(romm.address());
    same(romm.deleted(), "", "not revoked while a request is on its way with it");
    romm.letSlowGo = true;
    slow.join();
    check(!romm::tokenHeld("one-a") && romm::tokenHeld("one-b"),
          "the request over, only the new one is in use");
    check(sendOwed(romm.address()), "the server answers");
    same(romm.deleted(), "11 by one-a: 200", "the old token revokes itself, once, by its own number");
    check(!romm.accepts("one-a") && romm.accepts("one-b"), "RomM has exactly the new one");
    check(!owedFile(), "and nothing is owed");
    sendOwed(romm.address());
    same(romm.deleted(), "", "nor sent a second time");

    std::printf("Paired again, somebody else\n");
    romm.nextPairing("two-a", 21, 2);
    check(pair(romm, &who) && who.isNew && accounts::activeId() == 1, "a second person pairs");
    romm.nextPairing("two-b", 22, 2);
    check(pair(romm, &who) && !who.isNew, "and pairs again");
    sendOwed(romm.address());
    same(romm.deleted(), "21 by two-a: 200", "their old token goes at once: nothing here uses it");
    check(romm.accepts("one-b") && romm.accepts("two-b"), "both people's own are untouched");

    std::printf("A power cut\n");
    live.useToken("");
    owe(accounts::login(1), "person1");
    sendOwed(romm.address());
    same(romm.deleted(), "",
         "between the list and the login: somebody signs in with it, so it is not revoked");
    romm.nextPairing("one-c", 13, 1);
    check(pair(romm, &who), "they pair again after it");
    sendOwed(romm.address());
    same(romm.deleted(), "12 by one-b: 200", "then it goes, once");
    romm.nextPairing("one-d", 14, 1);
    check(pair(romm, &who) && owedFile(), "between the login and the revoke: it is still owed");
    sendOwed(romm.address());
    same(romm.deleted(), "13 by one-c: 200", "and the next start sends it");
    owe({"one-c", 13}, "person1");
    check(sendOwed(romm.address()), "after RomM revoked it, before the list said so");
    same(romm.deleted(), "13 by one-c: 401", "RomM no longer knows it");
    check(!owedFile(), "and it is settled");

    std::printf("A server that fails, is away, or refuses\n");
    romm.nextPairing("one-e", 15, 1);
    romm.behaveAs(StandIn::As::Failing);
    owe({"gone-a", 90}, "person1");
    check(!sendOwed(romm.address()) && owedFile(), "a 500 is not an answer: still owed");
    same(romm.deleted(), "90 by gone-a: 500", "asked once");
    check(!sendOwed("127.0.0.1:1") && owedFile(), "nothing there: still owed");
    romm.letSlowGo = false;
    romm.behaveAs(StandIn::As::Mute);
    auto t = Clock::now();
    const bool answered = sendOwed(romm.address());
    long took = msSince(t);
    check(!answered && owedFile() && took < kSendOwedSeconds * 1000 + 700,
          "one that never answers: still owed, and left after " + std::to_string(took) + " ms");
    romm.letSlowGo = true;
    romm.behaveAs(StandIn::As::Romm55);
    check(sendOwed(romm.address()) && !owedFile(), "and sent when it answers");
    same(romm.deleted(), "90 by gone-a: 401", "once");
    std::atomic<bool> exiting{true};
    owe({"gone-b", 91}, "person1");
    check(sendOwed(romm.address(), &exiting) && owedFile(), "told to stop, none is sent");
    same(romm.deleted(), "", "nothing");
    sendOwed(romm.address());
    romm.deleted();
    romm.issue("gone-c", 92, 1);
    owe({"gone-c", 93}, "person1");
    sendOwed(romm.address());
    same(romm.deleted(), "93 by gone-c: 403", "a number that is not its own is refused");
    check(romm.accepts("gone-c") && !owedFile(), "nothing is revoked, and it is not asked for ever");
    romm.behaveAs(StandIn::As::Romm54);
    romm.nextPairing("one-f", 16, 1, /*withNumber=*/false);
    check(pair(romm, &who) && accounts::login(1).token == "one-f" &&
              accounts::login(1).tokenId == 0,
          "a RomM before 5.5 gives no number: the pairing is kept without one");
    sendOwed(romm.address());
    same(romm.deleted(), "14 by one-d: 403", "and refuses the one before it");
    check(!owedFile(), "which stops nothing and is settled");
    romm.behaveAs(StandIn::As::Romm55);
    romm.nextPairing("one-g", 17, 1);
    check(pair(romm, &who) && !owedFile(), "a token with no number is owed nothing when replaced");
    sendOwed(romm.address());
    same(romm.deleted(), "", "and revokes nothing");

    std::printf("What cannot be written down stops nothing\n");
    storage::makeDirs(accounts::revokePath() + "/in-the-way");
    romm.nextPairing("one-h", 18, 1);
    check(pair(romm, &who) && accounts::login(1).token == "one-h", "a re-pairing goes on");
    check(accounts::remove(2, &err), "and so does a Remove");
    storage::removeEntry(accounts::revokePath());

    std::printf("Remove\n");
    romm.nextPairing("two-c", 23, 2);
    check(pair(romm, &who) && who.isNew, "the second person again");
    check(accounts::remove(2, &err) && !storage::exists(accounts::tokenPath(2)),
          "removed, their login with them");
    sendOwed(romm.address());
    same(romm.deleted(), "23 by two-c: 200", "their token revokes itself");
    check(romm.accepts("one-h"), "and the other person's stays");

    std::printf("Sign out\n");
    romm.nextPairing("two-d", 24, 2);
    pair(romm, &who);
    accounts::Account three;
    three.id = 3;
    three.name = "person3";
    accounts::add(three, "three-a", &err);   // paired before the console kept numbers
    romm.issue("three-a", 31, 3);
    romm.issue("one-old", 10, 1);
    owe({"one-old", 10}, "person1");
    owe(accounts::login(1), "person1");      // owed, and still the login
    {
        std::lock_guard<std::mutex> lk(romm.m);
        romm.loginsThere.clear();
    }
    server::signOut(&err);
    server::finishSignOut();
    same(romm.deleted(), "10 by one-old: 200, 18 by one-h: 200, 24 by two-d: 200",
         "what was owed, then each account's own, once each");
    check(romm.accepts("three-a"), "a token with no number is left to RomM");
    bool before = true;
    {
        std::lock_guard<std::mutex> lk(romm.m);
        for (const bool there : romm.loginsThere) before = before && there;
    }
    check(before, "while the logins were still there");
    check(!storage::exists(accounts::tokenPath(1)) && !storage::exists(accounts::tokenPath(2)) &&
              !owedFile() && accounts::all().empty(),
          "and then they are gone, what was owed with them");

    std::printf("Sign out, the server gone\n");
    auto signedIn = [&](const std::string& token, int number) {
        accounts::Account one;
        one.id = 1;
        one.name = "person1";
        accounts::add(one, token, &err, number);
        server::signOut(&err);
    };
    signedIn("one-i", 19);
    setenv("CABINETOS_ROMM", "127.0.0.1:1", 1);
    t = Clock::now();
    server::finishSignOut();
    took = msSince(t);
    check(took < 1000 && accounts::all().empty() && !storage::exists(accounts::tokenPath(1)),
          "nothing there: done in " + std::to_string(took) + " ms, the files gone");
    setenv("CABINETOS_ROMM", romm.address().c_str(), 1);
    signedIn("one-j", 20);
    romm.letSlowGo = false;
    romm.behaveAs(StandIn::As::Mute);
    t = Clock::now();
    server::finishSignOut();
    took = msSince(t);
    check(took < kSignOutSeconds * 1000 + 700 && accounts::all().empty() &&
              !storage::exists(accounts::tokenPath(1)),
          "one that takes the connection and never answers: done in " + std::to_string(took) +
              " ms, the files gone");
    romm.letSlowGo = true;
    romm.behaveAs(StandIn::As::Failing);
    signedIn("one-k", 25);
    accounts::Account two;
    two.id = 2;
    two.name = "person2";
    accounts::add(two, "two-k", &err, 26);
    server::finishSignOut();
    same(romm.deleted(), "25 by one-k: 500", "one that fails is not asked about the rest");
    check(accounts::all().empty(), "and the files go");

    romm.close();
    if (std::system(("rm -rf " + r).c_str()) != 0) std::printf("\n  (could not remove %s)\n", root);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

}  // namespace tokenrevoke
