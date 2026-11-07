// LinkUp's live side (#654): talking to the LinkUp program, whose answers fill what linkup.cpp's
// screens show, and a session with a friend from the first knock to the last word. It holds no
// internet code: everything on the network is the program's (linkup/), and here it is a local socket.
//
// The console owns the TV and the game: every screen and every word. The two consoles' main programs
// also say a few things to each other (a pause, a leave), which the program only carries.
//
// THE HOST'S PICTURE IS OFF unless the game alone is on the screen. It is turned off before anything
// else is drawn (the call in tickLinkUp comes before the frame's drawing), because the capture takes
// the whole screen: a menu's first frame, or a PIN being typed, must never reach the friend.

#include "app.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <json-c/json.h>

#include "accounts.h"
#include "fileio.h"
#include "linkup_wire.h"
#include "overlaywin.h"
#include "players.h"
#include "remoteplay.h"
#include "rumble.h"
#include "shortcuts.h"
#include "storage.h"
#include "unit.h"
#include "vpad.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {

constexpr const char* kUnit = "cabinetos-linkup.service";
constexpr const char* kRemotePlayUnit = "cabinetos-remoteplay.service";

// Where the LinkUp program keeps what it was paired with, and the switch its unit tests before it
// opens the firewall lock.
const char* folder() {
    const char* f = std::getenv("CABINETOS_LINKUP_DIR");
    return f && *f ? f : "/var/lib/cabinetos/linkup";
}
std::string switchPath() { return std::string(folder()) + "/on"; }

// Offline after this long with no press (#654, "Status belongs to each account"). The figures of the
// session's own idle rule are beside it.
constexpr double kAwaySeconds = 15 * 60.0;
constexpr float kIdlePauseSeconds = 10 * 60.0f;
constexpr float kIdleEndSeconds = 15 * 60.0f;
constexpr float kDropEndsSeconds = 60.0f;
constexpr float kPressEverySeconds = 2.0f;
constexpr float kPadKeepAliveSeconds = 0.1f;

json_object* command(const char* name) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "cmd", json_object_new_string(name));
    return o;
}
void setText(json_object* o, const char* key, const std::string& value) {
    json_object_object_add(o, key, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
}
std::string textOf(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_string)) return {};
    return json_object_get_string(v);
}
bool flagOf(json_object* o, const char* key) {
    json_object* v = nullptr;
    return o && json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_boolean) && json_object_get_boolean(v);
}

// The session's own gamescope, for the capture to find its node: the parent this program runs under.
int gamescopePid() {
    int pid = getpid();
    for (int hops = 0; hops < 8 && pid > 1; ++hops) {
        std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
        std::string line;
        std::getline(stat, line);
        const size_t open = line.find('(');
        const size_t close = line.rfind(')');
        if (open == std::string::npos || close == std::string::npos) return 0;
        const std::string comm = line.substr(open + 1, close - open - 1);
        if (comm.rfind("gamescope", 0) == 0 && comm != "gamescopereaper") return pid;
        std::istringstream rest(line.substr(close + 2));
        std::string state;
        int parent = 0;
        rest >> state >> parent;
        pid = parent;
    }
    return 0;
}

}  // namespace

// ---- The program ------------------------------------------------------------

void App::linkupStart() {
    if (linkupGallery) return;
    linkUp.on = !cab::readBytes(switchPath()).empty();
    const std::vector<uint8_t> saved = cab::readBytes(std::string(folder()) + "/server.json");
    if (!saved.empty()) {
        json_object* o = json_tokener_parse(std::string(saved.begin(), saved.end()).c_str());
        linkUp.server = textOf(o, "address");
        linkUp.paired = !linkUp.server.empty();
        if (o) json_object_put(o);
    }
    linkUp.appearOffline = !cab::readBytes(storage::userDir(storage::currentUser()) + "/linkup-offline").empty();
    if (linkUp.on && linkUp.paired) linkupWantProgram(true);
}

void App::linkupWantProgram(bool on) {
    if (linkupProgramWanted == on) return;
    linkupProgramWanted = on;
    std::string why;
    if (on) {
        if (!unit::start(kUnit, &why)) std::fprintf(stderr, "[linkup] could not start: %s\n", why.c_str());
    } else {
        linkupLink.close();
        linkupHelloSent = false;
        if (!unit::stop(kUnit, &why)) std::fprintf(stderr, "[linkup] could not stop: %s\n", why.c_str());
    }
}

void App::linkupSend(json_object* o) {
    linkupLink.send(std::string(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN)));
    json_object_put(o);
}

void App::linkupSetSwitch(bool on) {
    linkUp.on = on;
    if (on) cab::writeBytes(switchPath(), std::string("on"), 0600);
    else cab::removeFile(switchPath());
    if (on && linkUp.paired) linkupWantProgram(true);
    if (!on) {
        linkupWantProgram(false);
        linkUp.friends.clear();
    }
}

void App::linkupBeginPairing(const std::string& address, const std::string& code) {
    linkupPairingAddress = address;
    linkupPairingCode = code;
    linkupPairingLeft = 40.0f;
    menuNotice.say("Pairing\xE2\x80\xA6", Tone::Busy);
    linkupWantProgram(true);
}

// Who is signed in, for the server. RomM has no id for a whole server, so the date it made this user
// is what tells user 1 here from user 1 somewhere else (the program hashes the two).
void App::linkupFollowAccount() {
    romm::User got;
    std::string err;
    bool ok = false;
    if (linkupUserJob.take(&got, &err, &ok) && ok) {
        linkupUser = got;
        linkupUserFor = got.id;
        linkupSignedIn = false;
        // Status belongs to each account.
        linkUp.appearOffline = !cab::readBytes(storage::userDir(storage::currentUser()) + "/linkup-offline").empty();
        refreshLinkUpScreen();
    }
    if (linkupUserJob.busy()) return;
    const int active = accounts::activeId();
    if (active > 0 && linkupUserFor != active && !romm::serverAway()) {
        linkupUserFor = active;
        linkupUserJob.start([this](romm::User& out, std::string& e) { return liveClient.fetchCurrentUser(&out, &e); });
    }
}

std::string App::linkupStatusWord() const {
    const bool away = idleWatch.idleFor(clockSeconds()) > kAwaySeconds || idleShown == idle::Level::Blank;
    // In Steam this program is not running at all, and the LinkUp program says Offline for it.
    if (linkUp.appearOffline || away || players::streaming()) return "offline";
    return "online";
}

void App::linkupFollowStatus() {
    if (!linkupLink.connected() || !linkupHelloSent || linkupUser.id <= 0) return;
    const std::string word = linkupStatusWord();
    if (!linkupSignedIn) {
        json_object* c = command("signin");
        json_object_object_add(c, "id", json_object_new_int(linkupUser.id));
        setText(c, "created", linkupUser.createdAt);
        setText(c, "username", linkupUser.username);
        setText(c, "status", word);
        linkupSend(c);
        linkupSignedIn = true;
        linkupStatusSent = word;
    } else if (word != linkupStatusSent) {
        json_object* c = command("status");
        setText(c, "status", word);
        linkupSend(c);
        linkupStatusSent = word;
    }
    // While the console cannot reach its RomM server, check-ins stop, unless a game is running.
    const bool away = romm::serverAway() && !linkupSessionOpen();
    if (away != linkupAwaySent) {
        json_object* c = command("away");
        json_object_object_add(c, "on", json_object_new_boolean(away));
        linkupSend(c);
        linkupAwaySent = away;
    }
}

void App::linkupFollowLink() {
    if (!linkupProgramWanted) return;
    linkupLink.connectIfNeeded(clockSeconds());
    if (linkupLink.connected() && !linkupHelloSent) {
        linkupSend(command("hello"));
        linkupHelloSent = true;
        linkupSignedIn = false;
        linkupAwaySent = false;
    }
    if (linkupLink.connected() && !linkupPairingCode.empty() && linkupUser.id > 0) {
        json_object* c = command("pair");
        setText(c, "address", linkupPairingAddress);
        setText(c, "code", linkupPairingCode);
        json_object_object_add(c, "id", json_object_new_int(linkupUser.id));
        setText(c, "created", linkupUser.createdAt);
        setText(c, "username", linkupUser.username);
        linkupSend(c);
        linkupPairingCode.clear();
    }
    const bool alive = linkupLink.poll([this](const uint8_t* data, size_t len) { linkupOnMessage(data, len); });
    if (!alive && linkupHelloSent) {
        // The program has gone: a crash, which its unit restarts, or a stop.
        linkupHelloSent = false;
        std::fprintf(stderr, "[linkup] the program went away\n");
        if (linkupSessionOpen()) linkupProgramLost();
    }
}

void App::linkupOnMessage(const uint8_t* data, size_t len) {
    if (len == 0) return;
    if (data[0] == linkupwire::kTagPad && len >= sizeof(linkupwire::Pad)) {
        std::memcpy(&linkupFriendPad, data, sizeof linkupFriendPad);
        linkupFriendPadChanged();
        return;
    }
    if (data[0] == linkupwire::kTagRumble && len >= sizeof(linkupwire::Rumble)) {
        linkupwire::Rumble r;
        std::memcpy(&r, data, sizeof r);
        if (SDL_Gamepad* gp = players::gamepad(0)) {
            if (rumble::enabled()) SDL_RumbleGamepad(gp, r.low, r.high, 300);
        }
        return;
    }
    json_object* o = json_tokener_parse(std::string(reinterpret_cast<const char*>(data), len).c_str());
    if (!o) return;
    linkupOnEvent(o);
    json_object_put(o);
}

void App::linkupOnEvent(json_object* o) {
    const std::string ev = textOf(o, "ev");
    if (ev == "state") {
        const std::string connection = textOf(o, "connection");
        const bool wasPaired = linkUp.paired;
        linkUp.paired = flagOf(o, "paired");
        linkUp.server = textOf(o, "address");
        linkUp.instead = connection == "unreachable"      ? linkup::kCantConnect
                         : connection == "removed"        ? linkup::kConsoleRemoved
                         : connection == "console_behind" ? linkup::kThisNeedsUpdate
                         : connection == "server_behind"  ? linkup::kServerNeedsUpdate
                                                          : std::string();
        if (wasPaired != linkUp.paired && here() == Screen::Settings) buildSettings();
        refreshLinkUpScreen();
    } else if (ev == "pair_result") {
        linkupPairingLeft = 0.0f;
        linkupSaidPairing(textOf(o, "result"));
    } else if (ev == "left") {
        linkUp.paired = false;
        linkUp.server.clear();
        linkUp.friends.clear();
        linkupSignedIn = false;
        linkupWantProgram(false);
        if (here() == Screen::Settings) buildSettings();
    } else if (ev == "friends") {
        linkupTookFriends(json_object_object_get(o, "list"));
    } else if (ev == "invited") {
        // A person in a session gets no pills from other friends, because the name would reach the friend.
        if (!linkupSessionOpen()) linkupSayInvited(textOf(o, "from"));
    } else if (ev == "invite_out") {
        if (!flagOf(o, "ok") && linkupHostWaiting()) linkupNotAvailable(linkupGuest);
    } else if (ev == "invite_ended") {
        linkupTookInviteEnded(textOf(o, "invite"), textOf(o, "reason"));
    } else if (ev == "join_result") {
        if (!flagOf(o, "ok") && linkupSide == LinkupSide::Friend) {
            linkupSessionClosed();
            linkupPanel(linkup::notAvailable(linkupHost), "", {}, 0, [](int) {});
            choiceScreen.setPrompt("Press", "A", "to continue");
            choiceScreen.setOnlyA(true);
        }
    } else if (ev == "joined") {
        linkupJoined = true;
        // The friend is in. The game starts once the two consoles are connected.
        if (linkupPlay.shows() == screens::LinkUpPlayScreen::Shows::Inviting)
            linkupPlay.open(screens::LinkUpPlayScreen::Shows::Waiting, linkupGame, "", /*friendSide=*/false);
    } else if (ev == "connected") {
        linkupConnected(textOf(o, "path"));
    } else if (ev == "connect_failed") {
        linkupConnectFailed(textOf(o, "reason"));
    } else if (ev == "picture") {
        linkupFriendPicture(flagOf(o, "on"));
    } else if (ev == "peer") {
        json_object* p = json_tokener_parse(textOf(o, "text").c_str());
        if (p) {
            linkupFromPeer(textOf(p, "t"), p);
            json_object_put(p);
        }
    } else if (ev == "peer_lost") {
        linkupPeerLost();
    } else if (ev == "peer_back") {
        linkupPeerBack();
    } else if (ev == "peer_gone") {
        linkupPeerGone();
    } else if (ev == "session_ended") {
        linkupPeerGone();
    } else if (ev == "slow") {
        menuNotice.say(linkup::kTooSlow, Tone::Problem);
    }
}

void App::linkupSaidPairing(const std::string& result) {
    menuNotice.text.clear();
    if (result == "paired") {
        linkUp.paired = true;
        linkUp.server = linkupPairingAddress;
        linkupSignedIn = false;
        if (here() == Screen::Settings) buildSettings();
        return;
    }
    const char* words = result == "wrong_code"        ? "Wrong or expired code"
                        : result == "too_many_tries"  ? "Too many tries"
                        : result == "not_that_server" ? "That server failed its check"
                        : result == "refused"         ? "That server refused this console"
                                                      : "Can't reach that server";
    menuNotice.say(words, Tone::Problem);
    // With no server, the program does not stay running and nothing is contacted.
    if (!linkUp.paired) linkupWantProgram(false);
}

void App::linkupTookFriends(json_object* list) {
    if (!list) return;
    std::vector<linkup::Friend> fresh;
    for (size_t i = 0; i < json_object_array_length(list); ++i) {
        json_object* j = json_object_array_get_idx(list, i);
        linkup::Friend f;
        f.id = textOf(j, "id");
        f.name = textOf(j, "name");
        f.from = textOf(j, "server");
        f.invite = textOf(j, "invite");
        f.game = textOf(j, "game");
        const std::string status = textOf(j, "status");
        f.status = !f.invite.empty()         ? linkup::Status::InvitedYou
                   : status == "online"      ? linkup::Status::Online
                   : status == "linkedup"    ? linkup::Status::InGame
                                             : linkup::Status::Offline;
        json_object* stream = nullptr;
        if (json_object_object_get_ex(j, "stream", &stream)) f.stream = json_object_get_int(stream);
        fresh.push_back(std::move(f));
    }
    linkUp.friends = std::move(fresh);
    refreshLinkUpScreen();
}

void App::linkupTookInviteEnded(const std::string& invite, const std::string& reason) {
    // To whoever invited.
    if (linkupHostWaiting() && !linkupJoined) {
        if (reason == "declined") linkupDeclinedBy(linkupGuest);
        else if (reason == "timeout") linkupNoAnswerFrom(linkupGuest);
        else if (reason == "unavailable") linkupNotAvailable(linkupGuest);
        return;
    }
    // To whoever was invited: their line goes, and the pill with it.
    for (const linkup::Friend& f : linkUp.friends) {
        if (f.invite != invite) continue;
        if (menuNotice.text == linkup::invitedYou(f.name)) menuNotice.text.clear();
        linkupInviteEnded(f.name);
        break;
    }
}

// ---- Starting a session --------------------------------------------------------

bool App::linkupHostWaiting() const { return linkupSide == LinkupSide::Host && !linkupStreamUp; }

void App::linkupSessionOpens(LinkupSide side) {
    linkupSide = side;
    linkupJoined = false;
    linkupStreamUp = false;
    linkupPictureSent = false;
    linkupFriendShowsPicture = false;
    linkupPausedHere = false;
    linkupPausedThere = false;
    linkupLost = false;
    linkupLostFor = 0.0f;
    linkupIdleFor = 0.0f;
    linkupGameAsked = false;
    // ONE AT A TIME WITH REMOTE PLAY: Sunshine stops for the length of a session and comes back by
    // itself, with the switch unchanged, and nobody else's device can get into the game. Not
    // App::setRemotePlay, which saves the switch as off and takes Tailscale with it.
    if (rpWant) {
        remoteplay::watch(false);
        std::string why;
        if (!unit::stop(kRemotePlayUnit, &why)) std::fprintf(stderr, "[linkup] Remote Play would not stop: %s\n", why.c_str());
        linkupHeldRemotePlay = true;
    }
}

void App::linkupSessionClosed() {
    linkupSide = LinkupSide::None;
    linkupStreamUp = false;
    if (linkupHeldRemotePlay) {
        std::string why;
        if (!unit::start(kRemotePlayUnit, &why)) std::fprintf(stderr, "[linkup] Remote Play would not start again: %s\n", why.c_str());
        remoteplay::watch(rpWant, /*fresh=*/true);
        linkupHeldRemotePlay = false;
    }
    linkupUnseatFriend();
    linkupFriendHandover(false);
    linkupWaitEndsIn = 0.0f;
    linkupWaitEndsThen = nullptr;
    linkupNextClose();
    linkupEndPick();
    while (stack.size() > 1 && here() != Screen::LinkUp) stack.pop_back();
    if (stack.back() == Screen::LinkUpPlay && stack.size() > 1) stack.pop_back();
    refreshLinkUpScreen();
}

void App::linkupSendInvite() {
    json_object* c = command("invite");
    setText(c, "friend", linkupGuestId);
    setText(c, "game", linkupGame);
    json_object_object_add(c, "gamescope", json_object_new_int(gamescopePid()));
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    json_object_object_add(c, "width", json_object_new_int(w));
    json_object_object_add(c, "height", json_object_new_int(h));
    linkupSend(c);
    linkupSessionOpens(LinkupSide::Host);
    linkupFetchGame();
}

// A game still to download starts downloading when the invite goes out, because a big game can take
// longer than any wait here, and only the person inviting needs it. On the drive already, this is
// quick and asks almost nothing.
void App::linkupFetchGame() {
    const romm::Game* g = nullptr;
    for (const auto& x : games)
        if (x.id == linkupGameId) g = &x;
    if (!g || launchJob.busy() || launchJob.waiting()) return;
    std::string err;
    if (!beginLaunch(launchJob, liveClient, *g, coreDir, &err, /*playWhenReady=*/false, /*keepWhenReady=*/false))
        std::fprintf(stderr, "[linkup] %s\n", err.c_str());
}

void App::linkupSendCancel() {
    if (launchJob.romId == linkupGameId && !launchJob.playWhenReady) launchJob.cancel = true;
    linkupSend(command("cancel"));
    linkupSessionClosed();
}

void App::linkupSendJoin(const linkup::Friend& f) {
    json_object* c = command("join");
    setText(c, "invite", f.invite);
    if (const char* d = std::getenv("DISPLAY")) setText(c, "display", d);
    if (const char* x = std::getenv("XAUTHORITY")) setText(c, "xauthority", x);
    linkupSend(c);
    linkupSessionOpens(LinkupSide::Friend);
}

void App::linkupSendDecline(const linkup::Friend& f) {
    json_object* c = command("decline");
    setText(c, "invite", f.invite);
    linkupSend(c);
}

void App::linkupTellPeer(const std::string& word) {
    json_object* inner = json_object_new_object();
    setText(inner, "t", word);
    json_object* c = command("peer");
    setText(c, "text", json_object_to_json_string_ext(inner, JSON_C_TO_STRING_PLAIN));
    json_object_put(inner);
    linkupSend(c);
}

// The two consoles are connected. On the host the game starts, so that both begin at its title screen.
void App::linkupConnected(const std::string& path) {
    // Direct or relay goes to the log only, and no name goes with it: the report is made to be attached
    // to a public issue.
    std::fprintf(stderr, "[linkup] connected: %s\n", path.c_str());
    // Back from a drop on a new connection: everything already stands.
    if (linkupStreamUp) return;
    linkupStreamUp = true;
    if (linkupSide == LinkupSide::Host) {
        linkupSeatFriend();
        linkupStartGame();
    } else if (linkupSide == LinkupSide::Friend) {
        linkupFriendHandover(true);
    }
}

void App::linkupStartGame() {
    linkupGameAsked = true;
    linkupGameStartedAt = clockSeconds();
    linkupTellPeer("starting");
    std::fprintf(stderr, "[linkup] starting the game for both\n");
    if (!launchById(linkupGameId)) std::fprintf(stderr, "[linkup] that game would not start\n");
}

void App::linkupConnectFailed(const std::string& reason) {
    const std::string who = linkupSide == LinkupSide::Host ? linkupGuest : linkupHost;
    std::fprintf(stderr, "[linkup] could not connect: %s\n", reason.c_str());
    linkupSend(command("end"));
    linkupSessionClosed();
    // A real break names who is behind. An update is never forced.
    const std::string title = reason == "friend_behind" ? linkup::needsUpdate(who)
                              : reason == "this_behind" ? linkup::kThisNeedsUpdate
                                                        : linkup::cantConnectTo(who);
    linkupPanel(title, reason == "relay_limit" ? "The relay has reached its limit" : "", {}, 0, [](int) {});
    choiceScreen.setPrompt("Press", "A", "to continue");
    choiceScreen.setOnlyA(true);
}

// ---- The host: player two, and the picture ---------------------------------------

namespace {

bool hostAtFriendRumble(void* userdata, Uint16 low, Uint16 high) {
    linkupwire::Rumble r;
    r.low = low;
    r.high = high;
    r.ms = 300;
    static_cast<linkup::Link*>(userdata)->send(&r, sizeof r);
    return true;
}

}  // namespace

// The friend is a pad: a virtual one SDL treats as any other, so every game hears player two the way
// it hears anybody, and a game's rumble comes back through it. The menus never hear it
// (players::setAside), which is how a friend's presses stay out of the host's console.
void App::linkupSeatFriend() {
    if (linkupFriendJoystick) return;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    desc.name = "LinkUp friend";
    desc.userdata = &linkupLink;
    desc.Rumble = hostAtFriendRumble;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        std::fprintf(stderr, "[linkup] no seat for the friend: %s\n", SDL_GetError());
        return;
    }
    linkupFriendJoystick = id;
    players::seatFriend(id);
    linkupFriendPad = linkupwire::Pad{};
    linkupVpadSaid = linkupwire::Pad{};
    linkupLastPadSeq = 0;
}

void App::linkupUnseatFriend() {
    if (!linkupFriendJoystick) return;
    std::fprintf(stderr, "[linkup] heard %u states of the friend's pad\n", static_cast<unsigned>(linkupLastPadSeq));
    players::unseatFriend();
    SDL_DetachVirtualJoystick(linkupFriendJoystick);
    linkupFriendJoystick = 0;
}

// A pad's state from the friend. Trigger axes run from the bottom of a joystick's range in SDL's
// virtual pad, where a pad's own triggers read from 0.
void App::linkupFriendPadChanged() {
    if (linkupLastPadSeq == linkupFriendPad.seq) return;
    const int16_t ahead = static_cast<int16_t>(linkupFriendPad.seq - linkupLastPadSeq);
    if (ahead < 0 && ahead > -1000) return;   // an old one overtaken by a later one
    linkupLastPadSeq = linkupFriendPad.seq;
    linkupIdleFor = 0.0f;
    linkupPressHeard = true;
    SDL_Gamepad* gp = SDL_GetGamepadFromID(linkupFriendJoystick);
    SDL_Joystick* joy = gp ? SDL_GetGamepadJoystick(gp) : nullptr;
    if (!joy) return;
    SDL_LockJoysticks();
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
        SDL_SetJoystickVirtualButton(joy, b, (linkupFriendPad.buttons >> b) & 1u);
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
        int value = linkupFriendPad.axes[a];
        if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) value = value * 2 - 32768;
        SDL_SetJoystickVirtualAxis(joy, a, static_cast<Sint16>(std::clamp(value, -32768, 32767)));
    }
    SDL_UnlockJoysticks();
    linkupPassToEmulator();
}

// An emulator run as a program takes its pads through virtual controllers (vpad.h), press by press.
void App::linkupPassToEmulator() {
    if (!standaloneRun.active() || standalonePaused) return;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        const bool now = (linkupFriendPad.buttons >> b) & 1u;
        const bool was = (linkupVpadSaid.buttons >> b) & 1u;
        if (now != was) cab::vpad::button(1, static_cast<SDL_GamepadButton>(b), now);
    }
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
        if (linkupFriendPad.axes[a] != linkupVpadSaid.axes[a]) cab::vpad::axis(1, static_cast<SDL_GamepadAxis>(a), linkupFriendPad.axes[a]);
    linkupVpadSaid = linkupFriendPad;
}

// True when the game alone is on this console's screen.
bool App::linkupGameAloneOnScreen() const {
    if (choiceScreen.isOpen() || pinScreen.isOpen() || keyboard.isOpen()) return false;
    if (standaloneRun.active())
        return !standalonePaused && !overlayOpen && overlayFade.value() <= 0.001f;
    return playing && !overlayOpen && overlayFade.value() <= 0.001f && !stateHold && !stateHoldWaiting &&
           curtain.value() <= 0.001f;
}

// Before the frame draws. Anything drawn over the game itself is in the stream too, such as an
// achievement, and those are harmless and stay.
void App::linkupFollowGame() {
    if (linkupSide != LinkupSide::Host || !linkupStreamUp) return;
    const bool show = linkupGameAloneOnScreen();
    if (show != linkupPictureSent) {
        json_object* c = command("picture");
        json_object_object_add(c, "on", json_object_new_boolean(show));
        linkupSend(c);
        linkupPictureSent = show;
    }
    // The host's own pause menu: the other console says so, and a press there resumes both.
    const bool menuUp = overlayOpen && !powerMenu && !linkupPausedThere;
    if (menuUp != linkupPausedHere) {
        linkupPausedHere = menuUp;
        linkupTellPeer(menuUp ? "paused" : "resumed");
        if (!menuUp) cab::vpad::releaseAll();
        if (!menuUp) linkupVpadSaid = linkupwire::Pad{};
    }
    linkupPassToEmulator();
    // A game that never came: it could not start. The console's usual message says why here, and
    // the friend sees that the session ended.
    if (linkupGameAsked && !playing && !standaloneRun.active() && !launchJob.busy() && !launchJob.waiting() &&
        clockSeconds() - linkupGameStartedAt > 2.0 && curtain.value() <= 0.001f && !linkupNextOpen) {
        std::fprintf(stderr, "[linkup] the game did not start; the session is over\n");
        linkupGameAsked = false;
        linkupTellPeer("ended");
        linkupSend(command("end"));
        linkupSessionClosed();
    }
}

// The game has ended, however it ended. Quitting a game keeps the session: the friend waits, and the
// person hosting lands on the game list.
void App::linkupHostGameEnded() {
    if (linkupSide != LinkupSide::Host) return;
    linkupGameAsked = false;
    linkupPictureSent = false;
    if (linkupStreamUp) {
        json_object* c = command("picture");
        json_object_object_add(c, "on", json_object_new_boolean(false));
        linkupSend(c);
        linkupTellPeer("quit");
    }
    linkupPausedHere = false;
    linkupPausedThere = false;
    linkupVpadSaid = linkupwire::Pad{};
    linkupGameQuit();
}

void App::linkupPauseGame() {
    if (standaloneRun.active()) {
        if (!standalonePaused) openStandaloneMenu();
    } else if (playing && !overlayOpen) {
        toggleOverlay();
    }
}

void App::linkupResumeGame() {
    if (choiceScreen.isOpen()) choiceScreen.close();
    choiceThen = nullptr;
    if (overlayOpen) toggleOverlay();
}

// The panel over the host's stopped game. The pause menu is what stops a game, and its own rows do not
// show while a question is up (App::drawPauseMenu), so this reads as LinkUp's panel over the picture.
void App::linkupHostPanel(const std::string& title, std::function<void()> then) {
    linkupPauseGame();
    linkupPanel(title, "", {}, 0, [then = std::move(then)](int) { then(); });
    choiceScreen.setPrompt("Press", "A", "to resume");
    choiceScreen.setOnlyA(true);
}

// ---- The friend: the picture on this television ----------------------------------

// The LinkUp program's own window takes the television, as an emulator's does (standalone.h), and
// this window waits on top of it as gamescope's overlay, with the controllers. It says Waiting,
// Paused and the rest by drawing over the picture.
void App::linkupFriendHandover(bool on) {
    if (on == linkupOverlayMarked) return;
    linkupOverlayMarked = on;
    if (on) {
        SDL_HideWindow(window);
        cab::overlaywin::mark(window, /*takeInput=*/true);
        SDL_ShowWindow(window);
    } else {
        if (cab::overlaywin::active()) {
            SDL_HideWindow(window);
            cab::overlaywin::unmark(window);
            SDL_ShowWindow(window);
        }
        renderer.setTransparentBackground(false);
        linkupFriendShowsPicture = false;
    }
}

void App::linkupFriendPicture(bool on) {
    if (linkupSide != LinkupSide::Friend) return;
    linkupFriendShowsPicture = on;
    using Shows = screens::LinkUpPlayScreen::Shows;
    if (on) {
        // The game is back, and whatever was said about why it was gone is over.
        linkupPausedThere = false;
        linkupPlay.setResumes(false);
        if (!linkupPausedHere) linkupPlay.open(Shows::Picture, linkupGame, "", /*friendSide=*/true);
        standaloneClearFrames = 30;
    } else if (!linkupPausedHere && linkupPlay.shows() == Shows::Picture) {
        linkupPlay.open(Shows::Waiting, linkup::waitingFor(linkupHost), linkupGame, /*friendSide=*/true);
    }
}

// The picture is all there is: nothing of this window shows, and the frame loop does little else.
bool App::linkupPictureOnly() {
    return linkupSide == LinkupSide::Friend && linkupStreamUp && here() == Screen::LinkUpPlay &&
           linkupPlay.shows() == screens::LinkUpPlayScreen::Shows::Picture && !choiceScreen.visible() && !pinScreen.isOpen() &&
           menuNotice.text.empty();
}

bool App::linkupPictureBehind() {
    return linkupSide == LinkupSide::Friend && linkupOverlayMarked && here() == Screen::LinkUpPlay &&
           linkupPlay.shows() == screens::LinkUpPlayScreen::Shows::Picture;
}

// The friend's pad, to the host. Only presses travel: a friend's keyboard and mouse stay at home.
void App::linkupSendPad(float dt) {
    if (linkupSide != LinkupSide::Friend || !linkupStreamUp) return;
    linkupwire::Pad pad;
    SDL_Gamepad* gp = players::gamepad(0);
    const bool asleep = !linkupPictureBehind() || choiceScreen.visible();
    pad.buttons = linkupScriptPad;
    if (gp && !asleep) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            // The console's own button never reaches the other console, and a shortcut button is this
            // console's: it opens this console's menu.
            if (b == SDL_GAMEPAD_BUTTON_GUIDE) continue;
            if (SDL_GetGamepadButton(gp, static_cast<SDL_GamepadButton>(b))) pad.buttons |= 1u << b;
        }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) pad.axes[a] = SDL_GetGamepadAxis(gp, static_cast<SDL_GamepadAxis>(a));
    }
    linkupPadAge += dt;
    const bool changed = pad.buttons != linkupPadSent.buttons || std::memcmp(pad.axes, linkupPadSent.axes, sizeof pad.axes) != 0;
    if (!changed && linkupPadAge < kPadKeepAliveSeconds) return;
    linkupPadAge = 0.0f;
    pad.seq = ++linkupPadSeq;
    linkupPadSent = pad;
    linkupLink.send(&pad, sizeof pad);
}

// An event, while the picture is up. The pause: both stick clicks, as in every game, or a tap of the
// shortcut button with the shortcuts on.
bool App::takeLinkUpEvent(const SDL_Event& e) {
    if (!linkupPictureBehind() || choiceScreen.visible() || pinScreen.isOpen()) return false;
    switch (e.type) {
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            if (down) {
                idleWatch.input(clockSeconds());
                linkupPressHere = true;
                linkupIdleFor = 0.0f;
            }
            const auto b = static_cast<SDL_GamepadButton>(e.gbutton.button);
            if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK) l3Down = down;
            if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK) r3Down = down;
            if (down && l3Down && r3Down) {
                l3Down = r3Down = false;
                linkupAskLeave();
                return true;
            }
            SDL_Gamepad* gp = SDL_GetGamepadFromID(e.gbutton.which);
            if (gp && shortcuts::enabled()) {
                if (shortcuts::isShortcut(gp, b) && !down) {
                    linkupAskLeave();
                    return true;
                }
            } else if (b == SDL_GAMEPAD_BUTTON_GUIDE && !down) {
                linkupAskLeave();
                return true;
            }
            return true;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (std::abs(static_cast<int>(e.gaxis.value)) > idle::kAxisDeadzone) {
                idleWatch.input(clockSeconds());
                linkupPressHere = true;
                linkupIdleFor = 0.0f;
            }
            return true;
        case SDL_EVENT_GAMEPAD_ADDED:
            players::added(e.gdevice.which);
            return true;
        case SDL_EVENT_GAMEPAD_REMOVED:
            players::removed(e.gdevice.which);
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return true;
        default:
            return false;
    }
}

// Every frame while the picture is all there is. True when this frame is done: nothing is drawn but
// the dim, and a short wait for the next press, so nothing is added to the time a press takes.
bool App::linkupWatchPicture(float dt) {
    if (!linkupPictureOnly()) return false;
    tickLinkUpLive(dt);
    idleFrame(dt, /*playingNow=*/true, kScreenOff[screenOffIndex].seconds);
    if (std::abs(dimLayer.value() - standaloneDimDrawn) > 0.001f) drawOverGame();
    else if (standaloneClearFrames > 0) {
        --standaloneClearFrames;
        renderer.setTransparentBackground(true);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);
    }
    SDL_WaitEventTimeout(nullptr, 3);
    return true;
}

// ---- What the two consoles say to each other --------------------------------------

void App::linkupFromPeer(const std::string& word, json_object*) {
    const std::string who = linkupSide == LinkupSide::Host ? linkupGuest : linkupHost;
    using Shows = screens::LinkUpPlayScreen::Shows;
    if (word == "press") {
        idleWatch.input(clockSeconds());
        linkupIdleFor = 0.0f;
    } else if (word == "slow") {
        menuNotice.say(linkup::kTooSlow, Tone::Problem);
    } else if (word == "pause" && linkupSide == LinkupSide::Host) {
        // The friend paused: stopped here too, and A on either console resumes both.
        linkupPausedThere = true;
        linkupHostPanel(linkup::pausedBy(who), [this]() {
            linkupPausedThere = false;
            linkupTellPeer("resume");
            linkupResumeGame();
        });
    } else if (word == "resume" && linkupSide == LinkupSide::Host) {
        linkupPausedThere = false;
        choiceThen = nullptr;
        linkupResumeGame();
    } else if (word == "resume" && linkupSide == LinkupSide::Friend) {
        linkupPausedHere = false;
        if (choiceScreen.isOpen()) choiceScreen.close();
        choiceThen = nullptr;
    } else if (word == "leave" && linkupSide == LinkupSide::Host) {
        linkupFriendLeft();
    } else if (word == "paused" && linkupSide == LinkupSide::Friend) {
        linkupPausedThere = true;
        if (!linkupPausedHere) {
            linkupPlay.open(Shows::Waiting, linkup::pausedBy(who), linkupGame, /*friendSide=*/true);
            linkupPlay.setResumes(true);
        }
    } else if (word == "resumed" && linkupSide == LinkupSide::Friend) {
        linkupPausedThere = false;
        linkupPlay.setResumes(false);
        if (!linkupPausedHere && !linkupFriendShowsPicture)
            linkupPlay.open(Shows::Waiting, linkup::waitingFor(who), linkupGame, /*friendSide=*/true);
    } else if (word == "starting" && linkupSide == LinkupSide::Friend) {
        linkupPlay.open(Shows::Waiting, linkup::waitingFor(who), linkupGame, /*friendSide=*/true);
    } else if (word == "quit" && linkupSide == LinkupSide::Friend) {
        linkupPlay.open(Shows::Waiting, linkup::waitingFor(who), "", /*friendSide=*/true);
    } else if (word == "ended" && linkupSide == LinkupSide::Friend) {
        linkupPeerEndedSession();
    } else if (word == "idle" ) {
        linkupIdlePause();
    }
}

void App::linkupFriendAsksPause() {
    linkupPausedHere = true;
    linkupTellPeer("pause");
    // The game stops for both of you while this is up; the other console says so.
    linkupPanel("Paused", "", {"Resume", "Leave"}, 0, [this](int c) {
        linkupPausedHere = false;
        if (c == 1) {
            linkupFriendLeaves();
            return;
        }
        linkupTellPeer("resume");
    });
    choiceBack = [this]() {
        linkupPausedHere = false;
        linkupTellPeer("resume");
    };
}

void App::linkupFriendLeaves() {
    linkupTellPeer("leave");
    linkupSend(command("end"));
    linkupSessionClosed();
}

// Their Leave, the minute running out, or their console powering off. The host's game carries on as a
// normal game with other controllers working, and player two stays plugged in and idle.
void App::linkupFriendLeft() {
    const std::string who = linkupGuest;
    linkupSend(command("end"));
    const bool inGame = playing || standaloneRun.active();
    linkupSide = LinkupSide::None;
    linkupStreamUp = false;
    players::unseatFriend();
    if (linkupFriendJoystick) {
        SDL_DetachVirtualJoystick(linkupFriendJoystick);
        linkupFriendJoystick = 0;
    }
    if (linkupHeldRemotePlay) {
        std::string why;
        if (!unit::start(kRemotePlayUnit, &why)) std::fprintf(stderr, "[linkup] %s\n", why.c_str());
        remoteplay::watch(rpWant, /*fresh=*/true);
        linkupHeldRemotePlay = false;
    }
    if (inGame) {
        if (choiceScreen.isOpen()) {
            choiceThen = [this](int) { linkupResumeGame(); };
            choiceScreen.changeTitle(linkup::left(who));
        } else {
            linkupHostPanel(linkup::left(who), [this]() { linkupResumeGame(); });
        }
    } else {
        linkupSessionClosed();
        linkupPanel(linkup::left(who), "", {}, 0, [](int) {});
        choiceScreen.setPrompt("Press", "A", "to continue");
        choiceScreen.setOnlyA(true);
    }
}

void App::linkupPeerEndedSession() {
    const std::string who = linkupHost;
    linkupSend(command("end"));
    linkupSessionClosed();
    linkupPanel(linkup::endedSession(who), "", {}, 0, [](int) {});
    choiceScreen.setPrompt("Press", "A", "to continue");
    choiceScreen.setOnlyA(true);
}

// A drop: under about two seconds shows nothing. Then the game pauses on both, and the panel's words
// change gently if they return. After a minute, they left.
void App::linkupPeerLost() {
    if (linkupSide == LinkupSide::None || linkupLost) return;
    linkupLost = true;
    linkupLostFor = 0.0f;
    const std::string who = linkupSide == LinkupSide::Host ? linkupGuest : linkupHost;
    if (linkupSide == LinkupSide::Host) {
        linkupHostPanel(linkup::reconnectingTo(who), [this]() { linkupResumeGame(); });
    } else {
        if (choiceScreen.isOpen()) choiceScreen.close();
        linkupPlay.open(screens::LinkUpPlayScreen::Shows::Reconnecting, linkup::reconnectingTo(who), "", /*friendSide=*/true);
    }
}

void App::linkupPeerBack() {
    if (!linkupLost) return;
    linkupLost = false;
    const std::string who = linkupSide == LinkupSide::Host ? linkupGuest : linkupHost;
    if (linkupSide == LinkupSide::Host) {
        if (choiceScreen.isOpen()) choiceScreen.changeTitle(linkup::isBack(who));
    } else {
        using Shows = screens::LinkUpPlayScreen::Shows;
        linkupPlay.open(linkupFriendShowsPicture ? Shows::Picture : Shows::Waiting,
                        linkupFriendShowsPicture ? linkupGame : linkup::waitingFor(who), linkupFriendShowsPicture ? "" : linkupGame,
                        /*friendSide=*/true);
    }
}

// The other console said goodbye, or the server says the session is over.
void App::linkupPeerGone() {
    if (linkupSide == LinkupSide::Host) linkupFriendLeft();
    else if (linkupSide == LinkupSide::Friend) linkupPeerEndedSession();
}

// The LinkUp program itself went away in a session. Its unit brings it back, but the connection and
// the session are gone with it. On the host the game carries on; on the friend's console the session is
// over. A running game is never ended for this.
void App::linkupProgramLost() {
    if (linkupSide == LinkupSide::Host) linkupFriendLeft();
    else if (linkupSide == LinkupSide::Friend) linkupPeerEndedSession();
}

void App::linkupIdlePause() {
    if (linkupSide == LinkupSide::Host && (playing || standaloneRun.active()) && !overlayOpen) {
        linkupPausedThere = true;
        linkupHostPanel(linkup::kStillPlaying, [this]() {
            linkupPausedThere = false;
            linkupIdleFor = 0.0f;
            linkupTellPeer("resume");
            linkupResumeGame();
        });
    } else if (linkupSide == LinkupSide::Friend && !choiceScreen.isOpen()) {
        linkupPausedHere = true;
        linkupPanel(linkup::kStillPlaying, "", {}, 0, [this](int) {
            linkupPausedHere = false;
            linkupIdleFor = 0.0f;
            linkupTellPeer("resume");
        });
        choiceScreen.setPrompt("Press", "A", "to resume");
        choiceScreen.setOnlyA(true);
    }
}

// The clocks of a session, once a frame.
void App::tickLinkUpSession(float dt) {
    if (linkupSide == LinkupSide::None) return;
    // A press from either player keeps both screens awake, and the idle rule below is the only clock.
    if (idleWatch.idleFor(clockSeconds()) < 0.2) {
        linkupIdleFor = 0.0f;
        linkupPressHere = true;
    }
    linkupPressAge += dt;
    if (linkupPressHere && linkupPressAge >= kPressEverySeconds && linkupStreamUp) {
        linkupPressHere = false;
        linkupPressAge = 0.0f;
        linkupTellPeer("press");
    }
    if (linkupPressHeard) {
        linkupPressHeard = false;
        idleWatch.input(clockSeconds());
    }
    if (linkupLost) {
        linkupLostFor += dt;
        if (linkupLostFor >= kDropEndsSeconds) {
            linkupLost = false;
            const std::string who = linkupSide == LinkupSide::Host ? linkupGuest : linkupHost;
            if (linkupSide == LinkupSide::Host) {
                linkupFriendLeft();
            } else {
                linkupSend(command("end"));
                linkupSessionClosed();
                linkupPanel(linkup::left(who), "", {}, 0, [](int) {});
                choiceScreen.setPrompt("Press", "A", "to continue");
                choiceScreen.setOnlyA(true);
            }
        }
    }
    // Only over a running game, on the host's clock: 10 minutes with no press from either player
    // pauses both, and through the relay those minutes are a full-quality stream nobody watches.
    if (linkupSide == LinkupSide::Host && linkupStreamUp && (playing || standaloneRun.active())) {
        linkupIdleFor += dt;
        if (!linkupIdlePaused && linkupIdleFor >= kIdlePauseSeconds && !overlayOpen && !choiceScreen.isOpen()) {
            linkupIdlePaused = true;
            linkupTellPeer("idle");
            linkupIdlePause();
        } else if (linkupIdlePaused && linkupIdleFor < kIdlePauseSeconds) {
            linkupIdlePaused = false;
        } else if (linkupIdlePaused && linkupIdleFor >= kIdlePauseSeconds + kIdleEndSeconds) {
            // LinkUp ends the session and frees the relay; the host's game stays paused on the TV.
            linkupIdlePaused = false;
            linkupTellPeer("ended");
            linkupSend(command("end"));
            linkupSide = LinkupSide::None;
            linkupStreamUp = false;
            linkupUnseatFriend();
        }
    }
}

// --linkup-script "<step;step;...>": a real session between two real consoles with nothing pressed,
// through the same calls a press makes, for the checks a television is not needed for. A step waits
// where it says so, and the next one runs when it is met. Its words:
//   on | off                     the switch
//   pair <address> <code>        adds a server
//   waitpaired                   until it is paired, and signed in
//   waitonline <name>            until that friend is Online
//   waitinvite                   until an invite is on the list
//   invite <name> <rom id>       invites them to that game
//   join | decline               answers the invite on the list
//   waitstream                   until the two consoles are connected
//   waitgame                     until the game is on the screen
//   sleep <seconds>
//   pad <bits>                   the friend's buttons, by SDL's numbers, as if held
//   next <rom id>                the next game, in the same session
//   pause | enter | leave | quit | end | offline | online
void App::runLinkUpScript() {
    if (linkupScript.empty() || clockSeconds() < linkupScriptWaitUntil) return;
    const size_t semi = linkupScript.find(';');
    const std::string step = linkupScript.substr(0, semi);
    std::istringstream in(step);
    std::string word, a, b;
    in >> word >> a >> b;
    bool done = true;
    auto findFriend = [&](const std::string& name) -> const linkup::Friend* {
        for (const linkup::Friend& f : linkUp.friends)
            if (f.name == name) return &f;
        return nullptr;
    };
    if (word == "on") linkupSetSwitch(true);
    else if (word == "off") linkupSetSwitch(false);
    else if (word == "pair") linkupBeginPairing(a, b);
    else if (word == "waitpaired") done = linkUp.paired && linkupSignedIn;
    else if (word == "waitonline") {
        const linkup::Friend* f = findFriend(a);
        done = f && f->status == linkup::Status::Online;
    } else if (word == "waitinvite") {
        done = false;
        for (const linkup::Friend& f : linkUp.friends) done = done || !f.invite.empty();
    } else if (word == "invite") {
        const linkup::Friend* f = findFriend(a);
        if (f) {
            linkupGuestId = f->id;
            linkupGuest = f->name;
            linkupGameId = std::atoi(b.c_str());
            // As --launch does (testhooks.cpp): nothing has walked into this game's grid.
            std::string title = "Game " + b;
            bool known = false;
            for (const romm::Game& g : games)
                if (g.id == linkupGameId) {
                    title = g.name;
                    known = true;
                }
            if (!known) {
                romm::Game g;
                std::string err;
                if (liveClient.fetchGame(linkupGameId, &g, &err)) {
                    title = g.name;
                    appendGame(lib, g);
                }
            }
            goToDestination(BarLinkUp);
            linkupInvite(title);
        }
    } else if (word == "join" || word == "decline") {
        for (const linkup::Friend& f : linkUp.friends) {
            if (f.invite.empty()) continue;
            const linkup::Friend copy = f;
            goToDestination(BarLinkUp);
            if (word == "join") linkupJoin(copy);
            else linkupSendDecline(copy);
            break;
        }
    } else if (word == "next") {
        linkupGameId = std::atoi(a.c_str());
        std::string title = "Game " + a;
        romm::Game g;
        std::string err;
        if (liveClient.fetchGame(linkupGameId, &g, &err)) {
            title = g.name;
            appendGame(lib, g);
        }
        linkupNextStarts(title);
    } else if (word == "waitstream") done = linkupStreamUp;
    else if (word == "waitgame") done = linkupSide != LinkupSide::Host || linkupGameAloneOnScreen();
    else if (word == "sleep") linkupScriptWaitUntil = clockSeconds() + std::atof(a.c_str());
    else if (word == "pad") linkupScriptPad = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 0));
    else if (word == "pause") {
        if (linkupSide == LinkupSide::Friend) linkupAskLeave();
        else linkupPauseGame();
    } else if (word == "enter") navigate(screens::Nav::Activate);
    else if (word == "leave") linkupFriendLeaves();
    else if (word == "quit") exitToHome();
    else if (word == "end") linkupNextDone();
    else if (word == "offline" || word == "online") {
        linkUp.appearOffline = word == "offline";
        refreshLinkUpScreen();
    } else if (!word.empty()) std::fprintf(stderr, "[linkup] script: no step %s\n", word.c_str());
    if (!done) return;
    std::fprintf(stderr, "[linkup] script: %s\n", word.c_str());
    linkupScript = semi == std::string::npos ? std::string() : linkupScript.substr(semi + 1);
}

// Every frame, before anything is drawn.
void App::tickLinkUpLive(float dt) {
    if (linkupGallery) return;
    runLinkUpScript();
    linkupFollowAccount();
    linkupFollowLink();
    linkupFollowStatus();
    linkupFollowGame();
    tickLinkUpSession(dt);
    linkupSendPad(dt);
    renderer.setTransparentBackground(linkupPictureBehind() || (standaloneRun.active() && overlayOpen) ||
                                      (standaloneRun.active() && standalonePaused));
    // A game still downloading says how far.
    if (linkupSide == LinkupSide::Host && here() == Screen::LinkUpPlay) {
        const int64_t total = launchJob.total.load();
        const int64_t got = launchJob.got.load();
        if (launchJob.romId == linkupGameId && launchJob.busy() && total > 0 && got < total)
            linkupPlay.setNote("Downloading " + std::to_string(got * 100 / total) + "%");
        else
            linkupPlay.setNote("");
    }
}
