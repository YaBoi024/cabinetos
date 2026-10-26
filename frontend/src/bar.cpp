// The top bar: its keys, its destinations, the account chip, Offline and
// the download ring (#398, restructure batch d). The controllers' battery
// row inside it is batteryrow.cpp.

#include "app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "batteryrow.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {
// The step from one destination's start to the next: its width plus this.
constexpr float kBarItemGap = 60.0f;
}  // namespace

// THE BAR'S OWN KEYS. Offered every key first whenever it holds focus, from
// whichever screen is underneath — which is the whole point of its cursor
// living here rather than in a screen.
auto App::barKey(screens::Nav n) -> bool {
    switch (n) {
        case screens::Nav::Left:
        case screens::Nav::Right: {
            const int d = (n == screens::Nav::Right) ? 1 : -1;
            const int next = std::clamp(barSlot + d, 0, BarCount - 1);
            if (next == barSlot) { sound::play(sound::Cue::Edge); return true; }
            barSlot = next;
            // MOVING ACROSS THE BAR SWITCHES THE SCREEN UNDER IT — MMagTech,
            // 2026-09-24: the shoulders already did, and the d-pad made
            // you press A on each one. The Apple TV's top bar works this
            // way too: A only drops you into the screen.
            //
            // Only a MOVE switches. Arriving in the bar with Up lands on
            // the screen you are on, so it has nothing to switch.
            // And the account chip opens a panel rather than going
            // anywhere, so sliding onto it opens nothing.
            if (barSlot != BarAccount) {
                const int want = barSlot;
                if (destinationGoing() != want) {
                    transitionTo(want);
                    barFocused = true;
                }
            }
            sound::play(sound::Cue::Move);
            return true;
        }
        // DOWN AND BACK BOTH LEAVE IT, and neither leaves the screen. Back
        // out of the bar going to the previous screen would mean a person
        // who walked up to look at it could not simply come back down.
        case screens::Nav::Down:
        case screens::Nav::Back:
            barFocused = false;
            sound::play(n == screens::Nav::Down ? sound::Cue::Move
                                                : sound::Cue::Back);
            return true;
        case screens::Nav::Up:
            sound::play(sound::Cue::Edge);
            return true;
        case screens::Nav::Activate:
            if (barSlot == BarAccount) {
                // WHO IS PLAYING. Rebuilt from the store every time it
                // opens rather than cached: the list is three lines of
                // JSON and a stale switcher is a switcher that signs the
                // console in as somebody who has been removed.
                //
                // **barFocused STAYS TRUE, and setting it false was a bug.**
                // MMagTech, 2026-09-22: *"when i click the user image the
                // recent game expands like its being selected."* It was —
                // dropping bar focus told everything underneath that focus
                // had come back to it, so Home lit its card while the panel
                // was open, and the chip that had just been pressed lost
                // its own rim. Focus is on the chip; the panel is what the
                // chip opened.
                refreshAccountRows();
                accountScreen.open();
                accountsOpen = true;
                sound::play(sound::Cue::Activate);
                return true;
            }
            if (barSlot <= BarSettings) {
                const int d = barSlot;
                barFocused = false;
                // Already standing in it: drop back into the screen rather
                // than rebuilding it under the person's feet.
                if (destinationGoing() == d) {
                    sound::play(sound::Cue::Move);
                    return true;
                }
                transitionTo(d);
                sound::play(sound::Cue::Activate);
                return true;
            }
            break;
    }
    return false;
}

// ---- The top bar, which is its own strip ----------------------------
//
// MMagTech, 2026-09-21: *"the top bar with library settings and search
// should not be on the hero and be its own separate thing at the top."*
//
// It used to be drawn over the hero's top edge on a black scrim, and
// the scrim was the tell: a strip of black laid across a piece of
// artwork to make text readable is a header bar that has been put
// somewhere it does not belong. It has its own room now, at the safe
// inset, and NO SURFACE UNDER IT — which is the one thing worth taking
// from how Valve handle chrome. The destinations sit on the backdrop
// like the shelf headings do, and the backdrop is already blurred and
// scrimmed for exactly that reason.
//
// IT DRAWS ON EVERY BROWSING SCREEN, not only on Home — 2026-09-21.
// MMagTech: *"when you switch from home to library the bar text at the
// top should be the same."* It is chrome, and chrome that appears and
// disappears as you move between destinations is not chrome, it is
// decoration on one screen. Keeping it fixed also does half the work of
// making the move to the Library read as smooth: the frame stays still
// and only what is inside it changes.
//
// NOT ON THE LAUNCH SCREEN. docs/PROJECT.md is explicit that game
// detail "is a full-screen cover, not a push — it replaces the screen
// entirely, with the artwork as its own backdrop". A bar across the top
// of it would make it a page, which is the thing it is deliberately not.
//
// A console whose recent history holds nothing this machine can play
// once drew no hero, and the bar vanished with it — which left Library
// unreachable on the one screen that is meant to reach everything.
void App::drawBar(bool gameUp, float sc, float dt) {
    if (!gameUp && here() != Screen::Detail) {
        // SELECTION IS NOT FOCUS, and the design system has had both since
        // the switcher pills: a SELECTED destination is where you are, a
        // FOCUSED one is what you would open. Standing in the Library, the
        // bar says Library without pretending the cursor is up there.
        const int selected = here() == Screen::Home ? BarHome
                             : (here() == Screen::Library || here() == Screen::Grid)
                                 ? BarLibrary
                                 : here() == Screen::Search   ? BarSearch
                                 : (here() == Screen::Settings || here() == Screen::HowTo)
                                     ? BarSettings
                                     : -1;
        const float barX = kContentInset;
        const float barW = ui::kCanvasWidth - kContentInset * 2.0f;
        const float barBaseline =
            barTop + (barHeight - text.lineHeight(ui::TextStyle::Callout, sc)) * 0.5f +
            text.ascent(ui::TextStyle::Callout, sc);
        float bx = barX;
        for (int i = 0; i < kBarCapsules; ++i) {
            const bool on = barFocused && barSlot == i;
            const bool sel = (i == selected);
            const float w = text.measure(kBarLabels[i], ui::TextStyle::Callout, sc);
            // FORK (handheld): the item's touch target, half the gap each side so they meet.
            barSpot[i] = {bx - kBarItemGap * 0.5f, bx + w + kBarItemGap * 0.5f};
            // SELECTED AND FOCUSED ARE BOTH A CAPSULE, at the design
            // system's two tints. A cyan underline under the selected one
            // was tried on 2026-09-21 and withdrawn the same minute —
            // MMagTech, looking at it: *"nevermind drop that looks bad."*
            //
            // The objection it was answering is still open and written down
            // here rather than lost: 0.35 against 0.25 is a difference you
            // can measure and can barely see from a sofa, so which
            // destination you are standing in is weakly said. Whatever
            // answers it next, it is not a second colour on the bar.
            if (on || sel) {
                // Treatment 2, the text-control one, the same as Resume: a
                // tinted pill rather than a scale, because a destination
                // growing would shove its neighbours along.
                //
                // **kBarPillPadX AND kBarItemGap MOVE TOGETHER.** MMagTech,
                // 2026-09-22: *"can it have a bit more padding so the
                // letters aren't right to its edge."* They were — 14pt,
                // which at Callout is about half a character.
                //
                // Widening the pill alone would have closed the gap
                // between neighbours to nothing: the items are spaced
                // `w + kBarItemGap`, so the space BETWEEN two pills is
                // `kBarItemGap - 2 * kBarPillPadX`. At 14 and 44 that was
                // 16 points; at 22 and 44 it would have been zero and the
                // pills would have met. The gap is what keeps them reading
                // as separate destinations rather than one segmented
                // control, so the spacing goes up with the padding.
                constexpr float kBarPillPadX = 22.0f;
                constexpr float kBarPillInsetY = 6.0f;
                const float ph = barHeight - kBarPillInsetY * 2.0f;
                renderer.draw(ui::Rect{bx - kBarPillPadX, barTop + kBarPillInsetY,
                                       w + kBarPillPadX * 2.0f, ph, ph * 0.5f,
                                       ui::Color::white(on ? kFocusedTint
                                                           : kSelectedTint)});
            }
            text.draw(renderer, kBarLabels[i], bx, barBaseline, ui::TextStyle::Callout,
                      ui::Color::white(on || sel ? 1.0f : 0.65f), sc);
            bx += w + kBarItemGap;
        }

        const float rightEdge = barX + barW;

        // The account, at the far right — the corner the reference
        // implementation reserves for it. `TVAccountChip`: "the signed-in
        // RomM username beside a small circular avatar, in Home's top-right
        // corner". Name and avatar, as MMagTech asked for.
        //
        // A lettered disc stands in until the real avatar is fetched, which
        // was Cabinet's fallback too; it used a person glyph. Account
        // switching is its own topic and nothing here is focusable yet.
        const storage::User me = storage::currentUser();
        const std::string who = me.valid() ? me.name : std::string("Not signed in");
        const float rowW = settleBatteryRow(sc, dt);
        // A STEP DOWN THE RAMP FROM THE DESTINATIONS, and that is the
        // whole of the sizing rule. MMagTech, looking at the first
        // capture: *"chip seems a bit too big."* It was, and the reason
        // was measurable rather than a matter of taste — the name was
        // Callout, which is exactly what Library, Search and Settings
        // are, so ambient state was typeset at destination weight and
        // competed with the navigation it sits opposite.
        //
        // Caption1 against the bar's Callout, and a disc sized to the
        // smaller text. The reference calls it "a SMALL circular avatar"
        // and that word was doing work nobody had read.
        // THE NAME MATCHES THE DESTINATIONS; THE DISC DOES NOT NEED TO.
        // MMagTech, 2026-09-22: *"the username is sized different to
        // library, settings and search."* It was, because "the chip seems
        // a bit too big" the day before had been read as the TEXT when it
        // was the disc — a 30pt avatar beside 31pt labels is a heavy
        // object in the corner, and a 26pt one is not.
        //
        // So the name goes back to Callout, level with the bar it sits in,
        // and the disc stays small. The chip is quiet because the picture
        // is small, not because the name is shrunken.
        const ui::TextStyle chipStyle = ui::TextStyle::Callout;
        const float discD = barHeight - 30.0f;
        const float discX = rightEdge - discD;
        const float discY = barTop + (barHeight - discD) * 0.5f;
        // THE NAME GIVES WAY, shortened with an ellipsis, when everything
        // left of it would otherwise run into Settings (#293, MMagTech
        // 2026-10-08): the batteries, Offline and the download ring are
        // the information. Room is what is left between the bar's last
        // destination (its pill included) and the disc. Only a long name
        // with most of a family's controllers on ever meets it; eight
        // controllers, Offline and a download at once still overlap, and
        // that was judged not worth more.
        const bool ringUp = launchJob.busy() && launchJob.busyFor >= kProgressDelay;
        const bool offlineUp = romm::serverAway();
        const float barEnd = bx - kBarItemGap + 22.0f + 24.0f;
        const float leftOfName =
            (batterySlots.empty() ? 0.0f : kToName + rowW + 10.0f) +
            (offlineUp ? 12.0f + text.measure("Offline", ui::TextStyle::Caption1, sc) + 32.0f + 12.0f : 0.0f) +
            (ringUp ? 24.0f + discD : 0.0f);
        const float nameRoom = discX - 10.0f - 22.0f - leftOfName - barEnd;
        std::string name = text.measure(who, chipStyle, sc) <= nameRoom
                               ? who
                               : text.truncate(who, chipStyle, sc, std::max(nameRoom, 0.0f));
        // Nothing left but the ellipsis: the picture alone says who it is.
        if (name != who && name.size() <= 3) name.clear();
        const float nameW = text.measure(name, chipStyle, sc);
        // THE PERSON'S OWN PICTURE, when RomM has one — new 2026-09-21.
        // MMagTech: *"i also noticed my user login isnt showing its image
        // from romm."* It never did: the comment below promised a lettered
        // disc "until the real avatar is fetched" and nothing ever fetched
        // one. It comes from `/api/users/<id>/avatar`, through the same
        // cache and the same authenticated client as every cover.
        //
        // The disc is drawn either way, as the ground under a picture with
        // transparency and as the fallback when there is none.
        // FOCUSED, THE CHIP GETS THE BAR'S OWN PILL, behind the name and
        // the picture together, at the focused tint the destinations use.
        // It used to take only a rim on its small disc, and MMagTech found
        // it hard to tell when focus had arrived on it, 2026-09-24:
        // *"can we make the text or some way grab your attention more
        // beside just the little highlight of the icon"*.
        // FORK (handheld): the chip's touch target, and that the bar was drawn just now.
        barSpot[BarAccount] = {discX - 10.0f - nameW - 22.0f, discX + discD + 12.0f};
        barDrawnAt = SDL_GetTicks();
        const bool chipOn = barFocused && barSlot == BarAccount;
        if (chipOn) {
            constexpr float kChipPillPadX = 22.0f;   // the bar pill's
            constexpr float kChipPillInsetY = 6.0f;
            const float ph = barHeight - kChipPillInsetY * 2.0f;
            const float left = discX - 10.0f - nameW - kChipPillPadX;
            const float right = discX + discD + 12.0f;
            renderer.draw(ui::Rect{left, barTop + kChipPillInsetY, right - left, ph,
                                   ph * 0.5f, ui::Color::white(kFocusedTint)});
        }
        renderer.draw(ui::Rect{discX, discY, discD, discD, discD * 0.5f,
                               ui::Color::white(chipOn ? 0.34f : 0.22f)});
        // WHERE THE PANEL HANGS FROM. The app knows where the chip is; the
        // screen must not guess, or the panel drifts the day the bar moves.
        accountScreen.setAnchor(discX + discD, barTop + barHeight + 12.0f);
        const ui::Image* face = nullptr;
        if (!me.avatar.empty()) {
            const ui::Image& img = images.get(me.avatar);
            if (img.ready) face = &img;
        }
        if (face) {
            // FILLED AND ROUND. A profile picture is not always square —
            // the one on the live server is 1200x1200 but nothing promises
            // that — and a contained fit inside a circle leaves bars that
            // the circle then slices into crescents.
            ui::drawImage(renderer, *face, discX, discY, discD, discD,
                          ui::Fit::Fill, face->fade, discD * 0.5f);
        } else if (!who.empty()) {
            const std::string initial(1, static_cast<char>(std::toupper(
                static_cast<unsigned char>(who[0]))));
            const float iw = text.measure(initial, chipStyle, sc);
            text.draw(renderer, initial, discX + (discD - iw) * 0.5f,
                      discY + (discD - text.lineHeight(chipStyle, sc)) * 0.5f +
                          text.ascent(chipStyle, sc),
                      chipStyle, ui::Color::white(0.90f), sc);
        }
        // Its own baseline, because it is no longer the bar's size and
        // sharing `barBaseline` would sit it a few points low.
        const float chipBaseline =
            barTop + (barHeight - text.lineHeight(chipStyle, sc)) * 0.5f +
            text.ascent(chipStyle, sc);
        // Brighter when focused, because it just got smaller and a focus
        // target you cannot find is worse than one that is too loud.
        text.draw(renderer, name, discX - 10.0f - nameW, chipBaseline,
                  chipStyle, ui::Color::white(chipOn ? 1.0f : 0.62f), sc);

        // OFFLINE (#88): a small capsule left of the account chip while
        // the server cannot be reached, gone when it answers. MMagTech,
        // 2026-10-05. Stated, not explained: the console is the normal
        // console with fewer games, and this is the only word about it.
        // Everything left of the chip moves left past it (the download
        // ring), so the chip itself never moves.
        float leftOfChip = discX - 10.0f - nameW - 22.0f;

        drawBatteryRow(leftOfChip, rowW, sc);
        if (offlineUp) {
            const char* kOffline = "Offline";
            const ui::TextStyle os = ui::TextStyle::Caption1;
            const float ow = text.measure(kOffline, os, sc);
            constexpr float kPadX = 16.0f, kInsetY = 14.0f;
            const float ph = barHeight - kInsetY * 2.0f;
            const float px = leftOfChip - 12.0f - ow - kPadX * 2.0f;
            renderer.draw(ui::Rect{px, barTop + kInsetY, ow + kPadX * 2.0f, ph, ph * 0.5f,
                                   ui::Color::white(0.22f)});
            text.draw(renderer, kOffline, px + kPadX,
                      barTop + (barHeight - text.lineHeight(os, sc)) * 0.5f +
                          text.ascent(os, sc),
                      os, ui::Color::white(0.80f), sc);
            leftOfChip = px - 12.0f;
        }

        // A DOWNLOAD IN FLIGHT: A RING LEFT OF THE ACCOUNT CHIP (#145).
        // For the person who started one and walked away: without it a
        // background fetch of a 1.78 GB arcade set is invisible the moment
        // you leave the screen that started it.
        //
        // IT USED TO BE A LINE AND A PERCENTAGE IN THE CORNER, and the chip
        // was fitted to its left, so the chip jumped sideways whenever a
        // download started or finished. MMagTech, on the TV, 2026-09-27:
        // he doesn't like it moving. Now the chip owns the corner and never
        // moves, and the download is a small ring that fills clockwise from
        // the top; no number. A down arrow inside was tried, drawn and then
        // as the font's glyph, and MMagTech found it horrible: the ring
        // alone says it. Unpacking has no fraction to show, so a quarter of the
        // ring turns instead. A readout, not a control: nothing reaches it.
        if (ringUp) {
            const int64_t got = launchJob.got.load();
            const int64_t total = launchJob.total.load();
            // Installing has no fraction either (RPCS3 says nothing while
            // it works), so it turns the same way.
            const bool unpacking =
                launchJob.stage.load() == LaunchJob::Stage::Unpacking ||
                launchJob.stage.load() == LaunchJob::Stage::Installing;
            // Clear of the chip's focus pill (kChipPillPadX), so the two
            // never touch when focus lands on the chip.
            const float chipLeft = leftOfChip;
            const float ringD = discD;
            const float cx = chipLeft - 24.0f - ringD * 0.5f;
            const float cy = barTop + barHeight * 0.5f;
            const float stroke = 4.0f;
            const float rad = (ringD - stroke) * 0.5f;
            // A stroke drawn as overlapping discs: the renderer has
            // rounded rectangles and nothing else, and at a 4-point width
            // discs a point apart read as one smooth line with round ends.
            // Only in SOLID colours: see-through discs stack where they
            // overlap and the line comes out blotchy (the first capture).
            const float width = stroke;
            auto dot = [&](float x, float y, ui::Color col) {
                renderer.draw(ui::Rect{x - width * 0.5f, y - width * 0.5f, width,
                                       width, width * 0.5f, col});
            };
            auto arc = [&](float from, float to, ui::Color col) {
                const float len = (to - from) * rad;
                const int n = std::max(2, static_cast<int>(len / 1.0f));
                for (int i = 0; i <= n; ++i) {
                    const float a = from + (to - from) * static_cast<float>(i) / n;
                    dot(cx + std::sin(a) * rad, cy - std::cos(a) * rad, col);
                }
            };
            constexpr float kTau = 6.2831853f;
            // The track is one shape, an inset rim on a clear circle, so
            // it can be faint without stacking.
            {
                ui::Rect track{cx - ringD * 0.5f, cy - ringD * 0.5f, ringD, ringD,
                               ringD * 0.5f, ui::Color::white(0.0f)};
                track.border = stroke;
                track.borderColor = ui::Color::white(0.28f);
                renderer.draw(track);
            }
            if (unpacking) {
                const float spin = static_cast<float>(SDL_GetTicks() % 1200) / 1200.0f * kTau;
                arc(spin, spin + kTau * 0.25f, ui::palette::kScreenCyan);
            } else if (total > 0) {
                const float frac = std::clamp(
                    static_cast<float>(got) / static_cast<float>(total), 0.0f, 1.0f);
                if (frac > 0.0f) arc(0.0f, kTau * frac, ui::palette::kScreenCyan);
            }
        }
    }
}
