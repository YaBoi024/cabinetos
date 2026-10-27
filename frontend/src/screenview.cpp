// A DS or 3DS game's screens, both or one alone, moved on by one button;
// the rule is in screenview.h.

#include "screenview.h"

#include <cstdio>

#include "core.h"
#include "coreoptions.h"

namespace screenview {

namespace {

bool twoScreens(const std::string& core) { return core == "melonds" || core == "azahar"; }

const char* word(View v) {
    switch (v) {
        case View::Top: return "the top screen alone";
        case View::Bottom: return "the bottom screen alone";
        default: return "both screens";
    }
}

}  // namespace

std::map<std::string, std::string> coreOptions(const std::string& core, View view) {
    // Both is melonDS's own default, stacked, and the 3DS's side by side
    // (coreoptions.cpp).
    if (core == "melonds") {
        switch (view) {
            case View::Top: return {{"melonds_screen_layout", "Top Only"}};
            case View::Bottom: return {{"melonds_screen_layout", "Bottom Only"}};
            default: return {{"melonds_screen_layout", "Top/Bottom"}};
        }
    }
    if (core == "azahar") {
        switch (view) {
            case View::Top:
                return {{"citra_layout_option", "single_screen"}, {"citra_swap_screen", "Top"}};
            case View::Bottom:
                return {{"citra_layout_option", "single_screen"},
                        {"citra_swap_screen", "Bottom"}};
            default:
                return {{"citra_layout_option", "side_by_side"}, {"citra_swap_screen", "Top"}};
        }
    }
    return {};
}

std::optional<View> step(Press& p, const std::string& core, uint32_t& buttons, uint64_t frame) {
    using cab::bit;
    if (!twoScreens(core)) return std::nullopt;
    // A new game counts its frames from zero, and starts with both.
    if (frame < p.frame) p = {};
    p.frame = frame;
    const bool down = buttons & bit(cab::L3);
    buttons &= ~bit(cab::L3);
    // The DS's lid, on R2 in place of melonDS's swap.
    if (core == "melonds" && (buttons & bit(cab::R2))) {
        buttons &= ~bit(cab::R2);
        buttons |= bit(cab::L3);
    }
    if (!down || p.down) {
        p.down = down;
        return std::nullopt;
    }
    p.down = true;
    p.view = static_cast<View>((static_cast<int>(p.view) + 1) % kViewCount);
    std::fprintf(stderr, "[screens] %s\n", word(p.view));
    return p.view;
}

// ---- --screen-view-test ----------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    using cab::bit;
    const std::string ds = "melonds", n3ds = "azahar";

    // Presses `b` for `frames` from frame `from`, then lets go for as long;
    // returns every view a press gave, and whether the game heard all of `b`.
    struct Run {
        int views = 0;
        std::optional<View> last;
        bool heard = false;
    };
    auto press = [&](Press& p, const std::string& core, uint32_t b, uint64_t from,
                     uint64_t frames = 3) {
        Run r;
        for (uint64_t f = from; f < from + 2 * frames; ++f) {
            uint32_t buttons = f < from + frames ? b : 0u;
            if (const std::optional<View> v = step(p, core, buttons, f)) {
                ++r.views;
                r.last = v;
            }
            if (f < from + frames && (buttons & b) == b) r.heard = true;
        }
        return r;
    };

    {
        Press p;
        const Run top = press(p, ds, bit(cab::L3), 100);
        const Run bottom = press(p, ds, bit(cab::L3), 110);
        const Run both = press(p, ds, bit(cab::L3), 120);
        check(top.last == View::Top && bottom.last == View::Bottom && both.last == View::Both,
              "DS: L3 three times shows the top screen alone, the bottom alone, then both");
        check(!top.heard && !bottom.heard && !both.heard,
              "and melonDS never hears it, so the lid stays open");
        check(coreOptions(ds, View::Top).at("melonds_screen_layout") == "Top Only" &&
                  coreOptions(ds, View::Bottom).at("melonds_screen_layout") == "Bottom Only" &&
                  coreOptions(ds, View::Both).at("melonds_screen_layout") == "Top/Bottom",
              "as its layouts Top Only, Bottom Only and Top/Bottom");
    }
    {
        Press p;
        check(press(p, ds, bit(cab::L3), 100, 300).views == 1,
              "DS: L3 held for five seconds moves the view on once");
    }
    {
        Press p;
        uint32_t lid = bit(cab::R2);
        const bool none = !step(p, ds, lid, 100);
        check(none && lid == bit(cab::L3),
              "DS: R2 held is the lid closed (melonDS's L3), never melonDS's swap");
        uint32_t both = bit(cab::L3) | bit(cab::R2);
        step(p, ds, both, 101);
        check(both == bit(cab::L3) && p.view == View::Top,
              "and L3 with R2 moves the view on and closes the lid");
        uint32_t rest = bit(cab::L2) | bit(cab::R3);
        step(p, ds, rest, 102);
        check(rest == (bit(cab::L2) | bit(cab::R3)),
              "the microphone and the touch reach melonDS as sent");
    }
    {
        Press p;
        const Run top = press(p, n3ds, bit(cab::L3), 100);
        const Run bottom = press(p, n3ds, bit(cab::L3), 110);
        const Run both = press(p, n3ds, bit(cab::L3), 120);
        check(top.last == View::Top && bottom.last == View::Bottom && both.last == View::Both,
              "3DS: L3 three times shows the top screen alone, the bottom alone, then both");
        check(!top.heard && !bottom.heard && !both.heard, "and Azahar never hears L3");
        const auto t = coreOptions(n3ds, View::Top), b = coreOptions(n3ds, View::Bottom);
        check(t.at("citra_layout_option") == "single_screen" &&
                  t.at("citra_swap_screen") == "Top" &&
                  b.at("citra_layout_option") == "single_screen" &&
                  b.at("citra_swap_screen") == "Bottom",
              "as its single screen, the top one and then the bottom one");
        const auto launch = catalog::optionOverrides(n3ds, true, true);
        const auto both3 = coreOptions(n3ds, View::Both);
        check(launch.count("citra_layout_option") &&
                  launch.at("citra_layout_option") == both3.at("citra_layout_option") &&
                  both3.at("citra_swap_screen") == "Top",
              "and both is the layout a 3DS game starts with, the top screen on the left");
    }
    {
        Press p;
        const Run r = press(p, n3ds, bit(cab::R3) | bit(cab::L2) | bit(cab::R2), 100);
        check(r.views == 0 && r.heard, "3DS: the touch, ZL and ZR reach Azahar");
    }
    {
        Press p;
        press(p, ds, bit(cab::L3), 5000);
        const Run r = press(p, ds, bit(cab::L3), 10);
        check(r.last == View::Top,
              "a new game, its frames from zero, starts with both: a press shows the top screen");
    }
    {
        Press p;
        const Run r = press(p, "snes9x_libretro", bit(cab::R2) | bit(cab::L3), 100);
        check(r.views == 0 && r.heard && coreOptions("snes9x_libretro", View::Top).empty(),
              "any other game: R2 and L3 reach it, and there is no view");
    }

    std::printf("%s\n", failures ? "screen view test: FAILED" : "screen view test: passed");
    return failures ? 1 : 0;
}

}  // namespace screenview
