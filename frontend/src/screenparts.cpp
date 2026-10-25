#include "screenparts.h"

#include <algorithm>

#include "look.h"
#include "text.h"
#include "ui.h"

// "PRESS (A) TO ...", centred, with the A drawn as a button badge the way
// consoles prompt: a white disc with a dark A, on the text's x-height. Home's
// empty state says "Press (A) to open the Library" this way, and a game held
// after a state loads says "Press (A) to continue".
void drawPressPrompt(ui::Renderer& r, ui::TextRenderer& text, float sc,
                     const char* before, const char* after, float base,
                     float alpha) {
    const ui::TextStyle st = ui::TextStyle::Title3;
    const float gap = 16.0f;
    const float badge = text.lineHeight(st, sc) * 0.95f;
    const float w1 = text.measure(before, st, sc);
    const float w2 = text.measure(after, st, sc);
    float x = (ui::kCanvasWidth - (w1 + gap + badge + gap + w2)) * 0.5f;
    text.draw(r, before, x, base, st, ui::Color::white(0.92f * alpha), sc);
    x += w1 + gap;
    const float capMid = base - text.ascent(st, sc) * 0.36f;
    r.draw(ui::Rect{x, capMid - badge * 0.5f, badge, badge, badge * 0.5f,
                    ui::Color::white(0.95f * alpha)});
    const float aw = text.measure("A", st, sc);
    text.draw(r, "A", x + (badge - aw) * 0.5f, capMid + text.ascent(st, sc) * 0.36f, st,
              ui::Color{0.07f, 0.05f, 0.12f, alpha}, sc);
    x += badge + gap;
    text.draw(r, after, x, base, st, ui::Color::white(0.92f * alpha), sc);
}

namespace screens {

using design::Card;

// Two caption lines with reserved space, so rows stay aligned whether a title
// wraps or not. One line truncated almost every real title at these widths —
// the reference implementation records that as a mistake already made.
//
// Greedy, and it breaks on spaces only. A CJK title has none, so it falls
// through to the truncating path below, which is the right answer for it: a
// Japanese title is short in characters and long in pixels.
std::vector<std::string> wrapTwoLines(ui::TextRenderer& text, const std::string& s,
                                      ui::TextStyle style, float sc, float width) {
    std::vector<std::string> lines;
    if (text.measure(s, style, sc) <= width) {
        lines.push_back(s);
        return lines;
    }

    // Break opportunities: after a space, and AFTER A HYPHEN. The hyphen is not
    // typographic pedantry — this library holds "TurboGrafx-16" and
    // "TurboGrafx-CD", which have no space between them and their only
    // distinguishing part, so a space-only wrap rendered both tiles as
    // "TurboGraf..." and made them the same tile to look at.
    //
    // A CJK title has neither, and falls through to the truncating path below,
    // which is the right answer for it: those are short in characters and long
    // in pixels.
    size_t brk = std::string::npos;
    std::string line;
    for (size_t i = 1; i <= s.size(); ++i) {
        const bool afterSpace = (i < s.size() && s[i] == ' ');
        const bool afterHyphen = (s[i - 1] == '-');
        if (!afterSpace && !afterHyphen) continue;
        const std::string candidate = s.substr(0, i);
        if (text.measure(candidate, style, sc) > width) break;
        // A space is dropped at the break; a hyphen stays on the line it ends.
        brk = afterSpace ? i + 1 : i;
        line = candidate;
    }
    if (brk == std::string::npos || brk >= s.size()) {
        // One unbreakable run. Truncate it rather than overflowing the column.
        lines.push_back(text.truncate(s, style, sc, width));
        return lines;
    }
    lines.push_back(line);
    lines.push_back(text.truncate(s.substr(brk), style, sc, width));
    return lines;
}

// The artwork focus treatment: lift, shadow, and a rim unless the element is a
// composite one. A rectangle drawn around a button whose label mixes art with
// its own text always crosses the text somewhere, which is why a platform tile
// and the hero card get the scale and the shadow and no rim.
void applyArtworkFocus(ui::Rect& rect, float f, bool rim) {
    rect.shadowBlur = f * design::kFocusShadowBlur;
    rect.shadowOffsetY = f * design::kFocusShadowOffsetY;
    rect.shadowColor = ui::Color::black(0.55f * f);
    if (rim) {
        rect.border = f * design::kFocusRimWidth;
        rect.borderColor = ui::palette::kFocusRim;
    }
}

// A cover drawn into a box: the coloured panel that shows while the art decodes
// or if there is none, the art over it, and the rim on top — the rim is the
// focus indicator and nothing may sit above it.
// The kept mark, top-right of a cover. See design::kKeptMark*.
//
// TOP-RIGHT AND NOT BOTTOM-RIGHT: box art puts its title at the top and its
// publisher logos and rating badges along the bottom, so the bottom corners are
// where a mark is most likely to land on something that matters. The top right
// is nearly always sky, or a border, or nothing.
void drawKeptMark(Ctx& c, const Card& card, float x, float y, float w, float radius) {
    if (!card.kept) return;
    const float d = design::kKeptMarkSize;
    const float mx = x + w - design::kKeptMarkInset - d;
    const float my = y + design::kKeptMarkInset;
    // The ring first and wider, so the dot reads against pale artwork too.
    const float r = design::kKeptMarkRing;
    c.r.draw(ui::Rect{mx - r, my - r, d + r * 2.0f, d + r * 2.0f,
                      (d + r * 2.0f) * 0.5f, ui::Color::black(0.55f)});
    c.r.draw(ui::Rect{mx, my, d, d, d * 0.5f, ui::palette::kScreenCyan});
}

void drawCover(Ctx& c, const Card& card, float x, float y, float w, float h,
               float radius, float f, bool rim, bool large) {
    ui::Rect panel{x, y, w, h, radius, card.art};
    applyArtworkFocus(panel, f, rim);
    c.r.draw(panel);
    // `large` asks for the 810x1080 original instead of the 162x216 thumbnail,
    // and only the launch screen does: it draws a 340x460 cover, which is 680
    // real pixels wide on a 4K panel. A shelf or a grid cover is small enough
    // that the thumbnail holds up and cheap enough that dozens stay resident.
    const std::string& key =
        (large && !card.coverLarge.empty()) ? card.coverLarge : card.cover;
    if (!key.empty()) {
        ui::drawImage(c.r, c.images.get(key), x, y, w, h, ui::Fit::Fill, 1.0f, radius);
        // Unfocused artwork sits back. See design::kRestArtDim.
        if (f < 1.0f)
            c.r.draw(ui::Rect{x, y, w, h, radius,
                              ui::Color::black(look::restDim(design::kRestArtDim) * (1.0f - f))});
    }
    // THE MARK AND THE GREY WITH OR WITHOUT ART. They came after a return
    // for a game with no cover, so a coverless greyed arcade set showed as
    // bright as a playable game and a coverless kept game had no dot
    // (#398, HOME-08).
    drawKeptMark(c, card, x, y, w, radius);
    // GREYED: a game this console cannot play until a controller is paired.
    // Held back whether focused or not, so focus lands on it and it still
    // reads as unavailable; its name says why.
    if (!card.unavailable.empty())
        c.r.draw(ui::Rect{x, y, w, h, radius, ui::Color::black(design::kUnavailableDim)});
    // Only over something drawn on the panel: the panel's own rim is the
    // same rim, and a second one on bare panel is a brighter edge.
    if (f > 0.0f && rim && (!key.empty() || !card.unavailable.empty())) {
        ui::Rect edge{x, y, w, h, radius, ui::Color::white(0)};
        edge.border = f * design::kFocusRimWidth;
        edge.borderColor = ui::palette::kFocusRim;
        c.r.draw(edge);
    }
}

// THE FOCUSED GAME'S LINE beside a heading: Home's shelves, a grid and Search
// each name the one card focus is on there, and they name it the same way.
// The name, then RomM's tags dimmer when `tags` (the caller's hasTwin, #383),
// then the system after a middle dot when `system`, then a greyed game's
// reason dimmer when `reason`. The system is at the name's brightness: it is
// part of the answer to "which game is this", where the tags and the reason
// are notes on it.
//
// `system` is for a list that mixes systems under a heading that names none
// (Home's shelves, a collection, Search, #579). A platform's own grid leaves
// it out: its heading already says it.
//
// THE NAME IS WHAT GETS SHORTENED. Everything after it always shows, so a long
// title cannot push the system or the reason off the end of the line.
void drawFocusedLine(ui::Renderer& r, ui::TextRenderer& text, float sc, const Card& card,
                     bool tags, bool system, bool reason, float x, float baseline, float room,
                     float nameAlpha) {
    const ui::TextStyle st = ui::TextStyle::Callout;
    const float gap = 24.0f;
    const std::string sys =
        (system && !card.platform.empty()) ? "   \xC2\xB7   " + card.platform : "";
    const bool showTags = tags && !card.tagLabel.empty();
    const bool showReason = reason && !card.unavailable.empty();
    const float tagsW = showTags ? gap + text.measure(card.tagLabel, st, sc) : 0.0f;
    const float sysW = sys.empty() ? 0.0f : text.measure(sys, st, sc);
    const float reasonW = showReason ? gap + text.measure(card.unavailable, st, sc) : 0.0f;
    const std::string name =
        text.truncate(card.title, st, sc, std::max(0.0f, room - tagsW - sysW - reasonW));
    text.draw(r, name, x, baseline, st, ui::Color::white(nameAlpha), sc);
    x += text.measure(name, st, sc);
    if (showTags) {
        text.draw(r, card.tagLabel, x + gap, baseline, st, ui::Color::white(0.45f), sc);
        x += tagsW;
    }
    if (!sys.empty()) {
        text.draw(r, sys, x, baseline, st, ui::Color::white(nameAlpha), sc);
        x += sysW;
    }
    if (showReason)
        text.draw(r, card.unavailable, x + gap, baseline, st, ui::Color::white(0.45f), sc);
}

}  // namespace screens
