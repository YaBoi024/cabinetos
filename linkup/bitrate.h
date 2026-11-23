// When the connection cannot keep up: lower the bitrate, then the resolution, falling fast and
// climbing slowly. Only when the lowest step has still not kept up for several seconds does anybody
// hear about it. No congestion controller is on the image, so this small one works from the loss the
// friend's console reports. What counts as bad is measured with the testers (#654, step 6): these are
// the first figures, and every one is here so that it can move.

#pragma once

#include <cstdint>
#include <vector>

class Bitrate {
public:
    // PROTOCOL's cap on a relay allows for this: at most 18 Mbit/s, and the hardware's short peaks.
    static constexpr int kTopKbit = 18000;

    struct Step {
        int kbit;
        int width;
        int height;
    };
    struct Result {
        bool changed = false;
        bool tooSlow = false;
    };

    // The host's own size, capped at 1080p already.
    void reset(int width, int height);

    // With each new report from the friend's console, about one a second: the share of the picture's
    // packets lost since the last one (0 to 1).
    Result report(double lost, int64_t nowMs);

    const Step& step() const { return steps_[at_]; }
    int stepIndex() const { return static_cast<int>(at_); }
    int stepCount() const { return static_cast<int>(steps_.size()); }

private:
    std::vector<Step> steps_;
    size_t at_ = 0;
    int badSeconds_ = 0;
    int goodSeconds_ = 0;
    int bottomBadSeconds_ = 0;
    int64_t lastSlowMs_ = 0;
};
