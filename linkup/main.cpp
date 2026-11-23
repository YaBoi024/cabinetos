// cabinetos-linkup: LinkUp's own program (#654). Whoever is on a console plays a game with a friend on
// another console, through a small server its owner runs. The main program holds no LinkUp internet
// code at all: everything on the network is here, so that a firewall can tell it apart, and off can
// mean off.
//
// THE FIRST THING IT DOES IS ASK THE LOCK, BEFORE ANYTHING ELSE, A NAME LOOKUP INCLUDED. A lookup goes
// over a Unix socket to systemd-resolved, which asks as itself, so no firewall rule on this program's
// group can stop it (measured, #654's second comment). So it refuses to run outside its group, and it
// sends one empty datagram to loopback: EPERM means the lock is closed, and it exits. The unit opens
// the lock and starts it; started any other way, it stops here, having contacted nothing.
//
// It takes no socket opened elsewhere and no argument that could point it anywhere: its server comes
// from what was paired, in its folder (keys.h).

#include <arpa/inet.h>
#include <curl/curl.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <netinet/in.h>
#include <pipewire/pipewire.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include "conductor.h"
#include "keys.h"
#include "linkup_wire.h"
#include "local_socket.h"

namespace {

// The group's number, in system_files/usr/lib/sysusers.d/cabinetos-linkup.conf and in the
// firewall table, which needs the number: by name, a missing group fails the whole table.
constexpr gid_t kGroup = 4654;

// Exit codes the off proof reads: not in the group, and the lock is closed.
constexpr int kNotInGroup = 3;
constexpr int kLockClosed = 4;

bool lockIsOpen() {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const ssize_t sent = sendto(fd, "", 0, 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
    const int why = errno;
    close(fd);
    return sent == 0 || why != EPERM;
}

const char* dataFolder() {
    const char* folder = std::getenv("CABINETOS_LINKUP_DIR");
    return folder && *folder ? folder : "/var/lib/cabinetos/linkup";
}

}  // namespace

int main() {
    if (getegid() != kGroup) {
        std::fprintf(stderr, "[linkup] not in its own group; stopping\n");
        return kNotInGroup;
    }
    if (!lockIsOpen()) {
        std::fprintf(stderr, "[linkup] the lock is closed; stopping\n");
        return kLockClosed;
    }

    // The swap wait off, for the friend's window: 44 to 50 ms of delay with it on (stream_friend.h).
    setenv("vblank_mode", "0", 1);
    gst_init(nullptr, nullptr);
    pw_init(nullptr, nullptr);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    Keys keys(dataFolder());
    if (!keys.load()) {
        std::fprintf(stderr, "[linkup] cannot use its folder; stopping\n");
        return 1;
    }
    LocalSocket local;
    Conductor conductor(keys, local);
    if (!local.listen(
            linkupwire::socketPath(), [&](const uint8_t* data, size_t len) { conductor.onMessage(data, len); },
            [&](bool connected) { conductor.onClient(connected); })) {
        std::fprintf(stderr, "[linkup] could not open the local socket; stopping\n");
        return 1;
    }
    conductor.start();

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_unix_signal_add(
        SIGTERM,
        [](gpointer data) -> gboolean {
            g_main_loop_quit(static_cast<GMainLoop*>(data));
            return G_SOURCE_REMOVE;
        },
        loop);
    g_main_loop_run(loop);

    conductor.shutDown();
    local.close();
    // The workers may be inside a request of up to 40 seconds, and nothing they hold needs saving.
    _exit(0);
}
