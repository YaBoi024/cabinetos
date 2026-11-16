// The friend's sound through PipeWire (friend_sound.h): a playback stream on a thread loop of its own.

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <cstdio>

#include "friend_sound.h"

namespace {
constexpr uint32_t kStride = SoundQueue::kChannels * sizeof(int16_t);
}  // namespace

struct FriendSound::Output {
    FriendSound* owner = nullptr;
    pw_thread_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    double rateGiven = 0;   // PipeWire's thread only

    static void onState(void*, pw_stream_state, pw_stream_state now, const char* error) {
        if (now == PW_STREAM_STATE_ERROR) std::fprintf(stderr, "[linkup] friend: the sound's stream failed: %s\n", error ? error : "");
    }

    // PipeWire's own thread: no locks, no waiting, no printing.
    static void onProcess(void* self) {
        auto* me = static_cast<Output*>(self);
        pw_buffer* b = pw_stream_dequeue_buffer(me->stream);
        if (!b) return;
        spa_data& d = b->buffer->datas[0];
        if (d.data) {
            uint32_t frames = d.maxsize / kStride;
            if (b->requested) frames = static_cast<uint32_t>(std::min<uint64_t>(b->requested, frames));
            me->owner->queue_.pull(static_cast<int16_t*>(d.data), frames);
            d.chunk->offset = 0;
            d.chunk->stride = kStride;
            d.chunk->size = frames * kStride;
        }
        pw_stream_queue_buffer(me->stream, b);
        const double rate = me->owner->queue_.rate();
        if (rate != me->rateGiven) {
            if (pw_stream_set_rate(me->stream, rate) < 0) me->owner->rateRefused_.fetch_add(1, std::memory_order_relaxed);
            else me->rateGiven = rate;
        }
    }
};

namespace {
const pw_stream_events* streamEvents() {
    static pw_stream_events events = {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = FriendSound::Output::onState;
    events.process = FriendSound::Output::onProcess;
    return &events;
}
}  // namespace

FriendSound::FriendSound() = default;

FriendSound::~FriendSound() { stop(); }

bool FriendSound::start() {
    if (out_) return true;
    auto* o = new Output;
    o->owner = this;
    o->loop = pw_thread_loop_new("cabinetos-linkup-sound", nullptr);
    if (!o->loop) {
        delete o;
        return false;
    }
    pw_thread_loop_lock(o->loop);
    // 10 ms a cycle, one Opus piece.
    o->stream = pw_stream_new_simple(pw_thread_loop_get_loop(o->loop), "LinkUp",
                                     pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                                       PW_KEY_MEDIA_ROLE, "Game", PW_KEY_NODE_NAME, "LinkUp", PW_KEY_APP_NAME,
                                                       "LinkUp", PW_KEY_NODE_LATENCY, "480/48000", nullptr),
                                     streamEvents(), o);
    int r = -1;
    if (o->stream) {
        uint8_t buf[512];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_S16_LE;
        info.rate = SoundQueue::kRate;
        info.channels = SoundQueue::kChannels;
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod* params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
        r = pw_stream_connect(o->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                              static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                           PW_STREAM_FLAG_RT_PROCESS),
                              params, 1);
    }
    if (r < 0) {
        std::fprintf(stderr, "[linkup] friend: the sound's stream would not connect: %d\n", r);
        if (o->stream) pw_stream_destroy(o->stream);
        pw_thread_loop_unlock(o->loop);
        pw_thread_loop_destroy(o->loop);
        delete o;
        return false;
    }
    pw_thread_loop_unlock(o->loop);
    pw_thread_loop_start(o->loop);
    out_ = o;
    return true;
}

void FriendSound::stop() {
    if (!out_) return;
    Output* o = out_;
    out_ = nullptr;
    pw_thread_loop_lock(o->loop);
    if (o->stream) {
        pw_stream_disconnect(o->stream);
        pw_stream_destroy(o->stream);
        o->stream = nullptr;
    }
    pw_thread_loop_unlock(o->loop);
    pw_thread_loop_stop(o->loop);
    pw_thread_loop_destroy(o->loop);
    delete o;
}
