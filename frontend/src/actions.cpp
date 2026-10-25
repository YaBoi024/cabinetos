// apply: the one place a screen's Result is answered (#398, restructure
// batch e). Each case is a call into its feature's file; Settings' rows
// are dispatched in settingsflow.cpp.

#include "app.h"


using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// What a screen asked for, and whether the app can do it. A screen never
// reaches the disk, the network or a core; it returns one of these.
// Declared before `apply` because `apply` is what it calls, and defined
// after it for the same reason. std::function rather than a lambda, which
// is what lets the two refer to each other at all.
void App::apply(const screens::Result& res) {
    switch (res.action) {
        case screens::Action::None:
            break;
        case screens::Action::Back:
            if (accountsOpen) {
                // Closes the panel and puts focus back ON THE CHIP, which
                // is where it came from. Dropping focus into the screen
                // underneath would lose the place the person was at.
                accountsOpen = false;
                barFocused = true;
                barSlot = BarAccount;
                sound::play(sound::Cue::Back);
            } else if (stack.size() > 1 && here() == Screen::Detail && !shotMode) {
                pendingDetailBack = true;
                sound::play(sound::Cue::Back);
            } else if (here() == Screen::Search) {
                // B FROM SEARCH'S RESULTS goes Home the way B from its keys
                // does, keyboard and all. Popping the screen alone left the
                // keyboard, still open below the results, over Home with
                // the search in it (MMagTech on the TV, 2026-10-06). The
                // same steps as goToDestination(0), declared further down.
                if (keyboard.isOpen()) keyboard.cancel();
                searchScreen.setFocused(false);
                stack.clear();
                stack.push_back(Screen::Home);
                sound::play(sound::Cue::Back);
            } else if (stack.size() > 1) {
                stack.pop_back(); sound::play(sound::Cue::Back);
            } else sound::play(sound::Cue::Edge);
            break;
        case screens::Action::OpenTile: {
            openTile(res);
            break;
        }
        case screens::Action::SwitchAccount: {
            pressSwitchAccount(res);
            break;
        }
        case screens::Action::AddAccount:
            pressAddAccount();
            break;
        case screens::Action::Setting:
            applySetting(res);
            break;
        case screens::Action::SettingChoice:
            applySettingChoice(res);
            break;
        case screens::Action::FocusKeyboard:
            // Back into the keyboard under the results. Focus does not
            // leave the screen, it moves down within it.
            //
            // AND THE KEYBOARD COMES BACK IF IT IS GONE. Opening a game
            // from the results closes it (OpenGame below), and so does
            // Done; Down then took focus off the results with nothing to
            // receive it (#398, HOME-10). It slides up from below, as it
            // does when Search is arrived at (takePendingSwitch), holding
            // what was typed.
            if (!keyboard.isOpen()) {
                openSearchKeyboard();
                keyboardSlide.from = keyboardSlide.to = 1.0f;
                keyboardSlide.elapsed = 0.0f;
                keyboardSlide.retarget(0.0f, kKeyboardSlide);
            }
            searchScreen.setFocused(false);
            sound::play(sound::Cue::Move);
            break;
        case screens::Action::FocusBar:
            // Where the cursor lands in the bar: on the destination you are
            // standing in, so walking up and straight back down is a no-op
            // rather than a silent change of where you would go.
            barSlot = (here() == Screen::Library || here() == Screen::Grid) ? BarLibrary
                    : here() == Screen::Search                               ? BarSearch
                    : (here() == Screen::Settings || here() == Screen::HowTo) ? BarSettings
                                                                             : BarHome;
            barFocused = true;
            sound::play(sound::Cue::Move);
            break;
        case screens::Action::OpenGame:
            // A GAME OPENED FROM SEARCH'S RESULTS CLOSES ITS KEYBOARD. Up
            // from the keys leaves them open below the results, so typing
            // can go on; opening a game left them open over its page,
            // taking every press until B (MMagTech on the TV, 2026-10-06).
            if (here() == Screen::Search && keyboard.isOpen()) keyboard.cancel();
            // Opening a game's page, Play and a state are choices like any
            // other, and were silent (#389).
            sound::play(sound::Cue::Activate);
            openDetailSoon(res.value);
            break;
        case screens::Action::Play:
            if (refuseRemoteStart(res.value)) break;
            sound::play(sound::Cue::Activate);
            launchById(res.value);
            break;
        case screens::Action::PlayState:
            playFromState(res);
            break;
        case screens::Action::Download:
            sound::play(sound::Cue::Activate);
            downloadById(res.value);
            break;
        case screens::Action::ShowAchievements:
            if (detailAchRom == res.value && detailAch.known && !detailAch.items.empty()) {
                raList.open(detailScreen.game().title, detailAch);
                sound::play(sound::Cue::Activate);
            }
            break;
        case screens::Action::ToggleFavorite: {
            toggleFavorite(res);
            break;
        }
        case screens::Action::RemoveDownload: {
            pressRemoveDownload(res);
            break;
        }
    }
}
