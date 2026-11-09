// Asking the router for LinkUp's port, for the length of a game, with a lease that is renewed while
// the program runs. libnice's own UPnP stays off: measured, it asks for a permanent lease on every
// local address, IPv6 too, and leaves them behind when the program dies (#654, the second comment).
// This asks for one port on one address, so a lease left by a crash runs out in ten minutes, and
// the firewall drops that port anyway once the program has gone.

#pragma once

struct _GUPnPSimpleIgd;

class Portmap {
public:
    ~Portmap();
    void start(unsigned port);
    void stop();

private:
    _GUPnPSimpleIgd* igd_ = nullptr;
    unsigned port_ = 0;
};
