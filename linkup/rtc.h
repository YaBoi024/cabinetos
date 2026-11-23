// The connection between two consoles: webrtcbin, the ports it is held to, its two data channels and
// passing its connection details on. The host's and the friend's pipelines (stream_host.cpp,
// stream_friend.cpp) are built around this.
//
// ICE IS HELD TO LINKUP'S FIXED PORTS, with ICE over TCP off and libnice's own UPnP off (measured,
// #654's second comment: libnice asks a router for a permanent lease on every local address, and
// leaves them behind when it dies). Those ports are the ones the firewall lock opens.
//
// A CABLE CARRIES THE GAME WHEN ONE IS CONNECTED: ICE gathers on its addresses alone, on the host's
// side and the friend's. With none, it gathers on every link on a device of its own, so a tunnel such
// as Tailscale's is left out. By the kind of link, and never by its name.
//
// TWO DATA CHANNELS, both made by the host. "control" is ordered and reliable, for what must arrive:
// a pause, a leave. "fast" is unordered with no retries, for a pad's state and the heartbeat: a later
// one replaces a lost one, and a retry would only be late.

#pragma once

#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class Rtc {
public:
    struct Hooks {
        std::function<void(const std::string& details)> signal;   // connection details to pass on
        std::function<void(const std::string& path)> connected;   // "direct" or "relay"
        std::function<void()> failed;
        std::function<void(const std::string& text)> control;     // main thread
        std::function<void(const uint8_t* data, size_t len)> fast;   // any thread
        std::function<void()> controlOpen;   // main thread: a word sent on "control" before this was lost
    };
    struct Paths {
        std::string stun;   // host:port, or empty
        std::string turn;   // turn://user:password@host:port, or empty
    };

    // The ports LinkUp's firewall table opens: nothing else on the console uses them.
    static constexpr unsigned kPortLow = 47710;
    static constexpr unsigned kPortHigh = 47714;

    Rtc(GstElement* webrtcbin, bool offerer, Hooks hooks);
    ~Rtc();

    void configure(const Paths& paths);
    // The host's two channels, before the pipeline plays.
    void makeChannels();
    // Connection details from the other console.
    void onSignal(const std::string& kind, const std::string& text, unsigned mline);

    void sendControl(const std::string& text);
    void sendFast(const void* data, size_t len);

    // Since anything was last heard from the other console on this connection, once any has been.
    int64_t quietMs() const;
    bool everHeard() const { return lastHeardMs_.load() != 0; }

    static int64_t nowMs();
    static void onMain(std::function<void()> fn);

private:
    // Only what arrives marks the other console heard.
    void heard();
    static void onNegotiationNeeded(GstElement* w, gpointer self);
    static void onIceCandidate(GstElement* w, guint mline, gchar* candidate, gpointer self);
    void reportEverySecond();
    static void onConnectionState(GObject* o, GParamSpec*, gpointer self);
    static void onDataChannel(GstElement* w, GObject* channel, gpointer self);
    static void onOfferCreated(GstPromise* promise, gpointer self);
    static void onAnswerCreated(GstPromise* promise, gpointer self);
    static void onStats(GstPromise* promise, gpointer self);
    static void onControlString(GObject*, gchar* text, gpointer self);
    static void onControlOpen(GObject*, gpointer self);
    static void onFastData(GObject*, GBytes* data, gpointer self);
    void attachChannel(GObject* channel);
    void sendDescription(const char* kind, GstWebRTCSessionDescription* d);

    GstElement* webrtc_;
    bool offerer_;
    Hooks hooks_;
    GObject* control_ = nullptr;
    GObject* fast_ = nullptr;
    bool saidConnected_ = false;
    std::atomic<int64_t> lastHeardMs_{0};
};
