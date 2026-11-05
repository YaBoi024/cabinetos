// LinkUp's two screens (linkupscreen.h).

#include "linkupscreen.h"

#include <algorithm>
#include <cmath>

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
        const std::string& line = state_.instead.empty() ? std::string("No friends yet")
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

// ---- The same friends as arcade cabinets --------------------------------

namespace {

constexpr int kCabColumns = 6;
constexpr float kCabCell = (ui::kCanvasWidth - design::kLibraryInset * 2.0f) / kCabColumns;
constexpr float kCabW = 176.0f;
constexpr float kCabH = kCabW * 338.0f / 280.0f;   // the mark's own proportions
constexpr float kCabLabelGap = 18.0f;

// The mark (docs/assets/mark.svg), drawn from its own numbers so it can light up.
void drawCabinet(Ctx& c, float x, float y, float w, const linkup::Status kind, bool dark, float clock,
                 float f) {
    const float k = w / 280.0f;
    auto at = [&](float left, float top, float width, float height, float radius, ui::Color fill) {
        return ui::Rect{x + (left - 160.0f) * k, y + (top - 146.0f) * k, width * k, height * k, radius * k, fill};
    };
    using linkup::Status;
    ui::Color bodyFill = dark ? ui::Color::rgb(0x2A2440) : ui::Color::rgb(0xF4EFE0);
    ui::Rect body = at(175, 150, 250, 330, 20, bodyFill);
    if (!dark) {
        const float breath = 0.5f + 0.5f * std::sin(clock * 3.2f);
        const bool invited = kind == Status::InvitedYou;
        const bool playing = kind == Status::InGame;
        body.shadowBlur = invited ? 44.0f + 26.0f * breath : 40.0f;
        body.shadowColor = playing ? ui::palette::kScreenCyan : ui::palette::kMarqueeAmber;
        body.shadowColor.a = invited ? 0.45f + 0.5f * breath : 0.38f;
    } else {
        body.shadowBlur = 18.0f;
        body.shadowOffsetY = 8.0f;
        body.shadowColor = ui::Color::black(0.4f);
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
        // A game on its screen: a band of light travelling down it.
        const float travel = std::fmod(clock * 60.0f, 159.0f - 22.0f);
        c.r.draw(at(193, 213 + travel, 214, 22, 0, ui::Color{0.35f, 0.91f, 0.96f, 0.55f}));
    }

    c.r.draw(at(165, 390, 270, 48, 10, dark ? ui::Color::rgb(0x241E38) : ui::Color::rgb(0xDAD4C3)));
    const float a = dark ? 0.35f : 1.0f;
    const auto dim = [&](uint32_t rgb) {
        ui::Color col = ui::Color::rgb(rgb);
        col.a = a;
        return col;
    };
    c.r.draw(at(210, 403, 22, 22, 11, dim(0x3D3548)));
    c.r.draw(at(282, 403, 22, 22, 11, dim(0xE8395A)));
    c.r.draw(at(320, 403, 22, 22, 11, dim(0xF5B82E)));
    c.r.draw(at(371, 405, 31, 18, 5, dim(0xFBE7CC)));

    if (f > 0.0f) {
        ui::Rect rim = at(175, 150, 250, 330, 20, ui::Color::white(0));
        rim.border = f * design::kFocusRimWidth;
        rim.borderColor = ui::palette::kFocusRim;
        c.r.draw(rim);
    }
}

}  // namespace

void LinkUpCabinets::setState(const linkup::State& state, bool inUse) {
    const int wasFriend = at_ < 0 || at_ >= static_cast<int>(cabs_.size()) ? -1 : cabs_[static_cast<size_t>(at_)].friendAt;
    pill_ = linkup::statusLine(state.appearOffline);
    emptyLine_ = state.instead.empty() ? "No friends yet" : state.instead;
    cabs_.clear();
    if (state.instead.empty()) {
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
                else if (linkup::nameOnList(state.friends, i) != f.name) cab.status = f.from;
                cabs_.push_back(std::move(cab));
            }
        }
    }
    // Focus follows the person when the order changes under it.
    at_ = -1;
    for (int i = 0; i < static_cast<int>(cabs_.size()); ++i)
        if (cabs_[static_cast<size_t>(i)].friendAt == wasFriend) at_ = i;
}

void LinkUpCabinets::enter() {
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);
    at_ = cabs_.empty() ? -1 : 0;
    scroll_.settle(0.0f);
    focus_.settle(1.0f);
}

void LinkUpCabinets::setHasFocus(bool on) {
    if (on == hasFocus_) return;
    hasFocus_ = on;
    if (on) moved();
}

void LinkUpCabinets::moved() {
    focus_.retarget(0.0f, 0.0f);
    focus_.elapsed = 0.0f;
    focus_.retarget(1.0f, design::kFocusDuration);
}

void LinkUpCabinets::tick(float dt) {
    clock_ += dt;
    appear_.tick(dt);
    focus_.tick(dt);
    scroll_.tick(dt);
}

Result LinkUpCabinets::key(Nav n) {
    const int count = static_cast<int>(cabs_.size());
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
            return go(at_ < kCabColumns ? -1 : at_ - kCabColumns);
        case Nav::Down:
            if (at_ / kCabColumns == (count - 1) / kCabColumns) break;
            return go(std::min(at_ + kCabColumns, count - 1));
        case Nav::Left:
            if (at_ % kCabColumns == 0) break;
            return go(at_ - 1);
        case Nav::Right:
            if (at_ % kCabColumns == kCabColumns - 1 || at_ + 1 >= count) break;
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

void LinkUpCabinets::drawGlass(Ctx& c) {
    c.r.setContentAlpha(appear_.value());
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
    const ui::TextStyle nameStyle = ui::TextStyle::Title3;
    const ui::TextStyle subStyle = ui::TextStyle::Footnote;
    const float nameH = c.text.lineHeight(nameStyle, c.sc);
    const float subH = c.text.lineHeight(subStyle, c.sc);
    const float pitch = kCabH + kCabLabelGap + nameH + subH + 44.0f;
    const float gridTop = design::kSwitcherTop + pillHeight(c.text, c.sc) + design::kSwitcherGapBelow;

    const int count = static_cast<int>(cabs_.size());
    const int rows = count == 0 ? 0 : (count - 1) / kCabColumns + 1;
    const float maxScroll = std::max(0.0f, gridTop + rows * pitch + design::kScreenBottomPad - ui::kCanvasHeight);
    float want = 0.0f;
    if (at_ >= 0) {
        const float rowBottom = gridTop + (at_ / kCabColumns + 1) * pitch;
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
            const float cellX = design::kLibraryInset + static_cast<float>(i % kCabColumns) * kCabCell;
            const float cellY = gridTop + static_cast<float>(i / kCabColumns) * pitch - scroll;
            const float ff = on ? f : 0.0f;
            const float s = 1.0f + ff * (design::kFocusScale - 1.0f);
            const float w = kCabW * s, h = kCabH * s;
            const float midX = cellX + kCabCell * 0.5f;
            drawCabinet(c, midX - w * 0.5f, cellY + kCabH - h, w, cab.kind, cab.dark, clock_, ff);

            const float room = kCabCell - 20.0f;
            const std::string name = c.text.truncate(cab.name, nameStyle, c.sc, room);
            const std::string sub = c.text.truncate(cab.status, subStyle, c.sc, room);
            const float textA = cab.dark ? 0.55f : (on ? 1.0f : 0.92f);
            const float nameBase = cellY + kCabH + kCabLabelGap + c.text.ascent(nameStyle, c.sc);
            c.text.draw(c.r, name, midX - c.text.measure(name, nameStyle, c.sc) * 0.5f, nameBase, nameStyle,
                        ui::Color::white(textA), c.sc);
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
