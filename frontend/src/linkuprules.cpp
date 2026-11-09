// LinkUp's rules and words (linkuprules.h).

#include "linkuprules.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace linkup {

const char* heading(Status s) {
    switch (s) {
        case Status::InvitedYou: return "Invited";
        case Status::InGame: return "Linked up";
        case Status::Online: return "Online";
        case Status::Offline: return "Offline";
    }
    return "";
}

std::vector<Group> groups(const std::vector<Friend>& list) {
    std::vector<Group> out;
    auto lower = [](std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    for (Status s : {Status::InvitedYou, Status::Online, Status::InGame, Status::Offline}) {
        Group g{s, {}};
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
            if (list[static_cast<size_t>(i)].status == s) g.friends.push_back(i);
        std::stable_sort(g.friends.begin(), g.friends.end(), [&](int a, int b) {
            return lower(list[static_cast<size_t>(a)].name) < lower(list[static_cast<size_t>(b)].name);
        });
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

int colourOf(const Friend& f, int count) {
    // FNV-1a over the name and where they are from: two people called admin on two
    // servers come out differently as often as anybody else does.
    uint32_t hash = 2166136261u;
    auto add = [&](const std::string& s) {
        for (char ch : s) {
            hash ^= static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(ch)));
            hash *= 16777619u;
        }
    };
    add(f.name);
    add(f.from);
    return count > 0 ? static_cast<int>(hash % static_cast<uint32_t>(count)) : 0;
}

std::vector<int> coloursApart(const std::vector<Friend>& list, int count, int apart) {
    std::vector<int> out(list.size(), 0);
    std::vector<int> order(list.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    auto lower = [](std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const Friend& x = list[static_cast<size_t>(a)];
        const Friend& y = list[static_cast<size_t>(b)];
        return lower(x.name + x.from) < lower(y.name + y.from);
    });
    std::vector<int> taken;
    for (int i : order) {
        int c = colourOf(list[static_cast<size_t>(i)], count);
        for (int tries = 0; tries < count; ++tries) {
            bool clear = true;
            for (int t : taken) {
                const int gap = std::abs(t - c);
                if (std::min(gap, count - gap) < apart) clear = false;
            }
            if (clear) break;
            c = (c + 1) % count;
        }
        taken.push_back(c);
        out[static_cast<size_t>(i)] = c;
    }
    return out;
}

std::string invitedYou(const std::string& who) { return "Invite from " + who; }
std::string declined(const std::string& who) { return who + " declined"; }
std::string noAnswer(const std::string& who) { return "No answer from " + who; }
std::string waitingFor(const std::string& who) { return "Waiting for " + who; }
std::string left(const std::string& who) { return who + " left"; }
std::string isBack(const std::string& who) { return who + " is back"; }
std::string joinQuestion(const std::string& who) { return "Join " + who + "'s game?"; }
std::string pausedBy(const std::string& who) { return who + " paused"; }
std::string reconnectingTo(const std::string& who) { return "Reconnecting to " + who; }
std::string endedSession(const std::string& who) { return who + " ended the session"; }
std::string notAvailable(const std::string& who) { return who + " isn't available"; }
std::string cantConnectTo(const std::string& who) { return "Can't connect to " + who; }
std::string inviteEnded(const std::string& who) { return who + "'s invite ended"; }
std::string playingWith(const std::string& who) { return "Playing with " + who; }
std::string endSessionQuestion(const std::string& who) { return "End the session with " + who + "?"; }
std::string sessionEndedWith(const std::string& who) { return "Session with " + who + " ended"; }
std::string leaveQuestion(const std::string& who) { return "Leave " + who + "'s game?"; }
std::string needsUpdate(const std::string& who) { return who + "'s console needs an update"; }

bool handheldSystem(const std::string& slug) {
    static const char* const kHandhelds[] = {
        "gb", "gbc", "gba", "nds", "nintendo-dsi", "3ds", "n3ds", "new-nintendo-3ds", "psp", "psvita", "gamegear",
        "game-gear", "lynx", "ngp", "neo-geo-pocket", "ngpc", "neo-geo-pocket-color", "wonderswan", "wonderswan-color",
        "virtualboy", "virtual-boy", "pokemon-mini", "supervision", "game-com", "gw", "game-and-watch"};
    for (const char* h : kHandhelds)
        if (slug == h) return true;
    return false;
}

bool systemOnInvite(const SystemSeen& s) {
    if (s.count == 0) return false;
    // Past a first page with none, the rest is read as its grid opens.
    if (s.read) return s.friendCanPlayOne || s.more;
    return true;
}

namespace {
constexpr int kCodeLength = 12;
constexpr int kCodeGroup = 4;
// No 0, O, 1, I or L, which read alike on a television and on a phone. What a reader accepts
// (PROTOCOL.md), which is wider than what a code is drawn from.
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
    check(handheldSystem("gba") && handheldSystem("psp") && !handheldSystem("snes") && !handheldSystem("ps3"),
          "a handheld has no second port, and a console does");
    check(!systemOnInvite({0, false, false, false}), "a system with no games has no tile while inviting");
    check(systemOnInvite({12, false, false, false}), "a system whose games all play as it does keeps its tile");
    check(systemOnInvite({-1, false, false, false}), "a system the server said nothing of keeps its tile");
    check(systemOnInvite({52, true, true, false}), "a system with a game a friend can play keeps its tile");
    check(!systemOnInvite({4, true, false, false}), "a system left with none a friend can play has no tile");
    check(systemOnInvite({900, true, false, true}), "a first page with none keeps the tile while more are to come");
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
    check(g[0].status == Status::InvitedYou && g[0].friends[0] == 1, "Invited comes first");
    check(g[1].status == Status::Online && g[1].friends.size() == 2, "Online comes second");
    check(g[2].status == Status::InGame, "Linked up comes third");
    check(g[3].status == Status::Offline, "Offline comes last");
    list.push_back({"Zoe", "", Status::Offline, ""});
    list.push_back({"Ben", "", Status::Offline, ""});
    {
        const std::vector<Group> sorted = groups(list);
        const std::vector<int>& off = sorted[3].friends;
        check(list[static_cast<size_t>(off[0])].name == "Ben" && list[static_cast<size_t>(off[2])].name == "Zoe",
              "a group is alphabetical");
    }
    list.resize(5);
    check(nameOnList(list, 2) == "admin   \xC2\xB7   Riley's server",
          "two lines that would read the same add their server");
    list.erase(list.begin() + 4);
    check(nameOnList(list, 2) == "admin", "a name nobody else has stands alone");

    {
        const Friend alex{"Alex", "", Status::Online, ""};
        const Friend alexAgain{"ALEX", "", Status::Offline, "Contra III"};
        check(colourOf(alex, 8) == colourOf(alexAgain, 8), "a person keeps their colour, whatever their status");
        check(colourOf(alex, 8) >= 0 && colourOf(alex, 8) < 8, "a colour is one of those on offer");
        check(colourOf(alex, 0) == 0, "no colours on offer is not a crash");
        std::vector<Friend> eight;
        for (const char* name : {"Riley", "Sam", "Alex", "Jordan", "admin", "Maya", "Theo", "Lily"})
            eight.push_back({name, "", Status::Online, ""});
        const std::vector<int> hues = coloursApart(eight, 360, 180 / 8);
        bool spread = true;
        for (size_t a = 0; a < hues.size(); ++a)
            for (size_t b = a + 1; b < hues.size(); ++b) {
                const int gap = std::abs(hues[a] - hues[b]);
                if (std::min(gap, 360 - gap) < 180 / 8) spread = false;
            }
        check(spread, "no two friends on a list are close in colour");
        const std::vector<int> themes = coloursApart(eight, 16, 1);
        bool different = true;
        for (size_t a = 0; a < themes.size(); ++a)
            for (size_t b = a + 1; b < themes.size(); ++b)
                if (themes[a] == themes[b]) different = false;
        check(different, "no two friends share one of a few colours while there are enough");
    }
    std::vector<Friend> nobody;
    check(groups(nobody).empty(), "nobody means no headings");

    std::fprintf(stderr, "[linkup-test] %s\n", failed == 0 ? "ok" : "FAILED");
    return failed == 0 ? 0 : 1;
}

}  // namespace linkup
