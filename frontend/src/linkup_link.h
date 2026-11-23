// The main program's end of the local socket to the LinkUp program (#654): non-blocking, read once a
// frame. This is all the main program knows of LinkUp's network. It holds no internet code
// (ci/check-linkup-no-network.sh): with LinkUp off, nothing is on the other end, and the socket does
// not exist.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace linkup {

class Link {
public:
    ~Link();
    // Tries the socket, at most twice a second. `nowSeconds` is any steady clock.
    void connectIfNeeded(double nowSeconds);
    bool connected() const { return fd_ >= 0; }
    void close();

    // Whatever has arrived, one message at a time. Returns false when the program went away.
    bool poll(const std::function<void(const uint8_t* data, size_t len)>& onMessage);

    void send(const void* data, size_t len);
    void send(const std::string& json) { send(json.data(), json.size()); }

private:
    int fd_ = -1;
    double triedAt_ = -1.0;
};

}  // namespace linkup
