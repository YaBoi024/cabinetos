// FORK (handheld): THE FRIENDS A PLAYER JOINS. A name and an address for each
// console this one streams from (Settings, Remote Play, Join a friend), kept
// in the console's config folder and nowhere else: no server holds the list,
// nobody is told they were added, and the friend's console decides who may
// stream from it, as it always did (the four digits typed on its PIN pad).
//
// WHAT AN ENTRY IS. A name the player chose, and the address typed for Join a
// friend: a Tailscale address or name for a friend away from home, a Wi-Fi
// address for one in the house. Neither is a secret and nothing here is a
// credential, so a leaked list opens nothing; it does say who the player plays
// with and where they are, so it is a file only the player can read (0600), at
// most kMax entries, and the console never sends it anywhere.
//
// "READY" IS ONE QUESTION: does anything answer on the friend's streaming
// port? It says the console is on, reachable (Tailscale up, or the same
// network) and has Streaming on. It does not say the friend is at it. The
// probe is a plain connection that sends nothing; the friend's Sunshine sees
// one connection from this address, as it does from Moonlight.
//
// This file keeps the list and asks that question. It draws nothing and
// starts no thread; friendspanel.cpp runs the probe on a worker.

#pragma once

#include <string>
#include <vector>

namespace friends {

struct Friend {
    std::string name;
    std::string host;   // as moonlight::cleanHost leaves it
};

constexpr size_t kMax = 16;
// The port Moonlight is given when none is typed: Sunshine's HTTP port.
constexpr int kProbePort = 47989;
// Longest name, in bytes, so a row stays one line on the Deck's screen.
constexpr size_t kNameMax = 24;

// The list, newest first. Empty when there is none or it cannot be read.
std::vector<Friend> list();
bool has(const std::string& host);

// A name as the player may keep it: no control characters, spaces trimmed,
// cut to kNameMax bytes at a character boundary. Empty when nothing is left.
std::string cleanName(const std::string& typed);

// Saves `f` as the newest entry, replacing one with the same address. False
// when the address or name is empty or the file cannot be written. The oldest
// entry goes when the list is full.
bool add(const Friend& f);
bool rename(const std::string& host, const std::string& name);
bool remove(const std::string& host);

// The list as the file holds it, one "address<TAB>name" line each, and back.
// Exposed so the format can be tested without a file.
std::string serialize(const std::vector<Friend>& v);
std::vector<Friend> parse(const std::string& text);

// "host", "host:port", "[v6]" or "[v6]:port" or a bare IPv6 literal, split.
// `port` is `fallback` when none is given. False for an empty host.
bool splitHostPort(const std::string& typed, int fallback, std::string* host, int* port);

// Whether `typed` (an address as kept above) answers a connection on its
// streaming port within `timeoutMs`. BLOCKS, name lookup included: a worker
// only. Connects to the address the player typed, like Join a friend does.
bool reachable(const std::string& typed, int timeoutMs);

}  // namespace friends
