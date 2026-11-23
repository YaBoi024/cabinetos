// Who the console is: switching, adding somebody, removing somebody and
// signing out, with the workers' results the frame takes (#398,
// restructure batch e).

#include "app.h"

#include <cstdio>
#include <utility>

#include "accountremove.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// SWITCHING WHO THE CONSOLE IS. Open question 26, and the half that
// `accounts::activate` deliberately does not do.
//
// THE LIST OF WHAT BELONGS TO AN ACCOUNT WAS WORKED OUT FROM
// `storage::userDir`'s children AND FROM WHAT THE STARTUP PATH BUILDS,
// before this was written rather than after somebody saw their sister's
// save. It is:
//
//   the token           -> accounts::activate
//   who we are          -> adoptUser, which asks /api/users/me again
//   recents, favourites -> loadLibrary; they are RomM's play history and
//   the hero, the shelf    they are per person, not per console
//   the kept badges     -> refreshKeeps, which reads this person's keeps
//
// **THE CATALOGUE IS PER ACCOUNT TOO, AND I HAD THIS WRONG.** This comment
// used to say the library of games is the same for everybody because there
// is one server per console, and that the refetch was incidental. Measured
// 2026-09-21 on the real server: MMagTech sees 1147 games and vivian sees
// 412. RomM scopes a library to the user, so the catalogue belongs on the
// list above rather than beside it.
//
// The code was already right — `loadLibrary` fetches the lot in one pass,
// so everything was being replaced regardless. It is the REASONING that was
// wrong, which matters because the next person to optimise this would have
// read that comment and skipped the refetch.
//
// TWO THINGS REFUSE THE SWITCH, and both are about a save reaching the
// wrong person rather than about tidiness. **THEY ARE NOT EQUALLY LIVE AND
// THE DIFFERENCE IS WORTH KNOWING** — MMagTech asked how you would even
// switch mid-game, and the answer is that you cannot.
auto App::switchAccount(int id, std::string* why) -> bool {
    // **UNREACHABLE THROUGH THE UI TODAY, AND KEPT ANYWAY.** While a game
    // runs the core owns the pad outright (`InputOwner::Game`), the bar is
    // not drawn at all (`if (!playing && ...)`), and the only overlay is
    // the pause menu, whose four rows are Resume, Save state, Load latest
    // and Exit. There is no way to reach the chip. Nothing but
    // `--switch-account` can make this branch fire.
    //
    // It stays because the hazard it names is real and the door is one row
    // wide: consoles do offer user switching from a pause menu, and the
    // day anybody adds that row this is what stops a running game's save
    // being filed under the wrong person — the core writes its card at
    // unload and `filesave` puts it under `storage::currentUser()`, which
    // a switch has just changed.
    //
    // **Do not read this as a tested guard.** It has never fired in
    // anger and it cannot until something calls this while a game runs.
    if (playing) {
        if (why) *why = "Quit the game before switching accounts.";
        return false;
    }
    // **THIS ONE IS REACHABLE AND IT IS THE ONE THAT MATTERS.** Exit a
    // game, the save starts going up in the background, you land on Home,
    // and the chip is right there — a window of a few seconds that anybody
    // switching users would walk straight into.
    //
    // AN UPLOAD IN FLIGHT WOULD GO UP AS THE NEW PERSON. The uploader holds
    // a pointer to this very client, so swapping the token underneath it
    // sends the previous account's save to the new account's library. An
    // unsent save is described elsewhere in this file as the one
    // irreplaceable thing on the machine; misfiling one is worse than
    // making somebody wait.
    if (const int owed = uploader.pending(); owed > 0) {
        if (why)
            *why = owed == 1 ? "A save is still uploading" : "Saves are still uploading";
        return false;
    }
    // NOR WHILE THE LIBRARY IS RELOADING after an offline start (#483,
    // FILES-10): its worker asks who the person is when it writes their
    // favourites, so a switch in between wrote the last person's into the
    // new person's favorites.json. It takes seconds; the chip is pressed
    // again after.
    if (onlineLoad.running.load()) {
        if (why) *why = "The library is still loading";
        return false;
    }

    // A REFUSED SWITCH PUTS THE LAST PERSON BACK (#398, SETTINGS-01).
    // `activate` writes the new id into accounts.json before anything below
    // can fail, and adoptUser moves the save folder, so every refusal from
    // here on undoes both. Left half switched, the console kept the last
    // person's Home and save folder under the new person's login and colour,
    // and sent one person's saves to the other's RomM account. Going back
    // can fail too; then it is only logged.
    const int was = accounts::activeId();
    const storage::User wasUser = storage::currentUser();
    const auto refuse = [&](const std::string& message) {
        if (why) *why = message;
        std::string e;
        if (was > 0 && !accounts::activate(was, liveClient, &e))
            std::fprintf(stderr, "[accounts] could not go back to %d: %s\n", was, e.c_str());
        storage::setCurrentUser(wasUser);
        std::fprintf(stderr, "[accounts] switch to %d refused (%s), back to %d\n", id,
                     message.c_str(), was);
        return false;
    };

    // Its own words in the journal, not on the television (#485): "account
    // 13's token would not load" is a line for whoever reads the log.
    std::string err;
    if (!accounts::activate(id, liveClient, &err)) {
        std::fprintf(stderr, "[accounts] %s\n", err.c_str());
        return refuse("Couldn't switch.");
    }
    // OFFLINE (#88): the person is who accounts.json says, and Home is
    // the games on the drive, which are the console's and not anybody's
    // (MMagTech, 2026-10-05). Their saves are their own, as always.
    if (romm::serverAway()) {
        if (!adoptAccountOffline(id)) return refuse("Couldn't switch.");
        lib = offlineLibrary();
        libFromDrive = true;   // loaded properly when the server answers
        libraryScreen.build(platformTiles, collectionTiles);
        homeFromTheStart();
        refreshKeeps();
        std::fprintf(stderr, "[accounts] switched to %d offline, %zu games\n", id,
                     cards.size());
        return true;
    }
    // ASKED AGAIN, NOT ASSUMED. The token changed, so the answer to "who is
    // this" changed, and every save path below is built from it. When the
    // server does not answer, adoptUser falls back to user.json, which still
    // names the last person: that is a refusal too, not a switch
    // (SETTINGS-02). An account's id is its RomM user id.
    if (!adoptUser(liveClient) || storage::currentUser().id != id)
        return refuse("The server would not say who that is.");

    // BUILT BESIDE THE LIVE ONE AND ONLY THEN SWAPPED IN. The store is a
    // single object now, so a failed switch must not have half-replaced it.
    Library fresh = loadLibrary(liveClient);
    // NOT `cards.empty()`, which this asked until open question 28. Cards
    // are recents and favourites now, so somebody who has played nothing
    // and starred nothing has none — and a real account with a working
    // server would have been refused. The platform list is what says the
    // server answered.
    if (fresh.platformTiles.empty()) {
        // Home left as it was rather than blanked, with the last person back
        // in to match it: an empty Home is worse than theirs.
        return refuse("That account's library came back empty.");
    }
    lib = std::move(fresh);
    libraryScreen.build(platformTiles, collectionTiles);
    startCoverFill();
    // HOME STARTS OVER FOR THE NEW PERSON: the first card of Recent, every
    // row scrolled back. Where the last person was is meaningless in a
    // different set of cards, and was out of range whenever the new set
    // was shorter.
    homeFromTheStart();
    refreshKeeps();

    const storage::User& now = storage::currentUser();
    std::fprintf(stderr, "[accounts] switched to %d - %s, %zu games\n", now.id,
                 now.name.c_str(), cards.size());
    return true;
}

void App::startAddAccount() {
    // ABOVE THE RUNNING TEST (SETTINGS-05): back out and come in again while
    // the code is live, and open() has just cleared the screen while the
    // poll runs on. The code is shown again from the running job, rather
    // than "Getting a code" until it expires.
    shownPairCode = false;
    if (addJob->running.load()) return;
    if (!rommAddress) {
        accountScreen.setNotice("This console has no server address.");
        return;
    }
    addJob->running = true;
    { std::lock_guard<std::mutex> lk(addJob->m);
      addJob->haveCode = addJob->finished = addJob->ok = false;
      addJob->err.clear(); }
    const std::string addr = rommAddress;
    if (addJob->th.joinable()) addJob->th.join();
    addJob->th = std::thread([addJob = addJob, addr]() {
        romm::Client c;              // ITS OWN. See the comment above.
        std::string err;
        if (!c.setAddress(addr, &err)) {
            std::lock_guard<std::mutex> lk(addJob->m);
            addJob->err = err; addJob->finished = true; addJob->running = false;
            return;
        }
        romm::Pairing p;
        if (!c.beginPairing(&p, &err)) {
            std::lock_guard<std::mutex> lk(addJob->m);
            addJob->err = err; addJob->finished = true; addJob->running = false;
            return;
        }
        { std::lock_guard<std::mutex> lk(addJob->m);
          addJob->pairing = p; addJob->haveCode = true; }

        // ON THE PAIRING'S OWN INTERVAL, and it expires in minutes — the
        // loop ends rather than running for ever, because a code nobody
        // approved is not an error worth blocking on.
        const int tries = p.expiresIn > 0 ? (p.expiresIn / (p.intervalSeconds > 0 ? p.intervalSeconds : 5)) + 1 : 60;
        for (int i = 0; i < tries; ++i) {
            {
                std::unique_lock<std::mutex> lk(addJob->m);
                if (addJob->wake.wait_for(
                        lk, std::chrono::seconds(p.intervalSeconds > 0 ? p.intervalSeconds : 5),
                        [&] { return addJob->stop; })) {
                    addJob->running = false;
                    return;
                }
            }
            const int state = c.pollPairing(p, &err);
            if (state == 0) continue;
            if (state < 0) break;
            std::string aerr;
            accounts::Paired who;
            const bool wrote = accounts::recordPairing(c, &who, &aerr);
            std::lock_guard<std::mutex> lk(addJob->m);
            addJob->ok = wrote;
            addJob->who = who;
            if (!wrote) addJob->err = aerr;
            addJob->finished = true; addJob->running = false;
            return;
        }
        std::lock_guard<std::mutex> lk(addJob->m);
        if (addJob->err.empty()) addJob->err = "expired";
        addJob->finished = true; addJob->running = false;
    });
    addAccountScreen.setBusy(true);
}

// Rebuilt from the store, which is the only thing that knows.
void App::refreshAccountRows() {
    // EVERYBODY EXCEPT WHOEVER IS SIGNED IN. The chip directly above the
    // panel is that person's row already; listing them again is the
    // redundancy MMagTech spotted in the first capture.
    std::vector<screens::AccountRow> rows;
    const int active = accounts::activeId();
    for (const accounts::Account& a : accounts::all())
        if (a.id != active) rows.push_back({a.id, a.name, a.avatar});
    accountScreen.setRows(std::move(rows));
    accountScreen.setAddEnabled(!romm::serverAway());
}

// WHO CAN BE REMOVED: everybody but whoever is signed in (the store
// refuses them) and the owner (the store refuses them too).
std::vector<accounts::Account> App::removableAccounts() {
    std::vector<accounts::Account> out;
    const int active = accounts::activeId(), owner = accounts::ownerId();
    for (const accounts::Account& a : accounts::all())
        if (a.id != active && a.id != owner) out.push_back(a);
    return out;
}

void App::finishRemove(const accounts::Account& who) {
    std::string err;
    if (!removePersonNow(who, &err)) {
        std::fprintf(stderr, "[accounts] could not remove %d: %s\n", who.id, err.c_str());
        menuNotice.say("Couldn't remove " + who.name, Tone::Problem);
    }
    refreshAccountRows();
    buildSettings();
}

void App::startRemove(const accounts::Account& who) {
    if (removeJob->running.load()) return;
    std::string err;
    if (!accounts::canRemove(who.id, &err)) {
        std::fprintf(stderr, "[accounts] could not remove %d: %s\n", who.id, err.c_str());
        return;
    }
    removeJob->running = true;
    { std::lock_guard<std::mutex> lk(removeJob->m);
      removeJob->finished = false; removeJob->left = 0; removeJob->who = who;
      removeJob->everyone = false; }
    menuNotice.say("Removing " + who.name, Tone::Info);
    const std::string addr = rommAddress ? rommAddress : "";
    // The last send has finished (running was false), so this join is quick.
    if (removeJob->th.joinable()) removeJob->th.join();
    removeJob->th = std::thread([job = removeJob.get(), who, addr]() {
        const int left = sendOwedBeforeRemoving(who, addr, &job->stop);
        std::lock_guard<std::mutex> lk(job->m);
        job->left = left;
        job->finished = true;
        job->running = false;
    });
}

// SIGN OUT SENDS FIRST TOO, the same way (MMagTech, 2026-10-05: "the two
// screens and behavior should be similar"). Everyone's owed saves go with
// their own logins; the uploader only ever sends the signed-in person's,
// so anybody else's would otherwise be lost to a sign out.
void App::startSignOut() {
    if (removeJob->running.load()) return;
    // NOT WHILE AN UPLOAD IS GOING (#483, FILES-11), as a switch is not:
    // the send below covers the signed-in person too, so the same file
    // could go to RomM twice at once.
    if (const int owed = uploader.pending(); owed > 0) {
        menuNotice.say(owed == 1 ? "A save is still uploading" : "Saves are still uploading",
                       Tone::Info);
        return;
    }
    removeJob->running = true;
    { std::lock_guard<std::mutex> lk(removeJob->m);
      removeJob->finished = false; removeJob->left = 0;
      removeJob->who = accounts::Account{}; removeJob->everyone = true; }
    menuNotice.say("Signing out", Tone::Info);
    const std::string addr = rommAddress ? rommAddress : "";
    if (removeJob->th.joinable()) removeJob->th.join();
    removeJob->th = std::thread([job = removeJob.get(), addr]() {
        int left = 0;
        // STOPPED BETWEEN PEOPLE TOO (#484): told to stop at exit, it went
        // on to ask the server for each person left, a heartbeat each.
        for (const accounts::Account& a : accounts::all()) {
            if (job->stop.load()) break;
            left += sendOwedBeforeRemoving(a, addr, &job->stop);
        }
        std::lock_guard<std::mutex> lk(job->m);
        job->left = left;
        job->finished = true;
        job->running = false;
    });
}

// ---- An account switch, behind the curtain ---------------------------
//
// The request (the account panel's SwitchAccount) only sets these. Each
// frame this brings the curtain down; once it is fully down and a frame
// with the name on it has been shown, it runs the blocking switch, then
// lifts the curtain on the new person's Home. MMagTech, 2026-09-24: the UI
// stalled on a switch with nothing to say why.
// (switchPendingId, switchLabel and switchCurtainFrames are declared by
// the curtain, further up, because the account panel's apply sets them.)
void App::pumpSwitch() {
    if (!switchPendingId) return;
    curtain.retarget(1.0f, kCurtainDown);
    if (curtain.value() < 0.995f) return;
    if (switchShownAt == 0) {
        switchShownAt = SDL_GetTicks();
        switchText.smooth = true;
        switchText.retarget(1.0f, kSwitchTextFade);
        // THE CURTAIN TURNS THEIR COLOUR while it says their name, rather
        // than the colour jumping behind it. A switch that fails puts it
        // back below.
        look::setColour(accountColour(switchPendingId));
    }
    if (!switchDone) {
        // Two frames at full curtain, so the name is on the television
        // before the frame loop stops for the network.
        if (++switchCurtainFrames < 3) return;
        const Uint64 t0 = SDL_GetTicks();
        switchWhy.clear();
        switchOk = switchAccount(switchPendingId, &switchWhy);
        // Whoever is active now: the new person, or the last one again
        // when the switch was refused (switchAccount puts them back).
        ra::useAccount(accounts::activeId());
        std::fprintf(stderr, "[accounts] switch took %llu ms\n",
                     static_cast<unsigned long long>(SDL_GetTicks() - t0));
        switchDone = true;
        // Straight to Home, because everything behind it belonged to the
        // last account. Behind the curtain, so it is ready when it lifts.
        if (switchOk) {
            stack.clear();
            stack.push_back(Screen::Home);
        }
    }
    // THE NAME STAYS UP LONG ENOUGH TO READ. A switch usually takes a
    // quarter of a second, and the curtain dropping and lifting around
    // that read as a glitch: MMagTech, 2026-09-24, *"the account switch
    // text flash to fast that it just seems like a glitch"*. A slow
    // switch is not held any longer than it took. Starting value.
    constexpr Uint64 kSwitchHoldMs = 700;
    if (SDL_GetTicks() - switchShownAt < kSwitchHoldMs) return;
    // The name goes before the curtain does.
    switchText.retarget(0.0f, kSwitchTextFade);
    if (switchText.value() > 0.01f) return;
    const bool ok = switchOk;
    const std::string why = switchWhy;
    switchPendingId = 0;
    switchDone = false;
    switchShownAt = 0;
    if (!ok) {
        applyAccountColour(false);
        // Back to the panel, saying why, with the old person still in.
        accountsOpen = true;
        barFocused = true;
        barSlot = BarAccount;
        refreshAccountRows();
        accountScreen.open();
        accountScreen.setNotice(why);
        sound::play(sound::Cue::Edge);
    }
    curtain.retarget(0.0f, kCurtainUp);
}

void App::takeAccountResults() {
    // A removal's or a sign out's sending done (#194): go ahead, or ask
    // the same question again with what would be lost, when something
    // could not reach RomM. The words are Sign out's own, for both
    // (MMagTech, 2026-10-05). No count and no list. Focus on Cancel,
    // which keeps everything.
    {
        bool fin = false, everyone = false;
        int left = 0;
        accounts::Account who;
        {
            std::lock_guard<std::mutex> lk(removeJob->m);
            fin = removeJob->finished;
            left = removeJob->left;
            who = removeJob->who;
            everyone = removeJob->everyone;
            removeJob->finished = false;
        }
        if (fin && everyone && left == 0) startLeaving(Leave::SignOut, "Signing out");
        if (fin && everyone && left > 0)
            askChoice("Sign out?", "Saves waiting to upload will be lost.",
                      {"Sign out", "Cancel"}, 1, [&](int k) {
                          if (k == 0) startLeaving(Leave::SignOut, "Signing out");
                      });
        if (fin && !everyone && left == 0) finishRemove(who);
        if (fin && !everyone && left > 0)
            askChoice("Remove " + who.name + "?", "Saves waiting to upload will be lost.",
                      {"Remove", "Cancel"}, 1, [&, who](int k) {
                          if (k == 0) finishRemove(who);
                      });
    }
    // The pairing worker's answer, picked up on the frame thread. Nothing
    // here touches the network — it reads what the thread published.
    if (here() == Screen::AddAccount) {
        bool code = false, fin = false, ok = false;
        std::string url, user, err;
        accounts::Paired who;
        {
            std::lock_guard<std::mutex> lk(addJob->m);
            code = addJob->haveCode; fin = addJob->finished; ok = addJob->ok;
            url = addJob->pairing.verificationUrl;
            user = addJob->pairing.userCode;
            err = addJob->err;
            who = addJob->who;
        }
        if (fin) {
            { std::lock_guard<std::mutex> lk(addJob->m); addJob->finished = false; }
            if (ok && who.isNew) {
                // Added, NOT switched to. Back to the panel with the new
                // person in it, which is where the switch is.
                refreshAccountRows();
                // AND SETTINGS UNDER IT, which it may have been opened
                // from: Remove an account stayed greyed out until Settings
                // was left and re-entered. MMagTech, on the A9.
                buildSettings();
                if (stack.size() > 1) stack.pop_back();
                accountsOpen = true;
                barFocused = true;
                barSlot = BarAccount;
                // NO NOTE. "<name> was added. Choose them to switch." used
                // to sit under the list; MMagTech, 2026-09-24: it looked
                // bad and said what scanning the code had just done. Their
                // name appearing in the list is the confirmation.
                accountScreen.setNotice("");
                std::fprintf(stderr, "[accounts] added %d - %s, now %zu accounts, "
                                     "still acting as %d\n",
                             who.id, who.name.c_str(), accounts::all().size(),
                             accounts::activeId());
            } else if (ok) {
                // **THE CASE THAT LIED.** The pairing worked and wrote a
                // valid token, and it added nobody: whoever approved it
                // already has an account here. Staying on this screen and
                // saying so is right — going back to a panel that looks
                // exactly as it did is what made this look broken.
                // IT RENEWED THEIR SIGN-IN, which is also how a console
                // paired before a new permission gets it (collections.write
                // for favourites, #267): say that, and use the new key now
                // if it is the person the console is acting as, as the
                // account switch does: only with nothing going up, since
                // the uploader holds this client. Otherwise at next start.
                if (who.id == accounts::activeId() && uploader.pending() == 0 &&
                    accounts::loadActiveToken(liveClient))
                    uploader.resendOwed();
                addAccountScreen.setError(
                    who.name + " is already on this console. Sign in to RomM as "
                    "the person you are adding.");
                std::fprintf(stderr, "[accounts] NOT ADDED: approved as %d - %s, "
                                     "who is already here. %zu accounts.\n",
                             who.id, who.name.c_str(), accounts::all().size());
            } else {
                // The reason stays in the log; the screen says it plainly.
                addAccountScreen.setError(err.find("expired") != std::string::npos
                                              ? "The code expired"
                                              : "Couldn't add the account");
                std::fprintf(stderr, "[accounts] add failed: %s\n",
                             err.empty() ? "no reason given" : err.c_str());
            }
        } else if (code && !shownPairCode) {
            shownPairCode = true;
            addAccountScreen.setPairing(url, user);
            // SAID IN THE JOURNAL AS WELL AS ON THE TELEVISION. A code
            // that exists only as pixels cannot be read back by anybody
            // helping from a shell, and this is the one screen whose whole
            // content is a string somebody has to act on within minutes.
            // It is not a secret: the device code is, and that is not this.
            std::fprintf(stderr, "[accounts] pair at %s (code %s)\n",
                         url.c_str(), user.c_str());
        }
    }
}

void App::pressRemoveAccount() {
    // PIN (if set), then who, then are you sure. With one
    // person to remove, "who" is skipped.
    askPin("Enter the PIN", "To remove an account", [&]() {
        auto confirm = [&](accounts::Account who) {
            // NO EXPLANATION. MMagTech, 2026-09-24: the people
            // using this are technical, "we don't need to spoon
            // feed them everything with an explanation".
            askChoice("Remove " + who.name + "?", "",
                      {"Remove", "Cancel"}, 1, [&, who](int i) {
                          if (i == 0) startRemove(who);
                      });
        };
        const std::vector<accounts::Account> people = removableAccounts();
        if (people.empty()) return;
        if (people.size() == 1) { confirm(people[0]); return; }
        std::vector<std::string> names;
        for (const auto& p : people) names.push_back(p.name);
        names.push_back("Cancel");
        askChoice("Remove an account", "", names, 0,
                  [&, people, confirm](int i) {
                      if (i >= 0 && i < static_cast<int>(people.size()))
                          confirm(people[i]);
                  });
    });
    sound::play(sound::Cue::Activate);
}

void App::pressAddAccountRow() {
    // The same route as the chip's Add user, PIN included.
    // Back from the pairing screen returns here, because it
    // is pushed.
    askPin("Enter the PIN", "To add an account", [&]() {
        addAccountScreen.open();
        stack.push_back(Screen::AddAccount);
        startAddAccount();
    });
    sound::play(sound::Cue::Activate);
}

void App::pressSwitchAccount(const screens::Result& res) {
    // THE REFUSALS BELONG TO THE APP AND SO DO THEIR WORDS. The
    // screen does not know whether a game is running or a save is
    // still going up; it asked to become somebody and this decides.
    const int id = res.value;
    // NOT SWITCHED HERE. The switch asks the server who this is and
    // reloads the library, which blocks the frame loop; done here,
    // the screen froze mid-panel for as long as that took (eleven
    // seconds once, on the A9). It is handed to pumpSwitch, which
    // brings the curtain down with the person's name on it first.
    auto go = [&, id]() {
        const std::vector<accounts::Account> list = accounts::all();
        const accounts::Account* a = accounts::find(list, id);
        switchPendingId = id;
        switchCurtain = true;
        switchLabel = "Switching to " + (a ? a->name : std::string("them"));
        switchCurtainFrames = 0;
        accountsOpen = false;
        barFocused = false;
        accountScreen.setNotice("");
        curtain.retarget(1.0f, kCurtainDown);
        sound::play(sound::Cue::Activate);
    };
    // THE SAME PIN STOPS ANYBODY SWITCHING INTO THE OWNER.
    // docs/SETTINGS.md, Accounts. With no PIN, askPin just goes.
    if (id == accounts::ownerId() && accounts::activeId() != id) {
        const std::vector<accounts::Account> list = accounts::all();
        const accounts::Account* a = accounts::find(list, id);
        askPin("Enter the PIN",
               "To switch to " + (a ? a->name : std::string("them")), go,
               /*always=*/true);
    } else {
        go();
    }
}

void App::pressAddAccount() {
    // WITH A PIN SET, ADDING SOMEBODY ASKS FOR IT, from here and
    // from Settings alike. MMagTech, 2026-09-24: without it,
    // anyone holding the controller could add themselves; with no
    // PIN, anyone can, which is the no-PIN rule everywhere.
    askPin("Enter the PIN", "To add an account", [&]() {
        // THE PANEL CLOSES AND A SCREEN OPENS. Leaving the panel
        // up behind a pairing code would put the list somebody is
        // about to change underneath the thing changing it.
        accountsOpen = false;
        barFocused = false;
        addAccountScreen.open();
        stack.push_back(Screen::AddAccount);
        startAddAccount();
    });
    sound::play(sound::Cue::Activate);
}
