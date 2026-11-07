// The host's side of a game: capture, encode, send. H.265 by default and H.264 for a friend's console
// that cannot decode it; at the host's own resolution, capped at 1080p; a steady bitrate of at most
// 18 Mbit/s with a quality floor, and the padding that leaves dropped before sending, so the stream
// spends what the picture needs. The figures are #654's, each measured on the reference console.
//
// NOTHING BUT THE GAME LEAVES THIS CONSOLE. The main program turns the picture off before it draws
// anything else: no picture and no sound go out until it turns it back on, starting from a fresh full
// picture.

#pragma once

#include <gst/gst.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "bitrate.h"
#include "capture.h"
#include "rtc.h"

class StreamHost {
public:
    struct Settings {
        uint32_t node = 0;
        int width = 1920;
        int height = 1080;
        bool h265 = true;
        Rtc::Paths paths;
        std::string soundTarget;   // the output whose monitor is captured; empty for the default
    };

    explicit StreamHost(Rtc::Hooks hooks);
    ~StreamHost();

    bool start(const Settings& settings);
    // A new connection for the same game, keeping the capture, after one was lost.
    bool reconnect();
    void stop();

    void setPicture(bool on);
    void askFullPicture();
    Rtc* rtc() { return rtc_.get(); }

    // Once a second, on the main thread: what the connection is doing, and what to do about it.
    // Returns true the moment the lowest step has not kept up for several seconds.
    void pollStats();

    void setOnSlow(std::function<void()> fn) { onSlow_ = std::move(fn); }

    // The two consoles are connected: from here only the game goes in.
    void stopPriming();

    // Whether this console can encode each codec at all.
    static bool canEncodeH265();
    static bool canEncodeH264();

private:
    bool buildPipeline();
    void destroyPipeline();
    void onFrame(const uint8_t* y, int yStride, const uint8_t* uv, int uvStride, int w, int h);
    void pushLocked(const uint8_t* y, int yStride, const uint8_t* uv, int uvStride, int w, int h);
    void pushBlack();
    static gboolean onPrime(gpointer self);
    void applyStep(const Bitrate::Step& step);
    static GstPadProbeReturn dropFiller(GstPad*, GstPadProbeInfo* info, gpointer fillerType);
    static void onStatsReply(GstPromise* promise, gpointer self);
    static gboolean onBus(GstBus*, GstMessage* m, gpointer self);

    Rtc::Hooks hooks_;
    Settings settings_;
    Capture capture_;
    Bitrate bitrate_;
    std::function<void()> onSlow_;
    GstElement* pipeline_ = nullptr;
    GstElement* webrtc_ = nullptr;
    GstElement* appsrc_ = nullptr;
    GstElement* encoder_ = nullptr;
    GstElement* mute_ = nullptr;
    std::unique_ptr<Rtc> rtc_;
    std::mutex frameMu_;
    int capsW_ = 0;
    int capsH_ = 0;
    std::atomic<bool> pictureOn_{true};
    std::atomic<bool> wantFull_{false};
    uint32_t keyAsks_ = 0;
    guint primeSource_ = 0;
    int primeWith_ = 0;
};
