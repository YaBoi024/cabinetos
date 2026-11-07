// What this console keeps for LinkUp, in its own folder: its one long-term key, the server's
// remembered key and address, and the last `t` it sent. Nothing else in the program writes to
// disk. The folder is /var/lib/cabinetos/linkup, 0700, and the key is made the first time the
// program is used for anything, not at first boot (nothing refers to it before that).

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "noise.h"

class Keys {
public:
    explicit Keys(std::string folder);

    // This console's key, made and kept if there is none yet. False when the folder cannot hold it.
    bool load();
    const noise::KeyPair& mine() const { return mine_; }

    struct Server {
        std::string address;
        noise::Key key{};
        std::string name;
    };
    std::optional<Server> server() const;
    bool remember(const Server& s);
    bool setAddress(const std::string& address);
    bool setName(const std::string& name);
    // Leaves: the server is forgotten, the console's own key stays.
    bool forget();

    // The session this console is in, kept so that a program that died in one can end it when it comes
    // back: a console that keeps checking in never counts as silent, so the server would hold both
    // people as Linked up for a day. Empty for none.
    void setOpenSession(const std::string& session);
    std::string openSession() const;

    // PROTOCOL.md's `t`: the larger of now in nanoseconds and one more than the last, on disk
    // before it goes out, so a clock that moves back cannot lock this console out.
    int64_t nextT();

private:
    bool saveServer();
    std::string folder_;
    mutable std::mutex mu_;
    noise::KeyPair mine_;
    std::optional<Server> server_;
    int64_t lastT_ = 0;
};
