// The connection between two consoles (rtc.h).

#include "rtc.h"

#include <gst/sdp/sdp.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "jsonutil.h"

namespace {

void runOnMain(std::function<void()> fn) {
    g_main_context_invoke_full(
        nullptr, G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
            (*static_cast<std::function<void()>*>(data))();
            return G_SOURCE_REMOVE;
        },
        new std::function<void()>(std::move(fn)), [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
}

void setIfThere(GObject* o, const char* name, gboolean value) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(o), name)) g_object_set(o, name, value, nullptr);
    else std::fprintf(stderr, "[linkup] no property %s on %s\n", name, G_OBJECT_TYPE_NAME(o));
}

}  // namespace

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
    if (paths.relayOnly)
        g_object_set(webrtc_, "ice-transport-policy", GST_WEBRTC_ICE_TRANSPORT_POLICY_RELAY, nullptr);
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
    runOnMain([this, details]() { hooks_.signal(details); });
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
    runOnMain([me, details]() { me->hooks_.signal(details); });
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
    runOnMain([me, path]() { me->hooks_.connected(path); });
}

void Rtc::onConnectionState(GObject* o, GParamSpec*, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    GstWebRTCPeerConnectionState state = GST_WEBRTC_PEER_CONNECTION_STATE_NEW;
    g_object_get(o, "connection-state", &state, nullptr);
    std::fprintf(stderr, "[linkup] connection state %d\n", static_cast<int>(state));
    if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED && !me->saidConnected_) {
        me->saidConnected_ = true;
        GstPromise* promise = gst_promise_new_with_change_func(onStats, self, nullptr);
        g_signal_emit_by_name(me->webrtc_, "get-stats", nullptr, promise);
    } else if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED) {
        runOnMain([me]() { me->hooks_.failed(); });
    }
}

void Rtc::onControlString(GObject*, gchar* text, gpointer self) {
    auto* me = static_cast<Rtc*>(self);
    me->heard();
    const std::string copy = text ? text : "";
    runOnMain([me, copy]() { me->hooks_.control(copy); });
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
