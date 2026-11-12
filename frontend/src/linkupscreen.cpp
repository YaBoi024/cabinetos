// LinkUp's two screens (linkupscreen.h).

#include "linkupscreen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "look.h"
#include "screenparts.h"
#include "sound.h"
#include "startscreen.h"

namespace screens {

// ---- The top-bar screen ------------------------------------------------

namespace {

// FORK (handheld): the style a line is drawn at, a step smaller each time it is wider than the
// screen allows (a phone held upright), down to Caption1.
ui::TextStyle fitting(ui::TextRenderer& text, const std::string& line, ui::TextStyle st, float sc) {
    const float room = ui::kCanvasWidth - 2.0f * 48.0f;
    const ui::TextStyle order[] = {ui::TextStyle::LargeTitle, ui::TextStyle::Title2, ui::TextStyle::Title3,
                                   ui::TextStyle::Callout, ui::TextStyle::Caption1};
    bool started = false;
    for (ui::TextStyle s : order) {
        if (s == st) started = true;
        if (started && text.measure(line, s, sc) <= room) return s;
    }
    return ui::TextStyle::Caption1;
}

constexpr float kCellGap = 24.0f;
constexpr float kTileH = 156.0f;
constexpr float kTileRadius = 18.0f;
constexpr int kColumns = 4;
// A first friend: the stars and picture go in one long fade whose parts overlap, so it
// does not read as steps. They are quite gone, a moment passes, and the friend comes in.
constexpr float kStarsLeave = 0.45f;
constexpr float kPictureWait = 0.15f;
constexpr float kPictureLeave = 0.75f;
constexpr float kChangeGap = 0.12f;
constexpr float kChangeWait = kPictureWait + kPictureLeave + kChangeGap;
constexpr float kChangeArrive = 0.60f;
constexpr float kChangeOver = kChangeWait + kChangeArrive;

// Eight of the console's menu colours (look.cpp), each one's top and middle: those that
// stay clearly apart once all are at one brightness. Past eight friends, colours repeat.
constexpr uint32_t kFriendTops[] = {0x3A2268, 0x144E56, 0x1F4A2C, 0x5A3514, 0x581618, 0x1C3466, 0xD9487A, 0x2A8FD6};
constexpr uint32_t kFriendMids[] = {0x120C26, 0x061B1F, 0x0A1A0F, 0x1E1208, 0x1C0708, 0x0A1226, 0x3B1650, 0x0B3A48};
constexpr int kFriendColours = 8;
// Nobody online is grey, and this is the same whatever colour the menus are, so Offline
// is never taken for a friend's colour.
constexpr uint32_t kOfflineTile = 0x2B2B2E;

// A colour's hue at one brightness to the eye (Oklch), so a red does not step forward of
// a green. Its strength stops at `chroma`, so a grey stays grey.
ui::Color evenColour(uint32_t rgb, float lightness, float chroma) {
    const ui::Color col = ui::Color::rgb(rgb);
    auto linear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    const float lr = linear(col.r), lg = linear(col.g), lb = linear(col.b);
    const float cl = std::cbrt(0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb);
    const float cm = std::cbrt(0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb);
    const float co = std::cbrt(0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb);
    const float labA = 1.9779984951f * cl - 2.4285922050f * cm + 0.4505937099f * co;
    const float labB = 0.0259040371f * cl + 0.7827717662f * cm - 0.8086757660f * co;
    const float keep = std::min(1.0f, chroma / std::max(0.0001f, std::hypot(labA, labB)));
    const float a = labA * keep, b = labB * keep;
    const float l = lightness + 0.3963377774f * a + 0.2158037573f * b;
    const float m = lightness - 0.1055613458f * a - 0.0638541728f * b;
    const float o = lightness - 0.0894841775f * a - 1.2914855480f * b;
    const float xl = l * l * l, xm = m * m * m, xo = o * o * o;
    auto encoded = [](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    };
    return ui::Color{encoded(4.0767416621f * xl - 3.3077115913f * xm + 0.2309699292f * xo),
                     encoded(-1.2684380046f * xl + 2.6097574011f * xm - 0.3413193965f * xo),
                     encoded(-0.0041960863f * xl - 0.7034186147f * xm + 1.7076147010f * xo), 1.0f};
}

}  // namespace

ui::Color LinkUpScreen::dotColour(const std::vector<linkup::Friend>& friends, const std::string& name) {
    const std::vector<int> colours = linkup::coloursApart(friends, kFriendColours, 1);
    for (size_t i = 0; i < friends.size(); ++i)
        if (friends[i].name == name) return evenColour(kFriendTops[static_cast<size_t>(colours[i])], 0.74f, 0.13f);
    return ui::palette::kMarqueeAmber;
}

void LinkUpScreen::setState(const linkup::State& state, bool inUse) {
    const int wasFriend = at_ < 0 || at_ >= static_cast<int>(tiles_.size())
                              ? -1
                              : tiles_[static_cast<size_t>(at_)].friendAt;
    const bool hadFriends = !tiles_.empty();
    // During a stream the line says why nothing can be pressed, and cannot be pressed itself.
    inUse_ = inUse;
    pill_ = inUse ? "Remote Play in use" : linkup::statusLine(state.appearOffline);
    instead_ = state.instead;
    tiles_.clear();
    if (state.instead.empty()) {
        const std::vector<int> colours = linkup::coloursApart(state.friends, kFriendColours, 1);
        for (const linkup::Group& g : linkup::groups(state.friends)) {
            for (int i : g.friends) {
                const linkup::Friend& f = state.friends[static_cast<size_t>(i)];
                const bool canPress = g.status == linkup::Status::InvitedYou || g.status == linkup::Status::Online;
                FriendTile tile;
                tile.friendAt = i;
                tile.kind = g.status;
                tile.name = f.name;
                tile.pressable = canPress && !inUse;
                tile.dark = g.status == linkup::Status::Offline || inUse;
                tile.status = linkup::heading(g.status);
                if (g.status == linkup::Status::InvitedYou) tile.status += ": " + f.game;
                else if (linkup::nameOnList(state.friends, i) != f.name) tile.status += ", " + f.from;
                const size_t colour = static_cast<size_t>(colours[static_cast<size_t>(i)]);
                tile.top = evenColour(kFriendTops[colour], 0.40f, 0.07f);
                tile.bottom = evenColour(kFriendMids[colour], 0.31f, 0.055f);
                tiles_.push_back(std::move(tile));
            }
        }
    }
    // Focus follows the person when the order changes under it.
    at_ = -1;
    for (int i = 0; i < static_cast<int>(tiles_.size()); ++i)
        if (tiles_[static_cast<size_t>(i)].friendAt == wasFriend) at_ = i;
    // A first friend, or the last one going, is a different screen, and changes as the
    // top bar's screens do: the old one dissolves, then the new one rises into place.
    if (hadFriends != !tiles_.empty()) {
        sinceChange_ = 0.0f;
        if (!tiles_.empty()) at_ = 0;
    }
}

void LinkUpScreen::enter() {
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);
    at_ = tiles_.empty() ? -1 : 0;
    scroll_.settle(0.0f);
    focus_.settle(1.0f);
    sinceChange_ = kChangeOver;
}

void LinkUpScreen::settleArrival() {
    appear_.settle(1.0f);
    sinceChange_ = kChangeOver;
}

void LinkUpScreen::setHasFocus(bool on) {
    if (on == hasFocus_) return;
    hasFocus_ = on;
    if (on) moved();
}

void LinkUpScreen::moved() {
    focus_.retarget(0.0f, 0.0f);
    focus_.elapsed = 0.0f;
    focus_.retarget(1.0f, design::kFocusDuration);
}

void LinkUpScreen::tick(float dt) {
    appear_.tick(dt);
    focus_.tick(dt);
    scroll_.tick(dt);
    sinceChange_ = std::min(sinceChange_ + dt, kChangeOver);
}

Result LinkUpScreen::key(Nav n) {
    const int count = static_cast<int>(tiles_.size());
    constexpr int cols = kColumns;
    auto go = [&](int next) {
        at_ = next;
        moved();
        sound::play(sound::Cue::Move);
        return Result{};
    };
    if (n == Nav::Back) return {Action::Back, 0};
    if (at_ < 0) {
        if (n == Nav::Up) return {Action::FocusBar, 0};
        if (n == Nav::Activate && count > 0 && !inUse_) return {Action::LinkUp, kAppearOffline};
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
            if (tiles_[static_cast<size_t>(at_)].pressable)
                return {Action::LinkUp, tiles_[static_cast<size_t>(at_)].friendAt};
            break;
        case Nav::Back:
            break;
    }
    sound::play(sound::Cue::Edge);
    return {};
}

void LinkUpScreen::drawTile(Ctx& c, const FriendTile& tile, float x, float y, float w, float h, float f) {
    using linkup::Status;
    // Solid, so the background cannot muddy it; everyone at one brightness and one lift,
    // so nobody steps forward. Offline is flat and grey: not here.
    ui::Rect plate{x, y, w, h, kTileRadius, tile.dark ? ui::Color::rgb(kOfflineTile) : tile.top};
    if (!tile.dark) {
        plate.gradient = true;
        plate.fillBottom = tile.bottom;
        plate.shadowBlur = 26.0f;
        plate.shadowOffsetY = 8.0f;
        plate.shadowColor = ui::Color::black(0.40f);
    }
    c.r.draw(plate);
    if (!tile.dark) {
        // An invite's edge is amber and holds still: a glow reaches the next tile.
        const bool invite = tile.kind == Status::InvitedYou;
        ui::Rect edge{x, y, w, h, kTileRadius, ui::Color::white(0)};
        edge.border = invite ? 3.0f : 1.5f;
        edge.borderColor = invite ? ui::palette::kMarqueeAmber : ui::Color::white(0.10f);
        c.r.draw(edge);
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
    const float textX = x + kPad;
    const float textRight = x + w - kPad;
    const float nameH = c.text.lineHeight(nameStyle, c.sc);
    const float subH = c.text.lineHeight(subStyle, c.sc);
    const float top = y + (h - nameH - subH - 4.0f) * 0.5f;
    c.text.draw(c.r, c.text.truncate(tile.name, nameStyle, c.sc, textRight - textX), textX,
                top + c.text.ascent(nameStyle, c.sc), nameStyle, ui::Color::white(tile.dark ? 0.55f : 1.0f), c.sc);
    const float subBase = top + nameH + 4.0f + c.text.ascent(subStyle, c.sc);
    float subX = textX;
    if (!tile.dark) {
        constexpr float kDot = 12.0f;
        ui::Color dot = tile.kind == Status::InvitedYou ? ui::palette::kMarqueeAmber
                        : tile.kind == Status::Online   ? ui::palette::kScreenCyan
                                                        : ui::Color::white(0.85f);
        const float mid = subBase - c.text.capHeight(subStyle, c.sc) * 0.5f;
        c.r.draw(ui::Rect{subX, mid - kDot * 0.5f, kDot, kDot, kDot * 0.5f, dot});
        subX += kDot + 10.0f;
    }
    c.text.draw(c.r, c.text.truncate(tile.status, subStyle, c.sc, textRight - subX), subX, subBase, subStyle,
                ui::Color::white(tile.dark ? 0.45f : 0.65f), c.sc);
}

// The console's cabinet, with a player two who is not here yet standing behind it, and
// what the screen is for in words. Nothing here takes a press.
void LinkUpScreen::drawNoFriends(Ctx& c, float stars, float picture) {
    c.r.setContentAlpha(appear_.value() * stars);
    constexpr float kCabinetH = 340.0f;
    constexpr float kTop = 250.0f;
    const float midX = ui::kCanvasWidth * 0.5f;
    // Stars, for a screen that would otherwise stay empty: square, as pixels are, holding
    // still, the same every time, and clear of the picture and the words.
    uint32_t seed = 20261105u;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for (int i = 0; i < 110; ++i) {
        const float x = static_cast<float>(next() % static_cast<uint32_t>(ui::kCanvasWidth));
        const float y = 120.0f + static_cast<float>(next() % static_cast<uint32_t>(ui::kCanvasHeight - 120.0f));
        const float size = 3.0f + static_cast<float>(next() % 3u);
        const uint32_t kind = next() % 6u;
        const float alpha = 0.25f + static_cast<float>(next() % 56u) / 100.0f;
        if (x > midX - 200.0f && x < midX + 260.0f && y > 240.0f && y < 640.0f) continue;
        if (x > midX - 440.0f && x < midX + 440.0f && y >= 640.0f && y < 840.0f) continue;
        ui::Color col = kind == 0   ? ui::palette::kMarqueeAmber
                        : kind == 1 ? ui::Color::rgb(0xF68CB8)
                                    : ui::Color::white(1.0f);
        col.a = alpha;
        c.r.draw(ui::Rect{x, y, size, size, 0.0f, col});
    }
    c.r.setContentAlpha(appear_.value());
    const float cabinetW = kCabinetH * 527.0f / 720.0f;
    // The cabinet is stacked shapes, and made see-through they show through each other. So
    // a dim one is drawn whole and the ground comes over it, only there.
    const auto dimCabinet = [&](float x, float y) {
        setup::drawCabinetIcon(c.r, x, y, kCabinetH, 1.0f);
        constexpr float kMargin = 24.0f;
        c.r.setScissor(x - kMargin, y - kMargin, cabinetW + kMargin * 2.0f, kCabinetH + kMargin * 2.0f);
        c.r.drawBackdrop(look::backdrop(), 0.68f);
        c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth, ui::kCanvasHeight - design::kScrollClipTop);
    };
    if (instead_.empty()) {
        // The bright one is what the eye weighs, so the pair sits a little right of even.
        dimCabinet(midX - 5.0f, kTop);
        setup::drawCabinetIcon(c.r, midX - 175.0f, kTop + 28.0f, kCabinetH, 1.0f);
    } else {
        // With something wrong, one cabinet and nothing lit.
        dimCabinet(midX - cabinetW * 0.5f, kTop + 14.0f);
    }
    auto centred = [&](const std::string& line, ui::TextStyle want, float baseline, float alpha) {
        const ui::TextStyle st = fitting(c.text, line, want, c.sc);
        c.text.draw(c.r, line, midX - c.text.measure(line, st, c.sc) * 0.5f, baseline, st,
                    ui::Color::white(alpha), c.sc);
    };
    const std::string first = "Player two,", rest = " from anywhere.";
    const ui::TextStyle big = fitting(c.text, first + rest, ui::TextStyle::Title2, c.sc);
    if (instead_.empty()) {
        const float x = midX - c.text.measure(first + rest, big, c.sc) * 0.5f;
        c.text.draw(c.r, first, x, 740.0f, big, ui::palette::kMarqueeAmber, c.sc);
        c.text.draw(c.r, rest, x + c.text.measure(first, big, c.sc), 740.0f, big, ui::Color::white(0.96f), c.sc);
        centred("Whoever runs your server adds friends", ui::TextStyle::Title3, 804.0f, 0.60f);
    } else {
        // What is wrong, and on a second line what to do when the words say.
        const size_t split = instead_.find(". ");
        centred(split == std::string::npos ? instead_ : instead_.substr(0, split + 1), big, 740.0f, 0.96f);
        if (split != std::string::npos)
            centred(instead_.substr(split + 2), ui::TextStyle::Title3, 804.0f, 0.60f);
    }
    // The cabinet is stacked shapes, and faded one by one they show through each other and
    // come apart. So it stays whole, and the ground comes over it.
    if (picture < 1.0f) c.r.drawBackdrop(look::backdrop(), 1.0f - picture);
}

void LinkUpScreen::drawGlass(Ctx& c) {
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
    // What the change between friends and no friends has reached: 1 at rest.
    const bool noFriends = tiles_.empty();
    // The last friend going leaves nothing to wait for.
    const float arriving = design::easeInOut((sinceChange_ - (noFriends ? 0.0f : kChangeWait)) / kChangeArrive);
    if (noFriends) {
        drawNoFriends(c, arriving, arriving);
    } else if (sinceChange_ < kPictureWait + kPictureLeave) {
        // The stars first, then the picture.
        drawNoFriends(c, 1.0f - design::easeInOut(sinceChange_ / kStarsLeave),
                      1.0f - design::easeInOut((sinceChange_ - kPictureWait) / kPictureLeave));
    }
    c.r.setContentAlpha(appear_.value() * arriving);
    if (noFriends) {
        c.r.setContentAlpha(1.0f);
        c.r.clearScissor();
        return;
    }
    const float rise = (1.0f - arriving) * design::kDetailRise;

    constexpr int cols = kColumns;
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const float cellW = (usable - kCellGap * (cols - 1)) / cols;
    const float pitch = kTileH + kCellGap;
    const float gridTop = design::kSwitcherTop + pillHeight(c.text, c.sc) + design::kSwitcherGapBelow;

    const int count = static_cast<int>(tiles_.size());
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
    // With nobody to see it, the switch has nothing to say.
    drawPill(c, pill_, design::kLibraryInset, design::kSwitcherTop - scroll + rise, true,
             at_ < 0 && hasFocus_ ? f : 0.0f);

    // The focused one last, so it grows over its neighbours.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < count; ++i) {
            const bool on = i == at_ && hasFocus_;
            if ((pass == 1) != on) continue;
            const float cellX = design::kLibraryInset + static_cast<float>(i % cols) * (cellW + kCellGap);
            const float cellY = gridTop + static_cast<float>(i / cols) * pitch - scroll + rise;
            const float ff = on ? f : 0.0f;
            const float s = 1.0f + ff * (design::kPillFocusScale - 1.0f);
            const float w = cellW * s, h = kTileH * s;
            drawTile(c, tiles_[static_cast<size_t>(i)], cellX - (w - cellW) * 0.5f, cellY - (h - kTileH) * 0.5f, w, h, ff);
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
    note_.clear();
    friendSide_ = friendSide;
    clock_ = 0.0f;
    fade_ = 1.0f;
}

void LinkUpPlayScreen::tick(float dt) { clock_ += dt; }

Result LinkUpPlayScreen::key(Nav n) {
    if (shows_ == Shows::Inviting) return n == Nav::Back ? Result{Action::LinkUp, kCancelInvite} : Result{};
    if (resumes_ && n == Nav::Activate) return {Action::LinkUp, kResumeBoth};
    // The friend's menu, on B as on the button that opens the pause menu.
    if (n == Nav::Back) return {Action::LinkUp, kLeave};
    return {};
}

void LinkUpPlayScreen::draw(Ctx& c) {
    c.r.setContentAlpha(fade_);
    const float midX = ui::kCanvasWidth * 0.5f;
    auto centred = [&](const std::string& s, ui::TextStyle want, float baseline, float alpha) {
        const ui::TextStyle st = fitting(c.text, s, want, c.sc);
        c.text.draw(c.r, s, midX - c.text.measure(s, st, c.sc) * 0.5f, baseline, st,
                    ui::Color::white(alpha), c.sc);
    };

    if (shows_ == Shows::Picture) {
        if (!standIn_) {
            c.r.setContentAlpha(1.0f);
            return;
        }
        // Stands in for the game, where there is no program to show one.
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
        c.r.setContentAlpha(1.0f);
        return;
    }

    const ui::TextStyle big = ui::TextStyle::LargeTitle;
    const float base = ui::kCanvasHeight * 0.5f - 20.0f;
    centred(title_, big, base, 0.96f);
    float y = base + 84.0f;
    if (!detail_.empty()) {
        centred(detail_, ui::TextStyle::Title3, y, 0.60f);
        y += 56.0f;
    }
    if (!note_.empty()) {
        centred(note_, ui::TextStyle::Callout, y, 0.60f);
        y += 56.0f;
    }
    // Three dots in turn on every one of these: working, not stuck.
    constexpr float kDot = 16.0f, kDotGap = 22.0f;
    const float x0 = midX - (kDot * 3.0f + kDotGap * 2.0f) * 0.5f;
    for (int i = 0; i < 3; ++i) {
        const float phase = clock_ * 3.0f - static_cast<float>(i) * 0.7f;
        const float alpha = 0.25f + 0.65f * (0.5f + 0.5f * std::sin(phase));
        c.r.draw(ui::Rect{x0 + static_cast<float>(i) * (kDot + kDotGap), y - kDot, kDot, kDot,
                          kDot * 0.5f, ui::Color::white(alpha)});
    }
    // Said rather than focused: nobody moved focus here. Above where the pill sits.
    if (shows_ == Shows::Inviting || friendSide_)
        drawPressPrompt(c.r, c.text, c.sc, "Press", shows_ == Shows::Inviting ? "to cancel" : "to leave",
                        ui::kCanvasHeight - ui::kSafeInset - 130.0f, 0.80f, "B");
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
