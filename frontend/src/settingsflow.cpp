// Settings: its rows built from what is true now, the row ids' dispatch,
// and the choice rows (#398, restructure batch e). Each feature's rows and
// presses are in its own file; the ids are settingsids.h.

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "chargelimit.h"
#include "idlesleep.h"
#include "prefs.h"
#include "quality.h"
#include "rumble.h"
#include "shortcuts.h"
#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// THE DRIVES' NAMES, as Storage shows them: the main drive is
// "CabinetOS", any other internal disk "Internal", one that can be
// unplugged "External". With two of one kind, counting drives that cannot
// be used, the drive's own name tells them apart: its label (the mount
// point) for one in use. Downloads names a game's drive the same way.
std::map<std::string, std::string> App::locationNames(const std::vector<std::string>& locs,
                                                      const std::vector<drives::Unusable>& unusable) {
    std::map<std::string, std::string> out;
    int internals = 0, externals = 0;
    for (size_t i = 1; i < locs.size(); ++i)
        (storage::isExternal(locs[i]) ? externals : internals)++;
    for (const drives::Unusable& u : unusable) (u.external ? externals : internals)++;
    for (size_t i = 0; i < locs.size(); ++i) {
        std::string name = "CabinetOS";
        if (i > 0) {
            const bool external = storage::isExternal(locs[i]);
            name = external ? "External" : "Internal";
            if ((external ? externals : internals) > 1) {
                // <mount>/CabinetOS: the mount point is named for the drive.
                std::string mount = locs[i].substr(0, locs[i].rfind('/'));
                name += " (" + mount.substr(mount.rfind('/') + 1) + ")";
            }
        }
        out[locs[i]] = name;
    }
    return out;
}

void App::buildSettings() {
    using Row = screens::SettingsRow;
    using K = Row::Kind;
    auto gb = driveSize;
    std::vector<screens::SettingsCategory> cats;

    cats.push_back({"Accounts", {
        {K::Action, SetAddAccount, "Add an account", "", ""},
    }});
    // REMOVE AN ACCOUNT: from this console only. Greyed out when there is
    // nobody it could remove (not the person signed in, not the owner).
    cats.back().rows.push_back(Row{removableAccounts().empty() ? K::Disabled : K::Action,
                                   SetRemoveAccount, "Remove an account", "", ""});
    // THE PIN IS THE OWNER'S, AND ONLY THE OWNER SEES ITS CONTROLS.
    // Anyone else sees whether there is one and whose it is, with nothing
    // to press. MMagTech, 2026-09-24, signed in as claire and offered
    // Change PIN and Turn off PIN: *"why would claire or anyone but me have
    // the option to change the pin or turn it off"*. To change it while
    // somebody else is signed in, the owner switches in, which asks for it.
    {
        auto& rows = cats.back().rows;
        const char* protects = "Protects accounts, network, storage, Steam and updates";
        const bool isOwner = accounts::activeId() == accounts::ownerId();
        const std::vector<accounts::Account> list = accounts::all();
        const accounts::Account* owner = accounts::find(list, accounts::ownerId());
        const std::string ownerName = owner ? owner->name : std::string("the owner");
        if (isOwner && accounts::pinIsSet()) {
            rows.push_back({K::Action, SetPinChange, "Change PIN", protects, ""});
            rows.push_back({K::Action, SetPinOff, "Turn off PIN", "", ""});
        } else if (isOwner) {
            rows.push_back({K::Action, SetPinSet, "Set a PIN", protects, ""});
        } else if (accounts::pinIsSet()) {
            rows.push_back({K::Info, 0, "PIN", "Set by " + ownerName, "On"});
        } else {
            rows.push_back({K::Info, 0, "PIN", "Only " + ownerName + " can set one", "Off"});
        }
        // RETROACHIEVEMENTS (#74), this person's own sign-in. The one
        // place it shows for somebody who has not signed in.
        rows.push_back({K::Action, SetRetroAchievements, "RetroAchievements", "",
                        ra::signedIn() ? ra::username() : "Sign in"});
    }

    // THE CONTROLLERS: ONE ROW, opening a panel of its own, as Wii
    // Remotes do (MMagTech on the TV, 2026-10-10: three pads, one row
    // each, filled the page and a fourth would push Rumble and the
    // shortcuts off it; one screen per section, as for the Remotes). The
    // panel has a row per pad, its player as the value, and Add a
    // controller. openPadsPanelAt. players.h, issue #64.
    {
        std::vector<Row> rows;
        const std::vector<players::Pad> pads = players::connected();
        settingsPadsSeen = players::generation();
        rows.push_back({K::Action, SetPads, "Controllers", "",
                        pads.empty() ? "None" : std::to_string(pads.size()) + " connected"});
        // REAL WII REMOTES (#200, PROJECT.md question 35): pair one once,
        // as on a Wii, and the Wii games made for it play. ONE ROW, opening
        // a panel of its own (the paired Remotes, Pair a Wii Remote, the
        // sensor bar): four rows of Wii here pushed the pads' own rows down
        // (MMagTech, 2026-10-02).
        {
            const size_t n = wiiremote::known().size();
            rows.push_back({K::Action, SetWiiRemotes, "Wii Remotes", "",
                            n == 0 ? "None" : std::to_string(n) + " paired"});
        }
        // RUMBLE, on by default, for every pad and every system with
        // motors: one switch, as Cabinet had it (rumble.h, #149). Above
        // the shortcuts so their button row stays under its own switch.
        {
            Row r{K::Choice, SetRumble, "Rumble", "", ""};
            r.choices = {"Off", "On"};
            r.choice = rumble::enabled() ? 1 : 0;
            rows.push_back(r);
        }
        // IN-GAME SHORTCUTS, off by default (shortcuts.h). The Button
        // mapping row that stood here went with #66: there is no mapping
        // screen. With the switch on, the shortcut button row is for the
        // pad in hand, named on the row, since the button is remembered
        // per kind of pad.
        {
            Row r{K::Choice, SetShortcuts, "In-game shortcuts", "", ""};
            r.choices = {"Off", "On"};
            r.choice = shortcuts::enabled() ? 1 : 0;
            rows.push_back(r);
        }
        if (shortcuts::enabled() && !pads.empty()) {
            const players::Pad* inHand = &pads.front();
            for (const players::Pad& p : pads)
                if (p.id == settingsLastPad) inHand = &p;
            SDL_Gamepad* gp = SDL_GetGamepadFromID(inHand->id);
            // "Shortcuts use: Home", read with the switch above it as one
            // sentence (MMagTech, 2026-10-20): a fact for the player with a
            // working Home, and the place to press a spare button for one
            // whose Home the console cannot see.
            rows.push_back({K::Action, SetShortcutButton, "Shortcuts use", inHand->name,
                            !shortcutListen           ? shortcuts::label(gp)
                            : !shortcutRefused.empty() ? shortcutRefused + "\xE2\x80\xA6"
                                                       : "Press a spare button\xE2\x80\xA6"});
        }
        cats.push_back({"Controllers", std::move(rows)});
    }

    std::string netValue = "Checking\xE2\x80\xA6", netDetail;
    {
        std::lock_guard<std::mutex> lk(settingsNet.m);
        if (settingsNet.have) {
            const net::Status& st = settingsNet.st;
            if (st.managerMissing) netValue = "Unknown";
            else if (!st.online) netValue = "Not connected";
            else if (st.link == net::Link::Ethernet) netValue = "Connected over Ethernet";
            else if (st.link == net::Link::WiFi)
                netValue = "Connected to " + (st.connection.empty() ? std::string("Wi-Fi")
                                                                    : st.connection);
            else netValue = "Connected";
            if (!st.ipv4.empty()) {
                // "192.168.0.20/24" -> the address a person would type.
                netDetail = "Address " + st.ipv4.substr(0, st.ipv4.find('/'));
            }
        }
    }
    cats.push_back({"Network", {
        {K::Info, 0, "Status", netDetail, netValue},
        {K::Action, SetServer, "RomM server", "", rommAddress ? rommAddress : ""},
    }});
    // ONE WI-FI ROW, showing the network in use. The networks themselves
    // are in a panel it opens: as rows here they flooded the page in a
    // crowded building. MMagTech, 2026-09-24.
    {
        bool radioPresent = true, radioOn = true;
        std::string onWifi;
        {
            std::lock_guard<std::mutex> lk(settingsNet.m);
            if (settingsNet.have) {
                radioPresent = settingsNet.st.wifiPresent;
                radioOn = settingsNet.st.wifiEnabled;
            }
        }
        {
            std::lock_guard<std::mutex> lk(settingsWifi.m);
            for (const net::Network& n : settingsWifi.list)
                if (n.active) onWifi = n.ssid;
        }
        if (radioPresent)
            cats.back().rows.push_back(
                {K::Action, SetWifi, "Wi-Fi", "",
                 !radioOn ? "Off" : (onWifi.empty() ? "Not connected" : onWifi)});
    }

    // REMOTE PLAY (#286): its own section, a highlight (MMagTech,
    // 2026-10-07). Three rows: the switch ("Streaming": "Remote Play"
    // twice, as section and row, read oddly), with the app to use and the
    // name to pick in it; the paired devices, one row however many there
    // are, opening a list; and Tailscale, for playing away from home.
    // Sunshine's own name is never on screen.
    {
        const bool on = rpState == "active";
        std::string value = on ? "On" : "Off";
        // "app", so it reads as something to get on the phone, not a mode;
        // and the name Moonlight lists it under, CabinetOS, the name the
        // start-up script gives Sunshine (MMagTech, 2026-10-07).
        std::string detail = on ? "Moonlight app \xC2\xB7 CabinetOS" : "Moonlight app";
        if (rpSaidAt != 0) value = rpWant ? "Turning on\xE2\x80\xA6" : "Turning off\xE2\x80\xA6";
        else if (!rpFailed.empty()) detail = rpFailed;
        const size_t n = remoteplay::paired().size();
        rpSeen = remoteplay::generation();
        // WHILE A DEVICE STREAMS, THE WHOLE SECTION IS GREYED: its
        // controller drives the console, and one press on Streaming cut
        // the stream it was playing on (MMagTech, from his phone,
        // 2026-10-08); Remove could do the same to the device playing.
        // The console's own controllers are set aside during a stream, so
        // nobody at the TV loses anything.
        const bool streamingNow = players::streaming();
        // "In use": a grey switch still reading On looks like a fault
        // when the reason is on another device (MMagTech, 2026-10-08).
        if (streamingNow) detail = "In use";
        cats.push_back({"Remote Play", {
            {streamingNow ? K::Disabled : K::Toggle, SetRemotePlay, "Streaming", detail, value},
            {on && n > 0 && !streamingNow ? K::Action : K::Disabled, SetPairedDevices,
             "Paired devices", "",
             n == 0 ? "None" : std::to_string(n) + " paired"},
            tailscaleRow(on, streamingNow),
            // FORK (handheld): the walk-through for a friend (friendflow.cpp).
            {on ? K::Action : K::Disabled, SetFriend, "Play with a friend", "", ""},
        }});
    }

    // PICTURE QUALITY IS THE OWNER'S WHEN A PIN IS SET (MMagTech,
    // 2026-10-02, #63): nobody else sees the row at all, and the owner is
    // never asked for the PIN. With no PIN, everyone sees it, as with
    // everything else. It is never urgent, and switching into the owner's
    // account already asks for the PIN.
    const bool showQuality =
        !accounts::pinIsSet() || accounts::activeId() == accounts::ownerId();
    cats.push_back({"Display and Sound", {
        [] {
            Row r{K::Choice, SetInterfaceSounds, "Interface sounds", "", ""};
            for (int i = 0; i < sound::kLevelCount; ++i)
                r.choices.push_back(sound::levelName(static_cast<sound::Level>(i)));
            r.choice = static_cast<int>(sound::level());
            return r;
        }(),
        // THE ACHIEVEMENT CHIME (#74), its own row and not the clicks'
        // (MMagTech, 2026-10-06). Only for somebody signed in to
        // RetroAchievements: nobody else ever hears it.
        [] {
            Row r{K::Choice, SetAchievementSound, "Achievement sound", "", ""};
            for (int i = 0; i < sound::kLevelCount; ++i)
                r.choices.push_back(sound::levelName(static_cast<sound::Level>(i)));
            r.choice = static_cast<int>(sound::chimeLevel());
            return r;
        }(),
        // FORK (handheld): the dim, before the screen goes out.
        [] {
            Row r{K::Choice, SetDimAfter, "Dim screen after", "Then the screen turns off", ""};
            for (int i = 0; i < idle::kDimCount; ++i) r.choices.push_back(idle::kDimChoices[i].name);
            r.choice = idle::dimIndex();
            return r;
        }(),
        // Here, not under System: it is about the screen, and it is where a
        // person looks for it. MMagTech, 2026-09-24.
        [&] {
            // A FACT THE ROW CANNOT OTHERWISE SHOW, not an explanation:
            // the screen dims first (the row above), and nothing else on
            // screen says so.
            Row r{K::Choice, SetScreenOff, "Turn off screen after", "After it has dimmed",
                  ""};
            for (int i = 0; i < kScreenOffCount; ++i) r.choices.push_back(kScreenOff[i].name);
            r.choice = screenOffIndex;
            return r;
        }(),
        // FORK (handheld): sleep after the screen has gone out, on battery only.
        [] {
            Row r{K::Choice, SetSleepAfter, "Sleep when idle on battery",
                  "Never while plugged in", ""};
            for (int i = 0; i < idlesleep::kCount; ++i)
                r.choices.push_back(idlesleep::kChoices[i].name);
            r.choice = idlesleep::index();
            return r;
        }(),
        // FORK (handheld): only where the Steam Deck's charge limit exists
        // (removed below on other machines).
        [] {
            Row r{K::Choice, SetChargeLimit, "Battery charge limit",
                  "Stops charging at this level", ""};
            for (int i = 0; i < chargelimit::kCount; ++i)
                r.choices.push_back(chargelimit::kChoices[i].name);
            r.choice = chargelimit::savedIndex();
            return r;
        }(),
    }});
    {
        // APPEARANCE AND COLOUR, under the picture rows: both are how the
        // menus look. #129 and #75, 2026-09-27. Colour is the signed-in
        // person's; the account chip above already says who that is.
        auto& rows = cats.back().rows;
        if (!chargelimit::available())
            rows.erase(std::remove_if(rows.begin(), rows.end(),
                                      [](const Row& r) { return r.id == SetChargeLimit; }),
                       rows.end());
        if (!ra::signedIn())
            rows.erase(std::remove_if(rows.begin(), rows.end(),
                                      [](const Row& r) { return r.id == SetAchievementSound; }),
                       rows.end());
        if (showQuality) {
            // NOTHING UNDER IT YET. Once the pause menu has its own row,
            // "Change per game in the pause menu", MMagTech's wording: a
            // fact the row cannot otherwise show (2026-10-02).
            // "Picture", as in the pause menu: "Picture quality ...
            // Quality" said it twice (MMagTech, 2026-10-02).
            Row r{K::Choice, SetPictureQuality, "Picture", "", ""};
            for (int i = 0; i < quality::kLevelCount; ++i)
                r.choices.push_back(quality::levelName(static_cast<quality::Level>(i)));
            r.choice = static_cast<int>(quality::console());
            rows.insert(rows.begin(), r);
        }
        auto at = rows.begin() + (showQuality ? 1 : 0);   // after Picture quality
        std::vector<Row> lookRows;
        {
            Row r{K::Choice, SetAppearance, "Appearance", "", ""};
            for (int i = 0; i < look::kAppearanceCount; ++i)
                r.choices.push_back(look::appearanceName(static_cast<look::Appearance>(i)));
            r.choice = static_cast<int>(appearance);
            lookRows.push_back(r);
        }
        // ONE ROW FOR THE HOURS, opening the question panel. Two rows
        // made the list too long (MMagTech, 2026-09-27); the panel is the
        // one Wi-Fi and Add a controller already use.
        if (appearance == look::Appearance::Scheduled)
            lookRows.push_back({K::Action, SetDarkHours, "Dark hours", "",
                                look::hourName(darkFrom) + " to " +
                                    look::hourName(darkUntil)});
        {
            Row r{K::Choice, SetColour, "Color", "", ""};
            for (int i = 0; i < look::kColourCount; ++i)
                r.choices.push_back(look::colourName(static_cast<look::Colour>(i)));
            r.choice = static_cast<int>(look::colour());
            lookRows.push_back(r);
        }
        rows.insert(at, lookRows.begin(), lookRows.end());
    }

    // THE DRIVES, by MMagTech's names: the main drive is "CabinetOS", any
    // other internal disk "Internal", one that can be unplugged
    // "External". With two of one kind, counting drives that cannot be
    // used, the drive's own name tells them apart: its label for one in
    // use, its model for one that is not. THE DRIVE'S OWN ROW IS THE
    // BUTTON: an External drive in use offers Eject (and Format, when
    // nothing of the console's is on it), any other drive Format, and
    // there are no separate action rows, so there is never a
    // question of which drive a row means. MMagTech on the TV, 2026-09-25:
    // a Format row under the drive "can make me feel like I'm formatting
    // something that isn't the unformatted drive".
    std::vector<Row> store;
    const std::vector<std::string> locs = storage::locations();
    const std::vector<drives::Unusable> unusableDrives = drives::unusable();
    const std::vector<drives::Drive> found = drives::found();
    formatable.clear();
    ejectFormats.clear();
    ejectPaths.clear();
    auto formatValue = [] {
        return std::string(drives::formatting() ? "Formatting\xE2\x80\xA6" : "Format");
    };
    int internals = 0, externals = 0;
    for (size_t i = 1; i < locs.size(); ++i)
        (storage::isExternal(locs[i]) ? externals : internals)++;
    for (const drives::Unusable& u : unusableDrives) (u.external ? externals : internals)++;
    const std::map<std::string, std::string> locNames = locationNames(locs, unusableDrives);
    for (size_t i = 0; i < locs.size(); ++i) {
        const std::string name = locNames.at(locs[i]);
        const bool external = i > 0 && storage::isExternal(locs[i]);
        // THE SPACE ON THE SECOND LINE AND THE ACTION AS THE VALUE, so
        // the row says what pressing it does: a chevron alone only says
        // "more". MMagTech on the TV, 2026-09-25.
        const storage::Space sp = storage::spaceOf(locs[i]);
        const std::string space = sp.ok ? gb(sp.freeBytes) + " free of " + gb(sp.totalBytes)
                                        : std::string("Unknown");
        // FORMAT ON A DRIVE IN USE, when nothing of the console's is on
        // it (#236): an External drive's row asks Eject or Format, an
        // Internal one's row is Format.
        const drives::Drive* dr = i > 0 ? drives::driveAt(found, locs[i]) : nullptr;
        const bool canFormat = dr && drives::mayFormat(*dr);
        std::string value = external ? (drives::ejecting() ? "Ejecting\xE2\x80\xA6" : "Eject")
                                     : canFormat ? formatValue() : "";
        int id = 0;
        if (external) {
            id = kSetEject + static_cast<int>(i);
            if (canFormat) {
                ejectFormats[id] = formatable.size();
                formatable.push_back(*dr);
            }
        } else if (canFormat) {
            id = kSetFormat + static_cast<int>(formatable.size());
            formatable.push_back(*dr);
        }
        store.push_back({id ? K::Action : K::Info, id, name, space, value});
        if (external) driveNames[kSetEject + static_cast<int>(i)] = name;
        if (external) ejectPaths[kSetEject + static_cast<int>(i)] = locs[i];
        // STEAM'S SHARE, under the main drive, once it is set up (#223):
        // the main drive's own row leaves it out, and this says where it
        // went. The one place to grow it or remove Steam. MMagTech,
        // 2026-10-03: nothing here for somebody who never set Steam up.
        if (i == 0 && steam::isSetUp())
            store.push_back({K::Action, SetSteam, "Steam", gb(steam::sliceBytes()), ""});
    }
    // DRIVES FOUND AND NOT USABLE, with the reason. A new internal SSD
    // arrives blank; without this row it would be invisible, because
    // internal drives get no notices. Format is offered on any of them
    // whose contents the console can see: blank, or another format.
    // One that would not mount stays greyed: what is on it is unknown.
    for (const drives::Unusable& u : unusableDrives) {
        std::string name = u.external ? "External" : "Internal";
        if ((u.external ? externals : internals) > 1 && !u.model.empty())
            name += " (" + u.model + ")";
        const std::string size = u.sizeBytes ? gb(static_cast<int64_t>(u.sizeBytes)) : "";
        std::string why = u.blank         ? std::string("Blank")
                          : u.wrongFormat ? std::string("Isn't exFAT, NTFS or ext4")
                                          : std::string("Couldn't use this drive");
        if (!size.empty()) why += " \xC2\xB7 " + size;
        const drives::Drive* dr = nullptr;
        for (const drives::Drive& d : found)
            if (d.id == u.id) dr = &d;
        if (dr && drives::mayFormat(*dr)) {
            store.push_back({K::Action, kSetFormat + static_cast<int>(formatable.size()),
                             name, why, formatValue()});
            formatable.push_back(*dr);
        } else {
            store.push_back({K::Disabled, 0, name, why, ""});
        }
    }
    // DOWNLOADS, #68: everyone's downloaded games, behind the PIN. The
    // cache is not shown. The count and size on the second line, like a
    // drive's space; greyed with nothing downloaded. docs/SETTINGS.md,
    // Storage.
    {
        const std::vector<screens::DownloadItem> dl = downloadItems();
        int64_t bytes = 0;
        for (const screens::DownloadItem& d : dl) bytes += d.bytes;
        if (dl.empty())
            store.push_back({K::Disabled, 0, "Downloads", "None", ""});
        else
            store.push_back({K::Action, SetDownloads, "Downloads",
                             screens::countText(static_cast<int>(dl.size()), bytes), ""});
    }
    // FILE ACCESS: ONE ROW. What a computer needs to reach it (address,
    // user name, the password in plain text) is in a panel the row opens,
    // as Wi-Fi's networks are: as rows under Storage they ran off the
    // bottom of the screen. MMagTech on the TV, 2026-09-25. SFTP, port
    // 22, user `cabinet`, the saves and games folders only.
    {
        std::string value = filesState.on ? "On" : "Off";
        std::string detail = "SFTP";
        if (filesSaidAt != 0) value = filesWant ? "Turning on\xE2\x80\xA6" : "Turning off\xE2\x80\xA6";
        else if (filesState.failed) detail = filesState.reason;
        store.push_back({K::Toggle, SetFiles, "File access", detail, value});
    }
    cats.push_back({"Storage", std::move(store)});

    std::vector<Row> sys = {
        updateRow(),
        [&] {
            // Weekly only ever checks, never downloads.
            Row r{K::Choice, SetUpdateCheck, "Check for updates", "", ""};
            r.choices = {"Manual", "Weekly"};
            r.choice = updWeekly ? 1 : 0;
            return r;
        }(),
    };
    // ONLY WHILE STEAM IS HIDDEN (#223, MMagTech 2026-10-03): the one way
    // to bring the tile back. Somebody who never hid it never sees it.
    if (steam::available() && steam::hidden())
        sys.push_back({K::Action, SetSteamShow, "Steam", "", "Hidden"});
    // THE DIAGNOSTIC REPORT (#195): one row, the screen it opens says the
    // rest. No PIN: it changes nothing, and nothing private is in it.
    sys.push_back({K::Action, SetReport, "Diagnostic report", "", ""});
    cats.push_back({"System", std::move(sys)});

    // VERSION: the date version, with Bazzite's under it. A console that
    // does not follow `latest` says which tag it does follow, which is how
    // a test console is told from the rest at a glance; nothing here
    // changes it. MMagTech, 2026-09-25.
    {
        std::string version = update::bootedVersion();
        if (version.empty()) version = "Unknown";
        const std::string ch = update::channel();
        if (ch == "testing") version += " \xC2\xB7 Testing";
        else if (!ch.empty() && ch != "latest") version += " \xC2\xB7 " + ch;
        const std::string base = update::baseVersion();
        std::vector<screens::SettingsRow> about;
        screens::SettingsRow v{K::Info, SetVersion, "Version",
                               base.empty() ? "" : "Bazzite " + base, version};
        v.pressable = true;
        about.push_back(v);
        // WHAT'S NEW (#329): under Version, its name and nothing else; the
        // screen it opens has the code to the What's new page (MMagTech,
        // 2026-10-19).
        about.push_back({K::Action, SetWhatsNew, "What's new", "", ""});
        about.push_back({K::Action, SetHowTo, "How to", "", ""});
        about.push_back({K::Action, SetCredits, "Credits and licenses", "", ""});
        // DEVELOPER ACCESS: one row, as File access; everything a computer
        // needs is in the panel it opens. Port 2222, the full command
        // line. LAST, under Credits (MMagTech, 2026-10-10): About is short
        // enough that it still appears in view on the seventh press of
        // Version, and the everyday rows never move when it shows.
        if (devShown) {
            std::string value = devState == "active" ? "On" : "Off";
            std::string detail = "SSH";
            if (devSaidAt != 0) value = devWant ? "Turning on\xE2\x80\xA6" : "Turning off\xE2\x80\xA6";
            else if (!devFailed.empty()) detail = devFailed;
            about.push_back({K::Toggle, SetDeveloper, "Developer access", detail, value});
        }
        cats.push_back({"About", std::move(about)});
    }


    settingsScreen.setCategories(std::move(cats));
}

void App::rebuildSettingsRows() { buildSettings(); }

void App::darkHoursPanel(int focus) {
    askChoice("Dark hours", "", {"From", "Until"}, focus, [&](int k) {
        std::vector<std::string> names;
        for (int h = 0; h < 24; ++h) names.push_back(look::hourName(h));
        int& which = k == 0 ? darkFrom : darkUntil;
        askChoice(k == 0 ? "Dark from" : "Until", "", names, which,
                  [&, k](int h) {
                      (k == 0 ? darkFrom : darkUntil) = h;
                      prefs::set(k == 0 ? "dark_from" : "dark_until",
                                 std::to_string(h));
                      look::setDark(wantDark());
                      buildSettings();
                      darkHoursPanel(k);
                  });
    });
    choiceScreen.setValues({look::hourName(darkFrom),
                            look::hourName(darkUntil)});
}

void App::readScreenOffAtStart() {
    screenOffIndex = savedScreenOff();
    std::fprintf(stderr, "[idle] screen off after %s\n", kScreenOff[screenOffIndex].name);
    // FORK (handheld): the sleep choice, and the charge limit written again.
    idlesleep::load();
    chargelimit::applySaved();
}

void App::closeDownloadsOffSettings() {
    if (here() != Screen::Settings) downloadsPanel.close();
}

void App::applySetting(const screens::Result& res) {
    if (res.value == SetPads) {
        openPadsPanelAt(0);
        sound::play(sound::Cue::Activate);
    } else if (res.value >= kSetFormat) {
        pressFormat(res);
    } else if (res.value >= kSetEject) {
        pressEject(res);
    } else if (res.value == SetWiiRemotes) {
        sound::play(sound::Cue::Activate);
        openWiiPanel();
    } else if (res.value == SetShortcutButton) {
        // The pad that pressed A, which the row already names.
        const std::vector<players::Pad> pads = players::connected();
        shortcutRefused.clear();
        shortcutListen = pads.empty() ? 0 : pads.front().id;
        for (const players::Pad& p : pads)
            if (p.id == settingsLastPad) shortcutListen = p.id;
        sound::play(sound::Cue::Activate);
        buildSettings();
    } else if (res.value == SetAddAccount) {
        pressAddAccountRow();
    } else if (res.value == SetWifi) {
        pressWifi();
    } else if (res.value == SetDarkHours) {
        // From and Until, then the hours of whichever was chosen,
        // then back to From and Until with the new one showing.
        darkHoursPanel(0);
        sound::play(sound::Cue::Activate);
    } else if (res.value == SetFiles) {
        pressFiles();
    } else if (res.value == SetVersion) {
        pressVersion();
    } else if (res.value == SetDeveloper) {
        pressDeveloper();
    } else if (res.value == SetRemotePlay) {
        pressRemotePlay();
    } else if (res.value == SetTailscale) {
        pressTailscale();
    } else if (res.value == SetPairedDevices) {
        pressPairedDevices();
    } else if (res.value == SetFriend) {
        pressFriend();
    } else if (res.value == SetSteam) {
        pressSteam();
    } else if (res.value == SetSteamShow) {
        pressSteamShow();
    } else if (res.value == SetDownloads) {
        pressDownloads();
    } else if (res.value == SetReport) {
        pressReport();
    } else if (res.value == SetWhatsNew) {
        pressWhatsNew();
    } else if (res.value == SetHowTo) {
        pressHowTo();
    } else if (res.value == SetCredits) {
        pressCredits();
    } else if (res.value == SetUpdate) {
        pressUpdate();
    } else if (res.value == SetServer) {
        pressServer();
    } else if (res.value == SetRemoveAccount) {
        pressRemoveAccount();
    } else if (res.value == SetPinSet) {
        pressPinSet();
    } else if (res.value == SetPinChange) {
        pressPinChange();
    } else if (res.value == SetRetroAchievements) {
        pressRetroAchievements();
    } else if (res.value == SetPinOff) {
        pressPinOff();
    }
}

void App::applySettingChoice(const screens::Result& res) {
    if (res.value == SetUpdateCheck) {
        updWeekly = settingsScreen.choiceOf(SetUpdateCheck) == 1;
        prefs::set("update_check", updWeekly ? "weekly" : "manual");
        sound::play(sound::Cue::Move);
    }
    if (res.value == SetRumble) {
        rumble::setEnabled(settingsScreen.choiceOf(SetRumble) == 1);
        sound::play(sound::Cue::Move);
    }

    if (res.value == SetShortcuts) {
        shortcuts::setEnabled(settingsScreen.choiceOf(SetShortcuts) == 1);
        sound::play(sound::Cue::Move);
        buildSettings();   // the Shortcut button row comes and goes
    }
    if (res.value == SetAppearance) {
        const int i = settingsScreen.choiceOf(SetAppearance);
        if (i >= 0 && i < look::kAppearanceCount) {
            appearance = static_cast<look::Appearance>(i);
            prefs::set("appearance", look::appearanceWord(appearance));
            look::setDark(wantDark());
        }
        sound::play(sound::Cue::Move);
        buildSettings();   // the From and Until rows come and go
    }
    if (res.value == SetColour) {
        const int i = settingsScreen.choiceOf(SetColour);
        if (i >= 0 && i < look::kColourCount) {
            const auto c = static_cast<look::Colour>(i);
            accounts::setColour(accounts::activeId(), look::colourWord(c));
            look::setColour(c);
        }
        sound::play(sound::Cue::Move);
    }
    if (res.value == SetScreenOff) {
        const int i = settingsScreen.choiceOf(SetScreenOff);
        if (i >= 0 && i < kScreenOffCount) {
            screenOffIndex = i;
            prefs::set("screen_off_after", kScreenOff[i].word);
        }
        sound::play(sound::Cue::Move);
    }
    if (res.value == SetDimAfter) {
        const int i = settingsScreen.choiceOf(SetDimAfter);
        if (i >= 0 && i < idle::kDimCount) idle::setDimIndex(i);
        sound::play(sound::Cue::Move);
    }
    if (res.value == SetSleepAfter) {
        const int i = settingsScreen.choiceOf(SetSleepAfter);
        if (i >= 0 && i < idlesleep::kCount) idlesleep::set(i);
        sound::play(sound::Cue::Move);
    }
    if (res.value == SetChargeLimit) {
        const int i = settingsScreen.choiceOf(SetChargeLimit);
        if (i >= 0 && i < chargelimit::kCount) chargelimit::set(i);
        sound::play(sound::Cue::Move);
    }
    if (res.value == SetPictureQuality) {
        // From each game's next start (quality.h); nothing running
        // changes.
        const int i = settingsScreen.choiceOf(SetPictureQuality);
        if (i >= 0 && i < quality::kLevelCount)
            quality::setConsole(static_cast<quality::Level>(i));
        sound::play(sound::Cue::Move);
    }
    if (res.value == SetAchievementSound) {
        const int i = settingsScreen.choiceOf(SetAchievementSound);
        if (i >= 0 && i < sound::kLevelCount) {
            const auto lv = static_cast<sound::Level>(i);
            sound::setChimeLevel(lv);
            prefs::set("achievement_sound", sound::levelWord(lv));
        }
        // The chime itself, at the level just chosen, as the
        // clicks row plays a click.
        sound::playChime();
    }
    if (res.value == SetInterfaceSounds) {
        const int i = settingsScreen.choiceOf(SetInterfaceSounds);
        if (i >= 0 && i < sound::kLevelCount) {
            const auto lv = static_cast<sound::Level>(i);
            sound::setLevel(lv);
            prefs::set("interface_sounds", sound::levelWord(lv));
        }
        // AFTER the change, so each step is heard at the level it
        // just chose, which is how a person picks one. Off is
        // silent, which says it too.
        sound::play(sound::Cue::Move);
    }
}
