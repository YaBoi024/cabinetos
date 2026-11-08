// The host's side of a game (stream_host.h).

#include "stream_host.h"

#include <algorithm>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kFillerH265 = 38;
constexpr int kFillerH264 = 12;

bool hasElement(const char* name) {
    GstElementFactory* f = gst_element_factory_find(name);
    if (!f) return false;
    gst_object_unref(f);
    return true;
}

}  // namespace

bool StreamHost::canEncodeH265() { return hasElement("vah265enc"); }
bool StreamHost::canEncodeH264() { return hasElement("vah264enc"); }

StreamHost::StreamHost(Rtc::Hooks hooks) : hooks_(std::move(hooks)) {}

StreamHost::~StreamHost() { stop(); }

// A steady bitrate with a floor fills what it does not need with filler data, over a third of the
// bytes on a race. Dropped here: the picture decodes the same (md5 checked, #654).
GstPadProbeReturn StreamHost::dropFiller(GstPad*, GstPadProbeInfo* info, gpointer fillerType) {
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!b) return GST_PAD_PROBE_OK;
    const int filler = GPOINTER_TO_INT(fillerType);
    GstMapInfo m;
    if (!gst_buffer_map(b, &m, GST_MAP_READ)) return GST_PAD_PROBE_OK;
    std::vector<std::pair<size_t, size_t>> keep;
    size_t i = 0;
    size_t start = static_cast<size_t>(-1);
    bool startFiller = false;
    size_t dropped = 0;
    const auto typeAt = [&](size_t p) { return filler == kFillerH265 ? (m.data[p] >> 1) & 0x3f : m.data[p] & 0x1f; };
    while (i + 3 < m.size) {
        if (m.data[i] == 0 && m.data[i + 1] == 0 && m.data[i + 2] == 1) {
            const size_t sc = (i > 0 && m.data[i - 1] == 0) ? i - 1 : i;
            if (start != static_cast<size_t>(-1)) {
                if (startFiller) dropped += sc - start;
                else keep.push_back({start, sc});
            }
            start = sc;
            startFiller = typeAt(i + 3) == filler;
            i += 3;
        } else {
            ++i;
        }
    }
    if (start != static_cast<size_t>(-1)) {
        if (startFiller) dropped += m.size - start;
        else keep.push_back({start, m.size});
    }
    if (dropped == 0) {
        gst_buffer_unmap(b, &m);
        return GST_PAD_PROBE_OK;
    }
    GstBuffer* out = gst_buffer_new_allocate(nullptr, m.size - dropped, nullptr);
    GstMapInfo mo;
    gst_buffer_map(out, &mo, GST_MAP_WRITE);
    size_t o = 0;
    for (const auto& k : keep) {
        std::memcpy(mo.data + o, m.data + k.first, k.second - k.first);
        o += k.second - k.first;
    }
    gst_buffer_unmap(out, &mo);
    gst_buffer_unmap(b, &m);
    gst_buffer_copy_into(out, b, static_cast<GstBufferCopyFlags>(GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META), 0, -1);
    gst_buffer_unref(b);
    GST_PAD_PROBE_INFO_DATA(info) = out;
    return GST_PAD_PROBE_OK;
}

gboolean StreamHost::onBus(GstBus*, GstMessage* m, gpointer) {
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
        GError* e = nullptr;
        gst_message_parse_error(m, &e, nullptr);
        std::fprintf(stderr, "[linkup] host: %s: %s\n", GST_OBJECT_NAME(m->src), e ? e->message : "");
        g_clear_error(&e);
    }
    return TRUE;
}

std::string StreamHost::encoderDescription(int kbit) const {
    const bool h265 = settings_.h265;
    // The floor differs by codec, each tuned on the same race (#654). Target usage 7 and one
    // reference frame are what brought the encode to 5.9 ms.
    return std::string(h265 ? "vah265enc" : "vah264enc") + " name=enc target-usage=7 b-frames=0 ref-frames=1 " +
           "key-int-max=600 rate-control=cbr bitrate=" + std::to_string(kbit) + (h265 ? " min-qp=28" : " min-qp=26");
}

void StreamHost::dropFillerFrom(GstElement* encoder) {
    GstPad* src = gst_element_get_static_pad(encoder, "src");
    gst_pad_add_probe(src, GST_PAD_PROBE_TYPE_BUFFER, dropFiller, GINT_TO_POINTER(settings_.h265 ? kFillerH265 : kFillerH264), nullptr);
    gst_object_unref(src);
}

// A VA ENCODER CANNOT CHANGE SIZE. It keeps the pool it made for its first frame, and the first frame
// of another size fails with "Failed to import the input frame", which ends the picture for good (a
// 5% loss test, #654). So a new size gets a new encoder, as the size passes on its way to it, in the
// one thread that feeds it. The old one's frames have all gone by then, and the new one starts with a
// full picture.
GstPadProbeReturn StreamHost::onSizeGoing(GstPad* pad, GstPadProbeInfo* info, gpointer self) {
    GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) != GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
    GstCaps* caps = nullptr;
    gst_event_parse_caps(event, &caps);
    int w = 0;
    int h = 0;
    const GstStructure* size = gst_caps_get_structure(caps, 0);
    gst_structure_get_int(size, "width", &w);
    gst_structure_get_int(size, "height", &h);
    auto* me = static_cast<StreamHost*>(self);
    const bool first = me->encoderW_ == 0;
    const bool same = w == me->encoderW_ && h == me->encoderH_;
    me->encoderW_ = w;
    me->encoderH_ = h;
    if (!first && !same && !me->closing_.load()) me->replaceEncoder(pad);
    return GST_PAD_PROBE_OK;
}

void StreamHost::replaceEncoder(GstPad* feed) {
    GError* error = nullptr;
    GstElement* fresh = gst_parse_launch(encoderDescription(kbit_.load()).c_str(), &error);
    if (!fresh) {
        std::fprintf(stderr, "[linkup] host: %s\n", error ? error->message : "no new encoder");
        g_clear_error(&error);
        return;
    }
    GstElement* old = nullptr;
    {
        std::lock_guard<std::mutex> lk(encoderMu_);
        old = encoder_;
        encoder_ = GST_ELEMENT(gst_object_ref(fresh));
    }
    GstPad* oldIn = gst_element_get_static_pad(old, "sink");
    GstPad* oldOut = gst_element_get_static_pad(old, "src");
    GstPad* next = gst_pad_get_peer(oldOut);
    gst_pad_unlink(feed, oldIn);
    gst_pad_unlink(oldOut, next);
    gst_element_set_state(old, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(pipeline_), old);
    gst_object_unref(oldIn);
    gst_object_unref(oldOut);
    gst_object_unref(old);

    gst_bin_add(GST_BIN(pipeline_), fresh);
    dropFillerFrom(fresh);
    GstPad* in = gst_element_get_static_pad(fresh, "sink");
    GstPad* out = gst_element_get_static_pad(fresh, "src");
    gst_pad_link(out, next);
    gst_element_sync_state_with_parent(fresh);
    // Linking is also what sends the stream's opening events to the new encoder, before its next frame.
    gst_pad_link(feed, in);
    gst_object_unref(in);
    gst_object_unref(out);
    gst_object_unref(next);
}

bool StreamHost::buildPipeline() {
    const bool h265 = settings_.h265;
    kbit_ = bitrate_.step().kbit;
    encoderW_ = encoderH_ = 0;
    reportHighest_ = 0;
    reportLost_ = 0;
    closing_ = false;
    const std::string encoder = encoderDescription(kbit_.load());
    const std::string payload = h265 ? "h265parse config-interval=-1 ! rtph265pay mtu=1200 config-interval=-1 aggregate-mode=zero-latency ! "
                                       "application/x-rtp,media=video,encoding-name=H265,payload=96"
                                     : "h264parse config-interval=-1 ! rtph264pay mtu=1200 config-interval=-1 aggregate-mode=zero-latency ! "
                                       "application/x-rtp,media=video,encoding-name=H264,payload=96";
    // The default output's monitor, through pipewire-pulse. Not pipewiresrc: on the mini PC it alone, in a
    // pipeline of nothing else, handed over less and less of the sound after its first 80 seconds (187
    // buffers a second falling to 80 within four minutes), where pulsesrc kept all of it; on the
    // reference console it lost none (#654). The Pulse elements are what GStreamer's and PipeWire's own
    // maintainers recommend for sound.
    const std::string device = settings_.soundTarget.empty() ? "@DEFAULT_MONITOR@" : settings_.soundTarget + ".monitor";
    const std::string sound =
        "pulsesrc name=sound device=\"" + device + "\" buffer-time=40000 latency-time=10000 client-name=LinkUp ! "
        // The capture's thread only captures. webrtcbin pushes the sound and the picture through one lock
        // (rtpfunnel), so without this queue a burst of the picture's packets would hold the capture up.
        "queue max-size-buffers=0 max-size-bytes=0 max-size-time=200000000 leaky=downstream ! "
        "audio/x-raw,format=S16LE,rate=48000,channels=2 ! audioconvert ! audioresample ! "
        // The encoder counts samples for the RTP timestamps, so they are as steady as the sound itself.
        "opusenc frame-size=10 bitrate=128000 bitrate-type=cbr audio-type=restricted-lowdelay perfect-timestamp=true ! "
        "rtpopuspay pt=97 ! "
        "application/x-rtp,media=audio,encoding-name=OPUS,payload=97,clock-rate=48000 ! "
        // After the encoder, which says what it makes only once it has made some, and webrtcbin makes no
        // offer until it knows: the caps pass this valve while no sound does.
        "valve name=mute drop=true drop-mode=forward-sticky-events";
    const std::string description =
        "webrtcbin name=w bundle-policy=max-bundle latency=100 "
        "appsrc name=cap is-live=true format=time do-timestamp=false max-bytes=0 max-buffers=2 leaky-type=downstream block=false ! " +
        encoder + " ! " + payload + " ! w. " + sound + " ! w.";
    GError* error = nullptr;
    pipeline_ = gst_parse_launch(description.c_str(), &error);
    if (!pipeline_) {
        std::fprintf(stderr, "[linkup] host: %s\n", error ? error->message : "could not build the pipeline");
        g_clear_error(&error);
        return false;
    }
    webrtc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "w");
    appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "cap");
    encoder_ = gst_bin_get_by_name(GST_BIN(pipeline_), "enc");
    mute_ = gst_bin_get_by_name(GST_BIN(pipeline_), "mute");
    dropFillerFrom(encoder_);
    GstPad* feed = gst_element_get_static_pad(appsrc_, "src");
    gst_pad_add_probe(feed, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, onSizeGoing, this, nullptr);
    gst_object_unref(feed);
    gst_bus_add_watch(GST_ELEMENT_BUS(pipeline_), onBus, this);
    g_object_set(mute_, "drop", pictureOn_.load() ? FALSE : TRUE, nullptr);

    rtc_ = std::make_unique<Rtc>(webrtc_, /*offerer=*/true, hooks_);
    rtc_->configure(settings_.paths);

    gst_element_set_state(pipeline_, GST_STATE_READY);
    GArray* transceivers = nullptr;
    g_signal_emit_by_name(webrtc_, "get-transceivers", &transceivers);
    for (guint i = 0; transceivers && i < transceivers->len; ++i) {
        GstWebRTCRTPTransceiver* t = g_array_index(transceivers, GstWebRTCRTPTransceiver*, i);
        g_object_set(t, "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY, "do-nack", TRUE, nullptr);
    }
    if (transceivers) g_array_unref(transceivers);
    rtc_->makeChannels();
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    capsW_ = capsH_ = 0;
    // webrtcbin makes no offer until the picture's caps reach it, and those come from the encoder's first
    // frame, and the real picture stays off until the game is on the screen. So until the two consoles
    // are connected a black frame goes in now and then. Nothing leaves before they are, and the
    // friend's console says Waiting over its first picture.
    primeWith_ = 0;
    if (!primeSource_) primeSource_ = g_timeout_add(100, onPrime, this);
    return true;
}

gboolean StreamHost::onPrime(gpointer self) {
    auto* me = static_cast<StreamHost*>(self);
    if (++me->primeWith_ > 100) {
        me->primeSource_ = 0;
        return G_SOURCE_REMOVE;
    }
    me->pushBlack();
    return G_SOURCE_CONTINUE;
}

void StreamHost::stopPriming() {
    if (primeSource_) g_source_remove(primeSource_);
    primeSource_ = 0;
}

void StreamHost::pushBlack() {
    const int w = settings_.width;
    const int h = settings_.height;
    const size_t ySize = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<uint8_t> frame(ySize * 3 / 2, 128);
    std::memset(frame.data(), 16, ySize);
    std::lock_guard<std::mutex> lk(frameMu_);
    if (appsrc_) pushLocked(frame.data(), w, frame.data() + ySize, w, w, h);
}

void StreamHost::destroyPipeline() {
    std::lock_guard<std::mutex> lk(frameMu_);
    if (!pipeline_) return;
    closing_ = true;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    rtc_.reset();
    gst_bus_remove_watch(GST_ELEMENT_BUS(pipeline_));
    if (mute_) gst_object_unref(mute_);
    if (encoder_) gst_object_unref(encoder_);
    if (appsrc_) gst_object_unref(appsrc_);
    if (webrtc_) gst_object_unref(webrtc_);
    gst_object_unref(pipeline_);
    pipeline_ = webrtc_ = appsrc_ = encoder_ = mute_ = nullptr;
}

bool StreamHost::start(const Settings& settings) {
    settings_ = settings;
    bitrate_.reset(settings.width, settings.height);
    if (!buildPipeline()) return false;
    if (!capture_.start(settings.node, settings.width, settings.height,
                        [this](const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h) { onFrame(y, ys, uv, uvs, w, h); })) {
        std::fprintf(stderr, "[linkup] host: the capture would not start\n");
        destroyPipeline();
        return false;
    }
    capture_.setActive(pictureOn_.load());
    return true;
}

bool StreamHost::reconnect() {
    stopPriming();
    destroyPipeline();
    return buildPipeline();
}

void StreamHost::stop() {
    stopPriming();
    capture_.stop();
    destroyPipeline();
    std::fprintf(stderr, "[linkup] host: %u frames sent to the encoder, at bitrate step %d of %d\n", framesIn_, bitrate_.stepIndex() + 1,
                 bitrate_.stepCount());
}

void StreamHost::onFrame(const uint8_t* y, int yStride, const uint8_t* uv, int uvStride, int w, int h) {
    std::lock_guard<std::mutex> lk(frameMu_);
    if (!appsrc_ || !pictureOn_.load()) return;
    pushLocked(y, yStride, uv, uvStride, w, h);
}

void StreamHost::pushLocked(const uint8_t* y, int yStride, const uint8_t* uv, int uvStride, int w, int h) {
    if (w != capsW_ || h != capsH_) {
        // gamescope states no frame rate, and the encoder took that for 30 and sent twice the
        // bitrate asked for, so one is stated here. Its NV12 is BT.601 limited.
        GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT, w, "height", G_TYPE_INT, h,
                                            "framerate", GST_TYPE_FRACTION, 60, 1, "colorimetry", G_TYPE_STRING, "bt601",
                                            "interlace-mode", G_TYPE_STRING, "progressive", nullptr);
        gst_app_src_set_caps(GST_APP_SRC(appsrc_), caps);
        gst_caps_unref(caps);
        capsW_ = w;
        capsH_ = h;
    }
    ++framesIn_;
    const size_t ySize = static_cast<size_t>(w) * static_cast<size_t>(h);
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, ySize * 3 / 2, nullptr);
    GstMapInfo m;
    if (!gst_buffer_map(buf, &m, GST_MAP_WRITE)) {
        gst_buffer_unref(buf);
        return;
    }
    for (int r = 0; r < h; ++r) std::memcpy(m.data + static_cast<size_t>(r) * static_cast<size_t>(w), y + static_cast<size_t>(r) * static_cast<size_t>(yStride), static_cast<size_t>(w));
    uint8_t* out = m.data + ySize;
    for (int r = 0; r < h / 2; ++r) std::memcpy(out + static_cast<size_t>(r) * static_cast<size_t>(w), uv + static_cast<size_t>(r) * static_cast<size_t>(uvStride), static_cast<size_t>(w));
    gst_buffer_unmap(buf, &m);
    GstClock* clock = gst_element_get_clock(pipeline_);
    if (clock) {
        GST_BUFFER_PTS(buf) = gst_clock_get_time(clock) - gst_element_get_base_time(pipeline_);
        gst_object_unref(clock);
    }
    GST_BUFFER_DURATION(buf) = GST_SECOND / 60;
    gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buf);
    if (wantFull_.exchange(false)) {
        std::lock_guard<std::mutex> encoderLock(encoderMu_);
        gst_element_send_event(encoder_, gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, ++keyAsks_));
    }
}

void StreamHost::setPicture(bool on) {
    pictureOn_ = on;
    {
        std::lock_guard<std::mutex> lk(frameMu_);
        if (mute_) g_object_set(mute_, "drop", on ? FALSE : TRUE, nullptr);
    }
    capture_.setActive(on);
    // Sending starts again with a fresh full picture, or the friend has nothing to build on.
    if (on) wantFull_ = true;
}

void StreamHost::askFullPicture() { wantFull_ = true; }

void StreamHost::applyStep(const Bitrate::Step& step) {
    std::lock_guard<std::mutex> lk(frameMu_);
    kbit_ = step.kbit;
    {
        std::lock_guard<std::mutex> encoderLock(encoderMu_);
        if (encoder_) g_object_set(encoder_, "bitrate", static_cast<guint>(step.kbit), nullptr);
    }
    capture_.setSize(step.width, step.height);
    wantFull_ = true;
    std::fprintf(stderr, "[linkup] host: bitrate step %d of %d, %d kbit at %dx%d\n", bitrate_.stepIndex() + 1,
                 bitrate_.stepCount(), step.kbit, step.width, step.height);
}

// The share of the picture's packets lost since the friend's console last reported, from its running
// totals: a report's own "fraction lost" covers only the moments since its previous report, which can be
// a few packets, and the same report can be read twice. No new report, no new figure.
void StreamHost::onStatsReply(GstPromise* promise, gpointer self) {
    auto* me = static_cast<StreamHost*>(self);
    const GstStructure* reply = gst_promise_get_reply(promise);
    int lostTotal = 0;
    guint highest = 0;
    bool found = false;
    if (reply) {
        const int fields = gst_structure_n_fields(reply);
        for (int i = 0; i < fields; ++i) {
            const GValue* value = gst_structure_get_value(reply, gst_structure_nth_field_name(reply, i));
            if (!GST_VALUE_HOLDS_STRUCTURE(value)) continue;
            const GstStructure* s = gst_value_get_structure(value);
            GstWebRTCStatsType type = GST_WEBRTC_STATS_CODEC;
            if (!gst_structure_get(s, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type, nullptr)) continue;
            if (type != GST_WEBRTC_STATS_OUTBOUND_RTP) continue;
            const gchar* kind = gst_structure_get_string(s, "kind");
            if (!kind || std::strcmp(kind, "video") != 0) continue;
            GstStructure* source = nullptr;
            if (!gst_structure_get(s, "gst-rtpsource-stats", GST_TYPE_STRUCTURE, &source, nullptr)) continue;
            gboolean have = FALSE;
            if (gst_structure_get_boolean(source, "have-rb", &have) && have && gst_structure_get_int(source, "rb-packetslost", &lostTotal) &&
                gst_structure_get_uint(source, "rb-exthighestseq", &highest))
                found = true;
            gst_structure_free(source);
        }
    }
    gst_promise_unref(promise);
    if (!found) return;
    Rtc::onMain([me, lostTotal, highest]() {
        const int64_t sentSince = static_cast<int64_t>(highest) - static_cast<int64_t>(me->reportHighest_);
        const int64_t lostSince = lostTotal - me->reportLost_;
        if (sentSince <= 0) return;
        const bool first = me->reportHighest_ == 0;
        me->reportHighest_ = highest;
        me->reportLost_ = lostTotal;
        if (first) return;
        const double lost = std::clamp(static_cast<double>(lostSince) / static_cast<double>(sentSince), 0.0, 1.0);
        const Bitrate::Result r = me->bitrate_.report(lost, Rtc::nowMs());
        if (r.changed) me->applyStep(me->bitrate_.step());
        if (r.tooSlow && me->onSlow_) me->onSlow_();
    });
}

void StreamHost::pollStats() {
    if (!webrtc_) return;
    GstPromise* promise = gst_promise_new_with_change_func(onStatsReply, this, nullptr);
    g_signal_emit_by_name(webrtc_, "get-stats", nullptr, promise);
}
