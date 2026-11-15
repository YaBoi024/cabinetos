// Game events (#716): the console tells one app in the owner's home what is
// happening in a game, and that app (n8n, Home Assistant, Node-RED) does the
// rest: dims the lights, posts to Discord, pings a phone.
// docs/decisions/2026-11-15-game-events.md says why it is this shape.
//
// ONE ADDRESS, CONSOLE-WIDE, AND IT IS THE SWITCH: `game_events_address` in
// config/settings.json (prefs.h), typed in Settings > System > Game events
// (gameeventsflow.cpp). Empty is off, and OFF IS OFF: the sender thread is
// never started, every call below returns at once and nothing is sent. It
// never listens, even when on.
//
// THE MESSAGES are JSON, POSTed one at a time, in order, by one thread of
// gameevents.cpp's own (job.h is one worker for one result, and ra_net.cpp's
// ordered worker is RetroAchievements'). Every game message carries
// `format` 1, `time` (local, with its offset), `person`, `game`, `system`,
// `rom_id`, `linkup` and `linkup_game` (`none`, or `hosting` or `joined` for
// a LinkUp game, never who with). Fields can be added, never renamed or
// removed; a break means `format` 2.
//
// HOME ADDRESSES ONLY, CHECKED EVERY TIME IT SENDS (gameevents_wire.cpp). The
// name is looked up and every address it gives must be private, local or
// Tailscale; the connection goes to the addresses that were checked, so the
// name cannot point elsewhere in between. Redirects are not followed, no
// proxy is used.
//
// A KNOCK FIRST ON EVERY NETWORK. A private address exists on any network, a
// friend's included. Each time the console joins one, the first thing sent is
// the `test` message, which names nobody; game messages go only while it was
// answered with a 2xx, and until the network changes nothing else is sent.
// No network, nothing sent: no queue on disk, since a "game started" an hour
// late is wrong. Connect 2 s, whole message 5 s, then it is dropped and
// logged once. Nothing here ever holds up the frame thread.
//
// EACH CONSOLE FOLLOWS ONLY ITS OWN SETUP. A LinkUp friend's console sends
// its own `game_started` and `game_ended` to its own address under its own
// name, without `rom_id`, `system` and `cover` (linkupsession.cpp). Each
// says it is a LinkUp game and which side it is on, and neither names the
// other person: who a friend is stays theirs. Nothing here ever crosses
// LinkUp.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gameevents {

// --- The address --------------------------------------------------------------

// The saved address, or empty when off.
std::string address();
// What the Settings row shows for it: its host, "n8n.local".
std::string hostOf(const std::string& url);

// Start-up (App::startGameEventsAtBoot): the sender starts when an address is
// saved; with none, nothing runs.
void start();
// Exit: what is still waiting gets one message's time to go, then the sender
// is joined.
void shutdown();
// Messages waiting or on the wire. Sleep waits for them as for the uploads
// (power_menu.cpp), so a game left by Sleep says so before the console sleeps.
int pending();

// THE KEYBOARD'S DONE: the `test` message to `url`, with every rule above.
// It blocks for up to one message's time, so it runs on a worker.
enum class Answer { Ok, NotHome, CantConnect, Error, Certificate };
struct Tried {
    Answer answer = Answer::CantConnect;
    long status = 0;   // the HTTP status, for Error
};
Tried tryAddress(const std::string& url);
// The words for the keyboard's field, empty for Ok.
std::string why(const Tried& t);
// Saved, and sent to from now. The test that just answered is this network's
// knock.
void setAddress(const std::string& url);
// Stop sending: the address wiped and the sender told to stop. It is not
// waited for; the next start or the exit joins it.
void stopSending();

// --- The messages -------------------------------------------------------------
//
// Frame thread only. Each one only puts a message in the line.

// Who the messages are about, as things stand when one is made.
struct Who {
    std::string person;   // the console account
    std::string linkup;   // "online", "offline" or "unused"
};

struct Game {
    std::string title;
    std::string system;    // the Library's name for it; empty on a LinkUp friend's
    int romId = 0;         // RomM's number; 0 on a LinkUp friend's
    // The cover cache's keys (covercache.h), large first: the first one the
    // console keeps is sent. Read on the sender's thread.
    std::vector<std::string> covers;
    bool friendSide = false;   // this console joined a LinkUp friend's game
    bool linkupHost = false;   // this console hosts a LinkUp game
};

// An address is saved, so the sender runs. Every call below does nothing
// while this is false.
bool on();
// A game is between its started and its ended: this console's own, or a
// LinkUp friend's.
bool inGame(bool friendSide);
bool pausedNow();

void started(const Game& g, const Who& w);
// On a change only, and never for a LinkUp friend's game.
void paused(bool isPaused, const Who& w);
void ended(int64_t playedMs, const Who& w);
void unlocked(const std::string& achievement, uint32_t points, const Who& w);
void completed(const Who& w);

// `--game-events-test` (gameevents_test.cpp): the messages, the home-address
// rule, and a receiver on loopback inside the test (the knock, the order, a
// redirect, a slow receiver, off). Prints each check; returns the number that
// failed.
int test();

}  // namespace gameevents
