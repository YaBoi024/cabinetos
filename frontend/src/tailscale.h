// Tailscale, for Remote Play away from home. docs/SETTINGS.md, Remote Play;
// issue #286.
//
// WHY TAILSCALE, AND ONLY TAILSCALE (MMagTech, 2026-10-08; PROJECT.md
// question 39): it works behind any router with nothing forwarded, which is
// how PlayStation Remote Play feels, and it is already in the image (from
// Bazzite). Somebody with their own way home (WireGuard, a Tailscale subnet
// router of their own) leaves it off and loses nothing.
//
// IT LIVES UNDER STREAMING: tailscaled.service runs only while Streaming is on
// AND Tailscale is on, so it can never be on by itself, and off means it is not
// running at all. Its settings are the image's, set by the unit every time it
// starts (tailscaled.service.d/50-cabinetos.conf): the console's own user may
// drive it (operator), it takes no DNS and no routes from the network, and it
// sends Tailscale no logs. The firewall lets Tailscale reach Sunshine's ports
// and nothing else (cabinetos-tailscale zone).
//
// THIS DRIVES THE `tailscale` COMMAND, the part Tailscale documents and keeps
// stable; the local API under it can change without notice. Every call is a
// short process, so the status is asked on a thread of its own every two
// seconds, and only while somebody is looking at it.

#pragma once

#include <cstdint>
#include <string>

namespace tailscale {

struct Status {
    bool answered = false;   // tailscaled answered at all
    // Tailscale's own word for where it is: "NeedsLogin", "Starting",
    // "Running", "Stopped", "NeedsMachineAuth", "NoState".
    std::string state;
    std::string authUrl;     // the sign-in link, while one is waiting
    std::string name;        // this console's name on the user's Tailscale
    std::string address;     // its Tailscale IPv4 address, what Moonlight is given
    // When its sign-in ends (seconds since 1970), or 0 when it never does:
    // Tailscale's default is 180 days, and an owner can turn it off.
    int64_t expires = 0;
};

// Ask every two seconds while on. The app turns it on while Settings or the
// sign-in screen is showing, and off otherwise.
//
// OFF, AND CANCELLING THE SIGN-IN, DO NOT WAIT FOR THEIR THREADS (#481,
// SETTINGS-15): each may be inside `tailscale status`, up to five seconds
// and two more to stop it, and leaving Settings held its picture that long
// with tailscaled stuck. The next watch(true) or startLogin waits for the
// last one, and join() at exit for both.
void watch(bool on);
void join();
Status last();
// Changes whenever the status does, so the app rebuilds its rows only then.
int generation();

// SIGNING IN: `tailscale up`, which waits until the person approves it in a
// browser. It runs on its own thread; the link appears in last().authUrl, and
// last().state becomes "Running" when they have signed in. `again` signs a
// console that is still signed in in afresh (--force-reauth), which starts
// its 180 days again and keeps its name and address.
//
// `up`, NOT `login`: `login` on a console that is signed in adds a second
// account beside the first (Tailscale's account switching); `up` signs in the
// one it has. And `up` refuses to run unless every setting that is not
// Tailscale's default is named, so it names the image's two (tested on the
// A9, 2026-10-08; the operator is not one it asks for).
void startLogin(bool again);
void cancelLogin();
// Whether the sign-in command is still running. It ends at once, with no
// link, when the console was signed in already.
bool loginRunning();
// The login command's result: true while it runs or once it succeeded; false
// with the reason when it failed.
bool loginFailed(std::string* why);

// Whether tailscaled answers now, asked directly rather than from last():
// for a worker that has just started it. Blocks for a moment.
bool answers();

// Off Tailscale: the console is removed from the user's Tailscale and has to
// sign in again. Blocks for a moment; run on a worker.
bool logout(std::string* why);

}  // namespace tailscale
