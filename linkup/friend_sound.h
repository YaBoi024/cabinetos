// The friend's sound: a queue of its own, played at the host's pace (PipeWire on a console).
//
// WHY A QUEUE OF ITS OWN (#654): the host's sound clock is not the friend's. Measured, the mini PC
// sent 47,960.6 samples a second by the reference console's clock, so a queue played at the friend's
// pace ran dry after 30 to 70 seconds and crackled from then on. GStreamer's sinks could not be made to
// follow the host (stream_friend.h), and pipewire-pulse reports 0 for its queue's delay, so nothing
// could steer by it. Here the program holds the queue itself, so it knows the fill exactly.
//
// HOW IT KEEPS PACE: the decoder writes into the queue (push, on GStreamer's thread, which never
// waits); PipeWire's thread takes what the output needs (pull). Each second of sound the queue's mean
// fill is held against the target and PipeWire's own resampler is trimmed by the difference
// (pw_stream_set_rate, as PipeWire's RTP receiver does): proportional and integral, at most 2000 ppm
// either way, 3.5 cents of pitch. Nothing plays until the queue holds the target; a real underrun
// plays silence until it holds it again; a queue far over the target drops its oldest down to it.
//
// HOW MUCH IT KEEPS: 40 ms to start. Wi-Fi can hold the sound back longer: measured on the reference
// console, pieces now and then 50 to 80 ms late, and a full scan of every channel every five minutes
// (NetworkManager's background scan) left gaps of 100 to 150 ms for ten seconds. So, as WebRTC's own
// player does, the target grows by 20 ms after each stall it did not cover, up to 100 ms (beyond that
// the sound trails the picture noticeably), and shrinks by 10 ms after each two calm minutes. Dry for
// longer than 300 ms is the host gone quiet (its menu, a pause), not the network, and grows nothing.

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

// The queue and its pace, with no sound system in it: what noise-test.cpp holds to a host's clock.
class SoundQueue {
public:
    static constexpr uint32_t kRate = 48000;
    static constexpr uint32_t kChannels = 2;   // 16-bit, interleaved

    explicit SoundQueue(uint32_t targetFrames = kRate * 40 / 1000);   // the least it keeps

    // GStreamer's thread. Never waits: what does not fit is dropped and counted.
    void push(const int16_t* frames, uint32_t count);
    // The output's thread: exactly count frames into out, silence while it fills.
    void pull(int16_t* out, uint32_t count);
    // For the output's resampler (pw_stream_set_rate), on its thread: below 1 takes more of the queue each cycle.
    double rate() const { return rate_; }

    // Any thread.
    uint32_t queued() const;
    struct Stats {
        uint32_t underruns = 0;   // ran dry while playing
        uint32_t tooFull = 0;     // dropped its oldest down to the target
        uint32_t noRoom = 0;      // pieces dropped on arrival, the queue full
        double trimPpm = 0;       // above 0 plays faster than this console's clock
        double lowMs = 0;         // the least in hand after a pull, since playing began
        double targetMs = 0;      // what it keeps now
    };
    Stats stats() const;

private:
    static constexpr uint32_t kFrames = 1u << 16;   // 1.37 s, a power of two for the index's mask
    void silence(int16_t* out, uint32_t count) const;
    void copyOut(int16_t* out, uint32_t index, uint32_t count) const;
    void pace(uint32_t cushion, uint32_t taken);
    void setTarget(uint32_t frames);

    std::vector<int16_t> data_;
    // Free-running indexes, as SPA's ring buffer has them: filled = write - read, the slot is index & mask.
    std::atomic<uint32_t> readIndex_{0};
    std::atomic<uint32_t> writeIndex_{0};
    const uint32_t least_;

    // The output's thread only.
    uint32_t target_;
    bool playing_ = false;
    bool stalled_ = false;   // ran dry, not yet playing again
    uint32_t dry_ = 0;       // frames of silence since it ran dry
    uint32_t calm_ = 0;      // frames played since it last ran dry, or last shrank
    double sum_ = 0;
    uint32_t samples_ = 0;
    uint32_t since_ = 0;
    double integral_ = 0;
    double rate_ = 1.0;
    uint32_t low_ = UINT32_MAX;

    std::atomic<uint32_t> underruns_{0};
    std::atomic<uint32_t> tooFull_{0};
    std::atomic<uint32_t> noRoom_{0};
    std::atomic<double> trimPpm_{0};
    std::atomic<uint32_t> lowFrames_{UINT32_MAX};
    std::atomic<uint32_t> targetFrames_{0};
};

// The output that drains a SoundQueue, on a thread of its own. What plays it is the platform's:
// friend_sound_pw.cpp (PipeWire) on a console, another file for another system.
class FriendSound {
public:
    FriendSound();
    ~FriendSound();

    // Plays silence until the queue holds the target. Any thread; a second call does nothing.
    bool start();
    void push(const int16_t* frames, uint32_t count) { queue_.push(frames, count); }
    // After the pipeline that pushes has stopped.
    void stop();
    bool running() const { return out_ != nullptr; }
    SoundQueue::Stats stats() const { return queue_.stats(); }
    uint32_t rateRefused() const { return rateRefused_.load(); }

    struct Output;   // the platform's, in its own file

private:
    SoundQueue queue_;
    Output* out_ = nullptr;
    std::atomic<uint32_t> rateRefused_{0};
};
