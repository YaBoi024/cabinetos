// LinkUp's rules and words, with no SDL and no network in them, as Seats has
// none (players.h): who goes under which heading, the lines a player reads, and
// the pairing code's format. LinkUp is #654. One string for each line, so a pill
// and a panel that say the same thing cannot drift.

#pragma once

#include <string>
#include <vector>

namespace linkup {

// The list's four kinds of friend. They are drawn by what you can do: an invite,
// then who can be invited, then who is busy, then who is away (groups).
enum class Status { InvitedYou, InGame, Online, Offline };

struct Friend {
    Friend() = default;
    Friend(std::string who, std::string where, Status s, std::string what)
        : name(std::move(who)), from(std::move(where)), status(s), game(std::move(what)) {}
    std::string name;      // their RomM username
    // Their server's name or the owner's note, shown beside the name only when
    // two lines would otherwise read the same (nameOnList).
    std::string from;
    Status status = Status::Offline;
    std::string game;      // what they invited you to, under InvitedYou
    std::string id;        // who they are to the server
    std::string invite;    // the invite waiting from them, under InvitedYou
    int stream = 0;        // the stream version their console last stated
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
    // kConsoleRemoved, kServerNeedsUpdate, kThisNeedsUpdate.
    std::string instead;
    bool picking = false;       // the Library is choosing a game to invite with
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
// Which of `count` colours is theirs, worked out from who they are, so a person is
// the same colour every time on every console and nothing has to be stored or sent.
int colourOf(const Friend& f, int count);
// Which of `count` colours each friend on a list gets: colourOf, moved along until it
// is at least `apart` from everybody already placed, in name order, so that adding a
// friend leaves nearly everyone where they were. It always finds room while `apart`
// is at most count / (2 * friends), which is what a caller should ask for.
std::vector<int> coloursApart(const std::vector<Friend>& list, int count, int apart);

// The switch: what friends see you as. It is Appear Offline by another name.
inline std::string statusLine(bool appearOffline) {
    return std::string("Status: ") + (appearOffline ? "Offline" : "Online");
}
inline const std::string kTooSlow = "Slow connection";
inline const std::string kThisNeedsUpdate = "Console needs an update";
inline const std::string kServerNeedsUpdate = "LinkUp server needs an update";
inline const std::string kCantConnect = "Can't connect to the LinkUp server";
inline const std::string kConsoleRemoved = "Removed from the LinkUp server";
inline const std::string kStillPlaying = "Still playing?";
inline const std::string kRepositoryUrl = "https://github.com/MMagTech/cabinet-linkup";
// How long the person hosting has to pick the next game before the session ends.
constexpr float kNextGameSeconds = 5.0f * 60.0f;

std::string invitedYou(const std::string& who);
std::string declined(const std::string& who);
std::string noAnswer(const std::string& who);
std::string waitingFor(const std::string& who);
std::string left(const std::string& who);
std::string isBack(const std::string& who);
std::string joinQuestion(const std::string& who);
std::string pausedBy(const std::string& who);
std::string reconnectingTo(const std::string& who);
std::string endedSession(const std::string& who);
std::string notAvailable(const std::string& who);
std::string cantConnectTo(const std::string& who);
std::string inviteEnded(const std::string& who);
std::string playingWith(const std::string& who);
std::string endSessionQuestion(const std::string& who);
std::string sessionEndedWith(const std::string& who);
std::string leaveQuestion(const std::string& who);
std::string needsUpdate(const std::string& who);

// A system with no second controller port, whose games a friend cannot join.
bool handheldSystem(const std::string& slug);

// A pairing code is 12 characters in 3 groups of 4, with no look-alikes. What
// was typed comes back upper case and grouped, anything else dropped.
std::string tidyCode(const std::string& typed);
bool codeComplete(const std::string& tidied);

// --linkup-test: 0 when every rule here holds.
int test();

}  // namespace linkup
