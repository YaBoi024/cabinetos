// Controls over the pause menu (controls.h): the panel Change disc uses
// (choice.h), drawn at the pause menu's own size, one row per game button
// with the control it is on, a Player row when two kinds of controller are
// on, and Reset to default; and the press that gives a row its control, read
// before anything else sees it, as the shortcut button's row reads its press
// in Settings (controllers.cpp).

#include "app.h"

#include <cstdio>

#include "controls.h"
#include "fightstick.h"
#include "shortcuts.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {

// Half travel, as play.cpp's readGameInput reads a trigger as pressed; let go
// below the nav's release line (input.cpp).
constexpr int kTriggerPressed = 16384;
constexpr int kTriggerLetGo = 8000;

// THE KINDS OF CONTROLLER ON, one entry each, by the lowest player holding
// one: "Player 1", "Player 2".
std::vector<App::ControlsKind> kindsOn() {
    std::vector<App::ControlsKind> out;
    for (const players::Pad& p : players::connected()) {
        const std::string k = fightstick::kind(SDL_GetGamepadFromID(p.id));
        bool seen = k.empty();
        for (const App::ControlsKind& e : out) seen = seen || e.kind == k;
        if (!seen) out.push_back({k, p.player, p.id, p.name});
    }
    return out;
}

// What a kind's pad sends with nothing moved: its fight-stick layout in this
// game, or the plain table (the default the rows are shown against).
controls::Map defaultFor(const App::ControlsKind& k, const std::string& core) {
    SDL_Gamepad* gp = SDL_GetGamepadFromID(k.pad);
    const fightstick::Layout* l = gp ? fightstick::forPort(k.player, gp, core) : nullptr;
    return l ? controls::fromLayout(*l) : controls::plain();
}

}  // namespace

// ---- The panel ----------------------------------------------------------------

void App::openControlsPanel() {
    // Read again: the file may have been edited over File access mid-game.
    controls::reload();
    controlsPanel = ControlsPanel{};
    controlsPanel.kinds = kindsOn();
    if (controlsPanel.kinds.empty() || !controls::offered()) {
        sound::play(sound::Cue::Edge);
        return;
    }
    // ON THE KIND OF THE PAD THAT OPENED THE MENU, else player 1's.
    SDL_Gamepad* opener = SDL_GetGamepadFromID(menuOpener);
    if (!opener) opener = players::gamepad(0);
    const std::string want = fightstick::kind(opener);
    for (size_t i = 0; i < controlsPanel.kinds.size(); ++i)
        if (controlsPanel.kinds[i].kind == want) controlsPanel.shown = static_cast<int>(i);
    controlsPanel.open = true;
    showControls(true);
}

// The rows for the kind shown: the game's buttons with their controls in the
// pad's words, "Press a button..." on the one listening, and Reset to default
// last. Fresh opens the panel; otherwise it is put right in place, focus kept.
void App::showControls(bool fresh) {
    ControlsPanel& cp = controlsPanel;
    const ControlsKind& k = cp.kinds[static_cast<size_t>(cp.shown)];
    SDL_Gamepad* gp = SDL_GetGamepadFromID(k.pad);
    const std::vector<controls::Row>& rows = controls::rows();
    const std::vector<int> at = controls::current(k.kind, defaultFor(k, session.core));
    const bool two = cp.kinds.size() >= 2;
    std::vector<std::string> labels, values;
    // TWO KINDS ON: the first row says only whose layout the rows are, and
    // Left and Right go to the other's.
    if (two) {
        labels.push_back("Player " + std::to_string(k.player + 1));
        values.push_back("");
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        labels.push_back(rows[i].label);
        values.push_back(cp.listen && cp.row == static_cast<int>(i)
                             ? "Press a button\xE2\x80\xA6"
                             : controls::name(gp, at[i]));
    }
    labels.push_back("Reset to default");
    values.push_back("");
    cp.padsSeen = players::generation();
    cp.rowsSeen = controls::generation();
    if (fresh) {
        askChoice("Controls", "", labels, two ? 1 : 0, [&](int answer) { pickControlsRow(answer); });
        choiceScreen.setValues(values);
        choiceScreen.setStaysOpen(true);
        // IN THE PAUSE MENU'S OWN PANEL, at its size whatever the rows, its
        // rows giving way to these (MMagTech, 2026-10-25).
        choiceScreen.setOverPauseMenu(kOverlayPanelWidth, pauseMenuHeight());
        // B on the list goes back to the pause menu, which shows again.
        choiceBack = [&]() { controlsPanel = ControlsPanel{}; };
        choiceStep = [&](int dir) { stepControlsKind(dir, false); };
    } else if (labels == cp.labels) {
        choiceScreen.setValues(values);
    } else {
        choiceScreen.replace(labels, values, "");
    }
    cp.labels = labels;
    choiceScreen.setStepper(two ? 0 : -1, cp.shown > 0,
                            cp.shown + 1 < static_cast<int>(cp.kinds.size()));
}

void App::pickControlsRow(int answer) {
    ControlsPanel& cp = controlsPanel;
    const bool two = cp.kinds.size() >= 2;
    const int first = two ? 1 : 0;
    const int count = static_cast<int>(controls::rows().size());
    // A on the Player row walks on and wraps, as a choice row does.
    if (two && answer == 0) {
        stepControlsKind(+1, true);
        return;
    }
    if (answer == first + count) {
        controls::reset();
        showControls(false);
        return;
    }
    const int row = answer - first;
    if (row < 0 || row >= count) return;
    // THE PAD THAT PRESSED A, when it is of the kind shown; else that kind's
    // own (A from a keyboard, or from a pad of the other kind).
    const ControlsKind& k = cp.kinds[static_cast<size_t>(cp.shown)];
    SDL_JoystickID pad = k.pad;
    if (SDL_Gamepad* g = SDL_GetGamepadFromID(cp.lastPressed); g && fightstick::kind(g) == k.kind)
        pad = cp.lastPressed;
    SDL_Gamepad* gp = SDL_GetGamepadFromID(pad);
    cp.listen = pad;
    cp.row = row;
    cp.triggerDown[0] = gp && SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerPressed;
    cp.triggerDown[1] = gp && SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerPressed;
    std::fprintf(stderr, "[controls] listening on player %d's pad for %s\n",
                 players::playerOf(pad) + 1, controls::rows()[static_cast<size_t>(row)].label.c_str());
    showControls(false);
}

void App::stepControlsKind(int dir, bool wrap) {
    ControlsPanel& cp = controlsPanel;
    const int n = static_cast<int>(cp.kinds.size());
    int next = cp.shown + dir;
    if (wrap) next = (next % n + n) % n;
    if (next < 0 || next >= n || next == cp.shown) return;
    cp.shown = next;
    cp.listen = 0;
    cp.row = -1;
    showControls(false);
}

// THE ANSWER: given at once, written at once, played from the next frame.
// The button the row is already on leaves it as it was, which is the way
// out from a pad: it sounds as a step back.
void App::takeControl(int control) {
    ControlsPanel& cp = controlsPanel;
    const ControlsKind& k = cp.kinds[static_cast<size_t>(cp.shown)];
    const bool moved = controls::give(k.kind, k.name, defaultFor(k, session.core), cp.row, control);
    cp.listen = 0;
    cp.row = -1;
    sound::play(moved ? sound::Cue::Activate : sound::Cue::Back);
    showControls(false);
}

void App::stopListening() {
    controlsPanel.listen = 0;
    controlsPanel.row = -1;
    sound::play(sound::Cue::Back);
    showControls(false);
}

// LISTENING: the next press on the pad being edited is the answer, and no
// other press goes anywhere. A face button, a shoulder or a trigger is
// taken, B among them: on the one screen where buttons are being chosen, B
// is a button (the build session's call, 2026-10-25, for MMagTech to judge
// hands-on), and the button the row is already on is the way out. L3, R3,
// the d-pad, Start, Select and the shortcut button do nothing, and it
// listens on. Keys do nothing; Escape cancels. A release goes on as usual,
// so nothing stays held. What is pressed here never reaches the game: the
// menu is up (play.cpp, heldThroughMenu). B on the list, with nothing
// listening, goes back to the pause menu as on every panel.
bool App::interceptControls(const SDL_Event& e) {
    ControlsPanel& cp = controlsPanel;
    if (!cp.open) return false;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && !cp.listen) cp.lastPressed = e.gbutton.which;
    if (!cp.listen) return false;
    if (!overlayOpen || !choiceScreen.isOpen()) {
        cp.listen = 0;
        cp.row = -1;
        return false;
    }
    switch (e.type) {
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            if (e.gbutton.which != cp.listen) return true;
            const auto b = static_cast<SDL_GamepadButton>(e.gbutton.button);
            if (shortcuts::isShortcut(SDL_GetGamepadFromID(cp.listen), b)) return true;
            if (const int c = controls::controlOf(b); c != controls::kNone) takeControl(c);
            return true;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const bool left = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            if (e.gaxis.which != cp.listen ||
                (!left && e.gaxis.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
                return false;
            bool& held = cp.triggerDown[left ? 0 : 1];
            if (e.gaxis.value < kTriggerLetGo) held = false;
            else if (!held && e.gaxis.value > kTriggerPressed) {
                held = true;
                takeControl(left ? cab::L2 : cab::R2);
            }
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_ESCAPE && !e.key.repeat) stopListening();
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            return true;
        default:
            return false;
    }
}

// EVERY FRAME OF A GAME (play.cpp, readGameInput): the core's buttons, and
// an open panel kept true to the controllers on. A pad that goes or comes
// puts it right silently, on the kind it showed while that kind is still on;
// a listening pad that went cancels the prompt; no pad left closes it, back
// to the pause menu. Closed from outside (both sticks, the shortcut tap, a
// stream), it is forgotten.
void App::followControls() {
    controls::follow();
    ControlsPanel& cp = controlsPanel;
    if (!cp.open) return;
    if (!overlayOpen || !choiceScreen.isOpen()) {
        cp = ControlsPanel{};
        return;
    }
    if (cp.padsSeen == players::generation() && cp.rowsSeen == controls::generation()) return;
    const std::string was = cp.kinds[static_cast<size_t>(cp.shown)].kind;
    cp.kinds = kindsOn();
    if (cp.kinds.empty() || !controls::offered()) {
        std::fprintf(stderr, "[controls] no controller left: back to the pause menu\n");
        choiceScreen.close();
        cp = ControlsPanel{};
        return;
    }
    cp.shown = 0;
    for (size_t i = 0; i < cp.kinds.size(); ++i)
        if (cp.kinds[i].kind == was) cp.shown = static_cast<int>(i);
    if (cp.listen && (players::playerOf(cp.listen) < 0 ||
                      cp.kinds[static_cast<size_t>(cp.shown)].kind != was)) {
        cp.listen = 0;
        cp.row = -1;
    }
    if (cp.row >= static_cast<int>(controls::rows().size())) {
        cp.listen = 0;
        cp.row = -1;
    }
    showControls(false);
}
