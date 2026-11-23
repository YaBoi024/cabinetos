// What the program does: the main program's commands on one side, the server's events on the other,
// and at most one game with a friend between them. Everything here runs on the main loop except the
// two workers, one for the one long request and one for everything else, which only ever hand their
// answers back to it.
//
// The server owns who can be invited; the main program owns every screen and every word. This holds
// the keys and the boxes, passes connection details, runs the stream, and tells the main program what
// happened. local_socket.h has the channel, and CONTRIBUTING.md's table of what each command means is
// the comment on onCommand.

#pragma once

#include <glib.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "box.h"
#include "keys.h"
#include "local_socket.h"
#include "portmap.h"
#include "server_link.h"
#include "stream_friend.h"
#include "stream_host.h"

class Conductor {
public:
    Conductor(Keys& keys, LocalSocket& local);
    ~Conductor();

    void start();
    // SIGTERM: the friend is told, the capture stops the safe way. The process then ends.
    void shutDown();

    void onMessage(const uint8_t* data, size_t len);
    void onClient(bool connected);

private:
    struct Friend {
        std::string id;
        std::string username;     // as the server has it, what a box says
        std::string name;         // cleaned, what a person reads
        std::string server;
        std::string status;
        int stream = 0;
        noise::Key key{};
        bool hasKey = false;
        std::string inviteId;
        std::string inviteSealed;
    };
    struct Game {
        bool host = false;
        std::string inviteId;
        std::string sessionId;
        std::string friendId;
        std::string friendUsername;
        noise::Key friendKey{};
        std::string gameName;
        box::Counter counter;
        box::Reader reader;
        // What the connection is waiting on.
        bool haveConfig = false;
        bool haveRelay = false;
        bool haveHello = false;
        bool streamStarted = false;
        bool connected = false;
        std::string stun;
        std::string turn;
        std::string relayReason;
        bool friendH265 = false;
        bool friendH264 = false;
        int gamescopePid = 0;
        int width = 1920;
        int height = 1080;
        std::string display;
        std::string xauthority;
        int generation = 1;
        int64_t startedMs = 0;
        bool lost = false;
        int64_t lastReconnectMs = 0;
        std::vector<std::string> waitingSignals;
    };

    // Commands from the main program.
    void onCommand(json_object* o);
    void cmdPair(json_object* o);
    void cmdRetype(json_object* o);
    void cmdLeave();
    void cmdSignin(json_object* o);
    void cmdInvite(json_object* o);
    void cmdJoin(json_object* o);
    void cmdPicture(bool on);
    void cmdEnd();

    // The server.
    using Then = std::function<void(ServerLink::Outcome, json_object* answer)>;
    void ask(json_object* request, Then then);
    void send(const std::string& request, Then then, bool mayRetry);
    void signIn();
    void signInAgain();
    void sendStatus();
    void sendChosen();
    void tellChosen(const std::string& status);
    void work(std::function<void()> onWorker);
    void workerLoop();
    void waitLoop();
    void onEvents(json_object* events);
    void fetchFriends();
    void setConnection(ServerLink::Outcome outcome);
    void sendState();
    void sendFriends();
    bool openInviteBox(const Friend& f, std::string& game);

    // The game.
    void prepareConnection();
    void maybeStartStream();
    void startHost();
    void startFriend();
    void sendSignal(const std::string& details);
    void sendHello();
    void onSignalBox(const std::string& sealed);
    void applySignal(const std::string& details);
    void endGame();
    Rtc* rtc();
    Rtc::Hooks makeHooks();
    static gboolean onTick(gpointer self);
    void tick();
    void onFast(const uint8_t* data, size_t len);
    void onControl(const std::string& text);

    Keys& keys_;
    LocalSocket& local_;
    ServerLink link_;

    std::atomic<bool> stopping_{false};
    std::mutex workMu_;
    std::condition_variable workCv_;
    std::deque<std::function<void()>> workQueue_;
    std::thread worker_;
    std::thread waiter_;
    std::atomic<bool> away_{false};
    std::atomic<bool> paired_{false};
    std::atomic<int> waitGeneration_{0};

    std::string connection_ = "ok";
    // Taken off the server, as an answer said. Only a code typed again brings the console back
    // (cmdPair), so nothing else may say ok until then (setConnection).
    bool removed_ = false;
    // The last event handled, by its number on the server, in this run. A wait from 0 brings back
    // every event the server still holds for this console, and those are not handled twice: a choice
    // replayed after a pairing or a retyped address could otherwise undo a later one. The numbers
    // start again with a server restart (its epoch), and on a server that knows this console anew.
    int seenSeq_ = 0;
    std::string seenEpoch_;
    std::string username_;
    std::string account_;
    std::string status_;       // the account's own, as the main program last said it
    // Online or Offline the person chose here and the server has not taken yet (PROTOCOL.md, "One
    // account on more than one console"). Empty once it has.
    std::string chosenOwed_;
    bool signedIn_ = false;
    int signinsOut_ = 0;       // sign-ins sent and not yet answered
    std::vector<Friend> friends_;
    std::map<std::string, std::string> inviteGames_;       // invite id, the game its box named
    std::map<std::string, box::Reader> inviteReaders_;
    std::map<std::string, std::string> inviteSealed_;      // from the event, when a long list left it out
    std::vector<std::string> saidInvited_;

    std::optional<Game> game_;
    std::unique_ptr<StreamHost> host_;
    std::unique_ptr<StreamFriend> friend_;
    Portmap portmap_;
    guint tickSource_ = 0;
    int64_t lastStatsMs_ = 0;
    int64_t lastBeatMs_ = 0;
    int64_t lastRttLogMs_ = 0;
    std::vector<int> rttSamples_;
    std::string path_;
};
