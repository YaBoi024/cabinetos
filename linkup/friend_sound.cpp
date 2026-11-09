// The friend's sound (friend_sound.h).

#include "friend_sound.h"

#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// The pace, in ppm of the friend's clock: per ms the mean fill is off the target, and per ms for
// each second it stays off. Damped, settling in a minute or two; learning a host 1000 ppm slow it kept
// at least 26 ms in hand. Simulated against Wi-Fi's unevenness in noise-test.cpp, where gains twice
// as high swung the trim twice as far with every hiccup.
constexpr double kPpmPerMs = 80;
constexpr double kPpmPerMsSecond = 2.5;
constexpr double kMostPpm = 2000;
constexpr uint32_t kTooFullMs = 80;   // over the target before the oldest is dropped
// The target grows after a stall and shrinks back when calm (friend_sound.h, "How much it keeps").
constexpr uint32_t kGrowMs = 20;
constexpr uint32_t kMostTargetMs = 100;
constexpr uint32_t kShrinkMs = 10;
constexpr uint32_t kCalmSeconds = 120;
constexpr uint32_t kStallMostMs = 300;   // dry longer than this: the host went quiet, not the network
constexpr uint32_t kStride = SoundQueue::kChannels * sizeof(int16_t);

const pw_stream_events* streamEvents(void (*state)(void*, pw_stream_state, pw_stream_state, const char*),
                                     void (*process)(void*)) {
    static pw_stream_events events = {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = state;
    events.process = process;
    return &events;
}

}  // namespace

SoundQueue::SoundQueue(uint32_t targetFrames)
    : data_(static_cast<size_t>(kFrames) * kChannels), least_(targetFrames), target_(targetFrames) {
    spa_ringbuffer_init(&ring_);
    targetFrames_.store(target_, std::memory_order_relaxed);
}

void SoundQueue::setTarget(uint32_t frames) {
    target_ = frames;
    targetFrames_.store(target_, std::memory_order_relaxed);
}

void SoundQueue::push(const int16_t* frames, uint32_t count) {
    uint32_t index = 0;
    const int32_t filled = spa_ringbuffer_get_write_index(&ring_, &index);
    const uint32_t room = kFrames - std::min<uint32_t>(static_cast<uint32_t>(std::max(filled, 0)), kFrames);
    if (count > room) {
        noRoom_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    spa_ringbuffer_write_data(&ring_, data_.data(), kFrames * kStride, (index & (kFrames - 1)) * kStride, frames, count * kStride);
    spa_ringbuffer_write_update(&ring_, static_cast<int32_t>(index + count));
}

uint32_t SoundQueue::queued() const {
    uint32_t index = 0;
    return static_cast<uint32_t>(std::max(spa_ringbuffer_get_read_index(&ring_, &index), 0));
}

void SoundQueue::silence(int16_t* out, uint32_t count) const {
    std::memset(out, 0, static_cast<size_t>(count) * kStride);
}

void SoundQueue::copyOut(int16_t* out, uint32_t index, uint32_t count) const {
    spa_ringbuffer_read_data(&ring_, data_.data(), kFrames * kStride, (index & (kFrames - 1)) * kStride, out, count * kStride);
}

void SoundQueue::pull(int16_t* out, uint32_t count) {
    uint32_t index = 0;
    uint32_t filled = static_cast<uint32_t>(std::max(spa_ringbuffer_get_read_index(&ring_, &index), 0));
    if (!playing_) {
        dry_ += count;
        // Back after a stall the queue did not cover: keep more from now on, before playing again.
        if (stalled_ && filled >= target_ + count) {
            stalled_ = false;
            if (dry_ < kRate * kStallMostMs / 1000)
                setTarget(std::min(target_ + kRate * kGrowMs / 1000, kRate * kMostTargetMs / 1000));
        }
        if (filled < target_ + count) {
            silence(out, count);
            return;
        }
        playing_ = true;
    } else if (filled < count) {
        // A real underrun: what there is, then silence until the target is back.
        copyOut(out, index, filled);
        silence(out + static_cast<size_t>(filled) * kChannels, count - filled);
        spa_ringbuffer_read_update(&ring_, static_cast<int32_t>(index + filled));
        playing_ = false;
        stalled_ = true;
        dry_ = count - filled;
        calm_ = 0;
        sum_ = 0;
        samples_ = 0;
        since_ = 0;
        low_ = 0;
        lowFrames_.store(0, std::memory_order_relaxed);
        underruns_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (filled - count > target_ + kRate * kTooFullMs / 1000) {
        const uint32_t extra = filled - count - target_;
        index += extra;
        filled -= extra;
        tooFull_.fetch_add(1, std::memory_order_relaxed);
    }
    copyOut(out, index, count);
    spa_ringbuffer_read_update(&ring_, static_cast<int32_t>(index + count));
    pace(filled - count, count);
}

// What is left in hand after a pull, averaged over a second of sound, steers the next second.
void SoundQueue::pace(uint32_t cushion, uint32_t taken) {
    if (cushion < low_) {
        low_ = cushion;
        lowFrames_.store(low_, std::memory_order_relaxed);
    }
    sum_ += cushion;
    ++samples_;
    since_ += taken;
    calm_ += taken;
    if (calm_ >= kRate * kCalmSeconds && target_ > least_) {
        setTarget(std::max(target_ - kRate * kShrinkMs / 1000, least_));
        calm_ = 0;
    }
    if (since_ < kRate) return;
    const double offMs = (sum_ / samples_ - target_) * 1000.0 / kRate;
    integral_ = std::clamp(integral_ + kPpmPerMsSecond * offMs * since_ / kRate, -kMostPpm, kMostPpm);
    const double trim = std::clamp(kPpmPerMs * offMs + integral_, -kMostPpm, kMostPpm);
    // A rate below 1 has PipeWire's resampler take more of the queue for each piece of output.
    rate_ = 1.0 / (1.0 + trim * 1e-6);
    trimPpm_.store(trim, std::memory_order_relaxed);
    sum_ = 0;
    samples_ = 0;
    since_ = 0;
}

SoundQueue::Stats SoundQueue::stats() const {
    Stats s;
    s.underruns = underruns_.load(std::memory_order_relaxed);
    s.tooFull = tooFull_.load(std::memory_order_relaxed);
    s.noRoom = noRoom_.load(std::memory_order_relaxed);
    s.trimPpm = trimPpm_.load(std::memory_order_relaxed);
    const uint32_t low = lowFrames_.load(std::memory_order_relaxed);
    s.lowMs = low == UINT32_MAX ? 0 : low * 1000.0 / kRate;
    s.targetMs = targetFrames_.load(std::memory_order_relaxed) * 1000.0 / kRate;
    return s;
}

FriendSound::~FriendSound() { stop(); }

void FriendSound::onState(void*, pw_stream_state, pw_stream_state now, const char* error) {
    if (now == PW_STREAM_STATE_ERROR) std::fprintf(stderr, "[linkup] friend: the sound's stream failed: %s\n", error ? error : "");
}

// PipeWire's own thread: no locks, no waiting, no printing.
void FriendSound::onProcess(void* self) {
    auto* me = static_cast<FriendSound*>(self);
    pw_buffer* b = pw_stream_dequeue_buffer(me->stream_);
    if (!b) return;
    spa_data& d = b->buffer->datas[0];
    if (d.data) {
        uint32_t frames = d.maxsize / kStride;
        if (b->requested) frames = static_cast<uint32_t>(std::min<uint64_t>(b->requested, frames));
        me->queue_.pull(static_cast<int16_t*>(d.data), frames);
        d.chunk->offset = 0;
        d.chunk->stride = kStride;
        d.chunk->size = frames * kStride;
    }
    pw_stream_queue_buffer(me->stream_, b);
    const double rate = me->queue_.rate();
    if (rate != me->rateGiven_) {
        if (pw_stream_set_rate(me->stream_, rate) < 0) me->rateRefused_.fetch_add(1, std::memory_order_relaxed);
        else me->rateGiven_ = rate;
    }
}

bool FriendSound::start() {
    if (stream_) return true;
    loop_ = pw_thread_loop_new("cabinetos-linkup-sound", nullptr);
    if (!loop_) return false;
    pw_thread_loop_lock(loop_);
    // 10 ms a cycle, one Opus piece.
    stream_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), "LinkUp",
                                   pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                                     PW_KEY_MEDIA_ROLE, "Game", PW_KEY_NODE_NAME, "LinkUp", PW_KEY_APP_NAME,
                                                     "LinkUp", PW_KEY_NODE_LATENCY, "480/48000", nullptr),
                                   streamEvents(onState, onProcess), this);
    int r = -1;
    if (stream_) {
        uint8_t buf[512];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_S16_LE;
        info.rate = SoundQueue::kRate;
        info.channels = SoundQueue::kChannels;
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod* params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
        r = pw_stream_connect(stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                              static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                           PW_STREAM_FLAG_RT_PROCESS),
                              params, 1);
    }
    if (r < 0) {
        std::fprintf(stderr, "[linkup] friend: the sound's stream would not connect: %d\n", r);
        if (stream_) pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_start(loop_);
    return true;
}

void FriendSound::stop() {
    if (!loop_) return;
    pw_thread_loop_lock(loop_);
    if (stream_) {
        pw_stream_disconnect(stream_);
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_stop(loop_);
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
}
