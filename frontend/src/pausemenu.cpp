// The pause menu over a game: its rows, built on the way in, the choice
// rows, Resume and Exit, Remote Play's handover, and its scrim and panel,
// which the Power menu shares (#398, restructure batch f).

#include "app.h"

#include <utility>

#include "controls.h"
#include "core.h"
#include "ps2.h"
#include "quality.h"
#include "screenfx.h"
#include "sysopts.h"
#include "vectrexsheets.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

bool App::remoteOnlyHere() {
    return playing && sessionNeedsRemote && players::streaming();
}

// The choice row at pause menu slot `slot`, or nullptr.
auto App::choiceAt(int slot) -> PauseChoice* {
    int n = 0;
    for (int i = 0; i < slot && i < static_cast<int>(pauseItems.size()); ++i)
        if (pauseItems[i] == OvChoice) ++n;
    if (slot < 0 || slot >= static_cast<int>(pauseItems.size()) ||
        pauseItems[slot] != OvChoice || n >= static_cast<int>(pauseChoices.size()))
        return nullptr;
    return &pauseChoices[n];
}

int App::ovCount() {
    return static_cast<int>(powerMenu ? powerItems.size() : pauseItems.size());
}

// UP FROM THE TOP IS THE BOTTOM, and Down from the bottom the top: the
// pause menu is short, and Exit to Home is last (MMagTech on the TV,
// 2026-10-06). The same for the Power menu, which is the same panel.
void App::ovStep(int delta) {
    const int n = ovCount();
    if (n <= 0) return;
    overlaySlot = ((overlaySlot + delta) % n + n) % n;
    // The Power menu on Home moves with a sound; the pause menu does not
    // (#389, and powerCue further down).
    if (powerMenu && !playing && !standalonePaused) sound::play(sound::Cue::Move);
}

auto App::ovLabel(int i) -> const char* {
    if (!powerMenu) return kOverlayLabels[pauseItems[i]];
    switch (powerItems[i]) {
        case PwResume: return "Resume";
        case PwCancel: return "Cancel";
        // "Sleep", not "Rest": Rest is PlayStation's word alone, and
        // Switch, Xbox, SteamOS, Windows and macOS all say Sleep.
        // MMagTech, 2026-09-22.
        case PwRest: return "Sleep";
        case PwRestart: return "Restart";
        case PwPowerOff: return "Power off";
        case PwSteam: return "Switch to Steam";
    }
    return "";
}

// A WII REMOTE GAME DURING A STREAM STAYS PAUSED (MMagTech, 2026-10-07):
// the person streaming has no Remote, so Back does not resume it either.
// Exit to Home, and saving where there are states, still work; when the
// stream ends, Resume is back for whoever has the Remote.
// THE POWER MENU ON HOME SOUNDS LIKE EVERY OTHER MENU (#389): open is
// Activate, a move is Move, closing it is Back, and a choice in it is
// Activate. Over a game it stays silent, like the pause menu it shares a
// panel with (MMagTech: the pause menu may be the one exception).
void App::powerCue(sound::Cue c) {
    if (powerMenu && !playing && !standalonePaused) sound::play(c);
}

void App::closeOverlay(bool cue) {
    if (remoteOnlyHere() && !powerMenu) {
        sound::play(sound::Cue::Edge);
        return;
    }
    // B in the Power menu over such a game goes back to the pause menu
    // (#473, PLAY-18), as its Cancel does.
    if (remoteOnlyHere() && powerMenu && cue) {
        backToPauseMenu();
        return;
    }
    if (cue && overlayOpen) powerCue(sound::Cue::Back);
    overlayOpen = false;
    overlayFade.retarget(0.0f, overlayFadeSeconds);
    if (playing && choiceScreen.isOpen()) choiceScreen.closeWithMenu();
}

// AS A SETTINGS CHOICE ROW BEHAVES (settings.cpp): left and right stop at
// the ends with the edge sound, and A walks forward and wraps, so the row
// also works from A alone.
void App::stepChoice(int slot, int delta, bool wrap) {
    PauseChoice* c = choiceAt(slot);
    if (!c) return;
    const int count = static_cast<int>(c->values.size());
    int next = c->current + delta;
    if (wrap && next >= count) next = 0;
    if (next < 0 || next >= count) {
        sound::play(sound::Cue::Edge);
        return;
    }
    c->current = next;
    if (c->apply) c->apply(next);
    if (c->nextStart) menuNotice.say("Exit and relaunch to apply", Tone::Done);
    sound::play(sound::Cue::Move);
}

void App::overlayActivate() {
    if (powerMenu) {
        if (overlaySlot >= 0 && overlaySlot < static_cast<int>(powerItems.size()))
            powerActivate(powerItems[overlaySlot]);
        return;
    }
    if (overlaySlot < 0 || overlaySlot >= static_cast<int>(pauseItems.size())) return;
    switch (pauseItems[overlaySlot]) {
        case OvResume:
            overlayOpen = false;
            overlayFade.retarget(0.0f, overlayFadeSeconds);
            break;
        case OvSaveState: saveStateNow(session, uploader, menuNotice); break;
        case OvLoadState: beginLoadLatestState(stateLoad, session, liveClient, menuNotice); break;
        // THE PAUSED MOMENT, the menu not in it: a screenshot reads the
        // game's frame, not the screen. MMagTech's idea, 2026-09-27: pause
        // in an intense scene and take it without letting go of the game.
        // The menu stays open, saying "Screenshot saved".
        case OvScreenshot:
            if (standaloneRun.active()) standaloneScreenshot();
            else screenshotNow(session, uploader, menuNotice);
            break;
        case OvChoice: stepChoice(overlaySlot, +1, /*wrap=*/true); break;
        case OvDisc:
            if (session.discInserting) menuNotice.say("Not while the disc is changing", Tone::Info);
            else openDiscPanel();
            break;
        case OvControls: openControlsPanel(); break;
        case OvExit:
            // An emulator of its own is asked to close (thawed first) and
            // the menu goes; finishStandalone takes the rest of the way
            // Home once it has gone, saves and all.
            if (standaloneRun.active()) {
                overlayOpen = false;
                overlayFade.retarget(0.0f, overlayFadeSeconds);
                standaloneRun.stop();
            } else {
                exitToHome();
            }
            break;
        default: break;
    }
}

void App::toggleOverlay(SDL_JoystickID opener) {
    if (!playing && !standalonePaused) return;
    if (overlayOpen && remoteOnlyHere() && !powerMenu) {
        sound::play(sound::Cue::Edge);
        return;
    }
    overlayOpen = !overlayOpen;
    // A question asked over the menu goes with it (Change disc), and a
    // level of it fades with its panel (Controls).
    if (!overlayOpen && playing && choiceScreen.isOpen()) choiceScreen.closeWithMenu();
    // Only on the way IN: the list must not change under a panel that is
    // still fading out.
    if (overlayOpen) {
        powerMenu = false;
        menuOpener = opener;
        // Nothing of a level left showing from the last time it closed.
        choiceScreen.forget();
        pauseItems.clear();
        if (!remoteOnlyHere()) pauseItems.push_back(OvResume);
        // NOT WHILE A STATE PICKED ON THE GAME'S PAGE IS ON ITS WAY (#484,
        // PLAY-33): saved then, the newest state on RomM was the game's
        // first frames, and the next Resume started there.
        if (session.snapshots && !stateHoldWaiting) {
            pauseItems.push_back(OvSaveState);
            pauseItems.push_back(OvLoadState);
        }
        // In every game, as the shortcut is (MMagTech: screenshots go
        // wherever the shortcut button is recognised). PS2 reads PCSX2's
        // frame since #130; GameCube is a libretro core like the rest.
        pauseItems.push_back(OvScreenshot);
        // CHANGE DISC (#162), only on a game with several discs and only
        // where the console changes them (not GameCube: Dolphin does).
        // After the three one-press rows and beside Look, the other
        // "Name: value" row (MMagTech on the TV, 2026-10-11).
        if (session.discs.size() >= 2 &&
            (session.discChange == disc::Change::Insert ||
             session.discChange == disc::Change::StartOver))
            pauseItems.push_back(OvDisc);
        // PICTURE QUALITY, on the systems where a level changes something
        // (quality::hasLevels), and with a PIN set only for the owner, as in
        // Settings (MMagTech, 2026-10-02).
        pauseChoices.clear();
        if (quality::hasLevels(session.core) &&
            (!accounts::pinIsSet() || accounts::activeId() == accounts::ownerId())) {
            PauseChoice c;
            c.label = "Picture";
            c.values.push_back("Console");
            for (int l = 0; l < quality::kLevelCount; ++l)
                c.values.push_back(quality::levelName(static_cast<quality::Level>(l)));
            const std::optional<quality::Level> own = quality::gameChoice(session.romId);
            c.current = own ? static_cast<int>(*own) + 1 : 0;
            c.nextStart = true;
            const int romId = session.romId;
            c.apply = [romId](int i) {
                quality::setGameChoice(romId, i == 0 ? std::nullopt
                                                     : std::optional<quality::Level>(
                                                           static_cast<quality::Level>(i - 1)));
            };
            pauseChoices.push_back(std::move(c));
        }
        // WIDESCREEN (#368), for everyone, only on a PS2 game whose patch
        // file has a "Widescreen 16:9" section: a row that does nothing
        // is worse than none. Some patches leave the HUD stretched, and
        // some players want 4:3. From the next start, as Picture is.
        if (session.core == "pcsx2" && ps2::hasWidescreenPatch()) {
            PauseChoice c;
            c.label = "Widescreen";
            c.values = {"On", "Off"};
            c.current = ps2::widescreenOn(session.romId) ? 0 : 1;
            c.nextStart = true;
            const int romId = session.romId;
            c.apply = [romId](int i) { ps2::setWidescreenOn(romId, i == 0); };
            pauseChoices.push_back(std::move(c));
        }
        // THE SCREEN LOOK (#122), for everyone, on the systems this
        // console draws itself. Changes at once, behind the menu.
        if (session.standaloneCore.empty() &&
            !screenfx::looksFor(session.platformSlug).empty()) {
            PauseChoice c;
            c.label = "Look";
            for (const screenfx::Look& l : screenfx::looksFor(session.platformSlug))
                c.values.push_back(l.label);
            c.current = screenfx::chosen(session.platformSlug);
            const std::string slug = session.platformSlug;
            c.apply = [slug](int i) { screenfx::choose(slug, i); };
            pauseChoices.push_back(std::move(c));
        }
        // THE SCREEN SHEET (#224), where the Look row sits for the other
        // systems (the Vectrex has no look), only for a game that has a
        // sheet: a row that does nothing is worse than none. One choice for
        // the system, kept as a look is. Changes at once, behind the menu,
        // and the picture's fit with it (vectrexsheets::coreOptions).
        if (session.standaloneCore.empty() && vectrexsheets::ready()) {
            PauseChoice c;
            c.label = "Overlays";
            c.values = {"On", "Off"};
            c.current = vectrexsheets::on() ? 0 : 1;
            const std::string core = session.core;
            const std::string slug = session.platformSlug;
            c.apply = [core, slug, &pauseRedrawFrames = pauseRedrawFrames](int i) {
                vectrexsheets::setOn(i == 0);
                for (const auto& kv : vectrexsheets::coreOptions(core, slug, i == 0))
                    cab::Core::shared().setOptionLive(kv.first, kv.second);
                pauseRedrawFrames = 2;
            };
            pauseChoices.push_back(std::move(c));
        }
        // THE SYSTEM'S OWN ROWS (#73), for everyone: they are looks.
        // Built-in cores only; none of the separate emulators has any.
        if (session.standaloneCore.empty()) {
            for (const sysopts::Option& o : sysopts::forSystem(session.platformSlug)) {
                PauseChoice c;
                c.label = o.label;
                for (const sysopts::Choice& ch : o.choices) c.values.push_back(ch.label);
                c.current = sysopts::chosen(session.platformSlug, o);
                const std::string slug = session.platformSlug;
                const sysopts::Option* opt = &o;
                c.apply = [slug, opt, &pauseRedrawFrames = pauseRedrawFrames](int i) {
                    sysopts::choose(slug, *opt, i);
                    for (const auto& kv : opt->choices[i].sets)
                        cab::Core::shared().setOptionLive(kv.first, kv.second);
                    pauseRedrawFrames = 2;
                };
                pauseChoices.push_back(std::move(c));
            }
        }
        for (size_t i = 0; i < pauseChoices.size(); ++i) pauseItems.push_back(OvChoice);
        // CONTROLS (controls.h), directly above Exit to Home: only in the two
        // arcade cores, only for a game whose buttons give it a row, and only
        // with a controller on to change them on. Not over an emulator of its
        // own, which an arcade game played before it may have left offered.
        if (playing && controls::offered() && !players::connected().empty())
            pauseItems.push_back(OvControls);
        pauseItems.push_back(OvExit);
    }
    overlaySlot = 0;
    overlayFade.retarget(overlayOpen ? 1.0f : 0.0f, overlayFadeSeconds);
    overlayFocus.retarget(1.0f, kOverlayFocusDuration);
    overlayFocus.elapsed = kOverlayFocusDuration;
}

void App::remotePlayHandover() {
    const bool on = rpWant && remoteplay::streaming();
    if (on == players::streaming()) return;
    players::setStreaming(on);
    // A DEVICE ARRIVING COUNTS AS SOMEBODY THERE (#294): the idle timer
    // starts again and a dim or dark screen lifts, so the phone sees
    // the console, not the black layer, and its first press is not
    // spent waking it. Only the timer: no press reaches the menus. The
    // screen itself was already woken by the start step, before Sunshine
    // looked (cabinetos-remoteplay).
    if (on) idleWatch.input(clockSeconds());
    // Remote Play's section greys out for the stream, and back after. A
    // panel already open from it (Paired devices, Tailscale's menu)
    // closes, so nothing in it can be pressed from the stream either.
    if (here() == Screen::Settings) {
        if (on && choiceScreen.isOpen()) choiceScreen.close();
        buildSettings();
    }
    if (playing && !overlayOpen) toggleOverlay();
    else if (standaloneRun.active()) openStandaloneMenu();
    // A Remote game's pause menu already open loses or gets back its
    // Resume with the handover.
    if (playing && overlayOpen && !powerMenu) {
        const bool has = !pauseItems.empty() && pauseItems.front() == OvResume;
        if (remoteOnlyHere() && has) pauseItems.erase(pauseItems.begin());
        else if (!remoteOnlyHere() && !has) pauseItems.insert(pauseItems.begin(), OvResume);
        overlaySlot = 0;
    }
}

// The 64 is the padding above and below the buttons. There used to be 44
// more, reserved for the line the menu answered with; that answer is the
// notification pill now, so the panel is just its buttons and has no empty
// band at the foot.
float App::pauseMenuHeight() {
    const int n = ovCount();
    return static_cast<float>(n) * kOverlayButtonHeight +
           static_cast<float>(n - 1) * kOverlayButtonGap + 64.0f;
}

void App::tickPauseMenu(float dt) {
    overlayFade.tick(dt);
    overlayFocus.tick(dt);
}

// The overlay's scrim belongs to the WORLD, not to the overlay, so it
// is drawn before the scene is presented. Put it after and the panel's
// glass samples the UN-dimmed picture underneath: over a bright game
// the panel comes out milky and its own labels stop being readable,
// which is exactly what happened the first time. Dimming the picture is
// what a scrim is for; the glass should be blurring the dimmed thing.
void App::drawPauseScrim() {
    if (overlayFade.value() > 0.001f) {
        renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                               ui::Color::black(0.55f * overlayFade.value())});
    }
}

// ---- The in-game overlay -------------------------------------------
//
// No compositing trick: the frontend owns the frame loop, so this is a
// scrim, a panel and some buttons drawn over whatever the game just
// rendered. That is the direct payoff of hosting cores in process
// rather than launching them.
//
// Drawn after presentScene, so it is over the game's picture. The panel is
// solid, not glass (below).
void App::drawPauseMenu(float sc) {
    const float ovl = overlayFade.value();
    // A LEVEL OF IT (Controls, choice.h, setOverPauseMenu) is drawn in this
    // panel, which stays: only the rows give way to the level's, and come
    // back as it goes.
    const bool level = playing && choiceScreen.overPauseMenu();
    choiceScreen.setMenuFade(ovl);
    const float rowsA = ovl * (level ? choiceScreen.menuRows() : 1.0f);
    // NOT UNDER A QUESTION ASKED FROM IT (Change disc, #162): the panel
    // takes its place, and the menu's words showed through the glass.
    if (ovl > 0.001f && !(playing && choiceScreen.isOpen() && !level)) {
        const int ovN = ovCount();
        const float panelH = pauseMenuHeight();
        const float px = (ui::kCanvasWidth - kOverlayPanelWidth) * 0.5f;
        // Rises slightly as it arrives rather than only fading: a panel that
        // just materialises reads as a glitch.
        const float py = (ui::kCanvasHeight - panelH) * 0.5f + (1.0f - ovl) * overlayRise;
        // SOLID, NOT GLASS. See panels.h, kOverlayPanelFill, for why — in
        // short, glass blurs the console's own scene texture, and on the
        // composited path the game is not in it. A panel that is frosted on
        // one path and flat on the other is the exact difference this
        // console cannot afford between a Mega Drive and a PlayStation 2.
        //
        // The shadow is what separates it from the game now that the blur
        // does not, and it costs nothing on either path.
        ui::Color panelFill = look::surface();
        panelFill.a = kOverlayPanelFill * ovl;
        ui::Rect panel{px, py, kOverlayPanelWidth, panelH,
                       kOverlayPanelRadius, panelFill};
        // Lit from above: a little lighter at the head, denser and darker
        // at the foot, with a highlight on the top edge only.
        panel.gradient = true;
        panel.fillBottom =
            ui::Color{panelFill.r * kOverlayPanelBottomDarken,
                      panelFill.g * kOverlayPanelBottomDarken,
                      panelFill.b * kOverlayPanelBottomDarken,
                      kOverlayPanelFillBottom * ovl};
        panel.edgeLight = ui::Color::white(kOverlayPanelEdgeLight * ovl);
        panel.border = kOverlayPanelBorder;
        panel.borderColor = ui::Color::white(kOverlayPanelBorderAlpha * ovl);
        panel.shadowBlur = kOverlayPanelShadowBlur;
        panel.shadowOffsetY = kOverlayPanelShadowY;
        panel.shadowColor = ui::Color::black(kOverlayPanelShadowAlpha * ovl);
        renderer.draw(panel);

        for (int i = 0; i < ovN && rowsA > 0.001f; ++i) {
            const bool on = (i == overlaySlot);
            const float f = on ? overlayFocus.value() : 0.0f;
            // 1.04, the pause menu's own tier. A full-width button growing
            // a cover's tenth would run into its neighbours.
            const float sc2 = 1.0f + f * (kOverlayFocusScale - 1.0f);
            const float bw = (kOverlayPanelWidth - 64.0f) * sc2;
            const float bh = kOverlayButtonHeight * sc2;
            const float bx = px + (kOverlayPanelWidth - bw) * 0.5f;
            const float by = py + 32.0f +
                             i * (kOverlayButtonHeight + kOverlayButtonGap) -
                             (bh - kOverlayButtonHeight) * 0.5f;

            // FOCUS IS A RIM, the same one every card and pill in this
            // console uses. See panels.h — a light-filled bar was tried and
            // it both shouted and invented a second focus idiom for one
            // screen.
            ui::Rect btn{bx, by, bw, bh, kOverlayButtonRadius,
                         ui::Color::white(
                             (kOverlayButtonRestFill +
                              f * (kOverlayButtonFocusFill - kOverlayButtonRestFill)) * rowsA)};
            if (f > 0.0f) {
                btn.border = f * kFocusRimWidth;
                // FADED WITH THE PANEL. It was the one part of the menu
                // that did not follow the fade, so the focus rim arrived at
                // full strength on the first frame, floating over a panel
                // that had not faded in yet — MMagTech on the Power menu,
                // 2026-09-22: *"it opens in pieces."*
                btn.borderColor = ui::palette::kFocusRim;
                btn.borderColor.a *= rowsA;
                btn.shadowBlur = kOverlayButtonFocusShadowBlur;
                btn.shadowOffsetY = kOverlayButtonFocusShadowY;
                btn.shadowColor =
                    ui::Color::black(kOverlayButtonFocusShadowAlpha * f * rowsA);
            }
            renderer.draw(btn);

            const char* label = ovLabel(i);
            // CHANGE DISC WITH ITS VALUE, written as a choice row is
            // ("Change disc: Disc 2 of 4") with no arrows: A opens the
            // question panel, Left and Right do nothing on it.
            std::string discLine;
            if (!powerMenu && pauseItems[i] == OvDisc) {
                discLine = std::string(label) + ": " +
                           disc::ofText(session.disc, disc::highest(session.discs));
                label = discLine.c_str();
            }
            // A CHOICE ROW, CENTRED LIKE EVERY OTHER BUTTON: "Picture
            // quality: Balanced" on one line, and under focus an arrow at
            // each edge of the button, bright where left or right still
            // goes somewhere and faint at the end, as Settings shows it.
            if (PauseChoice* choice = powerMenu ? nullptr : choiceAt(i)) {
                const ui::TextStyle st = ui::TextStyle::Title3;
                const ui::Color c = ui::Color::white(
                    (kOverlayButtonRestText + f * (1.0f - kOverlayButtonRestText)) * rowsA);
                const float baseY = by + (bh - text.lineHeight(st, sc)) * 0.5f +
                                    text.ascent(st, sc);
                const std::string line =
                    choice->label + ": " + choice->values[choice->current];
                const float lw = text.measure(line.c_str(), st, sc);
                text.draw(renderer, line.c_str(), bx + (bw - lw) * 0.5f, baseY, st, c, sc);
                if (f > 0.0f) {
                    const char* kNext = "\xE2\x80\xBA";
                    const char* kBack = "\xE2\x80\xB9";
                    const float aw = text.measure(kNext, st, sc);
                    const bool canRight =
                        choice->current + 1 < static_cast<int>(choice->values.size());
                    const bool canLeft = choice->current > 0;
                    text.draw(renderer, kBack, bx + 28.0f, baseY, st,
                              ui::Color::white((canLeft ? 0.70f : 0.18f) * f * rowsA), sc);
                    text.draw(renderer, kNext, bx + bw - 28.0f - aw, baseY, st,
                              ui::Color::white((canRight ? 0.70f : 0.18f) * f * rowsA), sc);
                }
                continue;
            }
            const float tw = text.measure(label, ui::TextStyle::Title3, sc);
            // Dark on the light row, light on the dark ones. Crossfaded by
            // the focus animation so the text does not snap between them.
            // Note Color::white(a) is WHITE AT ALPHA a, not a grey. A
            // build that wanted dark text asked for white(0.06) and got a
            // six-per-cent white, which was invisible. It reads like a
            // brightness and is not one.
            const ui::Color labelColor = ui::Color::white(
                (kOverlayButtonRestText + f * (1.0f - kOverlayButtonRestText)) * rowsA);
            text.draw(renderer, label, bx + (bw - tw) * 0.5f,
                      by + (bh - text.lineHeight(ui::TextStyle::Title3, sc)) * 0.5f +
                          text.ascent(ui::TextStyle::Title3, sc),
                      ui::TextStyle::Title3, labelColor, sc);
        }

    }
}
