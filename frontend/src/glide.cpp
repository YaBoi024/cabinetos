#include "glide.h"

#include <algorithm>
#include <cmath>

namespace glide {

namespace {

// The things of `from` that kept their order among themselves, the most of
// them there are: they slide and none of them crosses another.
std::vector<bool> inOrder(const std::vector<int>& from) {
    const size_t n = from.size();
    std::vector<int> length(n, 0), before(n, -1);
    int last = -1;
    for (size_t i = 0; i < n; ++i) {
        if (from[i] < 0) continue;
        length[i] = 1;
        for (size_t k = 0; k < i; ++k)
            if (from[k] >= 0 && from[k] < from[i] && length[k] + 1 > length[i]) {
                length[i] = length[k] + 1;
                before[i] = static_cast<int>(k);
            }
        if (last < 0 || length[i] > length[static_cast<size_t>(last)]) last = static_cast<int>(i);
    }
    std::vector<bool> kept(n, false);
    for (; last >= 0; last = before[static_cast<size_t>(last)]) kept[static_cast<size_t>(last)] = true;
    return kept;
}

}  // namespace

void Glide::start(const std::vector<int>& from, const std::vector<Point>& was,
                  const std::vector<Point>& now) {
    const std::vector<bool> kept = inOrder(from);
    offset_.assign(now.size(), Point{});
    coming_.assign(now.size(), true);
    std::vector<bool> slid(was.size(), false);
    for (size_t i = 0; i < now.size() && i < from.size(); ++i) {
        const int f = from[i];
        if (f < 0 || f >= static_cast<int>(was.size()) || !kept[i]) continue;
        const Point d{was[static_cast<size_t>(f)].x - now[i].x, was[static_cast<size_t>(f)].y - now[i].y};
        // One that wrapped onto another line would cross the whole screen.
        if (std::fabs(d.x) > 0.5f && std::fabs(d.y) > 0.5f) continue;
        offset_[i] = d;
        coming_[i] = false;
        slid[static_cast<size_t>(f)] = true;
    }
    leaving_.clear();
    for (size_t k = 0; k < slid.size(); ++k)
        if (!slid[k]) leaving_.push_back(static_cast<int>(k));
    clock_.from = 0.0f;
    clock_.to = 1.0f;
    clock_.elapsed = 0.0f;
    clock_.duration = design::kGlideDuration;
}

void Glide::stop() {
    offset_.clear();
    coming_.clear();
    leaving_.clear();
    clock_.elapsed = clock_.duration;
}

void Glide::tick(float dt) {
    if (!going()) return;
    clock_.tick(dt);
    if (!going()) stop();
}

Look Glide::look(size_t i) const {
    if (!going() || i >= offset_.size()) return {};
    if (coming_[i])
        return {0.0f, 0.0f, std::min(1.0f, clock_.elapsed / clock_.duration / 0.7f), true};
    const float t = clock_.value();
    return {offset_[i].x * (1.0f - t), offset_[i].y * (1.0f - t), 1.0f, false};
}

// Gone before the rest have closed up over it.
float Glide::leavingAlpha() const {
    return going() ? std::max(0.0f, 1.0f - clock_.elapsed / clock_.duration / 0.5f) : 0.0f;
}

}  // namespace glide
