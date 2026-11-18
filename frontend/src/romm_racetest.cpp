// `--romm-race-test <address> <seconds>`: the RomM client's thread rule
// under load (#398, batch i). One client, as the app's own is, called by
// eight readers at once, each making every kind of request the app's workers
// make (a page of games, a game, a cover, a download to a file, a range
// read, a save upload, a state deletion, a favourite with its own POST, a
// game's save channels and a snapshot pushed with a state, #346) and
// reading the address and the token back, while this thread, standing in for
// the frame thread, writes the client the three ways the app does: the
// address (setAddress, under two names and with a dead port between, as a
// reconnect may meet), the token read back from a file (an account switch)
// and the token a pairing hands back.
//
// AGAINST A STAND-IN, NEVER RomM. The stand-in at `address` (the
// restructure's suite's stand-in, now tools/racemock.py) answers these requests from
// memory: the heartbeat, with a save channels block; any /api/roms page with
// one game, id 1, `race.sfc` on platform 1, and that game by id; its cover;
// its content, whole or by a range; a save upload; a state deletion;
// /api/collections, empty and with a favourites collection (id 7, user 1)
// in turn, the POST that makes one and its roms; /api/users/me (id 1); a
// pairing's token; /api/channels for file 1, one channel; and a snapshot
// push, made (201) only when its manifest names file 1 and it carries the
// save and the state the test sends. It refuses a token or a host it did not
// hand out. A request that could not reach it at all is counted apart (the
// stand-in going away is part of the test); any other failure is a request
// that went out wrong, and so is an address or a token read back that is
// neither of the two the writer sets. Returns 0 when nothing failed, 3 when
// only the stand-in's absence did, 1 when anything went out wrong, 2 when
// the stand-in did not answer to begin with.

#include "romm.h"

#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace romm {

namespace {

// Could not reach the stand-in, or lost it in the middle of an answer: its
// absence, not a request that went out wrong.
bool unreachable(const std::string& e) {
    for (const CURLcode c : {CURLE_COULDNT_CONNECT, CURLE_COULDNT_RESOLVE_HOST,
                             CURLE_OPERATION_TIMEDOUT, CURLE_RECV_ERROR, CURLE_SEND_ERROR,
                             CURLE_GOT_NOTHING, CURLE_PARTIAL_FILE})
        if (e == curl_easy_strerror(c)) return true;
    return false;
}

bool writeFile(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

// One line of the report: what came back in five seconds.
struct Tally {
    std::atomic<long> ok{0};
    std::atomic<long> away{0};    // unreachable, or empty where no reason is given
    std::atomic<long> wrong{0};
};

}  // namespace

int raceTest(const std::string& address, int seconds) {
    const size_t colon = address.rfind(':');
    if (colon == std::string::npos || seconds <= 0) {
        std::fprintf(stderr, "[race] usage: --romm-race-test <host:port> <seconds>\n");
        return 2;
    }
    const std::string port = address.substr(colon + 1);
    // The stand-in under two names, so the address really changes, and a
    // port nothing listens on, which a probe must try and give back.
    const std::string names[2] = {"127.0.0.1:" + port, "localhost:" + port};
    const std::string bases[2] = {"http://" + names[0], "http://" + names[1]};
    const std::string dead = "127.0.0.1:9";
    const std::string tokenText[2] = {"race-a", "race-token-b-longer"};
    const std::string dir = "/tmp/cabinetos-race-" + std::to_string(getpid());
    ::mkdir(dir.c_str(), S_IRWXU);
    const std::string tokens[2] = {dir + "/a.json", dir + "/b.json"};
    if (!writeFile(tokens[0], "{\"access_token\":\"" + tokenText[0] + "\"}") ||
        !writeFile(tokens[1], "{\"access_token\":\"" + tokenText[1] + "\"}")) {
        std::fprintf(stderr, "[race] cannot write %s\n", dir.c_str());
        return 2;
    }

    Client client;
    std::string err;
    if (!client.setAddress(names[0], &err) || !client.loadToken(tokens[0])) {
        std::fprintf(stderr, "[race] the stand-in at %s did not answer: %s\n", names[0].c_str(),
                     err.c_str());
        return 2;
    }
    std::vector<Game> games;
    if (!client.fetchRoms("platform_ids=1", 1, &games, &err) || games.empty()) {
        std::fprintf(stderr, "[race] the stand-in sent no game: %s\n", err.c_str());
        return 2;
    }
    const Game game = games[0];
    const std::string content = "/api/roms/" + std::to_string(game.id) + "/content/" + game.fsName;
    // A push into the game's channel, with a save and a state's bytes.
    SnapshotPush push;
    push.romFileId = 1;
    push.channelId = "00000000-0000-7000-8000-000000000001";
    push.expectedCurrentId = 1;
    push.emulator = "race";
    push.core = "race";
    push.hasSave = true;
    push.save.fileName = "race.srm";
    push.save.bytes.assign(64, 0x5a);
    push.save.hash = contentHash(push.save.bytes);
    PushState state;
    state.core = "race";
    state.slot = "1";
    state.file.fileName = "race.state1";
    state.file.bytes.assign(128, 0xa5);
    state.file.hash = contentHash(state.file.bytes);
    push.states.push_back(state);

    constexpr int kReaders = 8;
    constexpr int kWindow = 5;
    const int windows = (seconds + kWindow - 1) / kWindow;
    std::vector<Tally> tally(static_cast<size_t>(windows));
    std::mutex seenMutex;
    std::vector<std::string> seen;   // the first few that went out wrong
    const auto start = std::chrono::steady_clock::now();
    const auto until = start + std::chrono::seconds(seconds);
    auto note = [&](int kind, const std::string& what) {
        const long s = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - start).count();
        Tally& t = tally[static_cast<size_t>(std::min<long>(s / kWindow, windows - 1))];
        if (kind == 0) { ++t.ok; return; }
        if (kind == 1) { ++t.away; return; }
        ++t.wrong;
        std::lock_guard<std::mutex> lock(seenMutex);
        if (seen.size() < 8) seen.push_back(what);
    };
    auto result = [&](bool good, const std::string& e, const char* what) {
        note(good ? 0 : unreachable(e) ? 1 : 2, std::string(what) + ": " + e);
    };

    std::vector<std::thread> readers;
    for (int r = 0; r < kReaders; ++r) {
        readers.emplace_back([&, r]() {
            const std::string file = dir + "/download-" + std::to_string(r);
            for (int n = r; std::chrono::steady_clock::now() < until; ++n) {
                std::string e;
                switch (n % 10) {
                case 0: {
                    std::vector<Game> page;
                    result(client.fetchRoms("platform_ids=1", 1, &page, &e), e, "a page of games");
                    break;
                }
                case 1: {
                    Game g;
                    result(client.fetchGame(game.id, &g, &e), e, "a game");
                    break;
                }
                case 2:   // empty on any failure; it says why itself, as [covers]
                    note(client.fetchBytes(game.coverPath).empty() ? 1 : 0, "");
                    break;
                case 3:
                    result(client.fetchToFile(content, file, {}, &e), e, "a download");
                    break;
                case 4:   // empty on any failure, and silent
                    note(client.fetchHead(game, 64).size() == 64 ? 0 : 1, "");
                    break;
                case 5:
                    result(client.uploadSave(game.id, "race", "race.srm",
                                             std::vector<uint8_t>(64, 0x5a), &e),
                           e, "a save upload");
                    break;
                case 6:
                    result(client.deleteStates({1}, &e), e, "a state deletion");
                    break;
                case 7: {
                    std::vector<Channel> channels;
                    const bool got = client.fetchChannels({1}, &channels, &e);
                    if (got && channels.size() != 1) e = "the channels came back short";
                    result(got && channels.size() == 1, e, "a game's channels");
                    break;
                }
                case 8: {
                    PushAnswer answer;
                    const bool sent = client.pushSnapshot(push, &answer, &e);
                    if (sent && answer.kind != PushAnswer::Kind::Made)
                        e = "answered " + std::to_string(answer.status) + ": " + answer.detail;
                    result(sent && answer.kind == PushAnswer::Kind::Made, e, "a snapshot push");
                    break;
                }
                default: {
                    bool forbidden = false;
                    result(client.setFavorite(game.id, n % 16 < 8, &forbidden, &e), e,
                           "a favourite");
                    break;
                }
                }
                // Read back as the game's page and the probes read them: only
                // ever one of the two the writer sets.
                const std::string base = client.baseUrl();
                if (base != bases[0] && base != bases[1]) note(2, "the address read as " + base);
                const std::string token = client.token();
                if (token != tokenText[0] && token != tokenText[1])
                    note(2, "a token read back that the writer never set");
                if (!client.haveToken()) note(2, "no token");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // The writer, as the frame thread: the server back under either name, a
    // dead port in between, an account switch and a pairing's answer.
    long addresses = 0, deadRefused = 0, tokensSet = 0, writerAway = 0;
    Pairing pairing;
    pairing.deviceCode = "race";
    for (int n = 0; std::chrono::steady_clock::now() < until; ++n) {
        std::string e;
        switch (n % 6) {
        case 0:
        case 3:
            if (client.setAddress(names[n % 6 == 0 ? 0 : 1], &e)) ++addresses;
            else ++writerAway;
            break;
        case 1:
        case 4:
            if (client.loadToken(tokens[n % 6 == 1 ? 1 : 0])) ++tokensSet;
            else note(2, "a token file did not load");
            break;
        case 2:
            if (client.setAddress(dead, &e)) note(2, "a dead port answered");
            else ++deadRefused;
            break;
        default:
            if (client.pollPairing(pairing, &e) == 1) ++tokensSet;
            else ++writerAway;
            break;
        }
    }
    for (std::thread& t : readers) t.join();
    // The stand-in's heartbeat carries a save channels block, which every
    // probe the writer made heard.
    if (!heardServerChannels()) note(2, "the heartbeat's save channels went unheard");

    long ok = 0, away = 0, wrong = 0;
    for (int w = 0; w < windows; ++w) {
        const Tally& t = tally[static_cast<size_t>(w)];
        std::printf("[race] %d-%d s: %ld ok, %ld away, %ld wrong\n", w * kWindow,
                    std::min((w + 1) * kWindow, seconds), t.ok.load(), t.away.load(),
                    t.wrong.load());
        ok += t.ok;
        away += t.away;
        wrong += t.wrong;
    }
    std::printf("[race] %ld requests: %ld ok, %ld away, %ld wrong; the writer: %ld addresses, "
                "%ld dead ports given back, %ld tokens, %ld not answered\n",
                ok + away + wrong, ok, away, wrong, addresses, deadRefused, tokensSet, writerAway);
    for (const std::string& s : seen) std::printf("[race] wrong: %s\n", s.c_str());
    const std::string base = client.baseUrl();
    std::printf("[race] at the end: %s\n",
                base == bases[0] || base == bases[1] ? "one of the two addresses" : "ANOTHER ADDRESS");

    for (int r = 0; r < kReaders; ++r) {
        ::unlink((dir + "/download-" + std::to_string(r)).c_str());
        ::unlink((dir + "/download-" + std::to_string(r) + ".part").c_str());
    }
    ::unlink(tokens[0].c_str());
    ::unlink(tokens[1].c_str());
    ::rmdir(dir.c_str());
    if (wrong > 0 || (base != bases[0] && base != bases[1])) return 1;
    return away > 0 ? 3 : 0;
}

}  // namespace romm
