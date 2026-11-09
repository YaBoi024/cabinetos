// The main program's end of the local socket to the LinkUp program (linkup_link.h).

#include "linkup_link.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "linkup_wire.h"

namespace linkup {

Link::~Link() { close(); }

void Link::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

void Link::connectIfNeeded(double nowSeconds) {
    if (fd_ >= 0 || (triedAt_ >= 0.0 && nowSeconds - triedAt_ < 0.5)) return;
    triedAt_ = nowSeconds;
    const std::string path = linkupwire::socketPath();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) return;
    std::strcpy(addr.sun_path, path.c_str());
    const int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return;
    }
    fd_ = fd;
}

bool Link::poll(const std::function<void(const uint8_t* data, size_t len)>& onMessage) {
    if (fd_ < 0) return false;
    uint8_t buf[64 * 1024];
    while (true) {
        const ssize_t n = recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            onMessage(buf, static_cast<size_t>(n));
            if (fd_ < 0) return false;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        close();
        return false;
    }
}

void Link::send(const void* data, size_t len) {
    if (fd_ < 0) return;
    if (::send(fd_, data, len, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 && errno != EAGAIN && errno != EWOULDBLOCK) close();
}

}  // namespace linkup
