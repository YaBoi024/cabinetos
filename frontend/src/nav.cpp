// Where the console is: the top-bar destinations and the dissolve between
// them, the screen stack, who a press belongs to, and the screens' own
// ticks and draws (#398, restructure batch d).

#include "app.h"

#include <algorithm>

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

App::Screen App::here() { return stack.back(); }

// THE SHOULDERS SWITCH DESTINATION — 2026-09-21.
//
// MMagTech: *"i also think that r1 and l1 should allow moving between the
// top rows."* They do now, and they do it product-wide rather than only on
// Home, which is the point of them: the top bar was a row of links a person
// had to TRAVEL to — up out of the shelves, across, press A — and the
// bumpers make it a tab strip reachable from wherever you are. Every
// console does this and the shoulders were unmapped in the whole product.
//
// ONLY AT THE TOP LEVEL. Home and Library are destinations; the grid of a
// platform and a game's launch screen are places you went INSIDE one, and a
// bumper that teleported out of them would lose somebody's place rather
// than move them. Back is what leaves those, and it already does.
//
// NO WRAPPING. The bumpers walk the four destinations, Home, Library,
// Search and Settings, and stop at the ends with the edge sound
// (switchDestination).
// GOING TO A DESTINATION RESETS TO ITS ROOT, which is what makes the bar a
// tab strip rather than a stack of pushes. Walking into a platform's grid
// and then pressing R1 twice must not leave that grid buried under two more
// screens: each destination is entered fresh and Back from any of them
// returns to Home.
//
// 0 Home, 1 Library, 2 Search, 3 Settings.
void App::goToDestination(int d) {
    if (keyboard.isOpen()) keyboard.cancel();
    stack.clear();
    stack.push_back(Screen::Home);
    if (d == 1) {
        libraryScreen.enter();
        stack.push_back(Screen::Library);
    } else if (d == 2) {
        searchScreen.open();
        searchTyped.clear();
        searchPending.clear();
        stack.push_back(Screen::Search);
        openSearchKeyboard();
    } else if (d == 3) {
        // Rebuilt on every visit: the drives, the network and the account
        // are read fresh, never remembered.
        buildSettings();
        askNetwork();
        askWifi();
        settingsScreen.enter();
        stack.push_back(Screen::Settings);
    }
}

int App::destinationHere() {
    switch (here()) {
        case Screen::Home: return 0;
        case Screen::Library:
        case Screen::Grid: return 1;
        case Screen::Search: return 2;
        case Screen::Settings: return 3;
        default: return -1;      // the launch screen is inside a destination
    }
}

void App::transitionTo(int d) {
    if (d == 0) homeFromTheStart();
    if (shotMode) { goToDestination(d); return; }
    // Taken at the end of the next frame, which still shows the old screen.
    pendingDest = d;
}

// Where the console is, or is about to be.
int App::destinationGoing() {
    return pendingDest >= 0 ? pendingDest : destinationHere();
}

void App::switchDestination(int delta) {
    const int at = destinationGoing();
    if (at < 0) return;
    const int want = at + delta;
    // No wrapping: the ends make the edge sound, so a bumper that goes
    // nowhere says so rather than reading as broken.
    if (want < 0 || want > 3 || want == at) {
        sound::play(sound::Cue::Edge);
        return;
    }
    transitionTo(want);
    // THE CURSOR GOES WITH IT when it is up in the bar. MMagTech,
    // 2026-09-27: R1 from Home switched the screen and left the bar's
    // highlight on Home.
    if (barFocused && barSlot != BarAccount) barSlot = want;
    sound::play(want > at ? sound::Cue::Activate : sound::Cue::Back);
}

auto App::navigate(screens::Nav n) -> bool {
    // THE PIN PAD BEFORE ANYTHING. It covers the screen, so nothing under
    // it may take a press.
    if (pinScreen.isOpen()) { pinOutcome(pinScreen.key(n)); return true; }
    if (choiceScreen.isOpen()) { choiceOutcome(choiceScreen.key(n)); return true; }
    if (raList.isOpen()) { raList.key(n); return true; }
    if (downloadsPanel.isOpen()) { downloadsOutcome(downloadsPanel.key(n)); return true; }
    // Nothing takes a press while an account switch is behind the curtain.
    // Nor while the startup screen is still fully down over Home: a
    // press there would move a Home nobody can see yet. Once it lifts,
    // Home is live.
    if (switchPendingId || leaving != Leave::None || (arriving && !arriveLifting))
        return true;
    // The bar first, wherever it is focused. One place, one behaviour.
    if (accountsOpen) { apply(accountScreen.key(n)); return true; }
    if (barFocused && barKey(n)) return true;
    switch (here()) {
        case Screen::Home: return homeKey(n);
        case Screen::Library: apply(libraryScreen.key(n)); return true;
        case Screen::Grid: apply(gridScreen.key(n)); return true;
        case Screen::Detail:
            // The manual over the page takes every press; B slides it back.
            if (manualShown) {
                if (n == screens::Nav::Back) closeManual();
                else manualView.key(n);
                return true;
            }
            apply(detailScreen.key(n));
            return true;
        case Screen::Search: apply(searchScreen.key(n)); return true;
        case Screen::AddAccount: apply(addAccountScreen.key(n)); return true;
        case Screen::TailscaleSignIn: apply(tsSignInScreen.key(n)); return true;
        case Screen::Report: apply(reportScreen.key(n)); return true;
        case Screen::WhatsNew: apply(whatsNewScreen.key(n)); return true;
        case Screen::HowTo: apply(howToScreen.key(n)); return true;
        case Screen::Settings: apply(settingsScreen.key(n)); return true;
    }
    return false;
}

App::InputOwner App::inputOwner() {
    // THE DOCKED KEYBOARD DOES NOT OWN THE SCREEN. Everywhere else an open
    // keyboard is modal and takes every key, which is right for a question
    // with one answer. On Search it shares the screen with the results, and
    // when focus is up in those results the keyboard is just something that
    // is still visible.
    // The overlay takes the pad FROM the core while it is open, which is
    // the whole rule: never both. tvOS does this by turning the focus
    // engine off during play; here it is this one line. FIRST, ahead of the
    // keyboard, since the Power menu can open over a screen that has the
    // keyboard up, and a menu nobody can steer is a trap.
    if (overlayOpen) return InputOwner::Overlay;
    // NOR WHEN THE BAR HAS FOCUS ABOVE IT: walking across the bar onto
    // Search opens its keyboard, and the next press still belongs to the
    // bar until Down leaves it.
    if (keyboard.isOpen() &&
        !(here() == Screen::Search && (searchScreen.focused() || barFocused)))
        return InputOwner::Keyboard;
    if (playing) return InputOwner::Game;
    return InputOwner::UI;
}

// Every screen ticks, not only the one in front: a screen that is
// pushed over keeps its focus animation settled rather than
// resuming mid-transition when the person comes back to it.
void App::tickScreens(float dt, screens::Ctx& ctx) {
    libraryScreen.tick(dt);
    accountScreen.tick(dt);
    addAccountScreen.tick(dt);
    tsSignInScreen.tick(dt);
    reportScreen.tick(dt);
    whatsNewScreen.tick(dt);
    howToScreen.setHasFocus(!barFocused && !accountsOpen);
    howToScreen.tick(dt);
    settingsScreen.setHasFocus(!barFocused && !accountsOpen);
    settingsScreen.tick(dt);
    pinScreen.tick(dt);
    choiceScreen.tick(dt);
    raList.tick(dt);
    downloadsPanel.tick(dt);
    tabDissolve.tick(dt);
    keyboardSlide.tick(dt);
    if (keyboard.sliding() || keyboard.isOpen()) {
        keyboard.setSlide(keyboardSlide.value());
        if (keyboard.sliding() && keyboardSlide.elapsed >= keyboardSlide.duration) {
            keyboard.endSlide();
            // Back to "in place", so a keyboard opened any other way
            // later is not left waiting below the screen.
            keyboardSlide.settle(0.0f);
        }
    }
    if (tabSince >= 0.0f) {
        tabSince += dt;
        if (tabSince > kTabContentDelay + kTabContentIn) tabSince = -1.0f;
    }
    gridScreen.tick(dt, ctx);
}

// The arriving screen's content, held back until the old frame has
// mostly dissolved. Under the bar, which does not fade; reset below.
void App::applyTabContentFade(bool gameUp) {
    auto tabContent = [&]() {
        if (tabSince < 0.0f || gameUp) return 1.0f;
        const float t = std::clamp((tabSince - kTabContentDelay) / kTabContentIn,
                                   0.0f, 1.0f);
        return design::easeInOut(t);
    };
    renderer.setContentFade(tabContent());
    renderer.setContentOffsetY(gameUp ? 0.0f : (1.0f - tabContent()) * kTabRise);
}

// Everything past Home draws itself. The world half goes here; the
// frosted half goes after presentScene, below, because glass reads
// the scene through itself.
void App::drawScreen(float sc) {
    screens::Ctx ctx{renderer, text, images, sc, &cards, controllersOn()};
    switch (here()) {
        case Screen::Library: libraryScreen.draw(ctx); break;
        case Screen::Grid: gridScreen.draw(ctx); break;
        case Screen::Detail:
            detailScreen.draw(ctx);
            manualView.draw(ctx, manualSlide.value());
            break;
        case Screen::Search: searchScreen.draw(ctx); break;
        case Screen::AddAccount: addAccountScreen.draw(ctx); break;
        case Screen::TailscaleSignIn: tsSignInScreen.draw(ctx); break;
        case Screen::Report: reportScreen.draw(ctx); break;
        case Screen::WhatsNew: whatsNewScreen.draw(ctx); break;
        case Screen::HowTo: howToScreen.draw(ctx); break;
        case Screen::Settings: settingsScreen.draw(ctx); break;
        case Screen::Home: break;   // unreachable, and the compiler asks
    }
    // A screen sets the transition alpha and its own scroll window for
    // its drawing, and owns neither for the rest of the frame. Reset
    // both here rather than trusting each screen to, so a new screen
    // cannot silently fade or clip the bar drawn on top of it.
    renderer.setContentAlpha(1.0f);
    renderer.clearScissor();

    // The top vignette, over the clipped content and under the bar that
    // is drawn later. See design::kScrollFade*.
    if (here() == Screen::Library || here() == Screen::Grid) {
        ui::Rect fade{0, 0, ui::kCanvasWidth, kScrollFadeHeight, 0,
                      ui::Color::black(kScrollFadeAlpha)};
        fade.gradient = true;
        fade.fillBottom = ui::Color::black(0.0f);
        renderer.draw(fade);
    }
}

void App::drawScreenGlass(bool gameUp, float sc) {
    if (!gameUp && here() != Screen::Home) {
        screens::Ctx ctx{renderer, text, images, sc, &cards, controllersOn()};
        switch (here()) {
            case Screen::Library: libraryScreen.drawGlass(ctx); break;
            case Screen::Grid: gridScreen.drawGlass(ctx); break;
            case Screen::Detail:
                detailScreen.drawGlass(ctx);
                manualView.drawGlass(ctx, manualSlide.value());
                break;
            // Search has no glass: the keyboard docked under it is the only
            // material on the screen and the app draws that itself.
            case Screen::Search: break;
            // The QR draws its own white card; there is nothing behind it
            // on this screen for glass to blur.
            case Screen::AddAccount: break;
            case Screen::TailscaleSignIn: break;
            case Screen::Report: break;
            case Screen::WhatsNew: break;
            case Screen::HowTo: howToScreen.drawGlass(ctx); break;
            case Screen::Settings: settingsScreen.drawGlass(ctx); break;
            case Screen::Home: break;
        }
        renderer.setContentAlpha(1.0f);
        renderer.clearScissor();
    }
}

void App::resetContentFade() {
    // Download progress is on the row that started it
    // (DetailScreen::setProgress) and in the bar's corner, not in a panel.
    renderer.setContentFade(1.0f);
    renderer.setContentOffsetY(0.0f);
}

void App::drawKeyboardAndDissolve() {
    // A switch asked for this frame: copy it now, before the keyboard, so
    // the keyboard can leave by sliding rather than dissolve with the copy.
    if ((pendingDest >= 0 || pendingDetail >= 0 || pendingDetailBack) && !playing &&
        renderer.sceneCaptured()) {
        renderer.captureSnapshot();
        snapTaken = true;
    }

    // The keyboard of the screen that is here, then the old screen's copy
    // dissolving over everything, then a keyboard that is leaving: over
    // the copy, or the copy would hide it at the start of its slide.
    if (!keyboard.sliding()) keyboard.draw(renderer, text, renderer.scale());
    if (!playing) renderer.drawSnapshot(tabDissolve.value());
    if (keyboard.sliding()) keyboard.draw(renderer, text, renderer.scale());
    if (safeGuides) renderer.drawSafeAreaGuides();
}

// A top-bar switch asked for: this frame still shows the old screen, so
// copy it, THEN switch. The next frame draws the new screen with this
// copy dissolving over it.
void App::takePendingSwitch() {
    if (pendingDetail >= 0 || pendingDetailBack) {
        if (snapTaken) {
            snapTaken = false;
            tabDissolve.from = tabDissolve.to = 1.0f;
            tabDissolve.elapsed = 0.0f;
            tabDissolve.retarget(0.0f, kTabDissolve);
            // Back to a grid or Home: its content arrives as a top-bar
            // switch's does. Into the page: the page times its own.
            if (pendingDetailBack) tabSince = 0.0f;
        }
        if (pendingDetail >= 0) openDetail(pendingDetail);
        else if (stack.size() > 1) stack.pop_back();
        if (pendingDetail >= 0 && !pendingDetailNotice.empty())
            detailScreen.setNotice(pendingDetailNotice);
        pendingDetailNotice.clear();
        pendingDetail = -1;
        pendingDetailBack = false;
    }
    if (pendingDest >= 0) {
        if (snapTaken) {
            snapTaken = false;
            tabDissolve.from = tabDissolve.to = 1.0f;
            tabDissolve.elapsed = 0.0f;
            tabDissolve.retarget(0.0f, kTabDissolve);
            tabSince = 0.0f;
        }
        // Leaving Search: its keyboard goes on drawing, sliding down.
        if (here() == Screen::Search && keyboard.isOpen()) {
            keyboard.keepForSlide();
            keyboardSlide.from = keyboardSlide.to = 0.0f;
            keyboardSlide.elapsed = 0.0f;
            keyboardSlide.retarget(1.0f, kKeyboardSlide);
        }
        goToDestination(pendingDest);
        // Arriving at Search: its keyboard comes up from below.
        if (here() == Screen::Search && keyboard.isOpen()) {
            keyboardSlide.from = keyboardSlide.to = 1.0f;
            keyboardSlide.elapsed = 0.0f;
            keyboardSlide.retarget(0.0f, kKeyboardSlide);
        }
        if (tabDissolve.value() > 0.0f) {
            libraryScreen.settleArrival();
            settingsScreen.settleArrival();
        }
        pendingDest = -1;
    }
}
