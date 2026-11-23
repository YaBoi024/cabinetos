// The Unix socket to the main program, which is the only way in or out of this program apart from
// its server and its friend. A Unix socket because the firewall lock drops TCP on loopback too. It
// listens in the runtime folder, 0600, and the main program connects when it wants to: with LinkUp
// off, there is nothing on the other end. One client; a new one replaces the old, as the main
// program restarts itself in place.

#pragma once

#include <glib.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <json-c/json.h>

class LocalSocket {
public:
    using OnMessage = std::function<void(const uint8_t* data, size_t len)>;
    using OnClient = std::function<void(bool connected)>;

    bool listen(const std::string& path, OnMessage onMessage, OnClient onClient);
    void close();

    // From any thread. A message the socket has no room for is dropped: a pad's state is replaced
    // by the next, and nothing else here comes fast enough to fill it.
    void send(const void* data, size_t len);
    // Puts the object.
    void sendJson(json_object* o);

private:
    static gboolean onListen(gint fd, GIOCondition cond, gpointer self);
    static gboolean onReadable(gint fd, GIOCondition cond, gpointer self);
    void dropClient();

    std::string path_;
    int listenFd_ = -1;
    guint listenWatch_ = 0;
    std::mutex mu_;
    int clientFd_ = -1;
    guint clientWatch_ = 0;
    OnMessage onMessage_;
    OnClient onClient_;
};
