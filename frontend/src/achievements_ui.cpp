// RetroAchievements on screen: signing in on the keyboard, the unlock pop-up
// and its age (#74) (#398, restructure batch e).

#include "app.h"

#include <algorithm>
#include <cstdio>

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::askRaUser(const std::string& initial) {
    ui::Keyboard::Config cfg;
    cfg.title = "RetroAchievements";
    cfg.placeholder = "Username";
    cfg.initial = initial;
    keyboard.open(cfg);
    keyboardThen = [&](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        std::string user = keyboard.value();
        while (!user.empty() && user.back() == ' ') user.pop_back();
        while (!user.empty() && user.front() == ' ') user.erase(user.begin());
        if (user.empty()) return;
        askRaPassword(user, "");
    };
}

void App::askRaPassword(const std::string& user, const std::string& why) {
    ui::Keyboard::Config cfg;
    cfg.title = user;
    cfg.placeholder = "Password";
    cfg.conceal = true;
    keyboard.open(cfg);
    if (!why.empty()) keyboard.sayInField(why, /*problem=*/true);
    keyboardThen = [&, user, cfg](ui::KeyboardResult r) {
        if (r != ui::KeyboardResult::Committed) return;
        const std::string pass = keyboard.value();
        if (pass.empty()) { askRaPassword(user, ""); return; }
        // UP WHILE IT ASKS, as Wi-Fi's does while it joins; B leaves and
        // the answer still lands (Settings is rebuilt either way).
        keyboard.open(cfg);
        keyboard.sayInField("Signing in\xE2\x80\xA6", /*problem=*/false);
        keyboard.setBusy(true);
        raKeyboardWaiting = true;
        keyboardThen = [&](ui::KeyboardResult) { raKeyboardWaiting = false; };
        ra::signIn(user, pass, [&, user](bool ok, const std::string& why) {
            const bool waiting = raKeyboardWaiting && keyboard.isOpen();
            raKeyboardWaiting = false;
            if (!ok && waiting) {
                askRaPassword(user, why);
                return;
            }
            if (waiting) {
                keyboard.cancel();
                keyboardThen = nullptr;
            }
            if (ok) menuNotice.say("Signed in as " + ra::username(), Tone::Done);
            else menuNotice.say(why, Tone::Problem);
            if (here() == Screen::Settings) buildSettings();
        });
    };
}

void App::tickRaToast(float dt) {
    if (raToast.on) {
        raToast.age += dt;
        if (raToast.over()) raToast.on = false;
    }
    // ONLY OVER A GAME. One that lands as the game is left is not shown on
    // Home, where the card's corner is the account chip's: the game's page
    // has it (MMagTech, 2026-10-06).
    if (!playing) {
        ra::Popup dropped;
        while (ra::takePopup(&dropped)) gameEventUnlocked(dropped);
        raToast.on = raToast.waiting = false;
    }
    if (!raToast.on && !raToast.waiting && ra::takePopup(&raToast.p)) {
        gameEventUnlocked(raToast.p);
        raToast.waiting = true;
        raToast.age = 0.0f;
    }
    if (raToast.waiting) {
        raToast.age += dt;
        const bool badge = raToast.p.badgeUrl.empty() || images.get(raToast.p.badgeUrl).ready;
        if (badge || raToast.age >= AchievementToast::kBadgeWait) {
            raToast.waiting = false;
            raToast.on = true;
            raToast.age = 0.0f;
            raToast.badge = !raToast.p.badgeUrl.empty() && badge;
            sound::playChime();
            std::fprintf(stderr, "[ra] pop-up: %s%s\n", raToast.p.title.c_str(),
                         badge ? "" : " (badge not in yet)");
        }
    }
}

// ---- A RetroAchievements unlock (#74) -------------------------------
//
// The same surface and edge as the notice pill, as a card: the badge
// on the left, what it was and its points above, its name below. TOP
// RIGHT inside the safe area, where consoles put these (MMagTech on
// the TV, 2026-10-06: bottom left was "horrible"), and clear of the
// notice pill at the foot. It drops in a little as it fades in.
void App::drawRaToast(float sc) {
    if (const float ta = raToast.alpha(); ta > 0.01f) {
        const float keepAlpha = renderer.contentAlpha();
        renderer.setContentAlpha(1.0f);
        // SMALL: enough to say "you got one", not to read about it; the
        // game's page has the rest (MMagTech on the TV, 2026-10-06: the
        // first size was "more distracting than informing").
        constexpr float kH = 84.0f, kPad = 12.0f, kBadge = 60.0f, kGap = 16.0f,
                        kMaxText = 520.0f;
        const ui::TextStyle top = ui::TextStyle::Caption1, name = ui::TextStyle::Callout;
        const ra::Popup& p = raToast.p;
        const std::string line1 =
            p.complete ? "Every achievement unlocked" : "Achievement unlocked";
        const std::string line2 = text.truncate(p.title, name, sc, kMaxText);
        const float tw = std::min(kMaxText, std::max(text.measure(line1, top, sc),
                                                     text.measure(line2, name, sc)));
        const float badgeRoom = raToast.badge ? kBadge + kGap : 4.0f;
        const float w = kPad + badgeRoom + tw + kPad + 8.0f;
        // TUCKED INTO THE CORNER: closer to the top than to the side, so
        // on a screen wider than it is tall the two gaps look the same
        // (MMagTech on the TV, 2026-10-06: equal margins read as low).
        constexpr float kTop = 32.0f, kRight = 48.0f;
        const float x = ui::kCanvasWidth - kRight - w;
        const float drop = (1.0f - std::min(1.0f, raToast.age / AchievementToast::kIn));
        const float y = kTop - drop * 10.0f;
        // A LITTLE OF THE GAME SHOWS THROUGH, so it passes over the game
        // rather than sitting on it; no lower, or white text washes out
        // over a bright screen (MMagTech, 2026-10-06).
        ui::Color fill = look::surface();
        fill.a = 0.80f * ta;
        ui::Rect card{x, y, w, kH, 20.0f, fill};
        card.border = 1.5f;
        card.borderColor = ui::Color::white(0.14f * ta);
        card.edgeLight = ui::Color::white(0.18f * ta);
        card.shadowBlur = 18.0f;
        card.shadowOffsetY = 6.0f;
        card.shadowColor = ui::Color::black(0.40f * ta);
        renderer.draw(card);
        const float bx = x + kPad, by = y + (kH - kBadge) * 0.5f;
        if (raToast.badge) {
            const ui::Image& img = images.get(p.badgeUrl);
            renderer.drawTextured(bx, by, kBadge, kBadge, img.texture, 0, 0, 1, 1,
                                  ui::Color{1, 1, 1, ta * img.fade}, false, 0.0f, bx, by,
                                  kBadge, kBadge, 10.0f);
        }
        const float tx = bx + badgeRoom;
        const float l1 = text.lineHeight(top, sc), l2 = text.lineHeight(name, sc);
        const float ty = y + (kH - (l1 + l2)) * 0.5f;
        text.draw(renderer, line1, tx, ty + text.ascent(top, sc), top,
                  ui::Color::white(0.66f * ta), sc);
        text.draw(renderer, line2, tx, ty + l1 + text.ascent(name, sc), name,
                  ui::Color::white(0.96f * ta), sc);
        renderer.setContentAlpha(keepAlpha);
    }
}

void App::pressRetroAchievements() {
    if (ra::signedIn()) {
        askChoice("RetroAchievements", "Signed in as " + ra::username(),
                  {"Sign out", "Cancel"}, 1, [&](int k) {
                      if (k == 0) ra::signOut();
                      buildSettings();
                  });
    } else {
        askRaUser("");
    }
    sound::play(sound::Cue::Activate);
}
