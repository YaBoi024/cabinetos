// The controllers' batteries in the top bar (#293): which pads are on, the
// row's slots and their slides, and drawing them (#398, restructure
// batch d). The bar calls both halves at their points in its draw.

#include "app.h"

#include <algorithm>
#include <utility>

#include "batteryrow.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// CONTROLLER BATTERIES (#293, MMagTech 2026-10-08): wherever this
// bar is (Home, Library, Search, Settings), so the bar keeps its
// shape as you move along it; never in a game or on a game's page,
// where the notice speaks instead. Whenever a pad is on, left of
// the name, so a longer name pushes the row along and the name
// never moves. P1-P4 in player order, then the Wii Remotes as
// W1-W4, the light lit on each (out of a game their numbers are
// their own, so a P would make two P1s). Each is its label and a
// battery of four segments, a bolt while charging. A pad with no
// reading, and every stream pad, is its label alone. While someone
// streams the Remotes leave the row with the console's own pads.
float App::settleBatteryRow(float sc, float dt) {
    std::vector<BatteryEntry> row;
    for (const players::Held& h : players::held()) {
        battery::Reading r;
        if (!h.stream) {
            r = battery::kernel(h.dir);
            if (r.segments == 0) r = battery::sdl(h.gp);
        }
        row.push_back({"pad:" + (h.key.empty() ? std::to_string(h.player) : h.key),
                       h.dir == battery::systemDir() ? std::string("Deck")   // FORK (handheld)
                                                     : "P" + std::to_string(h.player + 1),
                       r});
    }
    if (!players::streaming())
        for (const wiiremote::Battery& b : wiiremote::batteries()) {
            battery::Reading r;
            if (b.byte >= 0)
                r.segments = battery::segmentsOfWii(static_cast<uint8_t>(b.byte));
            row.push_back({"wii:" + b.address, "W" + std::to_string(b.light), r});
        }
    // FORK (handheld): the label centred above its battery; the bolt beside the battery.
    auto widthOf = [&](const BatteryEntry& e) {
        if (e.r.segments <= 0) return text.measure(e.label, ls, sc);
        const float labelW = text.measure(e.label, kRowLabel, sc);
        const float group = std::max(labelW, kBatteryW);
        if (!e.r.charging) return group;
        return std::max(group, (group - kBatteryW) * 0.5f + kBatteryW + kBoltGap + kBoltW);
    };
    constexpr float kFade = 0.2f, kSlide = 0.25f;
    {
        // Fresh: the bar was not on screen the frame before (a game or a
        // game's page); moving along the bar is not a change.
        const bool fresh = !batteryRowBefore;
        if (fresh) {
            batterySlots.clear();
            for (const BatteryEntry& e : row) {
                BatterySlot sl;
                sl.e = e;
                sl.alpha.settle(1.0f);
                batterySlots.push_back(std::move(sl));
            }
        } else {
            auto inRow = [&](const std::string& id) -> const BatteryEntry* {
                for (const BatteryEntry& e : row)
                    if (e.id == id) return &e;
                return nullptr;
            };
            for (BatterySlot& sl : batterySlots) {
                if (const BatteryEntry* e = inRow(sl.e.id)) {
                    sl.e = *e;
                    if (sl.leaving) {   // back before it had gone
                        sl.leaving = false;
                        sl.alpha.retarget(1.0f, kFade);
                    }
                } else if (!sl.leaving) {
                    sl.leaving = true;
                    sl.alpha.retarget(0.0f, kFade);
                }
            }
            // New ones go right after the entry before them in the row.
            for (size_t i = 0; i < row.size(); ++i) {
                bool have = false;
                for (const BatterySlot& sl : batterySlots) have = have || sl.e.id == row[i].id;
                if (have) continue;
                size_t at = 0;
                if (i > 0)
                    for (size_t k = 0; k < batterySlots.size(); ++k)
                        if (batterySlots[k].e.id == row[i - 1].id) at = k + 1;
                BatterySlot sl;
                sl.e = row[i];
                sl.offset.settle(-1.0f);   // placed below, where it belongs
                sl.alpha.settle(0.0f);
                sl.alpha.retarget(1.0f, kFade);
                batterySlots.insert(batterySlots.begin() + static_cast<long>(at), std::move(sl));
            }
            // Gone once faded; the rest close up after that.
            std::erase_if(batterySlots, [](const BatterySlot& sl) {
                return sl.leaving && sl.alpha.elapsed >= sl.alpha.duration;
            });
            // ALWAYS IN THE ROW'S ORDER, P1 to P4. A slot is kept by
            // its pad, so a swap (Make player N) relabelled two slots
            // and left them where they were: P2 then P1 (MMagTech on
            // the TV, 2026-10-10). Sorted by the row, they slide to
            // their places; one leaving stays after the one it
            // followed until it has faded.
            {
                std::vector<std::pair<float, size_t>> order;
                float last = -1.0f;
                for (size_t k = 0; k < batterySlots.size(); ++k) {
                    float rank = last + 0.5f;
                    if (!batterySlots[k].leaving)
                        for (size_t i = 0; i < row.size(); ++i)
                            if (row[i].id == batterySlots[k].e.id) rank = last = static_cast<float>(i);
                    order.push_back({rank, k});
                }
                std::stable_sort(order.begin(), order.end(),
                                 [](const auto& x, const auto& y) { return x.first < y.first; });
                bool moved = false;
                for (size_t k = 0; k < order.size(); ++k) moved = moved || order[k].second != k;
                if (moved) {
                    std::vector<BatterySlot> sorted;
                    sorted.reserve(batterySlots.size());
                    for (const auto& o : order) sorted.push_back(std::move(batterySlots[o.second]));
                    batterySlots = std::move(sorted);
                }
            }
        }
        // Where each one belongs, from the right end leftward. A leaving
        // one keeps its space until it has faded.
        float from = 0.0f;
        for (auto it = batterySlots.rbegin(); it != batterySlots.rend(); ++it) {
            from += widthOf(it->e);
            if (fresh || it->offset.to < 0.0f) it->offset.settle(from);
            else it->offset.retarget(from, kSlide);
            from += kBetween;
        }
        for (BatterySlot& sl : batterySlots) {
            sl.offset.tick(dt);
            sl.alpha.tick(dt);
        }
        batteryRowNow = true;
    }
    float rowW = 0.0f;
    for (const BatterySlot& sl : batterySlots) rowW = std::max(rowW, sl.offset.value());
    return rowW;
}

void App::drawBatteryRow(float& leftOfChip, float rowW, float sc) {
    if (!batterySlots.empty()) {
        // TRAFFIC LIGHTS ON THE FILL ONLY (MMagTech, 2026-10-08: "a
        // little color would be beneficial"): green at three and
        // four, amber at two, red at the last, the warning. The
        // outline, the bolt and the label stay grey, so the row
        // stays quiet and only the fill says how full.
        constexpr ui::Color kBatteryGreen = ui::Color::rgb(0x5FD068);
        constexpr ui::Color kBatteryAmber = ui::palette::kMarqueeAmber;
        constexpr ui::Color kBatteryRed = ui::Color::rgb(0xFF5A4E);
        const float rowRight = leftOfChip - kToName;
        const float rowLeft = rowRight - rowW;
        const float cy = barTop + barHeight * 0.5f;
        const float base = barTop + (barHeight - text.lineHeight(ls, sc)) * 0.5f +
                           text.ascent(ls, sc);
        for (const BatterySlot& sl : batterySlots) {
            const BatteryEntry& e = sl.e;
            const float a = sl.alpha.value();
            if (a <= 0.001f) continue;
            const float x = rowRight - sl.offset.value();
            auto faded = [a](ui::Color c) {
                c.a *= a;
                return c;
            };
            const ui::Color grey = faded(ui::Color::white(0.62f));
            if (e.r.segments <= 0) {   // no reading: the label alone, centred in the bar
                text.draw(renderer, e.label, x, base, ls, grey, sc);
                continue;
            }
            // The label above, the battery below, the pair centred in the bar.
            const float labelH = text.lineHeight(kRowLabel, sc);
            const float stackH = labelH + kLabelAbove + kBodyH;
            const float top = barTop + (barHeight - stackH) * 0.5f;
            const float labelW = text.measure(e.label, kRowLabel, sc);
            const float group = std::max(labelW, kBatteryW);
            const float gx = x + (group - kBatteryW) * 0.5f;
            const float by = top + labelH + kLabelAbove;   // the battery's top
            text.draw(renderer, e.label, x + (group - labelW) * 0.5f,
                      top + text.ascent(kRowLabel, sc), kRowLabel, grey, sc);
            ui::Rect body{gx, by, kBodyW, kBodyH, 5.0f, ui::Color::white(0.0f)};
            body.border = 2.5f;
            body.borderColor = grey;
            renderer.draw(body);
            renderer.draw(ui::Rect{gx + kBodyW + 1.0f, by + (kBodyH - kNubH) * 0.5f, kNubW,
                                   kNubH, 1.5f, grey});
            const ui::Color fill = faded(e.r.segments >= 3   ? kBatteryGreen
                                         : e.r.segments == 2 ? kBatteryAmber
                                                             : kBatteryRed);
            const float sw = (kBodyW - kInset * 2.0f - kSegGap * 3.0f) / 4.0f;
            for (int i = 0; i < std::min(e.r.segments, 4); ++i)
                renderer.draw(ui::Rect{gx + kInset + static_cast<float>(i) * (sw + kSegGap),
                                       by + kInset, sw, kBodyH - kInset * 2.0f, 1.0f, fill});
            if (e.r.charging) {
                ui::Rect bolt{gx + kBatteryW + kBoltGap, by + (kBodyH - kBoltH) * 0.5f, kBoltW,
                              kBoltH, 0.0f, grey};
                bolt.shape = ui::Rect::Shape::Bolt;
                renderer.draw(bolt);
            }
        }
        leftOfChip = rowLeft - 10.0f;
    }
}
