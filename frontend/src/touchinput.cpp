// FORK (handheld): the touchscreen in the menus.
//
// Home, Library, Search, Settings and every screen and panel under them are
// driven by which item has focus, and no screen records where its items are on
// the display. So a finger is turned into the presses the pad would send:
//
//   - a tap on a top-bar destination or the account chip opens it;
//   - a tap on a key of the on-screen keyboard presses that key (the keyboard
//     already knows where its keys are, keyboard.h `pressAt`);
//   - a tap anywhere else is A: it opens what has focus;
//   - a drag moves focus the way the d-pad does, one step for each stretch of
//     travel, and the content follows the finger (drag up = Down);
//   - a drag in from the left edge is B.
//
// Taps on one exact tile or row are a later step: they need each screen to say
// where its items are.
//
// The pointer reaches here as mouse events. gamescope turns a touch into a
// click for a program that does not ask for touch itself, which is also why
// the setup screens' pointer works (setup.cpp). Real finger events are not
// read: SDL makes mouse events from them as well, so reading both would act
// twice. A mouse works the same way.

#include "app.h"

#include <algorithm>
#include <cmath>

using screens::Nav;

namespace {

// All in canvas points (1920 x 1080), so the same on any display.
constexpr float kTapSlop = 28.0f;      // moving further than this is a drag, not a tap
constexpr float kFirstStep = 70.0f;    // the drag distance of the first focus step
constexpr float kStep = 150.0f;        // and of each one after it
constexpr float kEdge = 56.0f;         // a drag that begins this close to the left edge is Back
constexpr float kBackTravel = 220.0f;  // and goes this far across
constexpr uint64_t kTapMaxMs = 600;    // a touch held longer than this is not a tap

struct Finger {
    bool down = false;
    float x0 = 0.0f, y0 = 0.0f;   // where it landed
    uint64_t at = 0;              // when
    bool moved = false;           // went past the slop: no longer a tap
    bool fromEdge = false;
    bool backed = false;          // the Back swipe has already been sent
    int axis = 0;                 // 0 not yet known, 1 across, 2 up and down
    int stepped = 0;              // steps already sent, signed
};
Finger gFinger;

// The window's pointer position, through the letterbox, as canvas points. The
// same sum as the setup screens' (setup.cpp, Flow::canvasPoint).
bool canvasPoint(SDL_Window* w, float windowX, float windowY, float* cx, float* cy) {
    int dw = 0, dh = 0, lw = 0, lh = 0;
    SDL_GetWindowSizeInPixels(w, &dw, &dh);
    SDL_GetWindowSize(w, &lw, &lh);
    if (dw <= 0 || dh <= 0 || lw <= 0 || lh <= 0) return false;
    const float px = windowX * (static_cast<float>(dw) / static_cast<float>(lw));
    const float py = windowY * (static_cast<float>(dh) / static_cast<float>(lh));
    const float sc = std::min(static_cast<float>(dw) / ui::kCanvasWidth,
                              static_cast<float>(dh) / ui::kCanvasHeight);
    if (sc <= 0.0f) return false;
    *cx = (px - (dw - ui::kCanvasWidth * sc) * 0.5f) / sc;
    *cy = (py - (dh - ui::kCanvasHeight * sc) * 0.5f) / sc;
    return true;
}

// One step of focus, to whoever has the controls now: the same calls the pad's
// d-pad makes in handleEvent (input.cpp).
void stepFocus(App& a, App::InputOwner owner, Nav n) {
    if (owner == App::InputOwner::Overlay && !a.choiceScreen.isOpen()) {
        if (n == Nav::Up) a.ovStep(-1);
        else if (n == Nav::Down) a.ovStep(+1);
        else if ((n == Nav::Left || n == Nav::Right) && !a.powerMenu &&
                 a.choiceAt(a.overlaySlot))
            a.stepChoice(a.overlaySlot, n == Nav::Right ? 1 : -1, false);
        return;
    }
    a.navigate(n);
}

// A on the pad.
void activateFocus(App& a, App::InputOwner owner) {
    if (owner == App::InputOwner::Overlay && !a.choiceScreen.isOpen()) {
        a.overlayActivate();
        return;
    }
    a.navigate(Nav::Activate);
}

// B on the pad.
void goBack(App& a, App::InputOwner owner) {
    if (owner == App::InputOwner::Keyboard) {
        a.keyboardResult(a.keyboard.cancel());
        return;
    }
    if (owner == App::InputOwner::Overlay && !a.choiceScreen.isOpen()) {
        a.closeOverlay();
        return;
    }
    a.navigate(Nav::Back);
}

// A tap on the top bar: the destination or the account chip under the finger,
// opened as the pad's A opens it. False when the bar is not on screen or
// something is over it, or the tap is not on any of its items.
bool tapBar(App& a, float x, float y) {
    // drawBar stamps barDrawnAt every frame it draws: not drawn lately, not there.
    if (SDL_GetTicks() - a.barDrawnAt > 150) return false;
    if (a.pinScreen.isOpen() || a.choiceScreen.isOpen() || a.raList.isOpen() ||
        a.downloadsPanel.isOpen() || a.accountsOpen || a.overlayOpen || a.playing)
        return false;
    // The same refusals as navigate(): an account switch behind the curtain, a
    // sign-out under way, the startup screen still down.
    if (a.switchPendingId || a.leaving != App::Leave::None ||
        (a.arriving && !a.arriveLifting))
        return false;
    // A little taller than the bar: a finger is not a pointer.
    if (y < a.barTop - 10.0f || y > a.barTop + a.barHeight + 10.0f) return false;
    for (int i = 0; i < App::BarCount; ++i) {
        if (x >= a.barSpot[i].x0 && x < a.barSpot[i].x1) {
            a.barSlot = i;
            a.barFocused = true;
            a.barKey(Nav::Activate);
            return true;
        }
    }
    return false;
}

}  // namespace

// One mouse event, after handleEvent has had its look at it. True when it was
// a finger's and was used.
bool App::handleTouch(const SDL_Event& e) {
    const InputOwner owner = inputOwner();
    // A game takes every event itself; nothing here may act under it.
    if (owner == InputOwner::Game || poweringDown) {
        gFinger.down = false;
        return false;
    }

    float x = 0.0f, y = 0.0f;
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button != SDL_BUTTON_LEFT) return false;
            if (!canvasPoint(window, e.button.x, e.button.y, &x, &y)) return false;
            gFinger = Finger{};
            gFinger.down = true;
            gFinger.x0 = x;
            gFinger.y0 = y;
            gFinger.at = SDL_GetTicks();
            gFinger.fromEdge = x < kEdge;
            return true;

        case SDL_EVENT_MOUSE_MOTION: {
            if (!gFinger.down) return false;
            if (!canvasPoint(window, e.motion.x, e.motion.y, &x, &y)) return false;
            const float dx = x - gFinger.x0;
            const float dy = y - gFinger.y0;
            if (!gFinger.moved && std::hypot(dx, dy) < kTapSlop) return true;
            gFinger.moved = true;
            // A drag in from the left edge is Back, once, and only that.
            if (gFinger.fromEdge) {
                if (!gFinger.backed && dx > kBackTravel && dx > 2.0f * std::fabs(dy)) {
                    gFinger.backed = true;
                    goBack(*this, owner);
                }
                return true;
            }
            // A drag over the on-screen keyboard moves nothing: its keys are tapped.
            if (owner == InputOwner::Keyboard) return true;
            // One direction for the whole drag, so a wobble does not wander.
            if (gFinger.axis == 0) gFinger.axis = std::fabs(dx) > std::fabs(dy) ? 1 : 2;
            // The content follows the finger: a finger going left or up brings
            // the next item, which is Right or Down.
            const float travel = gFinger.axis == 1 ? -dx : -dy;
            const float past = std::fabs(travel) - kFirstStep;
            const int count = past < 0.0f ? 0 : 1 + static_cast<int>(past / kStep);
            const int want = travel < 0.0f ? -count : count;
            const Nav forward = gFinger.axis == 1 ? Nav::Right : Nav::Down;
            const Nav backward = gFinger.axis == 1 ? Nav::Left : Nav::Up;
            while (gFinger.stepped < want) {
                stepFocus(*this, owner, forward);
                ++gFinger.stepped;
            }
            while (gFinger.stepped > want) {
                stepFocus(*this, owner, backward);
                --gFinger.stepped;
            }
            return true;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (e.button.button != SDL_BUTTON_LEFT || !gFinger.down) return false;
            gFinger.down = false;
            const bool tap = !gFinger.moved && SDL_GetTicks() - gFinger.at < kTapMaxMs;
            if (!tap) return true;
            if (!canvasPoint(window, e.button.x, e.button.y, &x, &y)) return true;
            // The keyboard first, docked on Search or asking a question: it knows where
            // its keys are, and a tap that misses them is not a press.
            if (keyboard.isOpen() && !overlayOpen) {
                bool hit = false;
                const ui::KeyboardResult res = keyboard.pressAt(x, y, &hit);
                if (hit) {
                    keyboardResult(res);
                    return true;
                }
                if (owner == InputOwner::Keyboard) return true;
            }
            if (tapBar(*this, x, y)) return true;
            activateFocus(*this, owner);
            return true;
        }

        default:
            return false;
    }
}
