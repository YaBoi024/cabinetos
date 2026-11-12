// The short messages over the console: the pill near the foot of the screen
// and a RetroAchievements unlock. Moved out of main.cpp in the restructure
// (#398).

#pragma once

#include <algorithm>
#include <string>

#include "achievements.h"
#include "ui.h"

// WHAT THE PAUSE MENU SAYS BACK — new 2026-09-21.
//
// MMagTech, in the menu: *"are the save and load from the menu not wired in?"*
// They were, and had been for days. Every outcome of both went to stderr and
// nowhere else, so pressing Save state looked identical whether it wrote two
// megabytes, refused because the core cannot serialise, or failed to write at
// all — and "did that do anything?" is the one question a save button must
// never leave a person asking.
//
// The same fault as the invisible launch refusal fixed earlier today, in the
// one menu where somebody is most likely to be doing something they want
// confirmed. It fades after a few seconds because it is a receipt, not a state.
//
// A PILL NEAR THE FOOT OF THE SCREEN, NOT A LINE IN THE MENU — 2026-09-23.
// It used to be small text in a strip reserved at the bottom of the pause
// panel, and MMagTech: it *"looks like it was added there with no thought to
// the visuals of the menu."* The strip also left the Power menu with an empty
// band under its last button. So it is its own object now, the way PS5, Switch
// and Apple TV confirm things, and it is not tied to the menu: an upload that
// finishes after Resume still says so, over the game.
//
// ONE PLACE FOR EVERY SHORT-LIVED MESSAGE ON THE CONSOLE, so they all look the
// same and `--notice-gallery` can show every one of them on the panel.
struct MenuNotice {
    // The dot's colour says which kind of answer it is before the words do.
    enum class Tone { Done, Busy, Info, Problem };
    std::string text;
    Tone tone = Tone::Info;
    float life = 0.0f;
    float age = 0.0f;
    // A pill nobody may be there for: it stays until the next press (App::handleEvent),
    // then has what is left of its time, and goes soon if that has already passed.
    bool holdUntilPress = false;
    // An invite's dot is the friend's own colour.
    bool ownDot = false;
    // FORK (handheld): whose invite this pill is, so a tap on it can answer it. Empty otherwise.
    std::string inviteFrom;
    ui::Color dot;
    void pressed() {
        if (!holdUntilPress) return;
        holdUntilPress = false;
        life = std::max(0.8f, life - age);
    }
    void say(std::string t, Tone k) {
        holdUntilPress = false;
        ownDot = false;
        inviteFrom.clear();
        text = std::move(t);
        tone = k;
        // Busy holds until the answer replaces it; a stuck "Saving…" is still
        // gone in fifteen seconds rather than for ever.
        //
        // EVERYTHING ELSE STAYS AS LONG AS IT TAKES TO READ (#110): 1.5 s and
        // 60 ms a character, from 2.5 s to 4.5 s. It was a flat 3.2 s, too
        // long for "Saved to RomM" and short for a sentence.
        if (k == Tone::Busy) {
            life = 15.0f;
        } else {
            int chars = 0;
            for (unsigned char ch : text)
                if ((ch & 0xC0) != 0x80) ++chars;   // characters, not UTF-8 bytes
            life = std::clamp(1.5f + 0.06f * static_cast<float>(chars), 2.5f, 4.5f);
        }
        age = 0.0f;
    }
    void tick(float dt) {
        if (life > 0.0f) {
            if (!holdUntilPress) life -= dt;
            age += dt;
        }
    }
    float alpha() const {
        if (life <= 0.0f) return 0.0f;
        const float in = std::min(1.0f, age / 0.25f);
        const float out = life < 0.5f ? life / 0.5f : 1.0f;   // the last half second fades
        return std::min(in, out);
    }
};
using Tone = MenuNotice::Tone;

// A RETROACHIEVEMENTS UNLOCK (#74), over the game: it fades in at the top
// right of the screen, says what was earned, and fades out on its own. It
// never pauses the game or takes a button (MMagTech, 2026-10-06). One at a
// time, in the order they were earned. It waits up to a second and a half
// for its badge so the picture and the chime arrive together.
struct AchievementToast {
    ra::Popup p;
    bool waiting = false;   // taken, badge not in yet
    bool on = false;
    // Whether it shows its badge. Decided as it appears and kept: no badge
    // in by then (offline, first time) and the card is words only, with no
    // empty square, and it does not grow if the picture lands later.
    bool badge = false;
    float age = 0.0f;
    static constexpr float kIn = 0.35f, kHold = 4.0f, kOut = 0.6f, kBadgeWait = 1.5f;
    float alpha() const {
        if (!on) return 0.0f;
        if (age < kIn) return age / kIn;
        if (age < kIn + kHold) return 1.0f;
        return std::max(0.0f, 1.0f - (age - kIn - kHold) / kOut);
    }
    bool over() const { return age >= kIn + kHold + kOut; }
};
