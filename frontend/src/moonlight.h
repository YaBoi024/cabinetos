// FORK (handheld): MOONLIGHT ON THIS CONSOLE, so it can be the guest in
// another console's stream (Settings, Remote Play, Join a friend;
// joinflow.cpp). This file is the program and what is kept about it: whether
// it is installed, the command lines that pair with a host and stream from
// it, and the hosts this console has paired with. Nothing here draws or
// waits; joinflow.cpp runs the commands.
//
// Moonlight is the Flathub app com.moonlight_stream.Moonlight, installed on
// first boot when flatpaks.list pins it (tools/moonlight-pin.sh prints the
// lines). Its own command line does the work: `pair <host> --pin <digits>`
// and `stream <host> <app>`. The host's app is "CabinetOS", the name
// cabinetos-remoteplay gives the console's only Sunshine app.
//
// UNTESTED ON A DECK as written (2026-10-29): the pair and stream commands
// are Moonlight's documented command line, not something run here.

#pragma once

#include <string>
#include <vector>

namespace moonlight {

constexpr const char* kApp = "com.moonlight_stream.Moonlight";

// The Flatpak is in the system installation, where cabinetos-flatpak-setup
// puts it.
bool installed();

// Four digits for Moonlight to pair with, which the host's PIN pad asks for.
std::string newPin();

// `flatpak run ... pair <host> --pin <pin>`. Moonlight's window is not
// wanted for this, so it runs with no screen of its own.
std::vector<std::string> pairArgs(const std::string& host, const std::string& pin);

// `flatpak run ... stream <host> CabinetOS`, full screen, seeing only this
// console's virtual controllers (vpad.h), one per player here.
std::vector<std::string> streamArgs(const std::string& host, int players);

// The hosts paired with, newest first, kept in the console's config folder.
// Moonlight keeps its own pairing; this list only says whether to pair first
// and what to offer in the address field.
std::vector<std::string> pairedHosts();
bool isPaired(const std::string& host);
void rememberPaired(const std::string& host);
void forget(const std::string& host);

// What was last typed, paired or not, for the address field.
std::string lastHost();
void setLastHost(const std::string& host);

// An address as typed: spaces off, a scheme off. Empty when nothing is left
// or it has a character no address has.
std::string cleanHost(const std::string& typed);

}  // namespace moonlight
