// FORK (handheld): gyro for emulators. The console reads the seated pad's motion
// sensors through SDL and serves them to an emulator over the Cemuhook (DSU)
// protocol on 127.0.0.1, which Eden and others read as a motion source. The
// virtual pads (vpad.h) carry no motion, so this is a separate path.

#pragma once

#include <cstdint>
#include <string>

namespace cab::motion {

constexpr int kPort = 26760;

// Once a frame while an emulator runs, next to vpad::pump. The first call opens
// the socket; each call answers requests and sends subscribers fresh motion.
void pump(int64_t nowMs);

// What Eden's cemuhookudp motion binding calls this server's address (127.0.0.1).
std::string edenGuid();

}  // namespace cab::motion