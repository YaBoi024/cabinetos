// Holding Start in an arcade game opens the game's own settings (#577);
// the rule is in arcadesettings.h.

#include "arcadesettings.h"

#include <cstdio>

#include "core.h"
#include "libretro.h"

namespace arcadesettings {

static_assert(kMenuKey == RETROK_TAB, "both MAMEs open their menu on Tab");

void keepClicks(const std::string& core, uint32_t& buttons) {
    if (core == "flycast") buttons &= ~(cab::bit(cab::L3) | cab::bit(cab::R3));
}

bool step(Hold& h, const std::string& core, uint32_t& buttons, uint64_t frame) {
    const bool old = core == "mame2003_plus", mame = old || core == "mame_libretro",
               board = core == "flycast";
    if (!mame && !board) return false;
    // A new game counts its frames from zero.
    if (frame < h.since || frame + kPressFrames < h.pressUntil) h = {};
    if (old) buttons &= ~cab::bit(cab::L3);
    keepClicks(core, buttons);
    const bool start = buttons & cab::bit(cab::Start);
    if (!start) {
        h.down = false;
        h.counted = false;
    } else if (!h.down) {
        h.down = true;
        h.since = frame;
    } else if (!h.counted && frame - h.since >= kHoldFrames) {
        h.counted = true;
        h.pressUntil = frame + kPressFrames;
        std::fprintf(stderr, "[arcade] Start held: %s\n",
                     old    ? "MAME 2003-Plus's menu"
                     : mame ? "MAME's menu"
                            : "the board's Test button");
    }
    const bool pressing = frame < h.pressUntil;
    if (board) {
        if (pressing) buttons |= cab::bit(cab::L3);
        return false;
    }
    return pressing;
}

// ---- --arcade-settings-test ---------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    using cab::bit;
    const std::string mame = "mame2003_plus", fbneo = "fbneo_libretro", board = "flycast";
    const uint32_t start = bit(cab::Start), l3 = bit(cab::L3);

    // Holds Start from frame `from` for `frames`, then lets go; returns the
    // frames Tab was down in, and what the game heard on the last held frame.
    struct Run {
        int tabFrames = 0;
        uint64_t firstTab = 0;
        uint32_t heard = 0;
        int testFrames = 0;     // frames the game heard L3, a board's Test
        uint64_t firstTest = 0;
    };
    auto hold = [&](Hold& h, const std::string& core, uint64_t from, uint64_t frames,
                    uint32_t also = 0) {
        Run r;
        for (uint64_t f = from; f < from + frames + 20; ++f) {
            uint32_t b = (f < from + frames ? start : 0u) | also;
            const bool tab = step(h, core, b, f);
            if (tab && r.tabFrames++ == 0) r.firstTab = f;
            if ((b & l3) && r.testFrames++ == 0) r.firstTest = f;
            if (f == from + frames - 1) r.heard = b;
        }
        return r;
    };

    {
        Hold h;
        const Run r = hold(h, mame, 100, 90);
        check(r.tabFrames == static_cast<int>(kPressFrames) && r.firstTab == 100 + kHoldFrames,
              "MAME: Start held 90 frames presses Tab once, 60 frames in, for 4 frames");
        check(r.heard & start, "and the game hears Start the whole time");
    }
    {
        Hold h;
        check(hold(h, mame, 100, kHoldFrames).tabFrames == 0,
              "MAME: a hold one frame short of 60 frames presses nothing");
        check(hold(h, mame, 200, 30).tabFrames == 0, "nor does a tap to join");
    }
    {
        Hold h;
        const Run first = hold(h, mame, 100, 300);
        check(first.tabFrames == static_cast<int>(kPressFrames),
              "MAME: a hold of five seconds counts once");
        check(hold(h, mame, 500, 90).tabFrames == static_cast<int>(kPressFrames),
              "a second hold after letting go counts again, which closes MAME's menu");
    }
    {
        Hold h;
        hold(h, mame, 5000, 50);
        check(hold(h, mame, 10, 90).tabFrames == static_cast<int>(kPressFrames),
              "MAME: a new game, its frames from zero, starts a hold afresh");
    }
    {
        Hold h;
        uint32_t b = l3 | bit(cab::R3) | bit(cab::B);
        step(h, mame, b, 1);
        check(!(b & l3) && (b & bit(cab::R3)) && (b & bit(cab::B)),
              "MAME: player 1's left stick click never reaches it; the rest do");
    }
    {
        Hold h;
        const std::string m289 = "mame_libretro";
        const Run r = hold(h, m289, 100, 90, l3);
        check(r.tabFrames == static_cast<int>(kPressFrames) && r.firstTab == 100 + kHoldFrames,
              "MAME 0.289: Start held 90 frames presses Tab once, 60 frames in, for 4 frames");
        check((r.heard & start) && (r.heard & l3),
              "and its left stick click, its button 7, reaches it, as Start does");
        check(hold(h, m289, 300, 90).tabFrames == static_cast<int>(kPressFrames),
              "a second hold presses Tab again, which closes its menu");
    }
    {
        Hold h;
        const Run r = hold(h, fbneo, 100, 90, l3);
        check(r.tabFrames == 0, "FBNeo: the console presses nothing, it holds Start itself");
        check((r.heard & start) && (r.heard & l3), "and hears Start and the stick click as sent");
        check(hold(h, "snes9x_libretro", 100, 90).tabFrames == 0, "nor in any other game");
    }
    {
        Hold h;
        const Run r = hold(h, board, 100, 90, l3 | bit(cab::R3));
        check(r.tabFrames == 0 && r.testFrames == static_cast<int>(kPressFrames) &&
                  r.firstTest == 100 + kHoldFrames,
              "a Naomi board: Start held 90 frames presses Test once, 60 frames in, for 4 "
              "frames, and its own stick clicks never reach it");
        check((r.heard & start) && !(r.heard & bit(cab::R3)),
              "and the board hears Start the whole time, and never Service");
        check(hold(h, board, 300, kHoldFrames).testFrames == 0,
              "a board: a hold one frame short presses nothing");
        uint32_t two = l3 | bit(cab::R3) | bit(cab::B);
        keepClicks(board, two);
        check(two == bit(cab::B), "another player's L3 and R3 never reach a board either");
        uint32_t dc = l3 | bit(cab::R3);
        keepClicks("", dc);
        Hold d;
        check(hold(d, "", 100, 90, l3).testFrames == 90 + 20 && dc == (l3 | bit(cab::R3)),
              "a Dreamcast game in Flycast, no arcade core given, keeps both and presses nothing");
    }

    std::printf("%s\n", failures ? "arcade settings test: FAILED" : "arcade settings test: passed");
    return failures ? 1 : 0;
}

}  // namespace arcadesettings
