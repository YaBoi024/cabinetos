// The Unix socket to the main program (local_socket.h).

#include "local_socket.h"

#include <fcntl.h>
#include <glib-unix.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "jsonutil.h"

bool LocalSocket::listen(const std::string& path, OnMessage onMessage, OnClient onClient) {
    path_ = path;
    onMessage_ = std::move(onMessage);
    onClient_ = std::move(onClient);
    listenFd_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) return false;
    std::strcpy(addr.sun_path, path.c_str());
    unlink(path.c_str());
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return false;
    chmod(path.c_str(), 0600);
    if (::listen(listenFd_, 2) != 0) return false;
    listenWatch_ = g_unix_fd_add(listenFd_, G_IO_IN, onListen, this);
    return true;
}

void LocalSocket::close() {
    if (listenWatch_) g_source_remove(listenWatch_);
    listenWatch_ = 0;
    dropClient();
    if (listenFd_ >= 0) ::close(listenFd_);
    listenFd_ = -1;
    if (!path_.empty()) unlink(path_.c_str());
}

void LocalSocket::dropClient() {
    std::lock_guard<std::mutex> lk(mu_);
    if (clientWatch_) g_source_remove(clientWatch_);
    clientWatch_ = 0;
    if (clientFd_ >= 0) ::close(clientFd_);
    clientFd_ = -1;
}

gboolean LocalSocket::onListen(gint fd, GIOCondition, gpointer self) {
    auto* me = static_cast<LocalSocket*>(self);
    const int client = accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client < 0) return G_SOURCE_CONTINUE;
    me->dropClient();
    {
        std::lock_guard<std::mutex> lk(me->mu_);
        me->clientFd_ = client;
        me->clientWatch_ = g_unix_fd_add(client, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), onReadable, me);
    }
    if (me->onClient_) me->onClient_(true);
    return G_SOURCE_CONTINUE;
}

gboolean LocalSocket::onReadable(gint fd, GIOCondition, gpointer self) {
    auto* me = static_cast<LocalSocket*>(self);
    uint8_t buf[64 * 1024];
    while (true) {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n > 0) {
            me->onMessage_(buf, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return G_SOURCE_CONTINUE;
        break;
    }
    // The other end has gone. This source goes with it, so it is not removed twice.
    {
        std::lock_guard<std::mutex> lk(me->mu_);
        me->clientWatch_ = 0;
        if (me->clientFd_ >= 0) ::close(me->clientFd_);
        me->clientFd_ = -1;
    }
    if (me->onClient_) me->onClient_(false);
    return G_SOURCE_REMOVE;
}

void LocalSocket::send(const void* data, size_t len) {
    std::lock_guard<std::mutex> lk(mu_);
    if (clientFd_ < 0) return;
    if (::send(clientFd_, data, len, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        std::fprintf(stderr, "[linkup] the local socket: %s\n", std::strerror(errno));
}

void LocalSocket::sendJson(json_object* o) {
    const std::string text = jsonutil::text(o);
    json_object_put(o);
    send(text.data(), text.size());
}
