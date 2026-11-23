#include "tokenrevoke.h"

#include <json-c/json.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <vector>

#include "fileio.h"
#include "romm.h"

namespace tokenrevoke {
namespace {

struct Owed {
    accounts::Login login;
    std::string who;
};

// Guards the file: a re-pairing's thread and the frame thread add to it while
// the reconnect thread works through it. Never held over a request.
std::mutex gFile;

std::vector<Owed> read() {
    std::vector<Owed> out;
    const std::vector<uint8_t> bytes = cab::readBytes(accounts::revokePath());
    json_object* root = json_tokener_parse(std::string(bytes.begin(), bytes.end()).c_str());
    if (!root) return out;
    if (json_object_get_type(root) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(root); ++i) {
            json_object* e = json_object_array_get_idx(root, i);
            json_object* v = nullptr;
            Owed o;
            if (json_object_object_get_ex(e, "access_token", &v))
                o.login.token = json_object_get_string(v);
            if (json_object_object_get_ex(e, "token_id", &v))
                o.login.tokenId = json_object_get_int(v);
            if (json_object_object_get_ex(e, "name", &v)) o.who = json_object_get_string(v);
            if (!o.login.token.empty() && o.login.tokenId > 0) out.push_back(std::move(o));
        }
    }
    json_object_put(root);
    return out;
}

bool write(const std::vector<Owed>& list) {
    if (list.empty()) return cab::removeFile(accounts::revokePath());
    json_object* root = json_object_new_array();
    for (const Owed& o : list) {
        json_object* e = json_object_new_object();
        json_object_object_add(e, "access_token", json_object_new_string(o.login.token.c_str()));
        json_object_object_add(e, "token_id", json_object_new_int(o.login.tokenId));
        json_object_object_add(e, "name", json_object_new_string(o.who.c_str()));
        json_object_array_add(root, e);
    }
    std::string body = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);
    body += '\n';
    return cab::writeBytes(accounts::revokePath(), body, 0600);
}

void settled(const std::string& token) {
    std::lock_guard<std::mutex> lock(gFile);
    std::vector<Owed> list = read();
    std::erase_if(list, [&](const Owed& o) { return o.login.token == token; });
    if (!write(list))
        std::fprintf(stderr, "[revoke] could not write what is still owed\n");
}

void sayNoNumber(const std::string& who) {
    std::fprintf(stderr, "[revoke] %s's token has no number here (paired before the console kept "
                         "it); it stays on RomM until removed there\n", who.c_str());
}

// One token, by itself. False when RomM did not answer.
bool revoke(romm::Client& client, const Owed& o) {
    client.useToken(o.login.token);
    std::string why;
    const char* what = "revoked on RomM";
    bool answered = true;
    switch (client.revokeOwnToken(o.login.tokenId, &why)) {
        case romm::Client::Revoke::Done: break;
        case romm::Client::Revoke::Gone: what = "already gone from RomM: "; break;
        case romm::Client::Revoke::Refused:
            what = "not revoked, RomM refused; it stays there until removed there: ";
            break;
        case romm::Client::Revoke::NoAnswer:
            what = "not revoked, RomM did not answer: ";
            answered = false;
            break;
    }
    std::fprintf(stderr, "[revoke] %s's token %d %s%s\n", o.who.c_str(), o.login.tokenId, what,
                 why.c_str());
    return answered;
}

}  // namespace

void owe(const accounts::Login& login, const std::string& who) {
    if (login.token.empty()) return;
    if (login.tokenId <= 0) {
        sayNoNumber(who);
        return;
    }
    std::lock_guard<std::mutex> lock(gFile);
    std::vector<Owed> list = read();
    for (const Owed& o : list)
        if (o.login.token == login.token) return;
    list.push_back({login, who});
    if (!write(list))
        std::fprintf(stderr, "[revoke] %s's token %d could not be written down to revoke; it "
                             "stays on RomM until removed there\n", who.c_str(), login.tokenId);
}

bool sendOwed(const std::string& address, const std::atomic<bool>* stop) {
    std::vector<Owed> list;
    {
        std::lock_guard<std::mutex> lock(gFile);
        list = read();
    }
    if (list.empty()) return true;
    // SOMEBODY HERE SIGNS IN WITH IT: the power went, or a re-pairing is right
    // now, between this list and the login. It stays on the list, so whichever
    // it was, it goes once the login is another.
    std::set<std::string> logins;
    for (const accounts::Account& a : accounts::all()) logins.insert(accounts::login(a.id).token);
    std::erase_if(list, [&](const Owed& o) {
        return logins.count(o.login.token) > 0 || romm::tokenHeld(o.login.token);
    });
    if (list.empty()) return true;
    romm::Client client;
    client.giveUpBy(std::chrono::steady_clock::now() + std::chrono::seconds(kSendOwedSeconds));
    std::string err;
    if (!client.setAddress(address, &err)) return false;
    for (const Owed& o : list) {
        if (stop && stop->load()) return true;
        if (!revoke(client, o)) return false;
        settled(o.login.token);
    }
    return true;
}

void signOut(const std::string& address) {
    std::vector<Owed> list;
    {
        std::lock_guard<std::mutex> lock(gFile);
        list = read();
    }
    for (const accounts::Account& a : accounts::all()) {
        const accounts::Login login = accounts::login(a.id);
        if (login.token.empty()) continue;
        if (login.tokenId <= 0) {
            sayNoNumber(a.name);
            continue;
        }
        const bool owed = std::any_of(list.begin(), list.end(), [&](const Owed& o) {
            return o.login.token == login.token;
        });
        if (!owed) list.push_back({login, a.name});
    }
    if (list.empty()) return;
    const auto t0 = std::chrono::steady_clock::now();
    romm::Client client;
    client.giveUpBy(t0 + std::chrono::seconds(kSignOutSeconds));
    std::string err = "no address";
    size_t done = 0;
    if (!address.empty() && client.setAddress(address, &err)) {
        while (done < list.size() && revoke(client, list[done])) ++done;
    } else {
        std::fprintf(stderr, "[revoke] no server to revoke on: %s\n", err.c_str());
    }
    std::fprintf(stderr, "[revoke] sign out: %zu of %zu token(s) answered for, %lld ms\n", done,
                 list.size(),
                 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0).count()));
}

}  // namespace tokenrevoke
