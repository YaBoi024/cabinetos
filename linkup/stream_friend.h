// The friend's side of a game: receive, decode, and show it in this program's own window, which the
// main program hands the television as it hands it to an emulator.
//
// THREE SETTINGS THE DEFAULTS DO NOT GIVE, each measured (#654, the third comment): a one-frame leaky
// queue, without which the first frames' delay stays for good (255 ms); the swap wait off
// (vblank_mode=0, main.cpp), without which it is 44 to 50 ms; and the capture's colour stated after
// the decoder, BT.601, because a decoder assumes BT.709 at this size. The sink is OpenGL on a
// measurement against Vulkan (the fourth comment).
//
// THE WATCHDOG. A picture lost with a full frame's worth of packets freezes the stream: the
// depayloader asks once for a fresh full picture, and that picture can lose a packet too. So while
// packets are arriving and no picture has come out for 150 ms, this asks the host over the data
// channel, and keeps asking.

#pragma once

#include <gst/gst.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "rtc.h"

class StreamFriend {
public:
    struct Settings {
        Rtc::Paths paths;
        std::string display;
        std::string xauthority;
    };

    explicit StreamFriend(Rtc::Hooks hooks, std::function<void()> askFullPicture);
    ~StreamFriend();

    bool start(const Settings& settings);
    void stop();
    Rtc* rtc() { return rtc_.get(); }

    // Whether this console can decode each codec, for the host to choose.
    static bool canDecodeH265();
    static bool canDecodeH264();

private:
    static void onPadAdded(GstElement*, GstPad* pad, gpointer self);
    static void onNewTransceiver(GstElement*, GstWebRTCRTPTransceiver* t, gpointer self);
    static gboolean onBus(GstBus*, GstMessage* m, gpointer self);
    static GstPadProbeReturn onPacket(GstPad*, GstPadProbeInfo*, gpointer self);
    static GstPadProbeReturn onPicture(GstPad*, GstPadProbeInfo*, gpointer self);
    static gboolean onWatchdog(gpointer self);

    Rtc::Hooks hooks_;
    std::function<void()> askFull_;
    GstElement* pipeline_ = nullptr;
    GstElement* webrtc_ = nullptr;
    std::unique_ptr<Rtc> rtc_;
    guint watchdog_ = 0;
    std::atomic<int64_t> lastPacketMs_{0};
    std::atomic<int64_t> lastPictureMs_{0};
    int64_t lastAskMs_ = 0;
    std::atomic<uint32_t> pictures_{0};
    uint32_t asks_ = 0;
};
