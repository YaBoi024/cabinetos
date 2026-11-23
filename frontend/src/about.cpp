// About: What's new (#329), How to, the page of what every control does
// (#314), and Credits and licenses (#398, restructure batch e).

#include "app.h"

#include <utility>

#include "credits.h"
#include "shortcuts.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::openHowTo() {
    using K = screens::SettingsRow::Kind;
    std::string button = "Home";
    if (SDL_Gamepad* gp = settingsLastPad ? SDL_GetGamepadFromID(settingsLastPad) : nullptr) {
        const std::string l = shortcuts::label(gp);
        if (!l.empty() && l != "Not set") button = l;
    }
    const bool on = shortcuts::enabled();
    auto line = [](const char* what, std::string how, K k = K::Info) {
        return screens::SettingsRow{k, 0, what, "", std::move(how)};
    };
    std::vector<screens::SettingsCategory> cats;
    cats.push_back({"In a game",
                    {line("Pause menu", "Both sticks in (L3 + R3)"),
                     line("Pause menu, Wii Remote", "Hold HOME"),
                     line("Pause menu, Remote Play", "Hold Select"),
                     line("Leave a game", "Pause menu, Exit to Home"),
                     line("Power menu", "The PC's power button")}});
    // WHAT WORKS IN ONE KIND OF GAME ONLY, apart from what works in every
    // game, so "In a game" stays short (MMagTech, 2026-10-25, #577).
    cats.push_back({"Systems",
                    {line("Arcade game settings", "Hold Start"),
                     line("DS and 3DS screens", "Click L3"),
                     line("DS and 3DS touch screen", "Right stick moves, R3 touches"),
                     line("DS lid", "Hold R2"),
                     line("PS1 analog button", "L1 + R1 + Select")},
                    ""});
    {
        std::vector<screens::SettingsRow> rows;
        if (!on) rows.push_back(line("Turn on", "Settings, Controllers, In-game shortcuts"));
        const K k = on ? K::Info : K::Disabled;
        rows.push_back(line("Pause menu", "Tap " + button, k));
        rows.push_back(line("Save state", button + " + R1", k));
        rows.push_back(line("Load state", button + " + L1", k));
        rows.push_back(line("Fast forward", button + " + R2", k));
        rows.push_back(line("Rewind", button + " + L2", k));
        rows.push_back(line("Screenshot", button + " + X", k));
        cats.push_back({"Shortcuts", std::move(rows)});
    }
    cats.push_back({"Menus",
                    {line("Select", "A"),
                     line("Back", "B"),
                     line("Change screen", "L1 / R1"),
                     line("Game manual", "R1, on a game's page"),
                     line("Jump a letter", "L2 / R2"),
                     line("Power menu", "Start, on Home"),
                     line("Switch person", "Up to the top bar, A on your name")}});
    cats.push_back({"Steam",
                    {line("Open", "Power menu, Switch to Steam"),
                     line("Come back", "Steam's Power menu, Switch to Desktop")}});
    cats.push_back({"Controllers",
                    {line("Pair a controller", "Settings, Controllers, Add a controller"),
                     line("Pair a Wii Remote", "The red sync button, under the battery cover")}});
    cats.push_back({"Drives", {line("Unplug", "Eject it first, in Settings, Storage")}});
    {
        std::string shown = kWikiUrl;
        shown = shown.substr(shown.find("//") + 2);
        if (!shown.empty() && shown.back() == '/') shown.pop_back();
        cats.push_back({"Wiki", {line("Address", shown)}, kWikiUrl});
    }
    howToScreen.setCategories(std::move(cats));
    howToScreen.enter();
    stack.push_back(Screen::HowTo);
}

void App::pressCredits() {
    // A list, one line per project: what it does, and its
    // licence. Nothing to choose; A or B closes it.
    std::vector<std::string> names, values;
    for (const Credit& cr : kCredits) {
        names.push_back(cr.name);
        values.push_back(cr.what);
    }
    askChoice("Credits and licenses", "", names, 0, [](int) {});
    choiceScreen.setValues(values);
    sound::play(sound::Cue::Activate);
}

void App::pressHowTo() {
    openHowTo();
    sound::play(sound::Cue::Activate);
}

void App::pressWhatsNew() {
    // The page itself, not a version on it: whoever updates is already on
    // the newest, and the newest is the top of the page (MMagTech,
    // 2026-10-19). The code never changes, so it is made once
    // (tools/make-wiki-qr.py), and B is the way back.
    whatsNewScreen.open();
    whatsNewScreen.setText("What's new", {"Scan to read what changed."}, "");
    whatsNewScreen.setBaked(wikiqr::Which::WhatsNew);
    stack.push_back(Screen::WhatsNew);
    sound::play(sound::Cue::Activate);
}
