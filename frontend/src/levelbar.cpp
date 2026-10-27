// FORK (handheld): the volume and brightness bar (levelbar.h), and the three
// App calls that watch for a change and draw it.

#include "levelbar.h"

#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "app.h"
#include "volume.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace levelbar {
namespace {

constexpr float kHold = 1.6f;     // seconds fully shown
constexpr float kRise = 0.12f;
constexpr float kFall = 0.35f;

long readNumber(const std::string& path) {
    long v = -1;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        if (std::fscanf(f, "%ld", &v) != 1) v = -1;
        std::fclose(f);
    }
    return v;
}

}  // namespace

int backlightPercent() {
    // The first backlight that has a range. Looked for once; a machine with
    // none (a television) is not looked at again.
    static std::string dir;
    static bool looked = false;
    if (!looked) {
        looked = true;
        if (DIR* d = ::opendir("/sys/class/backlight")) {
            while (dirent* e = ::readdir(d)) {
                if (e->d_name[0] == '.') continue;
                const std::string p = std::string("/sys/class/backlight/") + e->d_name;
                if (readNumber(p + "/max_brightness") > 0) {
                    dir = p;
                    break;
                }
            }
            ::closedir(d);
        }
        if (!dir.empty()) std::fprintf(stderr, "[brightness] watching %s\n", dir.c_str());
    }
    if (dir.empty()) return -1;
    const long max = readNumber(dir + "/max_brightness");
    const long now = readNumber(dir + "/brightness");
    if (max <= 0 || now < 0) return -1;
    return static_cast<int>((now * 100 + max / 2) / max);
}

void Bar::show(std::string what, int pct) {
    label = std::move(what);
    percent = std::clamp(pct, 0, 100);
    // Already up (a held key, a second press): stay up. Only a bar that was gone
    // rises from nothing, or it would flash on every repeat.
    if (alpha() > 0.0f) age = std::min(age, kRise);
    else age = 0.0f;
    if (age >= kRise) age = kRise;
}

float Bar::alpha() const {
    if (label.empty()) return 0.0f;
    if (age < kRise) return age / kRise;
    if (age < kRise + kHold) return 1.0f;
    const float f = (age - kRise - kHold) / kFall;
    return f >= 1.0f ? 0.0f : 1.0f - f;
}

}  // namespace levelbar

// Once a frame: a volume key was pressed, or the backlight moved.
void App::tickLevelBars(float dt) {
    if (const unsigned g = volume::generation(); g != volumeBarSeen) {
        volumeBarSeen = g;
        levelBar.show(volume::muted() ? "Muted" : "Volume", volume::muted() ? 0 : volume::percent());
    }
    backlightLook -= dt;
    if (backlightLook <= 0.0f) {
        backlightLook = 0.2f;
        const int p = levelbar::backlightPercent();
        // The first reading is only the starting point.
        if (p >= 0 && backlightSeen >= 0 && p != backlightSeen) levelBar.show("Brightness", p);
        if (p >= 0) backlightSeen = p;
    }
    levelBar.tick(dt);
}

void App::drawLevelBar(float sc) {
    const float a = levelBar.alpha();
    if (a < 0.01f) return;
    const float keepAlpha = renderer.contentAlpha();
    renderer.setContentAlpha(1.0f);

    const ui::TextStyle st = ui::TextStyle::Callout;
    constexpr float kW = 600.0f, kH = 64.0f, kPad = 30.0f, kBarH = 10.0f, kGap = 22.0f;
    // Above the notice pill, which keeps its place (notice.cpp).
    const float x = (ui::kCanvasWidth - kW) * 0.5f;
    const float y = ui::kCanvasHeight - ui::kSafeInset - 64.0f - 24.0f - 14.0f - kH;
    ui::Color fill = look::surface();
    fill.a = 0.96f * a;
    ui::Rect pill{x, y, kW, kH, kH * 0.5f, fill};
    pill.border = 1.5f;
    pill.borderColor = ui::Color::white(0.14f * a);
    pill.edgeLight = ui::Color::white(0.18f * a);
    pill.shadowBlur = 26.0f;
    pill.shadowOffsetY = 10.0f;
    pill.shadowColor = ui::Color::black(0.45f * a);
    renderer.draw(pill);

    const float labelW = text.measure("Brightness", st, sc);
    const float pctW = text.measure("100%", st, sc);
    const float baseline = y + (kH - text.lineHeight(st, sc)) * 0.5f + text.ascent(st, sc);
    text.draw(renderer, levelBar.label, x + kPad, baseline, st, ui::Color::white(0.94f * a), sc);
    const float trackX = x + kPad + labelW + kGap;
    const float trackW = kW - 2.0f * kPad - labelW - pctW - 2.0f * kGap;
    const float trackY = y + (kH - kBarH) * 0.5f;
    renderer.draw(ui::Rect{trackX, trackY, trackW, kBarH, kBarH * 0.5f, ui::Color::white(0.18f * a)});
    if (levelBar.percent > 0) {
        ui::Color on = ui::palette::kScreenCyan;
        on.a *= a;
        renderer.draw(ui::Rect{trackX, trackY, std::max(kBarH, trackW * levelBar.percent / 100.0f),
                               kBarH, kBarH * 0.5f, on});
    }
    const std::string pct = std::to_string(levelBar.percent) + "%";
    text.draw(renderer, pct, x + kW - kPad - text.measure(pct, st, sc), baseline, st,
              ui::Color::white(0.80f * a), sc);
    renderer.setContentAlpha(keepAlpha);
}
