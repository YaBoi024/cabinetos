// FORK (handheld): PLAY WITH A FRIEND, the walk-through. One screen under
// Remote Play that says what a friend does, in order, with the Moonlight
// download as a QR code and this console's addresses written out: the
// Wi-Fi one and, when Tailscale is running, the one for a friend away from
// home. Pairing itself is the PIN pad that already comes up when Moonlight
// asks (remoteplayflow.cpp), so this screen only has to be right about the
// steps and the addresses, and say when a friend is in.

#include "app.h"

#include <mutex>

#include "remoteplay.h"
#include "tailscale.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {
// Where a friend gets Moonlight: the project's own page lists every store.
constexpr const char* kMoonlightUrl = "https://moonlight-stream.org";
}  // namespace

// The lines are short on purpose: the left column is 760 points and the
// screen does not wrap (qrlink.cpp).
void App::refreshFriend() {
    std::string lan;
    {
        std::lock_guard<std::mutex> lk(settingsNet.m);
        if (settingsNet.have && !settingsNet.st.ipv4.empty())
            lan = settingsNet.st.ipv4.substr(0, settingsNet.st.ipv4.find('/'));
    }
    const tailscale::Status ts = tailscale::last();
    std::vector<std::string> lines;
    lines.push_back("1  Your friend installs Moonlight.");
    lines.push_back("    Scan the code to find it.");
    lines.push_back("2  In Moonlight, add this console:");
    lines.push_back("    On your Wi-Fi: " + (lan.empty() ? std::string("connect to Wi-Fi") : lan));
    lines.push_back(ts.state == "Running" && !ts.address.empty()
                        ? "    Away from home: " + ts.address
                        : std::string("    Away from home: set up Tailscale"));
    lines.push_back("3  Tap CabinetOS in Moonlight.");
    lines.push_back("    Type its four digits here.");
    lines.push_back("4  Then start a game.");
    if (remoteplay::streaming()) {
        lines.push_back("A friend is connected.");
        // FORK (handheld): WHETHER THE FRIEND REACHES THIS CONSOLE DIRECTLY OR
        // THROUGH TAILSCALE'S RELAY SERVERS. A relayed stream lags whatever
        // the console does, and now the person can see why. Nothing is said
        // for a friend on the same Wi-Fi, which never touches Tailscale.
        if (ts.path == "direct") lines.push_back("Connection: direct");
        else if (ts.path == "relayed") {
            lines.push_back("Connection: relayed (" + ts.relay + ")");
            lines.push_back("A relay adds lag.");
        }
    }
    friendScreen.setText("Play with a friend", std::move(lines), "");
}

void App::pressFriend() {
    if (rpState != "active") {
        menuNotice.say("Turn on Streaming first", Tone::Info);
        sound::play(sound::Cue::Edge);
        return;
    }
    friendScreen.open();
    friendScreen.setPairing(kMoonlightUrl, kMoonlightUrl);
    refreshFriend();
    stack.push_back(Screen::Friend);
    sound::play(sound::Cue::Activate);
}
