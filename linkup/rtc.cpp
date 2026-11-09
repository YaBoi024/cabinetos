// The connection between two consoles (rtc.h).

#include "rtc.h"

#include <gst/sdp/sdp.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#include "jsonutil.h"

namespace {

void setIfThere(GObject* o, const char* name, gboolean value) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(o), name)) g_object_set(o, name, value, nullptr);
    else std::fprintf(stderr, "[linkup] no property %s on %s\n", name, G_OBJECT_TYPE_NAME(o));
}

bool exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

// A link on a device of its own: a cable, a Wi-Fi card, a USB adapter. A tunnel has none (Tailscale's
// and WireGuard's are ARPHRD_NONE, with no hardware address), nor has a bridge or a container's link,
// and none of those reaches a friend.
bool onDevice(const std::string& link) {
    return exists("/sys/class/net/" + link + "/device");
}

// A cable: an Ethernet link on a device of its own, which no Wi-Fi card is.
bool wired(const std::string& link) {
    const std::string at = "/sys/class/net/" + link;
    if (exists(at + "/wireless") || exists(at + "/phy80211") || !onDevice(link)) return false;
    std::ifstream type(at + "/type");
    int arphrd = 0;
    return (type >> arphrd) && arphrd == 1;   // ARPHRD_ETHER
}

// The links a default route leaves by, IPv4 or IPv6: a link that reaches past its own network.
std::set<std::string> linksOut() {
    std::set<std::string> out;
    std::ifstream v4("/proc/net/route");
    std::string line;
    std::getline(v4, line);   // the heading
    while (std::getline(v4, line)) {
        std::istringstream f(line);
        std::string link, destination, gateway, flags, refs, use, metric, mask;
        if ((f >> link >> destination >> gateway >> flags >> refs >> use >> metric >> mask) &&
            destination == "00000000" && mask == "00000000")
            out.insert(link);
    }
    std::ifstream v6("/proc/net/ipv6_route");
    while (std::getline(v6, line)) {
        std::istringstream f(line);
        std::string destination, length, source, sourceLength, next, metric, refs, use, flags, link;
        if (!(f >> destination >> length >> source >> sourceLength >> next >> metric >> refs >> use >> flags >> link)) continue;
        constexpr unsigned long kReject = 0x0200;   // RTF_REJECT: an unreachable route, which goes nowhere
        if (destination == std::string(32, '0') && length == "00" && !(std::stoul(flags, nullptr, 16) & kReject))
            out.insert(link);
    }
    return out;
}

// The addresses of every link that is connected and `fits`, written as libnice writes the ones it
// finds itself (getnameinfo, a link-local one with its %link), and the links they are on.
std::vector<std::string> addressesOn(const std::function<bool(const std::string&)>& fits, std::set<std::string>* links) {
    std::vector<std::string> addresses;
    ifaddrs* all = nullptr;
    if (getifaddrs(&all) != 0) return addresses;
    for (const ifaddrs* a = all; a; a = a->ifa_next) {
        if (!a->ifa_addr || (a->ifa_addr->sa_family != AF_INET && a->ifa_addr->sa_family != AF_INET6)) continue;
        if (!(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_RUNNING) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        if (!fits(a->ifa_name)) continue;
        char text[NI_MAXHOST];
        const socklen_t size = a->ifa_addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        if (getnameinfo(a->ifa_addr, size, text, sizeof text, nullptr, 0, NI_NUMERICHOST) != 0) continue;
        addresses.emplace_back(text);
        links->insert(a->ifa_name);
    }
    freeifaddrs(all);
    return addresses;
}

}  // namespace

// On the main loop's thread, or not at all. GLib's own g_main_context_invoke runs a function on the
// thread that asked when no loop owns the context, as while shutting down, and that stopped a stream
// from two threads at once (a crash in Capture::stop, #654).
void Rtc::onMain(std::function<void()> fn) {
    if (g_main_context_is_owner(nullptr)) {
        fn();
        return;
    }
    GSource* source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
            (*static_cast<std::function<void()>*>(data))();
            return G_SOURCE_REMOVE;
        },
        new std::function<void()>(std::move(fn)), [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
    g_source_attach(source, nullptr);
    g_source_unref(source);
}

int64_t Rtc::nowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

Rtc::Rtc(GstElement* webrtcbin, bool offerer, Hooks hooks) : webrtc_(webrtcbin), offerer_(offerer), hooks_(std::move(hooks)) {
    g_signal_connect(webrtc_, "on-ice-candidate", G_CALLBACK(onIceCandidate), this);
    g_signal_connect(webrtc_, "notify::connection-state", G_CALLBACK(onConnectionState), this);
    g_signal_connect(webrtc_, "on-data-channel", G_CALLBACK(onDataChannel), this);
    if (offerer_) g_signal_connect(webrtc_, "on-negotiation-needed", G_CALLBACK(onNegotiationNeeded), this);

    GObject* ice = nullptr;
    g_object_get(webrtc_, "ice-agent", &ice, nullptr);
    if (ice) {
        g_object_set(ice, "min-rtp-port", kPortLow, "max-rtp-port", kPortHigh, nullptr);
        setIfThere(ice, "ice-tcp", FALSE);
        GObject* agent = nullptr;
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(ice), "agent")) g_object_get(ice, "agent", &agent, nullptr);
        if (agent) {
            setIfThere(agent, "upnp", FALSE);
            g_object_unref(agent);
        } else {
            std::fprintf(stderr, "[linkup] the ICE agent could not be reached; UPnP is its default\n");
        }
        // A CABLE, WHEN ONE IS CONNECTED, CARRIES THE GAME. libnice ranks a console's addresses by where
        // each comes in the system's list of them, not by what kind of link it is on, so with a cable and
        // Wi-Fi both connected it chose the Wi-Fi on one run and the cable on others (#654, a packet
        // capture on the friend's console). Given the cable's addresses, it gathers on those alone, its
        // way to the server and to the relay included. A cable that reaches no further than its own
        // network counts as none.
        // WITH NO CABLE, EVERY LINK ON A DEVICE OF ITS OWN. Left to itself libnice also gathers on a
        // tunnel: on a console with Tailscale up it sent STUN from its Tailscale address and offered that
        // address to the friend (#654, a packet capture on the reference console). A console with no such
        // link at all leaves the choice to libnice.
        std::set<std::string> links;
        const std::set<std::string> out = linksOut();
        std::vector<std::string> mine = addressesOn([&](const std::string& link) { return out.count(link) && wired(link); }, &links);
        const bool cable = !mine.empty();
        if (!cable) mine = addressesOn(onDevice, &links);
        for (const std::string& address : mine) {
            gboolean added = FALSE;
            g_signal_emit_by_name(ice, "add-local-ip-address", address.c_str(), &added);
            if (!added) std::fprintf(stderr, "[linkup] the ICE agent refused %s\n", address.c_str());
        }
        if (!mine.empty()) {
            std::string named;
            for (const std::string& link : links) named += (named.empty() ? "" : ", ") + link;
            std::fprintf(stderr, "[linkup] %s: %s, %zu addresses\n", cable ? "on the cable" : "on", named.c_str(), mine.size());
        }
        g_object_unref(ice);
    }
}

Rtc::~Rtc() {
    g_signal_handlers_disconnect_by_data(webrtc_, this);
    if (control_) {
        g_signal_handlers_disconnect_by_data(control_, this);
        g_object_unref(control_);
    }
    if (fast_) {
        g_signal_handlers_disconnect_by_data(fast_, this);
        g_object_unref(fast_);
    }
}

void Rtc::configure(const Paths& paths) {
    if (!paths.stun.empty()) {
        const std::string uri = "stun://" + paths.stun;
        g_object_set(webrtc_, "stun-server", uri.c_str(), nullptr);
    }
    if (!paths.turn.empty()) {
        gboolean ok = FALSE;
        g_signal_emit_by_name(webrtc_, "add-turn-server", paths.turn.c_str(), &ok);
        if (!ok) std::fprintf(stderr, "[linkup] webrtcbin refused the relay\n");
    }
}

void Rtc::makeChannels() {
    GstStructure* reliable = gst_structure_new_empty("control");
    GObject* control = nullptr;
    g_signal_emit_by_name(webrtc_, "create-data-channel", "control", reliable, &control);
    gst_structure_free(reliable);
    GstStructure* lossy = gst_structure_new("fast", "ordered", G_TYPE_BOOLEAN, FALSE, "max-retransmits", G_TYPE_INT, 0, nullptr);
    GObject* fast = nullptr;
    g_signal_emit_by_name(webrtc_, "create-data-channel", "fast", lossy, &fast);
    gst_structure_free(lossy);
    if (control) attachChannel(control);
    if (fast) attachChannel(fast);
    if (control) g_object_unref(control);
    if (fast) g_object_unref(fast);
}

void Rtc::attachChannel(GObject* channel) {
    gchar* label = nullptr;
    g_object_get(channel, "label", &label, nullptr);
    if (label && std::strcmp(label, "control") == 0) {
        control_ = G_OBJECT(g_object_ref(channel));
        g_signal_connect(channel, "on-message-string", G_CALLBACK(onControlString), this);
    } else if (label && std::strcmp(label, "fast") == 0) {
        fast_ = G_OBJECT(g_object_ref(channel));
        g_signal_connect(channel, "on-message-data", G_CALLBACK(onFastData), this);
    }
    g_free(label);
}

void Rtc::onDataChannel(GstElement*, GObject* channel, gpointer self) {
    static_cast<Rtc*>(self)->attachChannel(channel);
}

void Rtc::onNegotiationNeeded(GstElement* w, gpointer self) {
    GstPromise* promise = gst_promise_new_with_change_func(onOfferCreated, self, nullptr);
    g_signal_emit_by_name(w, "create-offer", nullptr, promise);
}

void Rtc::sendDescription(const char* kind, GstWebRTCSessionDescription* d) {
    gchar* text = gst_sdp_message_as_text(d->sdp);
    json_object* o = json_object_new_object();
    jsonutil::set(o, "kind", kind);
    jsonutil::set(o, "sdp", text);
    g_free(text);
    const std::string details = jsonutil::text(o);
    json_object_put(o);
    Rtc::onMain([this, details]() { hooks_.signal(details); });
}

void Rtc::onOfferCreated(GstPromise* promise, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* d = nullptr;
    if (reply) gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &d, nullptr);
    gst_promise_unref(promise);
    if (!d) return;
    g_signal_emit_by_name(me->webrtc_, "set-local-description", d, nullptr);
    me->sendDescription("offer", d);
    gst_webrtc_session_description_free(d);
}

void Rtc::onAnswerCreated(GstPromise* promise, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* d = nullptr;
    if (reply) gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &d, nullptr);
    gst_promise_unref(promise);
    if (!d) return;
    g_signal_emit_by_name(me->webrtc_, "set-local-description", d, nullptr);
    me->sendDescription("answer", d);
    gst_webrtc_session_description_free(d);
}

void Rtc::onIceCandidate(GstElement*, guint mline, gchar* candidate, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    json_object* o = json_object_new_object();
    jsonutil::set(o, "ice", candidate);
    jsonutil::set(o, "m", static_cast<int>(mline));
    const std::string details = jsonutil::text(o);
    json_object_put(o);
    Rtc::onMain([me, details]() { me->hooks_.signal(details); });
}

void Rtc::onSignal(const std::string& kind, const std::string& text, unsigned mline) {
    if (kind == "ice") {
        g_signal_emit_by_name(webrtc_, "add-ice-candidate", mline, text.c_str());
        return;
    }
    GstSDPMessage* message = nullptr;
    if (gst_sdp_message_new_from_text(text.c_str(), &message) != GST_SDP_OK) return;
    GstWebRTCSessionDescription* d = gst_webrtc_session_description_new(
        kind == "offer" ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, message);
    g_signal_emit_by_name(webrtc_, "set-remote-description", d, nullptr);
    gst_webrtc_session_description_free(d);
    if (kind == "offer") {
        GstPromise* promise = gst_promise_new_with_change_func(onAnswerCreated, this, nullptr);
        g_signal_emit_by_name(webrtc_, "create-answer", nullptr, promise);
    }
}

// Direct or relay goes to the log only, and to the server's count of how games connected.
void Rtc::onStats(GstPromise* promise, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    const GstStructure* reply = gst_promise_get_reply(promise);
    std::string path = "direct";
    if (reply) {
        const int fields = gst_structure_n_fields(reply);
        std::string localId;
        for (int i = 0; i < fields; ++i) {
            const GValue* value = gst_structure_get_value(reply, gst_structure_nth_field_name(reply, i));
            if (!GST_VALUE_HOLDS_STRUCTURE(value)) continue;
            const GstStructure* s = gst_value_get_structure(value);
            GstWebRTCStatsType type = GST_WEBRTC_STATS_CODEC;
            if (!gst_structure_get(s, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type, nullptr)) continue;
            if (type == GST_WEBRTC_STATS_CANDIDATE_PAIR) {
                if (const gchar* id = gst_structure_get_string(s, "local-candidate-id")) localId = id;
            }
        }
        if (!localId.empty()) {
            const GValue* value = gst_structure_get_value(reply, localId.c_str());
            if (value && GST_VALUE_HOLDS_STRUCTURE(value)) {
                const gchar* kind = gst_structure_get_string(gst_value_get_structure(value), "candidate-type");
                if (kind && std::strcmp(kind, "relay") == 0) path = "relay";
            }
        }
    }
    gst_promise_unref(promise);
    Rtc::onMain([me, path]() { me->hooks_.connected(path); });
}

// The bitrate's steps work from what the friend's console reports. RTCP's own arithmetic shares a very
// small bandwidth, and on a low bitrate with some loss the reports came over half a minute apart
// (measured, #654), which is no use for falling fast. So they are allowed a little more, once a second.
void Rtc::reportEverySecond() {
    GstElement* rtpbin = gst_bin_get_by_name(GST_BIN(webrtc_), "rtpbin");
    if (!rtpbin) return;
    GObject* session = nullptr;
    g_signal_emit_by_name(rtpbin, "get-internal-session", 0u, &session);
    if (session) {
        constexpr int kReportBitsPerSecond = 100000;
        g_object_set(session, "rtcp-min-interval", static_cast<guint64>(GST_SECOND), "rtcp-rr-bandwidth", kReportBitsPerSecond,
                     "rtcp-rs-bandwidth", kReportBitsPerSecond, nullptr);
        g_object_unref(session);
    }
    gst_object_unref(rtpbin);
}

void Rtc::onConnectionState(GObject* o, GParamSpec*, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    GstWebRTCPeerConnectionState state = GST_WEBRTC_PEER_CONNECTION_STATE_NEW;
    g_object_get(o, "connection-state", &state, nullptr);
    std::fprintf(stderr, "[linkup] connection state %d\n", static_cast<int>(state));
    if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING || state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED)
        me->reportEverySecond();
    if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED && !me->saidConnected_) {
        me->saidConnected_ = true;
        GstPromise* promise = gst_promise_new_with_change_func(onStats, self, nullptr);
        g_signal_emit_by_name(me->webrtc_, "get-stats", nullptr, promise);
    } else if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED) {
        Rtc::onMain([me]() { me->hooks_.failed(); });
    }
}

void Rtc::onControlString(GObject*, gchar* text, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    me->heard();
    const std::string copy = text ? text : "";
    Rtc::onMain([me, copy]() { me->hooks_.control(copy); });
}

void Rtc::onFastData(GObject*, GBytes* data, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    me->heard();
    gsize len = 0;
    const auto* bytes = static_cast<const uint8_t*>(g_bytes_get_data(data, &len));
    if (bytes && len > 0) me->hooks_.fast(bytes, len);
}

void Rtc::sendControl(const std::string& text) {
    if (!control_) return;
    GstWebRTCDataChannelState state = GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED;
    g_object_get(control_, "ready-state", &state, nullptr);
    if (state == GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) g_signal_emit_by_name(control_, "send-string", text.c_str());
}

void Rtc::sendFast(const void* data, size_t len) {
    if (!fast_) return;
    GstWebRTCDataChannelState state = GST_WEBRTC_DATA_CHANNEL_STATE_CLOSED;
    g_object_get(fast_, "ready-state", &state, nullptr);
    if (state != GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) return;
    GBytes* bytes = g_bytes_new(data, len);
    g_signal_emit_by_name(fast_, "send-data", bytes);
    g_bytes_unref(bytes);
}

int64_t Rtc::quietMs() const {
    const int64_t last = lastHeardMs_.load();
    return last == 0 ? 0 : nowMs() - last;
}

void Rtc::heard() { lastHeardMs_ = nowMs(); }
