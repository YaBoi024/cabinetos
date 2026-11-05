// The notification pill (MenuNotice, notice.h): drawn over the menus and a
// game, under the curtain and the dim (#398, restructure batch g).

#include "app.h"

#include <algorithm>
#include <cmath>

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// ---- The notification pill — see MenuNotice ------------------------
//
// Over the menus and over a game, under the curtain and the dim. Near
// the foot of the screen, inside the safe area, centred: where a
// console's confirmation is looked for, and clear of every menu's
// buttons. It rises a little as it arrives, like the panels do.
//
// After the top bar since 2026-09-26, so it sits over Settings'
// Downloads panel; the bar and the pill never meet, one at the top
// and one at the foot.
void App::drawNotice(float sc) {
    const float na = menuNotice.alpha();
    const float keepAlpha = renderer.contentAlpha();
    renderer.setContentAlpha(1.0f);   // not part of a screen's transition
    if (na > 0.01f && !menuNotice.text.empty()) {
        const ui::TextStyle st = ui::TextStyle::Callout;
        const float tw = text.measure(menuNotice.text, st, sc);
        constexpr float kPillH = 64.0f, kPad = 30.0f, kDot = 14.0f, kGap = 16.0f;
        const float w = kPad + kDot + kGap + tw + kPad;
        const float x = (ui::kCanvasWidth - w) * 0.5f;
        const float y = ui::kCanvasHeight - ui::kSafeInset - kPillH - 24.0f +
                        (1.0f - std::min(1.0f, menuNotice.age / 0.25f)) * 12.0f;
        ui::Color fill = look::surface();
        fill.a = 0.96f * na;
        ui::Rect pill{x, y, w, kPillH, kPillH * 0.5f, fill};
        pill.border = 1.5f;
        pill.borderColor = ui::Color::white(0.14f * na);
        pill.edgeLight = ui::Color::white(0.18f * na);
        pill.shadowBlur = 26.0f;
        pill.shadowOffsetY = 10.0f;
        pill.shadowColor = ui::Color::black(0.45f * na);
        renderer.draw(pill);

        // The dot says which kind of answer it is before the words do.
        ui::Color dot = ui::Color::white(0.70f);
        switch (menuNotice.tone) {
            case Tone::Done: dot = ui::palette::kScreenCyan; break;
            case Tone::Busy: {
                dot = ui::palette::kScreenCyan;
                // A slow breath, so "working" never reads as "done".
                const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
                dot.a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 4.2f));
                break;
            }
            case Tone::Info: break;
            case Tone::Problem: dot = ui::palette::kMarqueeAmber; break;
        }
        if (menuNotice.ownDot) dot = menuNotice.dot;
        dot.a *= na;
        renderer.draw(ui::Rect{x + kPad, y + (kPillH - kDot) * 0.5f, kDot, kDot,
                               kDot * 0.5f, dot});
        text.draw(renderer, menuNotice.text, x + kPad + kDot + kGap,
                  y + (kPillH - text.lineHeight(st, sc)) * 0.5f + text.ascent(st, sc),
                  st, ui::Color::white(0.94f * na), sc);
    }
    renderer.setContentAlpha(keepAlpha);
}
