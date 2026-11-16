// The friend's sound (friend_sound.h).

#include "friend_sound.h"

#include <algorithm>
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

}  // namespace

SoundQueue::SoundQueue(uint32_t targetFrames)
    : data_(static_cast<size_t>(kFrames) * kChannels), least_(targetFrames), target_(targetFrames) {
    targetFrames_.store(target_, std::memory_order_relaxed);
}

void SoundQueue::setTarget(uint32_t frames) {
    target_ = frames;
    targetFrames_.store(target_, std::memory_order_relaxed);
}

void SoundQueue::push(const int16_t* frames, uint32_t count) {
    const uint32_t index = writeIndex_.load(std::memory_order_relaxed);
    const uint32_t filled = index - readIndex_.load(std::memory_order_acquire);
    const uint32_t room = kFrames - std::min(filled, kFrames);
    if (count > room) {
        noRoom_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t at = index & (kFrames - 1);
    const uint32_t first = std::min(count, kFrames - at);
    std::memcpy(data_.data() + static_cast<size_t>(at) * kChannels, frames, static_cast<size_t>(first) * kStride);
    if (count > first) std::memcpy(data_.data(), frames + static_cast<size_t>(first) * kChannels, static_cast<size_t>(count - first) * kStride);
    writeIndex_.store(index + count, std::memory_order_release);
}

uint32_t SoundQueue::queued() const {
    return writeIndex_.load(std::memory_order_acquire) - readIndex_.load(std::memory_order_acquire);
}

void SoundQueue::silence(int16_t* out, uint32_t count) const {
    std::memset(out, 0, static_cast<size_t>(count) * kStride);
}

void SoundQueue::copyOut(int16_t* out, uint32_t index, uint32_t count) const {
    const uint32_t at = index & (kFrames - 1);
    const uint32_t first = std::min(count, kFrames - at);
    std::memcpy(out, data_.data() + static_cast<size_t>(at) * kChannels, static_cast<size_t>(first) * kStride);
    if (count > first) std::memcpy(out + static_cast<size_t>(first) * kChannels, data_.data(), static_cast<size_t>(count - first) * kStride);
}

void SoundQueue::pull(int16_t* out, uint32_t count) {
    uint32_t index = readIndex_.load(std::memory_order_relaxed);
    uint32_t filled = writeIndex_.load(std::memory_order_acquire) - index;
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
        readIndex_.store(index + filled, std::memory_order_release);
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
    readIndex_.store(index + count, std::memory_order_release);
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
