// Reaching a RomM server and pairing with it: the address and its probe
// (setAddress, looksLocal), the token read back from an account's file
// (loadToken), the device flow and the scopes it asks for (beginPairing,
// pollPairing), and who the token belongs to (fetchCurrentUser). Moved out of
// romm.cpp in the restructure (#398).

#include "romm.h"

#include "romm_internal.h"

#include <json-c/json.h>

#include <cstdio>
#include <mutex>

namespace romm {

namespace {

// What the console asks for, and where the line is drawn.
//
// READ: everything needed to show a library and start a game — the library
// itself, artwork, collections, and firmware. firmware.read is not optional:
// about half the systems here cannot start a game without a BIOS, and without
// it they fail pointing at a file the person has no way to supply.
//
// WRITE: only the person's own play data. assets.write uploads saves, memory
// cards and save states, which is the whole point of the server holding them —
// a console that could only download would lose progress the moment it
// reclaimed a game. roms.user.write is play state. collections.write is
// favourites: on RomM 5 a favourite is a member of the person's flagged
// collection, and adding to it needs this scope, not roms.user.write (found
// building #267; a console paired before then has to pair again to set one).
//
// NOT ASKED FOR, deliberately: roms.write, platforms.write and firmware.write
// (the console must never alter the library it is reading), users.*, tasks.run
// and logs.read. The line is that this can change YOUR data and never THE
// library.
//
// Two rounds of under-scoping got here. "Ask again when something needs it" is
// right for genuinely occasional access and wrong for a requirement, and each
// mistake costs a re-pairing that a person has to walk across a room for.
const char* kScopes[] = {
    "me.read", "platforms.read", "roms.read", "assets.read",
    "roms.user.read", "collections.read", "firmware.read",
    "assets.write", "roms.user.write", "collections.write",
};

}  // namespace

bool looksLocal(const std::string& host) {
    std::string h = host;
    auto colon = h.find(':');
    if (colon != std::string::npos) h = h.substr(0, colon);
    if (h.find('.') == std::string::npos) return true;         // bare hostname
    if (h.size() > 6 && h.compare(h.size() - 6, 6, ".local") == 0) return true;
    if (h.size() > 4 && h.compare(h.size() - 4, 4, ".lan") == 0) return true;
    if (h.compare(0, 4, "127.") == 0) return true;
    if (h.compare(0, 3, "10.") == 0) return true;
    if (h.compare(0, 8, "192.168.") == 0) return true;
    // 172.16.0.0/12
    if (h.compare(0, 4, "172.") == 0) {
        int second = atoi(h.c_str() + 4);
        if (second >= 16 && second <= 31) return true;
    }
    // 100.64.0.0/10, Tailscale's addresses (#479): a RomM on a tailnet is
    // the person's own, and speaks http as one on the LAN does.
    if (h.compare(0, 4, "100.") == 0) {
        int second = atoi(h.c_str() + 4);
        if (second >= 64 && second <= 127) return true;
    }
    return false;
}

// THE CANDIDATE IS ASKED WITHOUT BECOMING THE ADDRESS, and is made the
// address only once it has answered, so a request on another thread never
// goes to a candidate that is about to be put back (#398, batch i).
//
// THE SERVER MAY SEND THE CONSOLE ELSEWHERE, ONCE, HERE (#380). A reverse
// proxy that moves http to https, or an address typed as a number to the
// server's name, answers the heartbeat with a redirect; the address it names
// is then the server's, for every request after. Nowhere else is a redirect
// off the server followed, so this is the only place the console's server
// can change, and only to where the server itself said.
bool Client::probe(const std::string& candidate, bool report, std::string* why) {
    Where at = where();
    at.base = candidate;
    std::string body;
    std::string movedTo;
    static const std::string kHeartbeat = "/api/heartbeat";
    for (int moves = 0;; ++moves) {
        movedTo.clear();
        if (getAt(at, kHeartbeat, &body, why, report, &movedTo)) break;
        if (moves >= 2 || movedTo.size() <= kHeartbeat.size() ||
            movedTo.compare(movedTo.size() - kHeartbeat.size(), kHeartbeat.size(), kHeartbeat) != 0)
            return false;
        at.base = movedTo.substr(0, movedTo.size() - kHeartbeat.size());
    }

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) return false;
    json_object* sys = nullptr;
    const bool hasSystem = json_object_object_get_ex(root, "SYSTEM", &sys);
    const std::string version = hasSystem ? jstr(sys, "VERSION") : std::string();
    json_object_put(root);
    std::lock_guard<std::mutex> lock(mu_);
    if (at.base != candidate && at.base != base_)
        std::fprintf(stderr, "[romm] the server sent the console to another address; "
                             "that one is used\n");
    base_ = at.base;
    if (hasSystem) serverVersion_ = version;
    return true;
}

bool Client::setAddress(const std::string& address, std::string* err) {
    std::string a = address;
    while (!a.empty() && (a.back() == '/' || a.back() == ' ')) a.pop_back();
    if (a.empty()) { if (err) *err = "no address given"; return false; }

    // An explicit scheme is an instruction, not a hint. Honour it and do not
    // second-guess by trying the other one.
    if (a.compare(0, 7, "http://") == 0 || a.compare(0, 8, "https://") == 0) {
        std::string why;
        if (probe(a, true, &why)) return true;
        // THE REASON IS SAID, not dropped: a certificate that is not trusted, a
        // name that does not resolve and a port that refuses are three different
        // faults, and "nothing answered" named none of them.
        std::fprintf(stderr, "[romm] %s: %s\n", a.c_str(), why.empty() ? "no reason given" : why.c_str());
        if (err) *err = "nothing answered at " + a + (why.empty() ? "" : " (" + why + ")");
        return false;
    }

    // No scheme. Try the likelier one first — http for a LAN address, because
    // that is what a self-hosted RomM usually speaks, and refusing it is the
    // exact bug Phase 4 exists to not have.
    //
    // THE FIRST TRY SAYS NOTHING ABOUT THE SERVER (#479). A scheme the server
    // does not speak refuses, or is dropped until the timeout, and that used
    // to mark the whole console's server away until the second answered: the
    // Offline chip at every start, every minute's heartbeat and every
    // reconnect. Only the second try, which settles it, reports.
    const bool local = looksLocal(a);
    const std::string first  = (local ? "http://" : "https://") + a;
    const std::string second = (local ? "https://" : "http://") + a;
    std::string whyFirst, whySecond;
    if (probe(first, /*report=*/false, &whyFirst)) return true;
    if (probe(second, true, &whySecond)) return true;
    std::fprintf(stderr, "[romm] %s: %s: %s; %s: %s\n", a.c_str(), first.c_str(), whyFirst.c_str(),
                 second.c_str(), whySecond.c_str());
    if (err) {
        const std::string s1 = local ? "http" : "https", s2 = local ? "https" : "http";
        *err = "nothing answered at " + a + " over http or https (" + s1 + ": " +
               (whyFirst.empty() ? "no reason" : whyFirst) + "; " + s2 + ": " +
               (whySecond.empty() ? "no reason" : whySecond) + ")";
    }
    return false;
}

bool Client::loadToken(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string body;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    fclose(f);

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) return false;
    std::string t = jstr(root, "access_token");
    json_object_put(root);
    if (t.empty()) return false;
    std::lock_guard<std::mutex> lock(mu_);
    token_ = std::move(t);
    deviceId_.clear();   // a device belongs to a token
    return true;
}

bool Client::beginPairing(Pairing* out, std::string* err) {
    json_object* req = json_object_new_object();
    json_object_object_add(req, "client_device_identifier",
                           json_object_new_string("cabinetos"));
    json_object_object_add(req, "name", json_object_new_string("CabinetOS"));
    json_object_object_add(req, "client", json_object_new_string("cabinetos"));
    json_object_object_add(req, "platform", json_object_new_string("linux"));
    json_object* scopes = json_object_new_array();
    for (const char* s : kScopes) json_object_array_add(scopes, json_object_new_string(s));
    json_object_object_add(req, "requested_scopes", scopes);
    const std::string payload = json_object_to_json_string(req);
    json_object_put(req);

    std::string body;
    long status = 0;
    if (!postJson("/api/auth/device/init", payload, &body, &status, err)) return false;
    if (status < 200 || status >= 300) {
        if (err) *err = "HTTP " + std::to_string(status) + " starting pairing";
        return false;
    }

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "pairing response was not JSON"; return false; }
    out->userCode = jstr(root, "user_code");
    out->deviceCode = jstr(root, "device_code");
    out->expiresIn = static_cast<int>(jint(root, "expires_in"));
    out->intervalSeconds = static_cast<int>(jint(root, "interval"));
    if (out->intervalSeconds <= 0) out->intervalSeconds = 5;
    out->verificationUrl = baseUrl() + jstr(root, "verification_path_complete");
    json_object_put(root);

    if (out->userCode.empty() || out->deviceCode.empty()) {
        if (err) *err = "pairing response was missing its codes";
        return false;
    }
    return true;
}

int Client::pollPairing(const Pairing& p, std::string* err) {
    json_object* req = json_object_new_object();
    json_object_object_add(req, "device_code", json_object_new_string(p.deviceCode.c_str()));
    const std::string payload = json_object_to_json_string(req);
    json_object_put(req);

    std::string body;
    long status = 0;
    if (!postJson("/api/auth/device/token", payload, &body, &status, err)) return -1;

    // RomM answers 400 to every poll that has no token yet and says why in
    // `detail` (backend/endpoints/device_auth.py, the same from 5.1.0 to
    // 5.4.0): authorization_pending and slow_down are still waiting, the
    // common case for as long as it takes somebody to pick up a phone;
    // expired_token (ten minutes, or a minute after a deny) and access_denied
    // are the end of this code.
    if (status == 400 || status == 428) {
        std::string detail;
        if (json_object* root = json_tokener_parse(body.c_str())) {
            detail = jstr(root, "detail");
            json_object_put(root);
        }
        if (detail == "expired_token") {
            if (err) *err = "the code expired";
            return -2;
        }
        if (detail == "access_denied") {
            if (err) *err = "the pairing was refused";
            return -2;
        }
        return 0;
    }
    if (status == 404 || status == 410) {
        if (err) *err = "the pairing expired before it was approved";
        return -1;
    }
    if (status < 200 || status >= 300) {
        if (err) *err = "HTTP " + std::to_string(status) + " while pairing";
        return -1;
    }

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "token response was not JSON"; return -1; }
    std::string t = jstr(root, "access_token");
    json_object_put(root);
    if (t.empty()) return 0;   // approved-but-empty should not happen; treat as pending
    std::lock_guard<std::mutex> lock(mu_);
    token_ = std::move(t);
    deviceId_.clear();   // a device belongs to a token
    return 1;
}

bool Client::fetchCurrentUser(User* out, std::string* err) {
    std::string body;
    if (!get("/api/users/me", &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        if (err) *err = "users/me response was not an object";
        return false;
    }
    // Hand-written and deliberately partial, the same rule as every other
    // response here: decode the fields used and ignore the rest, because
    // generating from openapi.json is what makes a client break across RomM
    // releases. This one carries an email, a role, an avatar and a page of UI
    // preferences, and none of them are this console's business.
    out->id = static_cast<int>(jint(root, "id"));
    out->username = jstr(root, "username");
    out->createdAt = jstr(root, "created_at");
    // The device this token is (5.3.1 and 5.4.0 both say), kept for the
    // play-session list so it need not be asked again (#401, deviceId).
    out->deviceId = jstr(root, "current_device_id");
    if (!out->deviceId.empty()) {
        std::lock_guard<std::mutex> lock(mu_);
        deviceId_ = out->deviceId;
    }
    // `avatar_path` is used only as a FLAG — it says whether this person has a
    // picture at all. Its value is a path into RomM's asset tree that nothing
    // serves; see the note on User::avatarPath. The endpoint is built from the
    // id instead, which is the only thing that answers.
    if (!jstr(root, "avatar_path").empty() && jint(root, "id") > 0)
        out->avatarPath = "/api/users/" + std::to_string(jint(root, "id")) + "/avatar";
    json_object_put(root);
    if (out->id <= 0) {
        if (err) *err = "users/me carried no id";
        return false;
    }
    return true;
}

bool Client::deviceId(std::string* out, std::string* err) const {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!deviceId_.empty()) { *out = deviceId_; return true; }
    }
    std::string body;
    if (!get("/api/users/me", &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    const std::string id = root ? jstr(root, "current_device_id") : std::string();
    if (root) json_object_put(root);
    if (id.empty()) {
        if (err) *err = "users/me named no device";
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    deviceId_ = id;
    *out = id;
    return true;
}

}  // namespace romm
