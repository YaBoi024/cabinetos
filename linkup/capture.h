// The host's picture: our own libpipewire capture of gamescope, because only that can ask gamescope
// for a smaller size (its custom field 0x70000). GStreamer's pipewiresrc cannot, and a plain request
// for a size it does not offer shuts its capture off until a restart.
//
// EVERYTHING HERE COMES FROM A MEASUREMENT (#654, the third comment):
// - GAMESCOPE CAN CRASH WHEN A CAPTURE STOPS, and takes the session and the game with it. Turned off
//   first, then about 100 ms, then disconnected: 0 of 120. So stop() does that, every time, and a
//   menu or a download only turns it off.
// - The first frames after asking still arrive at full size, and are dropped.
// - A capture naming a node that has gone can attach to another gamescope, so a node is found by
//   the process that owns it, and any other is refused.
// - gamescope states no frame rate, so whoever takes these frames must state one.

#pragma once

#include <pipewire/pipewire.h>

#include <atomic>
#include <cstdint>
#include <functional>

class Capture {
public:
    // One frame, on PipeWire's thread. NV12 in shared memory.
    using OnFrame = std::function<void(const uint8_t* y, int yStride, const uint8_t* uv, int uvStride, int w, int h)>;

    ~Capture();

    // The capture node of the gamescope with this process id, or 0. Blocks for a moment.
    static uint32_t findNode(int gamescopePid);

    bool start(uint32_t node, int width, int height, OnFrame onFrame);
    // Off stops frames arriving, and stays connected, for anything that is not the game.
    void setActive(bool on);
    // Ask gamescope for another size, for the bitrate's lowest steps.
    void setSize(int width, int height);
    void stop();

    bool running() const { return stream_ != nullptr; }

private:
    static void onParam(void* self, uint32_t id, const spa_pod* param);
    static void onState(void* self, pw_stream_state old, pw_stream_state now, const char* error);
    static void onProcess(void* self);

    pw_thread_loop* loop_ = nullptr;
    pw_stream* stream_ = nullptr;
    spa_hook hook_{};
    OnFrame onFrame_;
    int askW_ = 0;
    int askH_ = 0;
    int w_ = 0;
    int h_ = 0;
    std::atomic<bool> active_{true};
};
