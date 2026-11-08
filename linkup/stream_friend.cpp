// The friend's side of a game (stream_friend.h).

#include "stream_friend.h"

#include <gst/webrtc/webrtc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int64_t kNoPictureMs = 150;
constexpr guint kSoundCushionMs = 60;   // stream_friend.h, "The sound is played as it arrives"

bool hasElement(const char* name) {
    GstElementFactory* f = gst_element_factory_find(name);
    if (!f) return false;
    gst_object_unref(f);
    return true;
}

// The decoder for a codec on this console: the graphics chip's, and a program's for a console
// with none, which only H.264 has on the image.
std::string decoderFor(bool h265) {
    if (h265) return hasElement("vah265dec") ? "vah265dec" : "";
    if (hasElement("vah264dec")) return "vah264dec";
    if (hasElement("avdec_h264")) return "avdec_h264";
    if (hasElement("openh264dec")) return "openh264dec";
    return "";
}

}  // namespace

bool StreamFriend::canDecodeH265() { return !decoderFor(true).empty(); }
bool StreamFriend::canDecodeH264() { return !decoderFor(false).empty(); }

StreamFriend::StreamFriend(Rtc::Hooks hooks, std::function<void()> askFullPicture)
    : hooks_(std::move(hooks)), askFull_(std::move(askFullPicture)) {}

StreamFriend::~StreamFriend() { stop(); }

gboolean StreamFriend::onBus(GstBus*, GstMessage* m, gpointer) {
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_WARNING) {
        GError* e = nullptr;
        gst_message_parse_warning(m, &e, nullptr);
        std::fprintf(stderr, "[linkup] friend: %s: %s\n", GST_OBJECT_NAME(m->src), e ? e->message : "");
        g_clear_error(&e);
        return TRUE;
    }
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
        GError* e = nullptr;
        gst_message_parse_error(m, &e, nullptr);
        std::fprintf(stderr, "[linkup] friend: %s: %s\n", GST_OBJECT_NAME(m->src), e ? e->message : "");
        g_clear_error(&e);
    }
    return TRUE;
}

GstPadProbeReturn StreamFriend::onPacket(GstPad*, GstPadProbeInfo*, gpointer self) {
    static_cast<StreamFriend*>(self)->lastPacketMs_ = Rtc::nowMs();
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn StreamFriend::onPicture(GstPad*, GstPadProbeInfo*, gpointer self) {
    auto* me = static_cast<StreamFriend*>(self);
    me->lastPictureMs_ = Rtc::nowMs();
    ++me->pictures_;
    return GST_PAD_PROBE_OK;
}

void StreamFriend::onPadAdded(GstElement*, GstPad* pad, gpointer self) {
    auto* me = static_cast<StreamFriend*>(self);
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media = gst_structure_get_string(s, "media");
    const gchar* encoding = gst_structure_get_string(s, "encoding-name");
    const bool video = media && std::strcmp(media, "video") == 0;
    const bool h265 = encoding && std::strcmp(encoding, "H265") == 0;
    std::string description;
    if (video) {
        const std::string decoder = decoderFor(h265);
        if (decoder.empty()) {
            std::fprintf(stderr, "[linkup] friend: no decoder for that picture\n");
            gst_caps_unref(caps);
            return;
        }
        description = std::string(h265 ? "rtph265depay name=depay request-keyframe=true wait-for-keyframe=true ! h265parse ! "
                                       : "rtph264depay name=depay request-keyframe=true wait-for-keyframe=true ! h264parse ! ") +
                      decoder + " name=dec ! capssetter caps=video/x-raw,colorimetry=(string)bt601 ! "
                                "queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! glimagesink sync=false";
    } else {
        guint ssrc = 0;
        if (gst_structure_get_uint(s, "ssrc", &ssrc)) me->cushionSound(ssrc);
        else std::fprintf(stderr, "[linkup] friend: the sound came with no SSRC; its cushion stays at the picture's\n");
        description = "rtpopusdepay ! opusdec plc=true ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! audioconvert ! "
                      "pulsesink async=false sync=false buffer-time=60000 latency-time=10000 "
                      "client-name=LinkUp stream-properties=\"props,media.role=(string)game\"";
    }
    gst_caps_unref(caps);
    GError* error = nullptr;
    GstElement* bin = gst_parse_bin_from_description(description.c_str(), TRUE, &error);
    if (!bin) {
        std::fprintf(stderr, "[linkup] friend: %s\n", error ? error->message : "");
        g_clear_error(&error);
        return;
    }
    gst_bin_add(GST_BIN(me->pipeline_), bin);
    gst_element_sync_state_with_parent(bin);
    GstPad* sink = gst_element_get_static_pad(bin, "sink");
    if (gst_pad_link(pad, sink) != GST_PAD_LINK_OK) std::fprintf(stderr, "[linkup] friend: could not link the %s\n", video ? "picture" : "sound");
    gst_object_unref(sink);
    if (video) {
        if (GstElement* depay = gst_bin_get_by_name(GST_BIN(bin), "depay")) {
            GstPad* in = gst_element_get_static_pad(depay, "sink");
            gst_pad_add_probe(in, GST_PAD_PROBE_TYPE_BUFFER, onPacket, me, nullptr);
            gst_object_unref(in);
            gst_object_unref(depay);
        }
        if (GstElement* dec = gst_bin_get_by_name(GST_BIN(bin), "dec")) {
            GstPad* out = gst_element_get_static_pad(dec, "src");
            gst_pad_add_probe(out, GST_PAD_PROBE_TYPE_BUFFER, onPicture, me, nullptr);
            gst_object_unref(out);
            gst_object_unref(dec);
        }
    }
}

void StreamFriend::onNewTransceiver(GstElement*, GstWebRTCRTPTransceiver* t, gpointer) {
    g_object_set(t, "do-nack", TRUE, nullptr);
}

// Every stream gets a jitter buffer as its first packet arrives, before its pad: kept by SSRC until the
// pad says which one is the sound.
void StreamFriend::onNewJitterbuffer(GstElement*, GstElement* jitterbuffer, guint, guint ssrc, gpointer self) {
    auto* me = static_cast<StreamFriend*>(self);
    std::lock_guard<std::mutex> lk(me->jitterMu_);
    auto it = me->jitterbuffers_.find(ssrc);
    if (it != me->jitterbuffers_.end()) gst_object_unref(it->second);
    me->jitterbuffers_[ssrc] = GST_ELEMENT(gst_object_ref(jitterbuffer));
}

void StreamFriend::cushionSound(guint ssrc) {
    std::lock_guard<std::mutex> lk(jitterMu_);
    auto it = jitterbuffers_.find(ssrc);
    if (it == jitterbuffers_.end()) {
        std::fprintf(stderr, "[linkup] friend: no jitter buffer for the sound; its cushion stays at the picture's\n");
        return;
    }
    // Lost is said downstream, so the decoder fills the hole in.
    g_object_set(it->second, "latency", kSoundCushionMs, "do-lost", TRUE, nullptr);
    std::fprintf(stderr, "[linkup] friend: the sound waits up to %u ms for a missing piece\n", kSoundCushionMs);
}

gboolean StreamFriend::onWatchdog(gpointer self) {
    auto* me = static_cast<StreamFriend*>(self);
    const int64_t now = Rtc::nowMs();
    const int64_t packet = me->lastPacketMs_.load();
    const int64_t picture = me->lastPictureMs_.load();
    // Packets are arriving and nothing is coming out. Quiet from the host is not this: it may have
    // stopped sending because something else is on its screen.
    if (packet != 0 && now - packet < kNoPictureMs && now - picture > kNoPictureMs && now - me->lastAskMs_ > kNoPictureMs) {
        me->lastAskMs_ = now;
        ++me->asks_;
        if (me->askFull_) me->askFull_();
    }
    return G_SOURCE_CONTINUE;
}

bool StreamFriend::start(const Settings& settings) {
    if (!settings.display.empty()) setenv("DISPLAY", settings.display.c_str(), 1);
    if (!settings.xauthority.empty()) setenv("XAUTHORITY", settings.xauthority.c_str(), 1);
    pipeline_ = gst_pipeline_new("friend");
    webrtc_ = gst_element_factory_make("webrtcbin", "w");
    if (!webrtc_) {
        std::fprintf(stderr, "[linkup] friend: no webrtcbin\n");
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }
    g_object_set(webrtc_, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, "latency", 100u, nullptr);
    gst_bin_add(GST_BIN(pipeline_), webrtc_);
    rtpbin_ = gst_bin_get_by_name(GST_BIN(webrtc_), "rtpbin");
    if (rtpbin_) g_signal_connect(rtpbin_, "new-jitterbuffer", G_CALLBACK(onNewJitterbuffer), this);
    gst_bus_add_watch(GST_ELEMENT_BUS(pipeline_), onBus, this);
    rtc_ = std::make_unique<Rtc>(webrtc_, /*offerer=*/false, hooks_);
    rtc_->configure(settings.paths);
    g_signal_connect(webrtc_, "pad-added", G_CALLBACK(onPadAdded), this);
    g_signal_connect(webrtc_, "on-new-transceiver", G_CALLBACK(onNewTransceiver), this);
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    lastPacketMs_ = 0;
    lastPictureMs_ = Rtc::nowMs();
    watchdog_ = g_timeout_add(20, onWatchdog, this);
    return true;
}

void StreamFriend::stop() {
    if (watchdog_) g_source_remove(watchdog_);
    watchdog_ = 0;
    if (!pipeline_) return;
    std::fprintf(stderr, "[linkup] friend: %u pictures decoded, asked for a full one %u times\n", pictures_.load(), asks_);
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    g_signal_handlers_disconnect_by_data(webrtc_, this);
    if (rtpbin_) {
        g_signal_handlers_disconnect_by_data(rtpbin_, this);
        gst_object_unref(rtpbin_);
        rtpbin_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(jitterMu_);
        for (auto& [ssrc, jb] : jitterbuffers_) gst_object_unref(jb);
        jitterbuffers_.clear();
    }
    rtc_.reset();
    gst_bus_remove_watch(GST_ELEMENT_BUS(pipeline_));
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    webrtc_ = nullptr;
}
