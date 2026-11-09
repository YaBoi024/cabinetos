// Asking the router for LinkUp's port (portmap.h).

#include "portmap.h"

#include <arpa/inet.h>
#include <libgupnp-igd/gupnp-simple-igd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace {

constexpr unsigned kLeaseSeconds = 600;

// The address this console uses to reach out: a UDP socket connected to nothing in particular
// sends nothing, and says which local address it would use.
std::string localAddress() {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return {};
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    inet_pton(AF_INET, "192.0.2.1", &to.sin_addr);
    std::string out;
    if (connect(fd, reinterpret_cast<sockaddr*>(&to), sizeof to) == 0) {
        sockaddr_in me{};
        socklen_t len = sizeof me;
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&me), &len) == 0) {
            char text[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &me.sin_addr, text, sizeof text)) out = text;
        }
    }
    close(fd);
    return out;
}

}  // namespace

Portmap::~Portmap() { stop(); }

void Portmap::start(unsigned port) {
    if (igd_) return;
    const std::string local = localAddress();
    if (local.empty()) return;
    igd_ = gupnp_simple_igd_new();
    if (!igd_) return;
    port_ = port;
    gupnp_simple_igd_add_port(igd_, "UDP", static_cast<guint16>(port), local.c_str(), static_cast<guint16>(port),
                              kLeaseSeconds, "CabinetOS LinkUp");
}

void Portmap::stop() {
    if (!igd_) return;
    gupnp_simple_igd_remove_port(igd_, "UDP", port_);
    g_object_unref(igd_);
    igd_ = nullptr;
}
