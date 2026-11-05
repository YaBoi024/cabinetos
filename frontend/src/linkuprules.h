// LinkUp's rules and words, with no SDL and no network in them, as Seats has
// none (players.h): who goes under which heading, the lines a player reads, and
// the pairing code's format. LinkUp is #654. One string for each line, so a pill
// and a panel that say the same thing cannot drift.

#pragma once

#include <string>
#include <vector>

namespace linkup {

// The list's four headings, in the order they are drawn.
enum class Status { InvitedYou, InGame, Online, Offline };

struct Friend {
    std::string name;      // their RomM username
    // Their server's name or the owner's note, shown beside the name only when
    // two lines would otherwise read the same (nameOnList).
    std::string from;
    Status status = Status::Offline;
    std::string game;      // what they invited you to, under InvitedYou
};

// What the screen and the Settings rows show. Off and with no server, this is
// all empty and LinkUp is not in the top bar.
struct State {
    bool on = false;            // the switch in Settings
    bool paired = false;
    std::string server;         // the address the console keeps for its server
    bool appearOffline = false;
    std::vector<Friend> friends;
    // A line in place of the list, when the list cannot be had: kCantConnect,
    // kConsoleRemoved, kServerNeedsUpdate.
    std::string instead;
    bool picking = false;       // Host: the Library is choosing a game
};

struct Group {
    Status status;
    std::vector<int> friends;   // indices into the list
};

const char* heading(Status s);
// The headings that have anybody under them, in order.
std::vector<Group> groups(const std::vector<Friend>& list);
// "admin", or "admin · Riley's server" when another line would also say admin.
std::string nameOnList(const std::vector<Friend>& list, int index);

inline const std::string kInUse = "In use";
inline const std::string kTooSlow = "The connection is too slow for a clean picture";
inline const std::string kThisNeedsUpdate = "This console needs an update";
inline const std::string kServerNeedsUpdate = "This LinkUp server needs an update";
inline const std::string kCantConnect = "Can't connect to the LinkUp server";
inline const std::string kConsoleRemoved =
    "This server no longer has this console. Ask its owner for a new code.";
inline const std::string kRepositoryUrl = "https://github.com/MMagTech/cabinet-linkup";

std::string invitedYou(const std::string& who, const std::string& game);
std::string declined(const std::string& who);
std::string noAnswer(const std::string& who);
std::string waitingFor(const std::string& who);
std::string joined(const std::string& who);
std::string left(const std::string& who);
std::string isBack(const std::string& who);
std::string sittingOut(const std::string& who);
std::string leaveQuestion(const std::string& who);
std::string rejoinQuestion(const std::string& who);
std::string endedSession(const std::string& who);
std::string needsUpdate(const std::string& who);
// "Direct, 28 ms" or "Relay, 64 ms": both players see it.
std::string connection(bool direct, int ms);

// A pairing code is 12 characters in 3 groups of 4, with no look-alikes. What
// was typed comes back upper case and grouped, anything else dropped.
std::string tidyCode(const std::string& typed);
bool codeComplete(const std::string& tidied);

// --linkup-test: 0 when every rule here holds.
int test();

}  // namespace linkup
