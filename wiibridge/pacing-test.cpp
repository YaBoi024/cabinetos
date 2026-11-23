// SPDX-License-Identifier: GPL-2.0-or-later
//
// cabinetos-wii-bridge's pacing test (#480), with no Remote: two Remotes on
// socket pairs, served by the bridge's own loop (pass).
//   1. Remote 2 comes back with 40 setup commands while Remote 1's reports
//      flow. The 40 reach Remote 2 in order, never closer than 15 ms, then
//      its report mode and light; Remote 1's reports are never kept waiting.
//   2. The game reads Remote 1's Miis, then sends it its light, while Remote
//      2's reports flow. The 94 answers go out in order, never closer than
//      1 ms; Remote 1's reports and its light wait until the last is out;
//      Remote 2's are never kept waiting.
//   3. Linux's Wii Remote driver (#548): Remote 1 becomes one of Nintendo's
//      own. After its status report the game's report mode goes back to it,
//      no sooner than 10 ms, with the game's rumble and no acknowledgement
//      asked; after another answer while the rumble is on, the rumble alone.
//      Remote 2, a TechKen, sends a status report too and is sent nothing.
// The times are the kernel's, taken as the bridge writes (SO_TIMESTAMPNS).
// `make test` builds and runs it, in the builder; it exits 0 when all holds.
// It includes bridge.cpp, whose loop is in an unnamed namespace.

#include <sys/socket.h>

#include <atomic>
#include <ctime>
#include <thread>

#define main bridgeMain
#include "bridge.cpp"
#undef main

namespace {

using std::chrono::milliseconds;

constexpr int64_t kMs = 1000000;   // in ns
// The longest a report of the Remote not being paced may wait: well under
// one 15 ms gap, so a loop that slept for one fails.
constexpr int64_t kNoWait = 10 * kMs;
// The kernel's stamps are wall-clock time, which may be slewed by 500 ppm.
constexpr int64_t kSlew = 10000;

int64_t wallNs() {
    timespec t{};
    ::clock_gettime(CLOCK_REALTIME, &t);
    return int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}

struct Pair {
    int bridge, test;
};

Pair socketPair() {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) != 0) {
        std::perror("socketpair");
        std::exit(2);
    }
    ::fcntl(sv[0], F_SETFL, O_NONBLOCK);   // as the bridge opens hidraw and uhid
    const int on = 1, big = 1 << 22;
    ::setsockopt(sv[1], SOL_SOCKET, SO_TIMESTAMPNS, &on, sizeof on);
    for (int fd : sv) ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &big, sizeof big);
    return {sv[0], sv[1]};
}

// The test's ends, in this order: what the bridge sent each game (uhid), and
// each Remote.
enum End { kGame1, kGame2, kRemote1, kRemote2 };

struct Got {
    End end;
    std::vector<uint8_t> report;
    int64_t at;   // when the bridge wrote it
};

// Everything the bridge writes to the test's ends, with the kernel's stamp.
void collect(const std::array<int, 4>& ends, const std::atomic<bool>& stop, std::vector<Got>& got) {
    std::vector<pollfd> fds;
    for (int fd : ends) fds.push_back({fd, POLLIN, 0});
    while (!stop) {
        if (::poll(fds.data(), fds.size(), 20) <= 0) continue;
        for (size_t i = 0; i < fds.size(); ++i) {
            if (!(fds[i].revents & POLLIN)) continue;
            uhid_event ev{};   // the largest message either way
            char control[CMSG_SPACE(sizeof(timespec))];
            iovec iov{&ev, sizeof ev};
            msghdr msg{};
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = control;
            msg.msg_controllen = sizeof control;
            const ssize_t n = ::recvmsg(fds[i].fd, &msg, 0);
            if (n <= 0) continue;
            int64_t at = 0;
            for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
                if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_TIMESTAMPNS) continue;
                timespec t{};
                std::memcpy(&t, CMSG_DATA(c), sizeof t);
                at = int64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
            }
            const End end = static_cast<End>(i);
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&ev);
            std::vector<uint8_t> report(p, p + n);
            if (end == kGame1 || end == kGame2)
                report.assign(ev.u.input2.data, ev.u.input2.data + ev.u.input2.size);
            got.push_back({end, std::move(report), at});
        }
    }
}

// A Remote's reports, one every 2 ms: 0x30 (buttons) carrying its count, and
// when each was sent.
void feed(int end, const std::atomic<bool>& stop, std::vector<int64_t>& sent) {
    while (!stop) {
        const size_t seq = sent.size();
        const uint8_t rep[3] = {0x30, static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq)};
        sent.push_back(wallNs());
        (void)::send(end, rep, sizeof rep, 0);
        std::this_thread::sleep_for(milliseconds(2));
    }
}

// A report from the game to its stand-in, as uhid hands it to the bridge.
void fromTheGame(int end, std::vector<uint8_t> report) {
    uhid_event ev{};
    ev.type = UHID_OUTPUT;
    ev.u.output.size = static_cast<__u16>(report.size());
    ev.u.output.rtype = UHID_OUTPUT_REPORT;
    std::memcpy(ev.u.output.data, report.data(), report.size());
    (void)::send(end, &ev, sizeof ev, 0);
}

bool ok = true;

void check(bool good, const char* what) {
    if (good) return;
    std::printf("FAIL: %s\n", what);
    ok = false;
}

std::vector<const Got*> sentTo(const std::vector<Got>& got, End end, uint8_t id) {
    std::vector<const Got*> out;
    for (const Got& g : got)
        if (g.end == end && !g.report.empty() && g.report[0] == id) out.push_back(&g);
    return out;
}

// A Remote's reports as the game got them: all of them, in order, and the
// longest any sent between `from` and `to` waited.
int64_t longestWait(const std::vector<Got>& got, End end, const std::vector<int64_t>& sent, int64_t from,
                    int64_t to, int* counted) {
    const auto reports = sentTo(got, end, 0x30);
    check(reports.size() == sent.size(), "a Remote's report was lost");
    int64_t longest = 0;
    *counted = 0;
    for (size_t i = 0; i < reports.size(); ++i) {
        const size_t seq = (size_t(reports[i]->report[1]) << 8) | reports[i]->report[2];
        check(seq == i, "a Remote's reports reached the game out of order");
        if (seq >= sent.size() || sent[seq] < from || sent[seq] > to) continue;
        longest = std::max(longest, reports[i]->at - sent[seq]);
        ++*counted;
    }
    return longest;
}

}  // namespace

int main() {
    ::signal(SIGPIPE, SIG_IGN);
    const Pair real1 = socketPair(), uhid1 = socketPair(), real2 = socketPair(), uhid2 = socketPair();
    auto one = std::make_unique<Remote>();
    one->uniq = "remote-1";
    one->node = "null";   // the loop checks that /dev/<node> is there
    one->real = real1.bridge;
    one->uhid = uhid1.bridge;
    auto two = std::make_unique<Remote>();
    two->uniq = "remote-2";
    two->uhid = uhid2.bridge;   // off: no real node yet
    for (int i = 0; i < 40; ++i) {   // camera register writes, each naming its place
        std::vector<uint8_t> c(22, 0);
        c[0] = 0x16;
        c[1] = 0x04;
        c[2] = 0xb0;
        c[4] = static_cast<uint8_t>(i);
        c[5] = 1;
        two->setup.push_back(c);
    }
    two->lastMode = {0x12, 0x04, 0x37};
    two->lastLed = {0x11, 0x20};
    Remote& r2 = *two;
    gRemotes.push_back(std::move(one));
    gRemotes.push_back(std::move(two));

    std::atomic<bool> stopFeeding{false}, stopCollecting{false};
    std::vector<int64_t> sent1, sent2;
    std::vector<Got> got;
    std::thread collector(collect, std::array<int, 4>{uhid1.test, uhid2.test, real1.test, real2.test},
                          std::cref(stopCollecting), std::ref(got));
    std::thread feeder1(feed, real1.test, std::cref(stopFeeding), std::ref(sent1));
    long passes = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto run = [&passes](int ms) {
        const auto end = std::chrono::steady_clock::now() + milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            pass();
            ++passes;
        }
    };

    run(100);
    const int64_t back = wallNs();
    reattach(r2, "null", real2.bridge);   // what rescan does when it finds it again
    run(900);
    std::thread feeder2(feed, real2.test, std::cref(stopFeeding), std::ref(sent2));
    run(100);
    const int64_t miiAsked = wallNs();
    fromTheGame(uhid1.test, {0x17, 0x00, 0x00, 0x0f, 0xca, 0x05, 0xe0});   // the Mii area, 1,504 bytes
    fromTheGame(uhid1.test, {0x11, 0x10});                                  // then its light
    run(400);
    stopFeeding = true;
    feeder1.join();
    feeder2.join();
    run(100);
    Remote& r1 = *gRemotes[0];
    r1.linuxDriver = true;
    fromTheGame(uhid1.test, {0x12, 0x06, 0x37});   // continuous, acknowledgement asked
    fromTheGame(uhid1.test, {0x10, 0x01});         // rumble on
    run(50);
    const uint8_t status[7] = {0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0};
    const int64_t status1 = wallNs();
    (void)::send(real1.test, status, sizeof status, 0);
    const int64_t status2 = wallNs();
    (void)::send(real2.test, status, sizeof status, 0);
    run(100);
    const uint8_t driversAck[5] = {0x22, 0x00, 0x00, 0x16, 0x07};   // its MotionPlus probe
    const int64_t answered = wallNs();
    (void)::send(real1.test, driversAck, sizeof driversAck, 0);
    run(100);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    stopCollecting = true;
    collector.join();

    // 1. The replay.
    std::vector<const Got*> toRemote2;
    for (const Got& g : got)
        if (g.end == kRemote2) toRemote2.push_back(&g);
    check(toRemote2.size() == 42, "Remote 2 did not get its 40 setup commands, mode and light");
    int64_t closest = INT64_MAX, widest = 0;
    for (size_t i = 0; i < toRemote2.size() && i < 42; ++i) {
        const Got& g = *toRemote2[i];
        check(g.at != 0, "the kernel stamped no time");
        if (i < 40) check(g.report.size() == 22 && g.report[4] == static_cast<uint8_t>(i), "a setup command out of order");
        if (i == 40) check(g.report == r2.lastMode, "the report mode is not next");
        if (i == 41) check(g.report == r2.lastLed, "the light is not last");
        if (i == 0 || i > 40) continue;
        const int64_t gap = g.at - toRemote2[i - 1]->at;
        closest = std::min(closest, gap);
        widest = std::max(widest, gap);
    }
    check(closest >= 15 * kMs - kSlew, "two of Remote 2's commands closer than 15 ms");
    const int64_t replayed = toRemote2.empty() ? back : toRemote2.back()->at;
    int counted1 = 0;
    const int64_t wait1 = longestWait(got, kGame1, sent1, back, replayed, &counted1);
    check(counted1 > 100, "Remote 1 sent too few reports during the replay to tell");
    check(wait1 < kNoWait, "Remote 1's reports waited for Remote 2's replay");
    std::printf("replay: %zu command(s) to Remote 2 in %.1f ms, %.3f to %.3f ms apart; "
                "Remote 1 meanwhile: %d report(s), the longest wait %.3f ms\n",
                toRemote2.size(), double(replayed - back) / kMs, double(closest) / kMs,
                double(widest) / kMs, counted1, double(wait1) / kMs);

    // 2. The Mii answers.
    const auto answers = sentTo(got, kGame1, 0x21);
    check(answers.size() == 94, "not 94 Mii answers");
    closest = INT64_MAX, widest = 0;
    for (size_t i = 0; i < answers.size(); ++i) {
        const unsigned at = (unsigned(answers[i]->report[4]) << 8) | answers[i]->report[5];
        check(at == 0x0fca + 16 * i && answers[i]->report[3] >> 4 == 15, "a Mii answer out of order");
        if (i == 0) continue;
        const int64_t gap = answers[i]->at - answers[i - 1]->at;
        closest = std::min(closest, gap);
        widest = std::max(widest, gap);
    }
    check(closest >= kMs - kSlew / 10, "two Mii answers closer than 1 ms");
    const int64_t firstAnswer = answers.empty() ? miiAsked : answers.front()->at;
    const int64_t lastAnswer = answers.empty() ? miiAsked : answers.back()->at;
    for (const Got* g : sentTo(got, kGame1, 0x30))
        check(g->at < firstAnswer || g->at > lastAnswer, "a report of Remote 1 came between its Mii answers");
    const auto lights = sentTo(got, kRemote1, 0x11);
    check(lights.size() == 1 && lights[0]->at > lastAnswer, "Remote 1's light did not wait for the answers");
    int counted2 = 0;
    const int64_t wait2 = longestWait(got, kGame2, sent2, miiAsked, lastAnswer, &counted2);
    check(counted2 > 20, "Remote 2 sent too few reports during the answers to tell");
    check(wait2 < kNoWait, "Remote 2's reports waited for Remote 1's Mii answers");
    std::printf("miis: %zu answer(s) in %.1f ms, %.3f to %.3f ms apart; "
                "Remote 2 meanwhile: %d report(s), the longest wait %.3f ms\n",
                answers.size(), double(lastAnswer - firstAnswer) / kMs, double(closest) / kMs,
                double(widest) / kMs, counted2, double(wait2) / kMs);

    // 3. Linux's driver.
    std::vector<const Got*> modes, rumbles;
    for (const Got& g : got) {
        if (g.end != kRemote1 || g.at <= status1) continue;
        if (g.report.size() >= 3 && g.report[0] == 0x12 && g.at < answered) modes.push_back(&g);
        if (g.report.size() >= 2 && g.report[0] == 0x10 && g.at > answered) rumbles.push_back(&g);
    }
    check(modes.size() == 1, "Remote 1 did not get the game's report mode back once after its status");
    if (!modes.empty()) {
        const Got& m = *modes.front();
        check(m.report[1] == 0x05 && m.report[2] == 0x37,
              "the mode put back is not the game's, with its rumble and no acknowledgement asked");
        check(m.at - status1 >= 10 * kMs - kSlew, "the mode went back sooner than 10 ms after the status");
    }
    check(rumbles.size() == 1 && rumbles[0]->report[1] == 0x01 && rumbles[0]->at - answered >= 10 * kMs - kSlew,
          "the rumble did not go back after the driver's answer");
    int toTechKen = 0;
    for (const Got& g : got)
        if (g.end == kRemote2 && g.at > status2) ++toTechKen;
    check(toTechKen == 0, "the TechKen was sent something after its status report");
    std::printf("driver: mode back %.1f ms after the status, rumble back %.1f ms after an answer; "
                "the TechKen sent %d\n",
                modes.empty() ? -1.0 : double(modes.front()->at - status1) / kMs,
                rumbles.empty() ? -1.0 : double(rumbles.front()->at - answered) / kMs, toTechKen);

    // A loop that waits on nothing would pass millions of times a second.
    check(passes < seconds * 5000, "the loop spins");
    std::printf("loop: %ld pass(es) in %.2f s\n%s\n", passes, seconds, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
