// LinkUp's rules and words (linkuprules.h).

#include "linkuprules.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace linkup {

const char* heading(Status s) {
    switch (s) {
        case Status::InvitedYou: return "Invited you";
        case Status::InGame: return "Linked up";
        case Status::Online: return "Online";
        case Status::Offline: return "Offline";
    }
    return "";
}

std::vector<Group> groups(const std::vector<Friend>& list) {
    std::vector<Group> out;
    for (Status s : {Status::InvitedYou, Status::InGame, Status::Online, Status::Offline}) {
        Group g{s, {}};
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
            if (list[static_cast<size_t>(i)].status == s) g.friends.push_back(i);
        if (!g.friends.empty()) out.push_back(std::move(g));
    }
    return out;
}

std::string nameOnList(const std::vector<Friend>& list, int index) {
    const Friend& f = list[static_cast<size_t>(index)];
    if (f.from.empty()) return f.name;
    for (int i = 0; i < static_cast<int>(list.size()); ++i)
        if (i != index && list[static_cast<size_t>(i)].name == f.name)
            return f.name + "   \xC2\xB7   " + f.from;
    return f.name;
}

std::string invitedYou(const std::string& who, const std::string& game) {
    return who + " invited you to " + game;
}
std::string declined(const std::string& who) { return who + " declined"; }
std::string noAnswer(const std::string& who) { return "No answer from " + who; }
std::string waitingFor(const std::string& who) { return "Waiting for " + who; }
std::string joined(const std::string& who) { return who + " joined"; }
std::string left(const std::string& who) { return who + " left"; }
std::string isBack(const std::string& who) { return who + " is back"; }
std::string sittingOut(const std::string& who) {
    return who + " is player two. This controller is sitting out.";
}
std::string joinQuestion(const std::string& who) { return "Join " + who + "'s game?"; }
std::string leaveQuestion(const std::string& who) { return "Leave " + who + "'s game?"; }
std::string rejoinQuestion(const std::string& who) { return "Rejoin " + who + "'s game?"; }
std::string endedSession(const std::string& who) { return who + " ended the session"; }
std::string needsUpdate(const std::string& who) { return who + "'s console needs an update"; }

std::string connection(bool direct, int ms) {
    return std::string(direct ? "Direct" : "Relay") + ", " + std::to_string(ms) + " ms";
}

namespace {
constexpr int kCodeLength = 12;
constexpr int kCodeGroup = 4;
// No 0, O, 1, I or L, which read alike on a television and on a phone.
constexpr const char* kCodeLetters = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
}  // namespace

std::string tidyCode(const std::string& typed) {
    std::string kept;
    for (char ch : typed) {
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (up != '\0' && std::strchr(kCodeLetters, up) && kept.size() < kCodeLength)
            kept.push_back(up);
    }
    std::string out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i > 0 && i % kCodeGroup == 0) out.push_back('-');
        out.push_back(kept[i]);
    }
    return out;
}

bool codeComplete(const std::string& tidied) {
    return tidied.size() == kCodeLength + (kCodeLength / kCodeGroup - 1);
}

int test() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "[linkup-test] FAILED: %s\n", what);
            ++failed;
        }
    };

    check(tidyCode("k7mr 4qxt-9hpv") == "K7MR-4QXT-9HPV", "a code comes back upper case and grouped");
    check(tidyCode("K7MR") == "K7MR", "a part of a code is not grouped past what there is");
    check(tidyCode("0O1IL") == "", "the look-alikes are dropped");
    check(tidyCode("K7MR4QXT9HPVABCD") == "K7MR-4QXT-9HPV", "a code stops at 12 characters");
    check(codeComplete("K7MR-4QXT-9HPV"), "12 characters in 3 groups is complete");
    check(!codeComplete("K7MR-4QXT"), "8 characters is not");

    std::vector<Friend> list;
    list.push_back({"Maya", "", Status::Offline, ""});
    list.push_back({"Alex", "", Status::InvitedYou, "Contra III"});
    list.push_back({"admin", "Riley's server", Status::Online, ""});
    list.push_back({"Sam", "", Status::InGame, ""});
    list.push_back({"admin", "Morgan's server", Status::Online, ""});
    const std::vector<Group> g = groups(list);
    check(g.size() == 4, "every heading with somebody under it is there");
    check(g[0].status == Status::InvitedYou && g[0].friends[0] == 1, "Invited you comes first");
    check(g[1].status == Status::InGame, "Linked up comes second");
    check(g[2].status == Status::Online && g[2].friends.size() == 2, "Online comes third");
    check(g[3].status == Status::Offline, "Offline comes last");
    check(nameOnList(list, 2) == "admin   \xC2\xB7   Riley's server",
          "two lines that would read the same add their server");
    list.erase(list.begin() + 4);
    check(nameOnList(list, 2) == "admin", "a name nobody else has stands alone");

    std::vector<Friend> nobody;
    check(groups(nobody).empty(), "nobody means no headings");

    std::fprintf(stderr, "[linkup-test] %s\n", failed == 0 ? "ok" : "FAILED");
    return failed == 0 ? 0 : 1;
}

}  // namespace linkup
