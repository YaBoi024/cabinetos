// FORK (handheld): Play together, the screens (NETPLAY-PLAN.md, section 11).
//
// A game's page has a "Play together" row for the systems that can. It asks
// "Host a game" or "Join a game", in the question panel every other choice
// uses:
//
//   HOST: the console picks four digits, says them with its Tailscale name,
//   and starts the game. The game waits for a friend (netplaylive.h), with the
//   code in the notice at the foot of the screen.
//
//   JOIN: a list of the player's own Tailscale devices, then the same pad
//   Remote Play pairing uses to type the host's four digits, then the game.
//   Both players open the same game on their own console; the handshake
//   refuses a different game or core with a plain reason.
//
// Nothing is ever typed as an address and nothing outside the player's own
// Tailscale is listed, which keeps to the fixed-places rule (CONTRIBUTING.md,
// "The rules"). MMagTech has not agreed to this yet; it is the fork's.

#include "app.h"

#include <cstdio>
#include <cstdlib>

#include "job.h"
#include "netplaylive.h"
#include "tailscale.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {

constexpr const char* kJoinTitle = "Join a game";

cab::Job<std::vector<tailscale::Peer>> gPeers;
std::vector<tailscale::Peer> gShown;   // the rows of the Join list, in order
int gRom = 0;                          // the game whose page asked

}  // namespace

void App::pressPlayTogether(int romId) {
    gRom = romId;
    sound::play(sound::Cue::Activate);
    askChoice("Play together", "Two players, each on their own console.\nSaves made together are kept apart\nfrom your own; the host's copy is used.",
              {"Host a game", "Join a game", "Cancel"}, 0, [&](int i) {
        if (i == 0) {
            // ---- HOST ----------------------------------------------------
            const int code = netplaylive::newCode();
            const tailscale::Status ts = tailscale::last();
            char line[96];
            std::snprintf(line, sizeof line, "Your code: %04d", code);
            std::string detail = line;
            detail += ts.state == "Running" && !ts.name.empty()
                          ? "\nOn Tailscale as " + ts.name
                          : "\nTailscale is off: turn on Remote Play in Settings";
            detail += "\nYour friend picks this game, then Join.";
            askChoice("Host a game", detail, {"Start", "Back"}, 0, [&, code](int k) {
                if (k != 0) { pressPlayTogether(gRom); return; }
                netplaylive::Request& r = netplaylive::pending();
                r = {};
                r.wanted = true;
                r.host = true;
                r.code = code;
                r.romId = gRom;
                std::fprintf(stderr, "[netplay] hosting game %d from its page\n", gRom);
                if (!launchById(gRom)) netplaylive::pending() = {};
            });
            choiceBack = [&]() { pressPlayTogether(gRom); };
        } else if (i == 1) {
            // ---- JOIN: the list first ------------------------------------
            gShown.clear();
            gPeers.start([](std::vector<tailscale::Peer>& out, std::string& why) {
                return tailscale::peers(&out, &why);
            });
            askChoice(kJoinTitle, "Looking for your devices\xE2\x80\xA6", {"Back"}, 0,
                      [&](int k) {
                if (k < 0 || k >= static_cast<int>(gShown.size())) {
                    pressPlayTogether(gRom);
                    return;
                }
                const tailscale::Peer peer = gShown[static_cast<size_t>(k)];
                if (!peer.online) {
                    menuNotice.say(peer.name + " is offline", Tone::Info);
                    sound::play(sound::Cue::Edge);
                    return;
                }
                // The host's four digits, on the pad Remote Play pairing uses.
                choiceScreen.close();
                pinCode = [&, peer](const std::string& digits) {
                    netplaylive::Request& r = netplaylive::pending();
                    r = {};
                    r.wanted = true;
                    r.host = false;
                    r.address = peer.address;
                    r.code = std::atoi(digits.c_str());
                    r.romId = gRom;
                    std::fprintf(stderr, "[netplay] joining %s with game %d\n",
                                 peer.name.c_str(), gRom);
                    if (!launchById(gRom)) netplaylive::pending() = {};
                };
                pinCodeCancel = [&]() { pressPlayTogether(gRom); };
                pinScreen.open(screens::PinScreen::Mode::Check, kJoinTitle,
                               "Enter the code on " + peer.name);
            });
            choiceScreen.setStaysOpen(true);
            choiceBack = [&]() { pressPlayTogether(gRom); };
        }
    });
}

// Once a frame (half a second): the list of devices, when the lookup is done.
void App::pollPlayTogether() {
    std::vector<tailscale::Peer> found;
    std::string why;
    bool ok = false;
    if (!gPeers.take(&found, &why, &ok)) return;
    // The player moved on while Tailscale answered.
    if (!choiceScreen.isOpen() || choiceScreen.title() != kJoinTitle) return;
    gShown.clear();
    std::vector<std::string> names, values;
    if (ok) {
        for (const tailscale::Peer& p : found) {
            gShown.push_back(p);
            names.push_back(p.name);
            values.push_back(p.online ? "Online" : "Offline");
        }
    }
    names.push_back("Back");
    values.push_back("");
    std::string detail;
    if (!ok)
        detail = why + "\nTurn on Remote Play in Settings.";
    else if (found.empty())
        detail = "No other devices on your Tailscale";
    else
        detail = "Pick your friend's console";
    choiceScreen.replace(names, values, detail);
}
