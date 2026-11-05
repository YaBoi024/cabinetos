// LinkUp's two screens (linkupscreen.h).

#include "linkupscreen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

#include "look.h"
#include "screenparts.h"
#include "sound.h"

namespace screens {

namespace {

constexpr const char* kChevron = "\xE2\x80\xBA";
constexpr float kHeadingGap = 28.0f;   // above a heading, which is nearer what it names
constexpr float kHeadingBelow = 10.0f;

float rowHeight(Ctx& c, const std::string& detail) {
    float h = design::kRowPadY * 2.0f + c.text.lineHeight(ui::TextStyle::Title3, c.sc);
    if (!detail.empty())
        h += design::kSettingsDetailGap + c.text.lineHeight(ui::TextStyle::Callout, c.sc);
    return h;
}

}  // namespace

// ---- The top-bar screen ------------------------------------------------

void LinkUpScreen::setState(const linkup::State& state, bool inUse) {
    const int wasFriend = line_ < 0 ? -1 : lines_[static_cast<size_t>(line_)].friendAt;
    state_ = state;
    inUse_ = inUse;
    lines_.clear();
    if (state_.instead.empty()) {
        for (const linkup::Group& g : linkup::groups(state_.friends)) {
            Line h;
            h.heading = linkup::heading(g.status);
            lines_.push_back(h);
            for (int i : g.friends) {
                const linkup::Friend& f = state_.friends[static_cast<size_t>(i)];
                Line l;
                l.friendAt = i;
                l.name = linkup::nameOnList(state_.friends, i);
                switch (g.status) {
                    case linkup::Status::InvitedYou:
                        l.detail = f.game;
                        l.value = inUse_ ? linkup::kInUse : "";
                        l.pressable = !inUse_;
                        l.chevron = !inUse_;
                        l.dim = inUse_;
                        break;
                    case linkup::Status::Online:
                        l.value = inUse_ ? linkup::kInUse : "";
                        l.pressable = !inUse_;
                        l.chevron = !inUse_;
                        l.dim = inUse_;
                        break;
                    case linkup::Status::InGame:
                        break;
                    case linkup::Status::Offline:
                        l.dim = true;
                        break;
                }
                lines_.push_back(std::move(l));
            }
        }
    }
    // Focus stays on the same friend when they are still there to press.
    line_ = -1;
    if (wasFriend >= 0)
        for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
            if (lines_[static_cast<size_t>(i)].pressable &&
                lines_[static_cast<size_t>(i)].friendAt == wasFriend)
                line_ = i;
}

int LinkUpScreen::firstPressable() const {
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
        if (lines_[static_cast<size_t>(i)].pressable) return i;
    return -1;
}

// Focus lands on the friends, which are what the screen is for.
void LinkUpScreen::enter() {
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);
    line_ = firstPressable();
    scroll_.settle(0.0f);
    focus_.settle(1.0f);
}

void LinkUpScreen::setHasFocus(bool on) {
    if (on == hasFocus_) return;
    hasFocus_ = on;
    if (on) moveFocus();
}

void LinkUpScreen::moveFocus() {
    focus_.retarget(0.0f, 0.0f);
    focus_.elapsed = 0.0f;
    focus_.retarget(1.0f, design::kFocusDuration);
}

void LinkUpScreen::tick(float dt) {
    appear_.tick(dt);
    focus_.tick(dt);
    scroll_.tick(dt);
}

Result LinkUpScreen::key(Nav n) {
    if (line_ < 0) {
        switch (n) {
            case Nav::Up:
                return {Action::FocusBar, 0};
            case Nav::Down:
            case Nav::Left:
                sound::play(sound::Cue::Edge);
                return {};
            case Nav::Right: {
                const int first = firstPressable();
                if (first < 0) { sound::play(sound::Cue::Edge); return {}; }
                line_ = first;
                moveFocus();
                sound::play(sound::Cue::Move);
                return {};
            }
            case Nav::Back:
                return {Action::Back, 0};
            case Nav::Activate:
                return {Action::LinkUp, kAppearOffline};
        }
        return {};
    }
    switch (n) {
        case Nav::Up:
        case Nav::Down: {
            const int d = n == Nav::Down ? 1 : -1;
            int next = line_ + d;
            while (next >= 0 && next < static_cast<int>(lines_.size()) &&
                   !lines_[static_cast<size_t>(next)].pressable)
                next += d;
            if (next < 0 || next >= static_cast<int>(lines_.size())) {
                if (d < 0) return {Action::FocusBar, 0};
                sound::play(sound::Cue::Edge);
                return {};
            }
            line_ = next;
            moveFocus();
            sound::play(sound::Cue::Move);
            return {};
        }
        case Nav::Left:
            line_ = -1;
            moveFocus();
            sound::play(sound::Cue::Move);
            return {};
        case Nav::Back:
            return {Action::Back, 0};
        case Nav::Right:
            sound::play(sound::Cue::Edge);
            return {};
        case Nav::Activate:
            return {Action::LinkUp, lines_[static_cast<size_t>(line_)].friendAt};
    }
    return {};
}

void LinkUpScreen::drawGlass(Ctx& c) {
    const float a = appear_.value();
    c.r.setContentAlpha(a);
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);

    const float top = design::kContentTop;
    const float f = focus_.value();
    const ui::TextStyle big = ui::TextStyle::Title3;
    const ui::TextStyle small = ui::TextStyle::Callout;

    // ---- Left: Appear Offline, as Settings' list is drawn ----
    {
        const float itemH = c.text.lineHeight(big, c.sc) + design::kSettingsListPadY * 2.0f;
        const float w0 = design::kSettingsListWidth;
        const bool focused = line_ < 0 && hasFocus_;
        const float ff = focused ? f : 0.0f;
        const float s = 1.0f + ff * (design::kRowFocusScale - 1.0f);
        const float w = w0 * s, h = itemH * s;
        const float x = design::settingsListX() - (w - w0) * 0.5f;
        const float iy = top - (h - itemH) * 0.5f;
        if (focused)
            c.r.drawGlass(ui::Rect{x, iy, w, h, design::kRowRadius, ui::Color::white(0)},
                          design::kRegularMaterialBlur,
                          ui::Color::white(design::kFocusedTint * ff));
        const float base = iy + design::kSettingsListPadY * s + c.text.ascent(big, c.sc);
        c.text.draw(c.r, "Status", design::kContentInset, base, big,
                    ui::Color::white(focused ? 1.0f : 0.80f), c.sc);
        const std::string value = state_.appearOffline ? "Offline" : "Online";
        const float vw = c.text.measure(value, small, c.sc);
        c.text.draw(c.r, value, x + w - design::kRowPadX - vw, base, small, ui::Color::white(0.60f), c.sc);
    }

    // ---- Right: the friends ----
    const float px = design::settingsPaneX(), pw = design::settingsPaneW();
    const float gap = design::kDetailRowGap;

    if (!state_.instead.empty() || lines_.empty()) {
        const std::string& line = state_.instead.empty() ? std::string("Ask the server's owner to add friends")
                                                         : state_.instead;
        const float h = rowHeight(c, "");
        c.r.drawGlass(ui::Rect{px, top, pw, h, design::kRowRadius, ui::Color::white(0)},
                      design::kRegularMaterialBlur, ui::Color::white(0.08f));
        c.text.draw(c.r, c.text.truncate(line, small, c.sc, pw - design::kRowPadX * 2.0f),
                    px + design::kRowPadX,
                    top + (h - c.text.lineHeight(small, c.sc)) * 0.5f + c.text.ascent(small, c.sc),
                    small, ui::Color::white(0.80f), c.sc);
        c.r.setContentAlpha(1.0f);
        c.r.clearScissor();
        return;
    }

    float contentH = 0.0f, focusTop = 0.0f, focusBottom = 0.0f;
    const float headH = c.text.lineHeight(small, c.sc);
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        const Line& l = lines_[static_cast<size_t>(i)];
        if (!l.heading.empty()) {
            contentH += (i == 0 ? 0.0f : kHeadingGap) + headH + kHeadingBelow;
            continue;
        }
        const float h = rowHeight(c, l.detail);
        if (i == line_) { focusTop = contentH; focusBottom = contentH + h; }
        contentH += h + gap;
    }
    const float room = ui::kCanvasHeight - design::kScreenBottomPad - top;
    const float maxScroll = std::max(0.0f, contentH - gap - room);
    float want = scroll_.to;
    if (line_ >= 0) {
        if (focusBottom - want > room) want = focusBottom - room;
        else if (focusTop - want < 0.0f) want = focusTop;
    }
    want = std::clamp(want, 0.0f, maxScroll);
    if (std::fabs(want - scroll_.to) > 0.5f) scroll_.retarget(want, design::kFocusDuration);

    float y = top - scroll_.value();
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        const Line& l = lines_[static_cast<size_t>(i)];
        if (!l.heading.empty()) {
            if (i > 0) y += kHeadingGap;
            c.text.draw(c.r, l.heading, px + design::kRowPadX * 0.5f, y + c.text.ascent(small, c.sc),
                        small, ui::Color::white(0.55f), c.sc);
            y += headH + kHeadingBelow;
            continue;
        }
        const float rh = rowHeight(c, l.detail);
        const bool on = i == line_ && hasFocus_;
        const float rf = on ? f : 0.0f;
        const float s = 1.0f + rf * (design::kRowFocusScale - 1.0f);
        const float w = pw * s, h = rh * s;
        const float x = px - (w - pw) * 0.5f;
        const float ry = y - (h - rh) * 0.5f;
        c.r.drawGlass(ui::Rect{x, ry, w, h, design::kRowRadius, ui::Color::white(0)},
                      design::kRegularMaterialBlur,
                      ui::Color::white(l.dim ? 0.04f : 0.08f + 0.14f * rf));
        const float textA = l.dim ? design::kSettingsUnbuiltAlpha : 1.0f;
        const float titleBase = ry + design::kRowPadY * s + c.text.ascent(big, c.sc);
        float right = x + w - design::kRowPadX;
        if (l.chevron) {
            right -= c.text.measure(kChevron, big, c.sc);
            c.text.draw(c.r, kChevron, right, titleBase, big, ui::Color::white(0.30f), c.sc);
            right -= 16.0f;
        }
        if (!l.value.empty()) {
            right -= c.text.measure(l.value, small, c.sc);
            c.text.draw(c.r, l.value, right, titleBase, small,
                        ui::Color::white((on ? 1.0f : 0.60f) * textA), c.sc);
            right -= 24.0f;
        }
        const float textX = x + design::kRowPadX;
        c.text.draw(c.r, c.text.truncate(l.name, big, c.sc, std::max(0.0f, right - textX)), textX,
                    titleBase, big, ui::Color::white((on ? 1.0f : 0.92f) * textA), c.sc);
        if (!l.detail.empty()) {
            const float base = ry + design::kRowPadY * s + c.text.lineHeight(big, c.sc) +
                               design::kSettingsDetailGap + c.text.ascent(small, c.sc);
            c.text.draw(c.r, c.text.truncate(l.detail, small, c.sc, w - design::kRowPadX * 2.0f),
                        textX, base, small, ui::Color::white(0.60f * textA), c.sc);
        }
        y += rh + gap;
    }
    c.r.setContentAlpha(1.0f);
    c.r.clearScissor();
}

// ---- The same friends as a grid, each in their own colour ----------------

namespace {

// The console's brighter menu colours (look.cpp): their top and their middle, so a
// friend's tile looks like a small piece of a colour a person can give their menus.
constexpr uint32_t kFriendTops[] = {0xC2367E, 0x1E7FD0, 0x3F8A1C, 0xC0581A,
                                    0xD9487A, 0x2A8FD6, 0x2E9A5A, 0xD0661A};
constexpr uint32_t kFriendBottoms[] = {0x3A0F28, 0x0A2644, 0x14300A, 0x3E1C08,
                                       0x3B1650, 0x0B3A48, 0x0C3A44, 0x4A1010};
constexpr int kFriendColours = 8;

constexpr float kCellGap = 24.0f;
constexpr float kTileH = 156.0f;
constexpr float kTileRadius = 18.0f;
constexpr float kCabW = 176.0f;
constexpr float kCabH = kCabW * 338.0f / 280.0f;   // the mark's own proportions
constexpr float kCabLabelGap = 18.0f;

// The console's sixteen menu colours (look.cpp): each one's top and middle.
constexpr uint32_t kThemeTops[] = {0x3A2268, 0x1C3466, 0x144E56, 0x1F4A2C, 0x5A3514, 0x581618, 0x5A1C3A, 0x34363C,
                                   0xC2367E, 0x1E7FD0, 0x3F8A1C, 0xC0581A, 0xD9487A, 0x2A8FD6, 0x2E9A5A, 0xD0661A};
constexpr uint32_t kThemeMids[] = {0x120C26, 0x0A1226, 0x061B1F, 0x0A1A0F, 0x1E1208, 0x1C0708, 0x1E0A14, 0x121316,
                                   0x3A0F28, 0x0A2644, 0x14300A, 0x3E1C08, 0x3B1650, 0x0B3A48, 0x0C3A44, 0x4A1010};

ui::Color fromHsl(float hue, float s, float l, float keep) {
    const float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    const float h = std::fmod(hue, 360.0f) / 60.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    const float m = l - c * 0.5f;
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = c; g = x; }
    else if (h < 2) { r = x; g = c; }
    else if (h < 3) { g = c; b = x; }
    else if (h < 4) { g = x; b = c; }
    else if (h < 5) { r = x; b = c; }
    else { r = c; b = x; }
    return ui::Color{(r + m) * keep, (g + m) * keep, (b + m) * keep, 1.0f};
}

// A colour at one brightness to the eye whatever its hue (Oklch), so a red does not step
// forward of a green.
ui::Color oklch(float radians, float lightness, float chroma) {
    const float a = chroma * std::cos(radians), b = chroma * std::sin(radians);
    const float l = lightness + 0.3963377774f * a + 0.2158037573f * b;
    const float m = lightness - 0.1055613458f * a - 0.0638541728f * b;
    const float o = lightness - 0.0894841775f * a - 1.2914855480f * b;
    const float cl = l * l * l, cm = m * m * m, co = o * o * o;
    auto encoded = [](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    };
    return ui::Color{encoded(4.0767416621f * cl - 3.3077115913f * cm + 0.2309699292f * co),
                     encoded(-1.2684380046f * cl + 2.6097574011f * cm - 0.3413193965f * co),
                     encoded(-0.0041960863f * cl - 0.7034186147f * cm + 1.7076147010f * co), 1.0f};
}

// From the colour wheel, by where its reds, yellows, greens and so on sit on Oklch's own.
ui::Color evenColour(float wheelDegrees, float lightness, float chroma) {
    static constexpr float kOnOklch[] = {29, 110, 142, 195, 264, 328, 389};
    const float along = std::clamp(wheelDegrees, 0.0f, 359.9f) / 60.0f;
    const int from = static_cast<int>(along);
    const float degrees = kOnOklch[from] + (kOnOklch[from + 1] - kOnOklch[from]) * (along - static_cast<float>(from));
    return oklch(degrees * 3.14159265f / 180.0f, lightness, chroma);
}

// From a colour already chosen: its hue, at that brightness and strength.
ui::Color evenColour(uint32_t rgb, float lightness, float chroma) {
    const ui::Color col = ui::Color::rgb(rgb);
    auto linear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    const float r = linear(col.r), g = linear(col.g), b = linear(col.b);
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float o = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    const float labA = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * o;
    const float labB = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * o;
    return oklch(std::atan2(labB, labA), lightness, chroma);
}

// A little of the console's own surface mixed in, so a colour sits down in the room.
ui::Color softened(uint32_t rgb, float keep) {
    const ui::Color col = ui::Color::rgb(rgb);
    const ui::Color ground = ui::Color::rgb(0x241A3D);
    const float mix = 0.68f;
    return ui::Color{(col.r * mix + ground.r * (1.0f - mix)) * keep, (col.g * mix + ground.g * (1.0f - mix)) * keep,
                     (col.b * mix + ground.b * (1.0f - mix)) * keep, 1.0f};
}

ui::Color shaded(uint32_t rgb, float keep) {
    const ui::Color col = ui::Color::rgb(rgb);
    return ui::Color{col.r * keep, col.g * keep, col.b * keep, 1.0f};
}

// The mark (docs/assets/mark.svg), drawn from its own numbers so it can light up.
void drawCabinet(Ctx& c, float x, float y, float w, const linkup::Status kind, bool dark, float clock,
                 float f) {
    const float k = w / 280.0f;
    auto at = [&](float left, float top, float width, float height, float radius, ui::Color fill) {
        return ui::Rect{x + (left - 160.0f) * k, y + (top - 146.0f) * k, width * k, height * k, radius * k, fill};
    };
    using linkup::Status;
    ui::Rect body = at(175, 150, 250, 330, 20, dark ? ui::Color::rgb(0x2A2440) : ui::Color::rgb(0xF4EFE0));
    if (!dark) {
        const float breath = 0.5f + 0.5f * std::sin(clock * 3.2f);
        const bool invited = kind == Status::InvitedYou;
        body.shadowBlur = invited ? 44.0f + 26.0f * breath : 40.0f;
        body.shadowColor = kind == Status::InGame ? ui::palette::kScreenCyan : ui::palette::kMarqueeAmber;
        body.shadowColor.a = invited ? 0.45f + 0.5f * breath : 0.38f;
    }
    c.r.draw(body);
    ui::Rect marquee = at(193, 168, 214, 34, 0, dark ? ui::Color::rgb(0x3A3252) : ui::Color::rgb(0xF68CB8));
    if (!dark) {
        marquee.gradient = true;
        marquee.fillBottom = ui::Color::rgb(0xFBC27A);
    }
    c.r.draw(marquee);
    const bool playing = kind == Status::InGame && !dark;
    ui::Rect screen = at(193, 213, 214, 159, 0,
                         dark ? ui::Color::rgb(0x15102A)
                              : playing ? ui::Color::rgb(0x1B6E8C) : ui::Color::rgb(0xFFD36A));
    if (!dark) {
        screen.gradient = true;
        screen.fillBottom = playing ? ui::Color::rgb(0x0B2A4A) : ui::Color::rgb(0xEF5F38);
    }
    c.r.draw(screen);
    if (playing) {
        const float travel = std::fmod(clock * 60.0f, 159.0f - 22.0f);
        c.r.draw(at(193, 213 + travel, 214, 22, 0, ui::Color{0.35f, 0.91f, 0.96f, 0.55f}));
    }
    c.r.draw(at(165, 390, 270, 48, 10, dark ? ui::Color::rgb(0x241E38) : ui::Color::rgb(0xDAD4C3)));
    if (f > 0.0f) {
        ui::Rect rim = at(175, 150, 250, 330, 20, ui::Color::white(0));
        rim.border = f * design::kFocusRimWidth;
        rim.borderColor = ui::palette::kFocusRim;
        c.r.draw(rim);
    }
}

}  // namespace

void LinkUpGrid::setState(const linkup::State& state, bool inUse) {
    const int wasFriend = at_ < 0 || at_ >= static_cast<int>(cabs_.size()) ? -1 : cabs_[static_cast<size_t>(at_)].friendAt;
    pill_ = linkup::statusLine(state.appearOffline);
    emptyLine_ = state.instead.empty() ? "Ask the server's owner to add friends" : state.instead;
    cabs_.clear();
    if (state.instead.empty()) {
        const int n = std::max(1, static_cast<int>(state.friends.size()));
        const std::vector<int> wheel = linkup::coloursApart(state.friends, 360, std::min(40, 180 / n));
        const std::vector<int> themes = linkup::coloursApart(state.friends, 16, 1);
        for (const linkup::Group& g : linkup::groups(state.friends)) {
            for (int i : g.friends) {
                const linkup::Friend& f = state.friends[static_cast<size_t>(i)];
                const bool canPress = g.status == linkup::Status::InvitedYou || g.status == linkup::Status::Online;
                Cab cab;
                cab.friendAt = i;
                cab.kind = g.status;
                cab.name = f.name;
                cab.pressable = canPress && !inUse;
                cab.dark = g.status == linkup::Status::Offline || (canPress && inUse);
                cab.status = linkup::heading(g.status);
                if (canPress && inUse) cab.status = linkup::kInUse;
                else if (g.status == linkup::Status::InvitedYou) cab.status += ": " + f.game;
                else if (linkup::nameOnList(state.friends, i) != f.name) cab.status += ", " + f.from;
                const float hue = static_cast<float>(wheel[static_cast<size_t>(i)]);
                const float keep = cab.dark ? 0.42f : 1.0f;
                switch (palette_) {
                    case Palette::Bright: {
                        const int colour = linkup::colourOf(f, kFriendColours);
                        cab.top = shaded(kFriendTops[colour], cab.dark ? 0.30f : 1.0f);
                        cab.bottom = shaded(kFriendBottoms[colour], cab.dark ? 0.30f : 1.0f);
                        break;
                    }
                    case Palette::Soft:
                        cab.top = fromHsl(hue, 0.34f, 0.36f, keep);
                        cab.bottom = fromHsl(hue, 0.36f, 0.17f, keep);
                        cab.evenTop = evenColour(hue, 0.40f, 0.07f);
                        cab.evenBottom = evenColour(hue, 0.31f, 0.055f);
                        break;
                    case Palette::Themes: {
                        const size_t t = static_cast<size_t>(themes[static_cast<size_t>(i)]);
                        cab.top = softened(kThemeTops[t], keep);
                        cab.bottom = softened(kThemeMids[t], keep);
                        cab.evenTop = evenColour(kThemeTops[t], 0.40f, 0.07f);
                        cab.evenBottom = evenColour(kThemeMids[t], 0.31f, 0.055f);
                        break;
                    }
                    case Palette::Dusk: {
                        const float near = 200.0f + hue / 360.0f * 140.0f;
                        cab.top = fromHsl(near, 0.38f, 0.37f, keep);
                        cab.bottom = fromHsl(near, 0.40f, 0.16f, keep);
                        break;
                    }
                    case Palette::Jewel:
                        cab.top = fromHsl(hue, 0.52f, 0.27f, keep);
                        cab.bottom = fromHsl(hue, 0.58f, 0.11f, keep);
                        break;
                }
                cabs_.push_back(std::move(cab));
            }
        }
    }
    // Focus follows the person when the order changes under it.
    at_ = -1;
    for (int i = 0; i < static_cast<int>(cabs_.size()); ++i)
        if (cabs_[static_cast<size_t>(i)].friendAt == wasFriend) at_ = i;
}

void LinkUpGrid::enter() {
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);
    at_ = cabs_.empty() ? -1 : 0;
    scroll_.settle(0.0f);
    focus_.settle(1.0f);
}

void LinkUpGrid::setHasFocus(bool on) {
    if (on == hasFocus_) return;
    hasFocus_ = on;
    if (on) moved();
}

void LinkUpGrid::moved() {
    focus_.retarget(0.0f, 0.0f);
    focus_.elapsed = 0.0f;
    focus_.retarget(1.0f, design::kFocusDuration);
}

void LinkUpGrid::tick(float dt) {
    clock_ += dt;
    appear_.tick(dt);
    focus_.tick(dt);
    scroll_.tick(dt);
}

Result LinkUpGrid::key(Nav n) {
    const int count = static_cast<int>(cabs_.size());
    const int cols = columns();
    auto go = [&](int next) {
        at_ = next;
        moved();
        sound::play(sound::Cue::Move);
        return Result{};
    };
    if (n == Nav::Back) return {Action::Back, 0};
    if (at_ < 0) {
        if (n == Nav::Up) return {Action::FocusBar, 0};
        if (n == Nav::Activate) return {Action::LinkUp, LinkUpScreen::kAppearOffline};
        if (n == Nav::Down && count > 0) return go(0);
        sound::play(sound::Cue::Edge);
        return {};
    }
    switch (n) {
        case Nav::Up:
            return go(at_ < cols ? -1 : at_ - cols);
        case Nav::Down:
            if (at_ / cols == (count - 1) / cols) break;
            return go(std::min(at_ + cols, count - 1));
        case Nav::Left:
            if (at_ % cols == 0) break;
            return go(at_ - 1);
        case Nav::Right:
            if (at_ % cols == cols - 1 || at_ + 1 >= count) break;
            return go(at_ + 1);
        case Nav::Activate:
            if (cabs_[static_cast<size_t>(at_)].pressable)
                return {Action::LinkUp, cabs_[static_cast<size_t>(at_)].friendAt};
            break;
        case Nav::Back:
            break;
    }
    sound::play(sound::Cue::Edge);
    return {};
}

void LinkUpGrid::drawTile(Ctx& c, const Cab& cab, float x, float y, float w, float h, float f) {
    using linkup::Status;
    const float breath = 0.5f + 0.5f * std::sin(clock_ * 3.2f);
    ui::Rect plate{x, y, w, h, kTileRadius, look_ == Look::Colour ? cab.top : look::surface()};
    if (look_ == Look::Colour) {
        plate.gradient = true;
        plate.fillBottom = cab.bottom;
    }
    // An invite breathes, so the eye goes to it; focus lifts a tile off the room.
    if (cab.kind == Status::InvitedYou && !cab.dark) {
        plate.shadowBlur = 34.0f + 24.0f * breath;
        plate.shadowColor = ui::palette::kMarqueeAmber;
        plate.shadowColor.a = 0.35f + 0.5f * breath;
    } else if (f > 0.0f) {
        plate.shadowBlur = design::kFocusShadowBlur;
        plate.shadowOffsetY = design::kFocusShadowOffsetY;
        plate.shadowColor = ui::Color::black(0.5f * f);
    }
    const bool solid = solid_ && look_ == Look::Wash && (palette_ == Palette::Soft || palette_ == Palette::Themes);
    if (solid) {
        // Solid, so the background cannot muddy it; everyone at one brightness and one
        // lift, so nobody steps forward. Offline has no colour at all: not here.
        ui::Rect tile{x, y, w, h, kTileRadius, cab.dark ? look::surface() : cab.evenTop};
        if (!cab.dark) {
            tile.gradient = true;
            tile.fillBottom = cab.evenBottom;
            tile.shadowBlur = 26.0f;
            tile.shadowOffsetY = 8.0f;
            tile.shadowColor = ui::Color::black(0.40f);
        }
        c.r.draw(tile);
        if (!cab.dark) {
            ui::Rect edge{x, y, w, h, kTileRadius, ui::Color::white(0)};
            // An invite's edge is amber and holds still: a glow reaches the next tile.
            const bool invite = cab.kind == Status::InvitedYou;
            edge.border = invite ? 3.0f : 1.5f;
            edge.borderColor = invite ? ui::palette::kMarqueeAmber : ui::Color::white(0.10f);
            c.r.draw(edge);
        }
    } else {
        c.r.draw(plate);
    }
    if (!solid && look_ == Look::Wash) {
        // A platform's tile with its cover gone: the colour comes down from the top.
        ui::Color from = cab.top;
        from.a = 0.80f;
        ui::Rect wash{x, y, w, h, kTileRadius, from};
        wash.gradient = true;
        wash.fillBottom = ui::Color{cab.bottom.r, cab.bottom.g, cab.bottom.b, 0.55f};
        c.r.draw(wash);
    }
    if (look_ == Look::Rise) {
        // The colour rises from the foot, so the name sits on the console's own dark.
        ui::Rect rise{x, y, w, h, kTileRadius, ui::Color{cab.bottom.r, cab.bottom.g, cab.bottom.b, 0.05f}};
        rise.gradient = true;
        rise.fillBottom = ui::Color{cab.top.r, cab.top.g, cab.top.b, 0.85f};
        c.r.draw(rise);
    }
    if (look_ == Look::Platform) {
        // Where a platform's tile has its cover blurred behind it, a wash of the colour.
        ui::Color from = cab.top;
        from.a = 0.42f;
        ui::Rect wash{x, y, w, h, kTileRadius, from};
        wash.gradient = true;
        wash.fillBottom = ui::Color{cab.bottom.r, cab.bottom.g, cab.bottom.b, 0.30f};
        c.r.draw(wash);
    }
    if (look_ == Look::Fade) {
        ui::Color from = cab.top;
        from.a = 0.85f;
        ui::Rect fade{x, y, w, h, kTileRadius, from};
        fade.gradient = true;
        fade.fillBottom = ui::Color{cab.top.r, cab.top.g, cab.top.b, 0.0f};
        c.r.draw(fade);
    }
    if (f > 0.0f) {
        ui::Rect rim{x, y, w, h, kTileRadius, ui::Color::white(0)};
        rim.border = f * design::kFocusRimWidth;
        rim.borderColor = ui::palette::kFocusRim;
        c.r.draw(rim);
    }

    const ui::TextStyle nameStyle = ui::TextStyle::Title3;
    const ui::TextStyle subStyle = ui::TextStyle::Footnote;
    constexpr float kPad = 24.0f;
    float textX = x + kPad;
    float textRight = x + w - kPad;
    if (look_ == Look::Platform) {
        // The cover's place and the cover's size, holding their colour and their letter.
        const float scale = h / design::kTileHeight;
        const float artH = design::kTileArtHeight * scale;
        const float artW = artH * 0.75f;
        const float artX = x + w - design::kTilePadding * scale - artW;
        const float artY = y + (h - artH) * 0.5f;
        ui::Rect art{artX, artY, artW, artH, 8.0f, cab.top};
        art.gradient = true;
        art.fillBottom = cab.bottom;
        art.edgeLight = ui::Color::white(cab.dark ? 0.06f : 0.22f);
        c.r.draw(art);
        const std::string letter(1, static_cast<char>(std::toupper(static_cast<unsigned char>(cab.name[0]))));
        const ui::TextStyle letterStyle = ui::TextStyle::LargeTitle;
        c.text.draw(c.r, letter, artX + (artW - c.text.measure(letter, letterStyle, c.sc)) * 0.5f,
                    artY + (artH - c.text.lineHeight(letterStyle, c.sc)) * 0.5f + c.text.ascent(letterStyle, c.sc),
                    letterStyle, ui::Color::white(cab.dark ? 0.50f : 1.0f), c.sc);
        textRight = artX - design::kTileArtGap * scale;
    }
    if (look_ == Look::Disc) {
        const float d = h - 56.0f;
        const float dx = x + kPad, dy = y + (h - d) * 0.5f;
        ui::Rect disc{dx, dy, d, d, d * 0.5f, cab.top};
        disc.gradient = true;
        disc.fillBottom = cab.bottom;
        c.r.draw(disc);
        if (!cab.dark) {
            ui::Rect ring{dx - 5.0f, dy - 5.0f, d + 10.0f, d + 10.0f, (d + 10.0f) * 0.5f, ui::Color::white(0)};
            ring.border = 4.0f;
            ring.borderColor = cab.kind == Status::InvitedYou ? ui::palette::kMarqueeAmber
                               : cab.kind == Status::Online   ? ui::palette::kScreenCyan
                                                              : ui::Color::white(0.55f);
            c.r.draw(ring);
        }
        const std::string letter(1, static_cast<char>(std::toupper(static_cast<unsigned char>(cab.name[0]))));
        const ui::TextStyle letterStyle = ui::TextStyle::Title2;
        c.text.draw(c.r, letter, dx + (d - c.text.measure(letter, letterStyle, c.sc)) * 0.5f,
                    dy + (d - c.text.lineHeight(letterStyle, c.sc)) * 0.5f + c.text.ascent(letterStyle, c.sc),
                    letterStyle, ui::Color::white(cab.dark ? 0.55f : 1.0f), c.sc);
        textX = dx + d + 22.0f;
    }
    const float room = textRight - textX;
    const float nameH = c.text.lineHeight(nameStyle, c.sc);
    const float subH = c.text.lineHeight(subStyle, c.sc);
    const float top = y + (h - nameH - subH - 4.0f) * 0.5f;
    c.text.draw(c.r, c.text.truncate(cab.name, nameStyle, c.sc, room), textX, top + c.text.ascent(nameStyle, c.sc),
                nameStyle, ui::Color::white(cab.dark ? 0.55f : 1.0f), c.sc);
    const float subBase = top + nameH + 4.0f + c.text.ascent(subStyle, c.sc);
    float subX = textX;
    // The ring says it on the Disc; the other looks say it with a dot before the words.
    if (look_ != Look::Disc && !cab.dark) {
        constexpr float kDot = 12.0f;
        ui::Color dot = cab.kind == Status::InvitedYou ? ui::palette::kMarqueeAmber
                        : cab.kind == Status::Online   ? ui::palette::kScreenCyan
                                                       : ui::Color::white(0.85f);
        const float mid = subBase - c.text.capHeight(subStyle, c.sc) * 0.5f;
        c.r.draw(ui::Rect{subX, mid - kDot * 0.5f, kDot, kDot, kDot * 0.5f, dot});
        subX += kDot + 10.0f;
    }
    c.text.draw(c.r, c.text.truncate(cab.status, subStyle, c.sc, textRight - subX), subX, subBase, subStyle,
                ui::Color::white(cab.dark ? 0.45f : look_ == Look::Colour ? 0.88f : 0.65f), c.sc);
}

void LinkUpGrid::drawGlass(Ctx& c) {
    c.r.setContentAlpha(appear_.value());
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);

    const int cols = columns();
    const bool cabinets = look_ == Look::Cabinet;
    const ui::TextStyle nameStyle = ui::TextStyle::Title3;
    const ui::TextStyle subStyle = ui::TextStyle::Footnote;
    const float nameH = c.text.lineHeight(nameStyle, c.sc);
    const float subH = c.text.lineHeight(subStyle, c.sc);
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const bool platform = look_ == Look::Platform;
    const float gap = platform ? design::kTileSpacing : kCellGap;
    const float tileH = platform ? design::kTileHeight : kTileH;
    const float cellW = cabinets ? usable / cols : (usable - gap * (cols - 1)) / cols;
    const float pitch = cabinets ? kCabH + kCabLabelGap + nameH + subH + 44.0f : tileH + gap;
    const float gridTop = design::kSwitcherTop + pillHeight(c.text, c.sc) + design::kSwitcherGapBelow;

    const int count = static_cast<int>(cabs_.size());
    const int rows = count == 0 ? 0 : (count - 1) / cols + 1;
    const float maxScroll = std::max(0.0f, gridTop + rows * pitch + design::kScreenBottomPad - ui::kCanvasHeight);
    float want = 0.0f;
    if (at_ >= 0) {
        const float rowBottom = gridTop + (at_ / cols + 1) * pitch;
        want = std::max(0.0f, rowBottom + design::kScreenBottomPad - ui::kCanvasHeight);
    }
    want = std::clamp(want, 0.0f, maxScroll);
    if (std::fabs(want - scroll_.to) > 0.5f) scroll_.retarget(want, design::kFocusDuration);
    const float scroll = scroll_.value();

    const float f = focus_.value();
    drawPill(c, pill_, design::kLibraryInset, design::kSwitcherTop - scroll, true,
             at_ < 0 && hasFocus_ ? f : 0.0f);

    if (cabs_.empty()) {
        const float w = c.text.measure(emptyLine_, nameStyle, c.sc);
        c.text.draw(c.r, emptyLine_, (ui::kCanvasWidth - w) * 0.5f, ui::kCanvasHeight * 0.5f, nameStyle,
                    ui::Color::white(0.60f), c.sc);
        c.r.setContentAlpha(1.0f);
        c.r.clearScissor();
        return;
    }

    // The focused one last, so it grows over its neighbours.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < count; ++i) {
            const bool on = i == at_ && hasFocus_;
            if ((pass == 1) != on) continue;
            const Cab& cab = cabs_[static_cast<size_t>(i)];
            const float cellX = design::kLibraryInset + static_cast<float>(i % cols) * (cabinets ? cellW : cellW + gap);
            const float cellY = gridTop + static_cast<float>(i / cols) * pitch - scroll;
            const float ff = on ? f : 0.0f;
            if (!cabinets) {
                const float s = 1.0f + ff * (design::kPillFocusScale - 1.0f);
                const float w = cellW * s, h = tileH * s;
                drawTile(c, cab, cellX - (w - cellW) * 0.5f, cellY - (h - tileH) * 0.5f, w, h, ff);
                continue;
            }
            const float s = 1.0f + ff * (design::kFocusScale - 1.0f);
            const float w = kCabW * s, h = kCabH * s;
            const float midX = cellX + cellW * 0.5f;
            drawCabinet(c, midX - w * 0.5f, cellY + kCabH - h, w, cab.kind, cab.dark, clock_, ff);
            const float room = cellW - 20.0f;
            const std::string name = c.text.truncate(cab.name, nameStyle, c.sc, room);
            const std::string sub = c.text.truncate(cab.status, subStyle, c.sc, room);
            const float nameBase = cellY + kCabH + kCabLabelGap + c.text.ascent(nameStyle, c.sc);
            c.text.draw(c.r, name, midX - c.text.measure(name, nameStyle, c.sc) * 0.5f, nameBase, nameStyle,
                        ui::Color::white(cab.dark ? 0.55f : (on ? 1.0f : 0.92f)), c.sc);
            c.text.draw(c.r, sub, midX - c.text.measure(sub, subStyle, c.sc) * 0.5f,
                        nameBase + nameH - c.text.ascent(nameStyle, c.sc) + c.text.ascent(subStyle, c.sc), subStyle,
                        ui::Color::white(0.60f * (cab.dark ? 0.8f : 1.0f)), c.sc);
        }
    }
    c.r.setContentAlpha(1.0f);
    c.r.clearScissor();
}

// ---- The friend's console, with no picture to show ---------------------

void LinkUpPlayScreen::open(Shows shows, std::string title, std::string detail, bool friendSide) {
    shows_ = shows;
    title_ = std::move(title);
    detail_ = std::move(detail);
    friendSide_ = friendSide;
    clock_ = 0.0f;
}

void LinkUpPlayScreen::tick(float dt) { clock_ += dt; }

Result LinkUpPlayScreen::key(Nav n) {
    if (shows_ == Shows::Inviting) return n == Nav::Back ? Result{Action::LinkUp, kCancelInvite} : Result{};
    // The friend's only menu, on B as on the button that opens the pause menu.
    if (n == Nav::Back) return {Action::LinkUp, kLeave};
    return {};
}

void LinkUpPlayScreen::draw(Ctx& c) {
    const float midX = ui::kCanvasWidth * 0.5f;
    auto centred = [&](const std::string& s, ui::TextStyle st, float baseline, float alpha) {
        c.text.draw(c.r, s, midX - c.text.measure(s, st, c.sc) * 0.5f, baseline, st,
                    ui::Color::white(alpha), c.sc);
    };

    if (shows_ == Shows::Picture) {
        // Stands in for the game until the LinkUp program's window is here.
        ui::Rect ground{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0, ui::Color::rgb(0x1B3A5C)};
        ground.gradient = true;
        ground.fillBottom = ui::Color::rgb(0x0E1B2C);
        c.r.draw(ground);
        for (int i = 0; i < 9; ++i) {
            const float t = clock_ * 0.4f + static_cast<float>(i) * 0.7f;
            const float bx = 160.0f + static_cast<float>(i) * 200.0f;
            const float by = 520.0f + std::sin(t) * 120.0f;
            c.r.draw(ui::Rect{bx, by, 90.0f, 90.0f, 14.0f, ui::Color::white(0.05f)});
        }
        centred(title_, ui::TextStyle::LargeTitle, 480.0f, 0.22f);
        centred("The game's picture goes here", ui::TextStyle::Callout, 540.0f, 0.18f);
        return;
    }

    const ui::TextStyle big = ui::TextStyle::LargeTitle;
    const float base = ui::kCanvasHeight * 0.5f - 20.0f;
    centred(title_, big, base, 0.96f);
    float y = base + 58.0f;
    if (!detail_.empty()) {
        centred(detail_, ui::TextStyle::Title3, y, 0.60f);
        y += 56.0f;
    }
    if (shows_ == Shows::Reconnecting) {
        // Three dots in turn: working, not stuck.
        constexpr float kDot = 16.0f, kDotGap = 22.0f;
        const float x0 = midX - (kDot * 3.0f + kDotGap * 2.0f) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const float phase = clock_ * 3.0f - static_cast<float>(i) * 0.7f;
            const float alpha = 0.25f + 0.65f * (0.5f + 0.5f * std::sin(phase));
            c.r.draw(ui::Rect{x0 + static_cast<float>(i) * (kDot + kDotGap), y - kDot, kDot, kDot,
                              kDot * 0.5f, ui::Color::white(alpha)});
        }
    }
    // Said rather than focused: nobody moved focus here. Above where the pill sits.
    if (shows_ == Shows::Inviting || friendSide_)
        drawPressPrompt(c.r, c.text, c.sc, "Press", shows_ == Shows::Inviting ? "to cancel" : "to leave",
                        ui::kCanvasHeight - ui::kSafeInset - 130.0f, 0.80f, "B");
}

}  // namespace screens
