// When the connection cannot keep up (bitrate.h).

#include "bitrate.h"

namespace {

constexpr double kBadLoss = 0.03;       // more than this in a second counts against the step
constexpr double kGoodLoss = 0.005;     // less than this counts towards climbing
constexpr int kBadToFall = 2;           // falling fast
constexpr int kGoodToClimb = 20;        // climbing slowly
constexpr int kBottomBadToSay = 6;      // several seconds at the lowest step
constexpr int64_t kSayAtMostEveryMs = 10 * 60 * 1000;

}  // namespace

void Bitrate::reset(int width, int height) {
    // The bitrate first, at the host's own size, then the resolution.
    steps_.clear();
    steps_.push_back({kTopKbit, width, height});
    steps_.push_back({12000, width, height});
    steps_.push_back({8000, width, height});
    if (height > 720) {
        const int w = (width * 720 / height) & ~1;
        steps_.push_back({6000, w, 720});
    }
    if (height > 540) {
        const int w = (width * 540 / height) & ~1;
        steps_.push_back({3500, w, 540});
    }
    at_ = 0;
    badSeconds_ = goodSeconds_ = bottomBadSeconds_ = 0;
}

Bitrate::Result Bitrate::report(double lost, int64_t nowMs) {
    Result r;
    if (steps_.empty()) return r;
    if (lost > kBadLoss) {
        ++badSeconds_;
        goodSeconds_ = 0;
    } else if (lost < kGoodLoss) {
        ++goodSeconds_;
        badSeconds_ = 0;
    } else {
        badSeconds_ = 0;
        goodSeconds_ = 0;
    }
    if (badSeconds_ >= kBadToFall && at_ + 1 < steps_.size()) {
        ++at_;
        badSeconds_ = 0;
        r.changed = true;
    } else if (goodSeconds_ >= kGoodToClimb && at_ > 0) {
        --at_;
        goodSeconds_ = 0;
        r.changed = true;
    }
    if (at_ + 1 == steps_.size() && lost > kBadLoss) {
        if (++bottomBadSeconds_ >= kBottomBadToSay && (lastSlowMs_ == 0 || nowMs - lastSlowMs_ >= kSayAtMostEveryMs)) {
            r.tooSlow = true;
            lastSlowMs_ = nowMs;
            bottomBadSeconds_ = 0;
        }
    } else if (lost < kBadLoss) {
        bottomBadSeconds_ = 0;
    }
    return r;
}
