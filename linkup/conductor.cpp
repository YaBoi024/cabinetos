// What the program does (conductor.h).

#include "conductor.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <openssl/rand.h>

#include "hexutil.h"
#include "jsonutil.h"
#include "linkup_wire.h"
#include "pairing.h"

namespace {

using jsonutil::flag;
using jsonutil::get;
using jsonutil::num;
using jsonutil::set;
using jsonutil::str;

// What goes on the fast channel beside a pad's state and a rumble.
constexpr uint8_t kTagBeat = 'B';
constexpr uint8_t kTagBeatBack = 'b';
constexpr uint8_t kTagFullPicture = 'K';

// The heartbeat, and how long with nothing heard counts as a drop. Under this shows nothing: the
// friend's last input is held and lost pieces are resent (#654, "A drop"). The figures are tuned
// on the testers' connections.
constexpr int64_t kBeatMs = 250;
constexpr int64_t kDropMs = 2000;
constexpr int64_t kReconnectAfterMs = 8000;
constexpr int64_t kReconnectEveryMs = 15000;
constexpr int64_t kConnectLimitMs = 30000;
constexpr int64_t kBoxLifeSeconds = 300;

json_object* eventNamed(const char* name) {
    json_object* o = json_object_new_object();
    set(o, "ev", name);
    return o;
}

json_object* requestNamed(const char* op) {
    json_object* o = json_object_new_object();
    set(o, "op", op);
    return o;
}

std::string randomId() {
    uint8_t bytes[16];
    RAND_bytes(bytes, sizeof bytes);
    return hexutil::encode(bytes, sizeof bytes);
}

// A person's stable id on this console. RomM has no id for a whole server, and a user's small number
// is not enough, because user 1 on another server is somebody else: the date RomM made the user tells
// them apart, and stays the same when the server's address changes.
std::string accountOf(int64_t userId, const std::string& created) {
    const noise::Key sum = noise::sha256("cabinet-linkup account 1|" + std::to_string(userId) + "|" + created);
    return hexutil::encode(sum.data(), sum.size());
}

// The first STUN server that answers, in the order the owner set (PROTOCOL.md, `config`). webrtcbin
// takes one. A binding request with half a second for each: this is a worker's, never the main loop's.
std::string firstStunThatAnswers(const std::vector<std::string>& list) {
    for (const std::string& entry : list) {
        const size_t colon = entry.rfind(':');
        if (colon == std::string::npos) continue;
        std::string host = entry.substr(0, colon);
        if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
        addrinfo hints{};
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), entry.substr(colon + 1).c_str(), &hints, &res) != 0) continue;
        bool answered = false;
        for (addrinfo* a = res; a && !answered; a = a->ai_next) {
            const int fd = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, 0);
            if (fd < 0) continue;
            uint8_t request[20] = {0, 1, 0, 0, 0x21, 0x12, 0xA4, 0x42};
            RAND_bytes(request + 8, 12);
            if (connect(fd, a->ai_addr, a->ai_addrlen) == 0 && send(fd, request, sizeof request, 0) == sizeof request) {
                pollfd p{fd, POLLIN, 0};
                if (poll(&p, 1, 500) == 1) {
                    uint8_t reply[256];
                    const ssize_t n = recv(fd, reply, sizeof reply, 0);
                    answered = n >= 20 && reply[0] == 1 && reply[1] == 1 && std::memcmp(reply + 8, request + 8, 12) == 0;
                }
            }
            close(fd);
        }
        freeaddrinfo(res);
        if (answered) return entry;
    }
    return {};
}

// `turn:host:port?transport=udp` and its login, as webrtcbin takes a relay.
std::string turnUri(const std::string& url, const std::string& user, const std::string& password) {
    if (url.rfind("turn:", 0) != 0) return {};
    std::string hostPort = url.substr(5);
    const size_t q = hostPort.find('?');
    if (q != std::string::npos) hostPort = hostPort.substr(0, q);
    gchar* u = g_uri_escape_string(user.c_str(), nullptr, FALSE);
    gchar* p = g_uri_escape_string(password.c_str(), nullptr, FALSE);
    std::string out = "turn://" + std::string(u) + ":" + p + "@" + hostPort;
    g_free(u);
    g_free(p);
    return out;
}

}  // namespace

Conductor::Conductor(Keys& keys, LocalSocket& local) : keys_(keys), local_(local), link_(keys) {}

Conductor::~Conductor() { finish(); }

void Conductor::finish() {
    stopping_ = true;
    workCv_.notify_all();
    if (tickSource_) {
        g_source_remove(tickSource_);
        tickSource_ = 0;
    }
    if (worker_.joinable()) worker_.join();
    if (waiter_.joinable()) waiter_.join();
}

void Conductor::start() {
    paired_ = keys_.server().has_value();
    worker_ = std::thread([this]() { workerLoop(); });
    waiter_ = std::thread([this]() { waitLoop(); });
    tickSource_ = g_timeout_add(50, onTick, this);
    // The last run of this program died in a game. The game is gone; the server has to hear.
    const std::string left = keys_.openSession();
    if (!left.empty() && paired_.load()) {
        json_object* q = requestNamed("end");
        set(q, "session", left);
        ask(q, nullptr);
    }
    keys_.setOpenSession("");
}

void Conductor::shutDown() {
    stopping_ = true;
    if (game_) {
        if (Rtc* r = rtc()) r->sendControl("bye");
        if (host_) host_->stop();
        if (friend_) friend_->stop();
        portmap_.stop();
    }
}

// ---- The main program ----------------------------------------------------

void Conductor::onClient(bool connected) {
    std::fprintf(stderr, connected ? "[linkup] the main program is here\n" : "[linkup] the main program has gone\n");
    // Nobody at the console can answer a knock: it is restarting, it crashed, or it is in Steam, where
    // nothing of it runs. Back again, it signs in with its own status. A session does not outlive it:
    // the game and the screens it was for have gone with it.
    if (!connected && game_) {
        if (Rtc* r = rtc()) r->sendControl("bye");
        cmdEnd();
    }
    if (!connected && signedIn_) {
        status_ = "offline";
        json_object* q = requestNamed("status");
        set(q, "status", status_);
        ask(q, nullptr);
    }
}

void Conductor::onMessage(const uint8_t* data, size_t len) {
    if (len == 0) return;
    if (data[0] == linkupwire::kTagPad) {
        if (Rtc* r = rtc()) r->sendFast(data, len);
        return;
    }
    if (data[0] == linkupwire::kTagRumble) {
        if (Rtc* r = rtc()) r->sendFast(data, len);
        return;
    }
    json_object* o = jsonutil::parse(std::string(reinterpret_cast<const char*>(data), len));
    if (!o) return;
    onCommand(o);
    json_object_put(o);
}

// Each is the main program asking for one thing; a press never waits on any of them.
void Conductor::onCommand(json_object* o) {
    const std::string cmd = str(o, "cmd");
    if (cmd == "hello") {
        sendState();
        if (signedIn_) sendFriends();
    } else if (cmd == "pair") {
        cmdPair(o);
    } else if (cmd == "retype") {
        cmdRetype(o);
    } else if (cmd == "leave") {
        cmdLeave();
    } else if (cmd == "signin") {
        cmdSignin(o);
    } else if (cmd == "signout") {
        signedIn_ = false;
        friends_.clear();
        ask(requestNamed("signout"), nullptr);
    } else if (cmd == "status") {
        status_ = str(o, "status");
        json_object* q = requestNamed("status");
        set(q, "status", status_);
        ask(q, nullptr);
    } else if (cmd == "friends") {
        fetchFriends();
    } else if (cmd == "away") {
        away_ = flag(o, "on");
    } else if (cmd == "invite") {
        cmdInvite(o);
    } else if (cmd == "cancel") {
        if (game_ && game_->host && game_->sessionId.empty()) {
            json_object* q = requestNamed("cancel");
            set(q, "invite", game_->inviteId);
            ask(q, nullptr);
            game_.reset();
        }
    } else if (cmd == "decline") {
        json_object* q = requestNamed("decline");
        set(q, "invite", str(o, "invite"));
        ask(q, nullptr);
    } else if (cmd == "join") {
        cmdJoin(o);
    } else if (cmd == "picture") {
        cmdPicture(flag(o, "on"));
    } else if (cmd == "peer") {
        if (Rtc* r = rtc()) r->sendControl("p:" + str(o, "text"));
    } else if (cmd == "end") {
        cmdEnd();
    }
}

void Conductor::cmdPair(json_object* o) {
    const std::string address = str(o, "address");
    const std::string code = str(o, "code");
    json_object* j = json_object_new_object();
    set(j, "kind", "console");
    set(j, "protocol", ServerLink::kProtocol);
    set(j, "oldest", ServerLink::kOldest);
    set(j, "stream", ServerLink::kStream);
    set(j, "account", accountOf(num(o, "id"), str(o, "created")));
    set(j, "username", str(o, "username"));
    const std::string joining = jsonutil::text(j);
    json_object_put(j);
    work([this, address, code, joining]() {
        std::string name;
        const pairing::Result result = pairing::pair(keys_, address, code, joining, name);
        Rtc::onMain([this, result, name]() {
            const char* word = "failed";
            switch (result) {
                case pairing::Result::Paired: word = "paired"; break;
                case pairing::Result::WrongCode: word = "wrong_code"; break;
                case pairing::Result::TooManyTries: word = "too_many_tries"; break;
                case pairing::Result::Unreachable: word = "unreachable"; break;
                case pairing::Result::NotThatServer: word = "not_that_server"; break;
                case pairing::Result::Refused: word = "refused"; break;
                case pairing::Result::Failed: word = "failed"; break;
            }
            std::fprintf(stderr, "[linkup] pairing: %s\n", word);
            json_object* ev = eventNamed("pair_result");
            set(ev, "result", word);
            local_.sendJson(ev);
            if (result == pairing::Result::Paired) {
                link_.reset();
                paired_ = true;
                connection_ = "ok";
                ++waitGeneration_;
                sendState();
            }
        });
    });
}

// The key is what is checked, so the same server at a new address needs no new pairing, and an
// address where nothing answers yet is kept. Another server's address is not: the main program asks
// for that server's code, and pairing with it forgets this one. Until then nothing here changes.
void Conductor::cmdRetype(json_object* o) {
    const std::string address = str(o, "address");
    work([this, address]() {
        if (link_.whatAnswers(address) == ServerLink::Place::Another) {
            Rtc::onMain([this, address]() {
                std::fprintf(stderr, "[linkup] another server at that address\n");
                json_object* ev = eventNamed("another_server");
                set(ev, "address", address);
                local_.sendJson(ev);
                sendState();
            });
            return;
        }
        keys_.setAddress(address);
        link_.reset();
        std::string answer;
        const ServerLink::Outcome outcome = link_.call("{\"op\":\"config\"}", answer);
        Rtc::onMain([this, outcome]() {
            ++waitGeneration_;
            setConnection(outcome);
            sendState();
            if (outcome == ServerLink::Outcome::Ok) fetchFriends();
        });
    });
}

void Conductor::cmdLeave() {
    ask(requestNamed("leave"), [this](ServerLink::Outcome, json_object*) {
        keys_.forget();
        link_.reset();
        paired_ = false;
        signedIn_ = false;
        friends_.clear();
        connection_ = "ok";
        ++waitGeneration_;
        local_.sendJson(eventNamed("left"));
        sendState();
    });
}

void Conductor::cmdSignin(json_object* o) {
    username_ = str(o, "username");
    account_ = accountOf(num(o, "id"), str(o, "created"));
    status_ = str(o, "status");
    signedIn_ = true;
    signIn();
}

void Conductor::signIn() {
    ++signinsOut_;
    json_object* q = requestNamed("signin");
    set(q, "account", account_);
    set(q, "username", username_);
    set(q, "status", status_);
    ask(q, [this](ServerLink::Outcome outcome, json_object* answer) {
        --signinsOut_;
        if (outcome == ServerLink::Outcome::Ok && str(answer, "error").empty()) fetchFriends();
    });
}

// The server keeps who is signed in on each console in memory only, so after it restarts it knows
// nobody here: it refuses an invite, a status reaches nobody, and the list stops changing, with
// nothing on the screen to say so. The console signs in again, with the account and status it last
// gave, whenever the server comes back or answers `no_account`. One at a time: requests go to the
// server in the order they were asked, so anything asked after this one finds the account there.
void Conductor::signInAgain() {
    if (!signedIn_ || signinsOut_ > 0) return;
    std::fprintf(stderr, "[linkup] signing in again\n");
    signIn();
}

void Conductor::cmdInvite(json_object* o) {
    if (game_) {
        json_object* ev = eventNamed("invite_out");
        set(ev, "ok", false);
        local_.sendJson(ev);
        return;
    }
    const std::string friendId = str(o, "friend");
    auto it = std::find_if(friends_.begin(), friends_.end(), [&](const Friend& f) { return f.id == friendId; });
    if (it == friends_.end() || !it->hasKey) {
        json_object* ev = eventNamed("invite_out");
        set(ev, "ok", false);
        set(ev, "reason", "unavailable");
        local_.sendJson(ev);
        return;
    }
    Game g;
    g.host = true;
    g.inviteId = randomId();
    g.friendId = friendId;
    g.friendUsername = it->username;
    g.friendKey = it->key;
    g.gameName = str(o, "game");
    g.gamescopePid = static_cast<int>(num(o, "gamescope"));
    g.width = static_cast<int>(num(o, "width", 1920));
    g.height = static_cast<int>(num(o, "height", 1080));
    box::Contents c;
    c.type = "invite";
    c.n = g.counter.take();
    c.expires = std::time(nullptr) + 1800;
    c.from = username_;
    c.to = g.friendUsername;
    c.stream = ServerLink::kStream;
    c.game = g.gameName;
    const std::string text = box::write(c);
    noise::Bytes sealed;
    if (!box::seal(keys_.mine(), g.friendKey, g.inviteId, noise::Bytes(text.begin(), text.end()), sealed)) {
        json_object* ev = eventNamed("invite_out");
        set(ev, "ok", false);
        local_.sendJson(ev);
        return;
    }
    json_object* q = requestNamed("invite");
    set(q, "invite", g.inviteId);
    set(q, "friend", friendId);
    set(q, "sealed", hexutil::toBase64(sealed));
    game_ = std::move(g);
    const std::string id = game_->inviteId;
    ask(q, [this, id](ServerLink::Outcome outcome, json_object* answer) {
        const std::string error = str(answer, "error");
        const bool ok = outcome == ServerLink::Outcome::Ok && error.empty();
        if (!ok && game_ && game_->inviteId == id) game_.reset();
        json_object* ev = eventNamed("invite_out");
        set(ev, "ok", ok);
        if (!ok) set(ev, "reason", outcome == ServerLink::Outcome::Ok ? error : std::string("unreachable"));
        local_.sendJson(ev);
    });
}

void Conductor::cmdJoin(json_object* o) {
    if (game_) {
        json_object* ev = eventNamed("join_result");
        set(ev, "ok", false);
        local_.sendJson(ev);
        return;
    }
    const std::string invite = str(o, "invite");
    auto it = std::find_if(friends_.begin(), friends_.end(), [&](const Friend& f) { return f.inviteId == invite; });
    if (it == friends_.end() || !it->hasKey) {
        json_object* ev = eventNamed("join_result");
        set(ev, "ok", false);
        local_.sendJson(ev);
        return;
    }
    Game g;
    g.host = false;
    g.inviteId = invite;
    g.friendId = it->id;
    g.friendUsername = it->username;
    g.friendKey = it->key;
    g.display = str(o, "display");
    g.xauthority = str(o, "xauthority");
    game_ = std::move(g);
    json_object* q = requestNamed("join");
    set(q, "invite", invite);
    ask(q, [this, invite](ServerLink::Outcome outcome, json_object* answer) {
        const std::string session = str(answer, "session");
        const bool ok = outcome == ServerLink::Outcome::Ok && !session.empty();
        json_object* ev = eventNamed("join_result");
        set(ev, "ok", ok);
        local_.sendJson(ev);
        if (!game_ || game_->inviteId != invite) return;
        if (!ok) {
            game_.reset();
            return;
        }
        game_->sessionId = session;
        keys_.setOpenSession(session);
        sendHello();
        prepareConnection();
    });
}

void Conductor::cmdPicture(bool on) {
    if (!game_ || !game_->host || !host_) return;
    host_->setPicture(on);
    if (Rtc* r = rtc()) r->sendControl(on ? "picture:1" : "picture:0");
}

void Conductor::cmdEnd() {
    if (!game_) return;
    if (!game_->sessionId.empty()) {
        json_object* q = requestNamed("end");
        set(q, "session", game_->sessionId);
        ask(q, nullptr);
    }
    endGame();
}

// ---- The server -----------------------------------------------------------

void Conductor::work(std::function<void()> onWorker) {
    std::lock_guard<std::mutex> lk(workMu_);
    workQueue_.push_back(std::move(onWorker));
    workCv_.notify_one();
}

void Conductor::workerLoop() {
    while (!stopping_) {
        std::function<void()> next;
        {
            std::unique_lock<std::mutex> lk(workMu_);
            workCv_.wait_for(lk, std::chrono::milliseconds(250), [this]() { return !workQueue_.empty() || stopping_.load(); });
            if (workQueue_.empty()) continue;
            next = std::move(workQueue_.front());
            workQueue_.pop_front();
        }
        next();
    }
}

void Conductor::ask(json_object* request, Then then) {
    const std::string text = jsonutil::text(request);
    json_object_put(request);
    send(text, std::move(then), true);
}

// A request the server refused because it knows nobody signed in here is asked once more, after the
// sign-in, so a press made just after a restart still works.
void Conductor::send(const std::string& request, Then then, bool mayRetry) {
    work([this, request, then = std::move(then), mayRetry]() {
        std::string answer;
        const ServerLink::Outcome outcome = link_.call(request, answer);
        Rtc::onMain([this, request, outcome, answer, then, mayRetry]() {
            json_object* o = jsonutil::parse(answer);
            setConnection(outcome);
            if (mayRetry && signedIn_ && str(o, "error") == "no_account") {
                if (o) json_object_put(o);
                signInAgain();
                send(request, then, false);
                return;
            }
            if (then) then(outcome, o);
            if (o) json_object_put(o);
        });
    });
}

void Conductor::waitLoop() {
    int after = 0;
    std::string epoch;
    int generation = waitGeneration_.load();
    bool missed = false;
    const auto stop = [this, &generation]() { return stopping_.load() || generation != waitGeneration_.load(); };
    while (!stopping_) {
        if (generation != waitGeneration_.load()) {
            generation = waitGeneration_.load();
            after = 0;
            epoch.clear();
        }
        // While the console cannot reach its RomM server, check-ins stop, unless a game is running.
        if (!paired_.load() || away_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        // After a miss, a short request first. The long one is held up to 25 seconds, so the server
        // would be seen back, and signed in to again, only that much later.
        if (missed) {
            std::string answer;
            const ServerLink::Outcome outcome = link_.call("{\"op\":\"config\"}", answer);
            if (outcome == ServerLink::Outcome::Unreachable) {
                link_.waitBeforeRetry(stop);
                continue;
            }
            missed = false;
            Rtc::onMain([this, outcome]() { setConnection(outcome); });
        }
        const std::string request = "{\"op\":\"wait\",\"after\":" + std::to_string(after) + "}";
        std::string answer;
        const ServerLink::Outcome outcome = link_.call(request, answer, 40);
        if (outcome != ServerLink::Outcome::Ok) {
            missed = outcome == ServerLink::Outcome::Unreachable;
            Rtc::onMain([this, outcome]() { setConnection(outcome); });
            link_.waitBeforeRetry(stop);
            continue;
        }
        json_object* o = jsonutil::parse(answer);
        if (!o) continue;
        const std::string nowEpoch = str(o, "epoch");
        // The server restarted, perhaps too quickly to be seen as gone: sign in again, and start over at 0.
        const bool restarted = !epoch.empty() && epoch != nowEpoch;
        epoch = nowEpoch;
        const int next = static_cast<int>(num(o, "next"));
        after = restarted ? 0 : next;
        std::string events = "[]";
        if (json_object* list = get(o, "events")) events = jsonutil::text(list);
        json_object_put(o);
        Rtc::onMain([this, events, restarted]() {
            setConnection(ServerLink::Outcome::Ok);
            if (restarted) {
                signInAgain();
                return;
            }
            json_object* list = jsonutil::parse(events);
            if (list) {
                onEvents(list);
                json_object_put(list);
            }
        });
    }
}

void Conductor::onEvents(json_object* events) {
    bool refresh = false;
    for (size_t i = 0; i < json_object_array_length(events); ++i) {
        json_object* e = json_object_array_get_idx(events, i);
        const std::string type = str(e, "type");
        if (type == "friends") {
            refresh = true;
        } else if (type == "invite") {
            inviteSealed_[str(e, "invite")] = str(e, "sealed");
            refresh = true;
        } else if (type == "invite_ended") {
            const std::string invite = str(e, "invite");
            json_object* ev = eventNamed("invite_ended");
            set(ev, "invite", invite);
            set(ev, "reason", str(e, "reason"));
            local_.sendJson(ev);
            if (game_ && game_->inviteId == invite && game_->sessionId.empty()) game_.reset();
            refresh = true;
        } else if (type == "joined") {
            if (game_ && game_->host && game_->sessionId.empty()) {
                game_->sessionId = str(e, "session");
                keys_.setOpenSession(game_->sessionId);
                local_.sendJson(eventNamed("joined"));
                prepareConnection();
            }
        } else if (type == "signal") {
            if (game_ && str(e, "session") == game_->sessionId) onSignalBox(str(e, "sealed"));
            else if (game_ && game_->sessionId.empty()) onSignalBox(str(e, "sealed"));
        } else if (type == "session_ended") {
            if (game_ && str(e, "session") == game_->sessionId) {
                local_.sendJson(eventNamed("session_ended"));
                endGame();
            }
        } else if (type == "removed") {
            setConnection(ServerLink::Outcome::Removed);
        }
    }
    if (refresh) fetchFriends();
}

void Conductor::fetchFriends() {
    if (!signedIn_) return;
    ask(requestNamed("friends"), [this](ServerLink::Outcome outcome, json_object* answer) {
        if (outcome != ServerLink::Outcome::Ok || !answer || !str(answer, "error").empty()) return;
        json_object* list = get(answer, "friends");
        if (!list) return;
        std::vector<Friend> fresh;
        for (size_t i = 0; i < json_object_array_length(list); ++i) {
            json_object* j = json_object_array_get_idx(list, i);
            Friend f;
            f.id = str(j, "id");
            f.username = str(j, "username");
            f.name = box::cleanText(f.username, box::kMaxUsername);
            f.server = box::cleanText(str(j, "server"), box::kMaxUsername);
            f.status = str(j, "status");
            f.stream = static_cast<int>(num(j, "stream"));
            std::vector<uint8_t> key;
            if (hexutil::fromBase64(str(j, "key"), key) && key.size() == f.key.size()) {
                std::copy(key.begin(), key.end(), f.key.begin());
                f.hasKey = true;
            }
            if (json_object* invite = get(j, "invite")) {
                f.inviteId = str(invite, "id");
                f.inviteSealed = str(invite, "sealed");
                if (f.inviteSealed.empty()) {
                    auto it = inviteSealed_.find(f.inviteId);
                    if (it != inviteSealed_.end()) f.inviteSealed = it->second;
                }
            }
            fresh.push_back(std::move(f));
        }
        friends_ = std::move(fresh);
        sendFriends();
    });
}

// A box only needs opening once, and the Reader would refuse its count the second time.
bool Conductor::openInviteBox(const Friend& f, std::string& game) {
    auto known = inviteGames_.find(f.inviteId);
    if (known != inviteGames_.end()) {
        game = known->second;
        return !game.empty();
    }
    inviteGames_[f.inviteId] = "";
    std::vector<uint8_t> raw;
    if (!f.hasKey || !hexutil::fromBase64(f.inviteSealed, raw)) return false;
    noise::Bytes plain;
    if (!box::open(keys_.mine(), f.key, f.inviteId, raw, plain)) {
        std::fprintf(stderr, "[linkup] an invite's box would not open\n");
        return false;
    }
    box::Contents c;
    if (!inviteReaders_[f.inviteId].read(plain, "invite", f.username, username_, std::time(nullptr), c)) {
        std::fprintf(stderr, "[linkup] an invite's box did not fit, and was dropped\n");
        return false;
    }
    inviteGames_[f.inviteId] = c.game;
    game = c.game;
    return !game.empty();
}

void Conductor::sendFriends() {
    json_object* ev = eventNamed("friends");
    json_object* list = json_object_new_array();
    std::vector<std::pair<std::string, std::string>> arrived;
    for (const Friend& f : friends_) {
        json_object* j = json_object_new_object();
        set(j, "id", f.id);
        set(j, "name", f.name);
        set(j, "server", f.server);
        set(j, "status", f.status);
        set(j, "stream", f.stream);
        if (!f.inviteId.empty()) {
            std::string game;
            if (openInviteBox(f, game)) {
                set(j, "invite", f.inviteId);
                set(j, "game", game);
                if (std::find(saidInvited_.begin(), saidInvited_.end(), f.inviteId) == saidInvited_.end()) {
                    saidInvited_.push_back(f.inviteId);
                    arrived.push_back({f.name, f.id});
                }
            }
        }
        json_object_array_add(list, j);
    }
    json_object_object_add(ev, "list", list);
    local_.sendJson(ev);
    for (const auto& a : arrived) {
        json_object* one = eventNamed("invited");
        set(one, "from", a.first);
        set(one, "friend", a.second);
        local_.sendJson(one);
    }
}

void Conductor::setConnection(ServerLink::Outcome outcome) {
    std::string word = "ok";
    switch (outcome) {
        case ServerLink::Outcome::Ok: word = "ok"; break;
        case ServerLink::Outcome::Unreachable: word = "unreachable"; break;
        case ServerLink::Outcome::Removed: word = "removed"; break;
        case ServerLink::Outcome::ConsoleBehind: word = "console_behind"; break;
        case ServerLink::Outcome::ServerBehind: word = "server_behind"; break;
    }
    if (word == connection_) return;
    const bool back = word == "ok";
    connection_ = word;
    std::fprintf(stderr, "[linkup] the server: %s\n", word.c_str());
    sendState();
    if (back) signInAgain();
}

void Conductor::sendState() {
    const auto server = keys_.server();
    json_object* ev = eventNamed("state");
    set(ev, "paired", server.has_value());
    set(ev, "address", server ? server->address : std::string());
    set(ev, "server", server ? server->name : std::string());
    set(ev, "connection", connection_);
    local_.sendJson(ev);
}

// ---- The game ---------------------------------------------------------------

Rtc* Conductor::rtc() {
    if (host_) return host_->rtc();
    if (friend_) return friend_->rtc();
    return nullptr;
}

Rtc::Hooks Conductor::makeHooks() {
    Rtc::Hooks hooks;
    hooks.signal = [this](const std::string& details) { sendSignal(details); };
    hooks.connected = [this](const std::string& path) {
        if (!game_) return;
        game_->connected = true;
        path_ = path;
        if (host_) host_->stopPriming();
        std::fprintf(stderr, "[linkup] connected: %s\n", path.c_str());
        json_object* ev = eventNamed("connected");
        set(ev, "path", path);
        local_.sendJson(ev);
        if (!game_->sessionId.empty()) {
            json_object* q = requestNamed("report");
            set(q, "session", game_->sessionId);
            set(q, "path", path);
            set(q, "reason", "none");
            ask(q, nullptr);
        }
    };
    hooks.failed = [this]() { std::fprintf(stderr, "[linkup] the connection failed\n"); };
    hooks.control = [this](const std::string& text) { onControl(text); };
    hooks.fast = [this](const uint8_t* data, size_t len) { onFast(data, len); };
    return hooks;
}

// Any thread: this is the one way a press or a rumble reaches the main program, so it goes straight on.
void Conductor::onFast(const uint8_t* data, size_t len) {
    switch (data[0]) {
        case linkupwire::kTagPad:
        case linkupwire::kTagRumble:
            local_.send(data, len);
            break;
        case kTagFullPicture:
            if (host_) host_->askFullPicture();
            break;
        case kTagBeat: {
            uint8_t back[9];
            std::memcpy(back, data, std::min<size_t>(len, sizeof back));
            back[0] = kTagBeatBack;
            if (Rtc* r = rtc()) r->sendFast(back, sizeof back);
            break;
        }
        case kTagBeatBack: {
            if (len < 9) break;
            int64_t sent = 0;
            std::memcpy(&sent, data + 1, sizeof sent);
            const int rtt = static_cast<int>(Rtc::nowMs() - sent);
            Rtc::onMain([this, rtt]() { rttSamples_.push_back(rtt); });
            break;
        }
        default:
            break;
    }
}

void Conductor::onControl(const std::string& text) {
    if (text == "picture:1" || text == "picture:0") {
        json_object* ev = eventNamed("picture");
        set(ev, "on", text == "picture:1");
        local_.sendJson(ev);
    } else if (text == "bye") {
        local_.sendJson(eventNamed("peer_gone"));
    } else if (text.rfind("p:", 0) == 0) {
        json_object* ev = eventNamed("peer");
        set(ev, "text", text.substr(2));
        local_.sendJson(ev);
    }
}

void Conductor::sendSignal(const std::string& details) {
    if (!game_ || game_->sessionId.empty()) return;
    // Which kind only: a candidate holds an internet address, and none goes in the log.
    json_object* said = jsonutil::parse(details);
    std::fprintf(stderr, "[linkup] connection details out: %s\n", str(said, "kind").empty() ? "candidate or hello" : str(said, "kind").c_str());
    if (said) json_object_put(said);
    json_object* wrap = json_object_new_object();
    set(wrap, "g", game_->generation);
    set(wrap, "d", details);
    box::Contents c;
    c.type = "signal";
    c.n = game_->counter.take();
    c.expires = std::time(nullptr) + kBoxLifeSeconds;
    c.from = username_;
    c.to = game_->friendUsername;
    c.details = jsonutil::text(wrap);
    json_object_put(wrap);
    const std::string text = box::write(c);
    noise::Bytes sealed;
    if (!box::seal(keys_.mine(), game_->friendKey, game_->inviteId, noise::Bytes(text.begin(), text.end()), sealed)) return;
    json_object* q = requestNamed("signal");
    set(q, "session", game_->sessionId);
    set(q, "sealed", hexutil::toBase64(sealed));
    ask(q, nullptr);
}

// The friend's first word: what it can decode and which stream version it speaks.
void Conductor::sendHello() {
    json_object* hello = json_object_new_object();
    set(hello, "stream", ServerLink::kStream);
    set(hello, "oldest", ServerLink::kStream);
    set(hello, "h265", StreamFriend::canDecodeH265());
    set(hello, "h264", StreamFriend::canDecodeH264());
    json_object* wrap = json_object_new_object();
    json_object_object_add(wrap, "hello", hello);
    const std::string details = jsonutil::text(wrap);
    json_object_put(wrap);
    sendSignal(details);
}

void Conductor::onSignalBox(const std::string& sealed) {
    if (!game_) return;
    std::vector<uint8_t> raw;
    if (!hexutil::fromBase64(sealed, raw)) return;
    noise::Bytes plain;
    if (!box::open(keys_.mine(), game_->friendKey, game_->inviteId, raw, plain)) {
        std::fprintf(stderr, "[linkup] connection details would not open\n");
        return;
    }
    box::Contents c;
    if (!game_->reader.read(plain, "signal", game_->friendUsername, username_, std::time(nullptr), c)) {
        std::fprintf(stderr, "[linkup] connection details did not fit, and were dropped\n");
        return;
    }
    json_object* outer = jsonutil::parse(c.details);
    if (!outer) return;
    const int generation = static_cast<int>(num(outer, "g", 1));
    const std::string details = str(outer, "d");
    json_object_put(outer);
    json_object* d = jsonutil::parse(details);
    if (!d) return;
    if (json_object* hello = get(d, "hello")) {
        game_->friendH265 = flag(hello, "h265");
        game_->friendH264 = flag(hello, "h264");
        game_->haveHello = true;
        // A number moves only on a real break, and a console names who is behind. An update is never
        // forced: the two simply cannot play until it comes.
        const int theirStream = static_cast<int>(num(hello, "stream", 1));
        const int theirOldest = static_cast<int>(num(hello, "oldest", 1));
        json_object_put(d);
        if (theirStream < ServerLink::kStream || ServerLink::kStream < theirOldest) {
            json_object* ev = eventNamed("connect_failed");
            set(ev, "reason", theirStream < ServerLink::kStream ? "friend_behind" : "this_behind");
            local_.sendJson(ev);
            json_object* q = requestNamed("report");
            set(q, "session", game_->sessionId);
            set(q, "path", "failed");
            set(q, "reason", "behind");
            ask(q, nullptr);
            return;
        }
        maybeStartStream();
        return;
    }
    json_object_put(d);
    // A new connection for this game, from a host that lost the old one.
    if (!game_->host && generation > game_->generation && friend_) {
        game_->generation = generation;
        friend_->stop();
        friend_.reset();
        game_->streamStarted = false;
        game_->waitingSignals.clear();
        game_->waitingSignals.push_back(details);
        maybeStartStream();
        return;
    }
    if (generation < game_->generation) return;
    game_->generation = generation;
    applySignal(details);
}

void Conductor::applySignal(const std::string& details) {
    Rtc* r = rtc();
    if (!r || !game_->streamStarted) {
        game_->waitingSignals.push_back(details);
        return;
    }
    json_object* d = jsonutil::parse(details);
    if (!d) return;
    const std::string sdp = str(d, "sdp");
    if (!sdp.empty()) {
        std::fprintf(stderr, "[linkup] connection details in: %s\n", str(d, "kind").c_str());
        r->onSignal(str(d, "kind"), sdp, 0);
    } else {
        const std::string ice = str(d, "ice");
        if (!ice.empty()) r->onSignal("ice", ice, static_cast<unsigned>(num(d, "m")));
    }
    json_object_put(d);
}

void Conductor::prepareConnection() {
    if (!game_) return;
    game_->startedMs = Rtc::nowMs();
    const std::string invite = game_->inviteId;
    portmap_.start(Rtc::kPortLow);
    ask(requestNamed("config"), [this, invite](ServerLink::Outcome, json_object* answer) {
        if (!game_ || game_->inviteId != invite) return;
        std::vector<std::string> list;
        if (json_object* stun = get(answer, "stun")) {
            for (size_t i = 0; i < json_object_array_length(stun); ++i) {
                json_object* one = json_object_array_get_idx(stun, i);
                if (json_object_is_type(one, json_type_string)) list.push_back(json_object_get_string(one));
            }
        }
        work([this, invite, list]() {
            const std::string first = firstStunThatAnswers(list);
            Rtc::onMain([this, invite, first]() {
                if (!game_ || game_->inviteId != invite) return;
                game_->stun = first;
                game_->haveConfig = true;
                maybeStartStream();
            });
        });
    });
    json_object* q = requestNamed("relay");
    set(q, "session", game_->sessionId);
    ask(q, [this, invite](ServerLink::Outcome, json_object* answer) {
        if (!game_ || game_->inviteId != invite) return;
        if (json_object* relay = get(answer, "relay")) {
            json_object* urls = get(relay, "urls");
            if (urls && json_object_array_length(urls) > 0) {
                json_object* first = json_object_array_get_idx(urls, 0);
                if (json_object_is_type(first, json_type_string))
                    game_->turn = turnUri(json_object_get_string(first), str(relay, "username"), str(relay, "password"));
            }
        } else {
            game_->relayReason = str(answer, "reason");
        }
        game_->haveRelay = true;
        maybeStartStream();
    });
}

void Conductor::maybeStartStream() {
    if (!game_ || game_->streamStarted || !game_->haveConfig || !game_->haveRelay) return;
    if (game_->host && !game_->haveHello) return;
    if (game_->host) startHost();
    else startFriend();
}

void Conductor::startHost() {
    const bool h265 = game_->friendH265 && StreamHost::canEncodeH265();
    const bool h264 = game_->friendH264 && StreamHost::canEncodeH264();
    if (!h265 && !h264) {
        std::fprintf(stderr, "[linkup] no picture both consoles can use\n");
        json_object* ev = eventNamed("connect_failed");
        set(ev, "reason", "other");
        local_.sendJson(ev);
        return;
    }
    const uint32_t node = Capture::findNode(game_->gamescopePid);
    if (node == 0) {
        std::fprintf(stderr, "[linkup] this console's gamescope has no capture\n");
        json_object* ev = eventNamed("connect_failed");
        set(ev, "reason", "other");
        local_.sendJson(ev);
        return;
    }
    StreamHost::Settings s;
    s.node = node;
    s.h265 = h265;
    // The host's own resolution, capped at 1080p: a 4K host is asked for that, with no 4K copy.
    s.width = game_->width;
    s.height = game_->height;
    if (s.height > 1080) {
        s.width = (s.width * 1080 / s.height) & ~1;
        s.height = 1080;
    }
    s.paths.stun = game_->stun;
    s.paths.turn = game_->turn;
    host_ = std::make_unique<StreamHost>(makeHooks());
    host_->setOnSlow([this]() {
        local_.sendJson(eventNamed("slow"));
        if (Rtc* r = rtc()) r->sendControl("p:{\"t\":\"slow\"}");
    });
    // The picture stays off until the main program has the game on the screen.
    host_->setPicture(false);
    if (!host_->start(s)) {
        host_.reset();
        json_object* ev = eventNamed("connect_failed");
        set(ev, "reason", "other");
        local_.sendJson(ev);
        return;
    }
    game_->streamStarted = true;
    std::fprintf(stderr, "[linkup] hosting: %s at %dx%d\n", h265 ? "H.265" : "H.264", s.width, s.height);
    std::vector<std::string> waiting;
    waiting.swap(game_->waitingSignals);
    for (const std::string& d : waiting) applySignal(d);
}

void Conductor::startFriend() {
    StreamFriend::Settings s;
    s.paths.stun = game_->stun;
    s.paths.turn = game_->turn;
    s.display = game_->display;
    s.xauthority = game_->xauthority;
    friend_ = std::make_unique<StreamFriend>(makeHooks(), [this]() {
        const uint8_t ask = kTagFullPicture;
        if (Rtc* r = rtc()) r->sendFast(&ask, 1);
    });
    if (!friend_->start(s)) {
        friend_.reset();
        json_object* ev = eventNamed("connect_failed");
        set(ev, "reason", "other");
        local_.sendJson(ev);
        return;
    }
    game_->streamStarted = true;
    std::vector<std::string> waiting;
    waiting.swap(game_->waitingSignals);
    for (const std::string& d : waiting) applySignal(d);
}

void Conductor::endGame() {
    keys_.setOpenSession("");
    if (host_) host_->stop();
    if (friend_) friend_->stop();
    host_.reset();
    friend_.reset();
    portmap_.stop();
    game_.reset();
    rttSamples_.clear();
}

gboolean Conductor::onTick(gpointer self) {
    static_cast<Conductor*>(self)->tick();
    return G_SOURCE_CONTINUE;
}

void Conductor::tick() {
    if (!game_ || !game_->streamStarted) {
        if (game_ && !game_->sessionId.empty() && game_->startedMs != 0 && Rtc::nowMs() - game_->startedMs > kConnectLimitMs && !game_->connected) {
            // Nothing to wait for any longer, whichever part never came.
            json_object* ev = eventNamed("connect_failed");
            set(ev, "reason", game_->relayReason == "limit" ? "relay_limit" : "other");
            local_.sendJson(ev);
            json_object* q = requestNamed("report");
            set(q, "session", game_->sessionId);
            set(q, "path", "failed");
            set(q, "reason", "other");
            ask(q, nullptr);
            game_->startedMs = 0;
        }
        return;
    }
    Rtc* r = rtc();
    if (!r) return;
    const int64_t now = Rtc::nowMs();
    if (!game_->connected) {
        if (game_->startedMs != 0 && now - game_->startedMs > kConnectLimitMs) {
            json_object* ev = eventNamed("connect_failed");
            set(ev, "reason", game_->relayReason == "limit" ? "relay_limit" : "no_path");
            local_.sendJson(ev);
            json_object* q = requestNamed("report");
            set(q, "session", game_->sessionId);
            set(q, "path", "failed");
            set(q, "reason", game_->relayReason == "limit" ? "relay_limit" : "no_path");
            ask(q, nullptr);
            game_->startedMs = 0;
        }
    }
    if (now - lastBeatMs_ >= kBeatMs) {
        lastBeatMs_ = now;
        uint8_t beat[9];
        beat[0] = kTagBeat;
        std::memcpy(beat + 1, &now, sizeof now);
        r->sendFast(beat, sizeof beat);
    }
    if (host_ && now - lastStatsMs_ >= 1000) {
        lastStatsMs_ = now;
        host_->pollStats();
    }
    // The delay goes to the log only, once a minute.
    if (now - lastRttLogMs_ >= 60000 && !rttSamples_.empty()) {
        lastRttLogMs_ = now;
        std::sort(rttSamples_.begin(), rttSamples_.end());
        std::fprintf(stderr, "[linkup] %s, round trip median %d ms, worst %d ms\n", path_.c_str(),
                     rttSamples_[rttSamples_.size() / 2], rttSamples_.back());
        rttSamples_.clear();
    }
    if (r->everHeard()) {
        const int64_t quiet = r->quietMs();
        if (!game_->lost && quiet > kDropMs) {
            game_->lost = true;
            std::fprintf(stderr, "[linkup] nothing heard from the friend for %lld ms\n", static_cast<long long>(quiet));
            local_.sendJson(eventNamed("peer_lost"));
        } else if (game_->lost && quiet < 500) {
            game_->lost = false;
            std::fprintf(stderr, "[linkup] the friend is back\n");
            local_.sendJson(eventNamed("peer_back"));
            // On a new connection the friend's console has heard none of this.
            if (host_) {
                host_->askFullPicture();
                r->sendControl(host_->pictureOn() ? "picture:1" : "picture:0");
            }
        }
        // Only the host makes a new connection: the friend follows its generation number.
        if (game_->lost && game_->host && quiet > kReconnectAfterMs && now - game_->lastReconnectMs > kReconnectEveryMs) {
            game_->lastReconnectMs = now;
            ++game_->generation;
            std::fprintf(stderr, "[linkup] trying a new connection\n");
            game_->connected = false;
            game_->startedMs = 0;
            host_->reconnect();
            r = rtc();
            if (r) r->heard();
        }
    }
}
