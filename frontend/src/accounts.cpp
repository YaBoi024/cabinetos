#include "accounts.h"

#include <json-c/json.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fileio.h"
#include "romm.h"
#include "storage.h"

namespace accounts {
namespace {

std::string homeDir() {
    const char* h = getenv("HOME");
    return std::string(h && *h ? h : ".");
}

// 0700 the whole way down, because what goes in it is a credential. THE
// DIRECTORY HAS TO BE MADE, AND NOT MAKING IT COST A PAIRING ON THE FIRST
// CONSOLE EVER INSTALLED: `~/.config/cabinetos/` does not exist on a machine
// nobody has configured, which is exactly the machine that is pairing. The
// token writer before accounts opened the file without making the directory,
// the pairing the person had just approved in a browser was thrown away, and
// the console went on showing the stand-in library (found 2026-09-19, on the
// A9 Pro, ten minutes after it first booted; the test VM never showed it
// because that directory had been created there by hand weeks earlier).
void makeDirs0700(const std::string& dir) {
    std::string built;
    size_t at = 0;
    while (at < dir.size()) {
        const size_t next = dir.find('/', at + 1);
        built = dir.substr(0, next == std::string::npos ? dir.size() : next);
        if (!built.empty()) ::mkdir(built.c_str(), S_IRWXU);
        if (next == std::string::npos) break;
        at = next;
    }
}

std::string readFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string body;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    std::fclose(f);
    return body;
}

bool fileExists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

std::string jstr(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (o && json_object_object_get_ex(o, key, &v) && v)
        if (const char* s = json_object_get_string(v)) return s;
    return {};
}

int jint(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (o && json_object_object_get_ex(o, key, &v) && v) return json_object_get_int(v);
    return 0;
}

// The whole file in one read. The list and the active id travel together
// because they are only ever correct together: an active id naming an account
// that is not in the list is the state this avoids by construction.
struct Book {
    std::vector<Account> list;
    int active = 0;
};

Book load() {
    Book b;
    const std::string body = readFile(listPath());
    if (body.empty()) return b;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root) return b;

    b.active = jint(root, "active");
    json_object* arr = nullptr;
    if (json_object_object_get_ex(root, "accounts", &arr) && arr &&
        json_object_get_type(arr) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(arr); ++i) {
            json_object* e = json_object_array_get_idx(arr, i);
            Account a;
            a.id = jint(e, "id");
            a.name = jstr(e, "name");
            a.avatar = jstr(e, "avatar");
            a.colour = jstr(e, "colour");
            if (a.valid()) b.list.push_back(std::move(a));
        }
    }
    json_object_put(root);

    // An active id that names nobody is corruption, not a state to honour.
    // Falling back to the first account is better than acting as nobody, and
    // better than picking up a stranger's id from a half-written file.
    if (b.active != 0) {
        bool found = false;
        for (const Account& a : b.list) if (a.id == b.active) { found = true; break; }
        if (!found) b.active = b.list.empty() ? 0 : b.list.front().id;
    }
    return b;
}

bool save(const Book& b, std::string* err) {
    makeDirs0700(storage::configDir());

    json_object* root = json_object_new_object();
    json_object_object_add(root, "active", json_object_new_int(b.active));
    json_object* arr = json_object_new_array();
    for (const Account& a : b.list) {
        json_object* e = json_object_new_object();
        json_object_object_add(e, "id", json_object_new_int(a.id));
        json_object_object_add(e, "name", json_object_new_string(a.name.c_str()));
        json_object_object_add(e, "avatar", json_object_new_string(a.avatar.c_str()));
        if (!a.colour.empty())
            json_object_object_add(e, "colour", json_object_new_string(a.colour.c_str()));
        json_object_array_add(arr, e);
    }
    json_object_object_add(root, "accounts", arr);

    std::string body = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    body += '\n';
    json_object_put(root);

    // Not a credential: this is the same tier as config/user.json.
    if (!cab::writeBytes(listPath(), body, 0644)) {
        if (err) *err = "could not write " + listPath();
        return false;
    }
    return true;
}

std::string pinPath() { return homeDir() + "/.config/cabinetos/pin"; }

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\r' || s[a] == '\t')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\n' || s[b-1] == '\r' || s[b-1] == '\t')) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::string listPath() { return storage::configDir() + "/accounts.json"; }

std::string tokenPath(int id) {
    return homeDir() + "/.config/cabinetos/accounts/" + std::to_string(id) + ".json";
}

std::vector<Account> all() { return load().list; }

int activeId() { return load().active; }

const Account* find(const std::vector<Account>& list, int id) {
    for (const Account& a : list) if (a.id == id) return &a;
    return nullptr;
}

bool add(const Account& a, const std::string& token, std::string* err) {
    if (!a.valid()) {
        if (err) *err = "an account needs the id RomM gave it";
        return false;
    }
    if (token.empty()) {
        // Refused rather than written, because the list is what firstrun's
        // `alreadyConfigured` and `havePairedAccount` and the switcher read:
        // an entry with no credential behind it is a row that cannot be chosen
        // and a console that may think it is configured when it is not.
        if (err) *err = "refusing to add an account with no token";
        return false;
    }

    Book b = load();
    for (Account& existing : b.list) {
        if (existing.id != a.id) continue;
        // Re-pairing somebody who is already here replaces their token and
        // refreshes their name rather than making a second row. Two rows for
        // one person is the fault that would follow from treating this as an
        // error, and it is worse than the duplicate add.
        existing.name = a.name;
        existing.avatar = a.avatar;
        makeDirs0700(homeDir() + "/.config/cabinetos/accounts");
        if (!cab::writeBytes(tokenPath(a.id), "{\"access_token\":\"" + token + "\"}\n", 0600)) {
            if (err) *err = "could not write the token for account " + std::to_string(a.id);
            return false;
        }
        return save(b, err);
    }

    makeDirs0700(homeDir() + "/.config/cabinetos/accounts");
    if (!cab::writeBytes(tokenPath(a.id), "{\"access_token\":\"" + token + "\"}\n", 0600)) {
        if (err) *err = "could not write the token for account " + std::to_string(a.id);
        return false;
    }
    b.list.push_back(a);
    // The first account to arrive on a console with none becomes the active
    // one, because there is nobody for it to take over from. Every later one
    // is added without switching.
    if (b.active == 0) b.active = a.id;
    return save(b, err);
}

bool canRemove(int id, std::string* err) {
    const Book b = load();
    if (id == b.active) {
        if (err) *err = "cannot remove the account this console is signed in as";
        return false;
    }
    if (!b.list.empty() && id == b.list.front().id) {
        if (err) *err = "cannot remove the console's owner";
        return false;
    }
    if (!find(b.list, id)) {
        if (err) *err = "no account with id " + std::to_string(id);
        return false;
    }
    return true;
}

bool remove(int id, std::string* err) {
    Book b = load();
    if (id == b.active) {
        if (err) *err = "cannot remove the account this console is signed in as";
        return false;
    }
    // THE OWNER STAYS. Ownership is "first in the list" (ownerId), so removing
    // them would hand the console, and the PIN's controls, to whoever was
    // added next without anybody deciding that.
    if (!b.list.empty() && id == b.list.front().id) {
        if (err) *err = "cannot remove the console's owner";
        return false;
    }
    const size_t before = b.list.size();
    for (size_t i = 0; i < b.list.size(); ++i) {
        if (b.list[i].id != id) continue;
        b.list.erase(b.list.begin() + static_cast<long>(i));
        break;
    }
    if (b.list.size() == before) {
        if (err) *err = "no account with id " + std::to_string(id);
        return false;
    }
    // The list first, then the token: if the list cannot be written the
    // account is still whole, rather than listed with no credential.
    if (!save(b, err)) return false;
    ::unlink(tokenPath(id).c_str());
    ::unlink(raPath(id).c_str());
    const std::string ra = homeDir() + "/.config/cabinetos/accounts/" + std::to_string(id) +
                           ".retroachievements";
    if (storage::exists(ra)) storage::removeEntry(ra);
    return true;
}

bool setActive(int id, std::string* err) {
    Book b = load();
    if (!find(b.list, id)) {
        if (err) *err = "no account with id " + std::to_string(id);
        return false;
    }
    if (!fileExists(tokenPath(id))) {
        // The list and the tokens can disagree if somebody has been in here by
        // hand. Refusing is right: switching to an account with no credential
        // would sign the console out with no way to say why.
        if (err) *err = "account " + std::to_string(id) + " has no token on disk";
        return false;
    }
    b.active = id;
    return save(b, err);
}

bool setColour(int id, const std::string& colour) {
    Book b = load();
    for (Account& a : b.list) {
        if (a.id != id) continue;
        a.colour = colour;
        return save(b, nullptr);
    }
    return false;
}

bool activate(int id, romm::Client& client, std::string* err) {
    if (!setActive(id, err)) return false;
    if (!client.loadToken(tokenPath(id))) {
        if (err) *err = "account " + std::to_string(id) + "'s token would not load";
        return false;
    }
    return true;
}

bool loadActiveToken(romm::Client& client) {
    const int id = activeId();
    if (id <= 0) return false;
    return client.loadToken(tokenPath(id));
}

bool recordPairing(romm::Client& client, Paired* out, std::string* err) {
    if (out) *out = Paired{};
    if (!client.haveToken()) {
        if (err) *err = "recordPairing was given a client with no token";
        return false;
    }
    // ASKED, NOT ASSUMED. The id decides where this person's saves go.
    romm::User me;
    std::string e;
    if (!client.fetchCurrentUser(&me, &e) || me.id <= 0) {
        if (err)
            *err = "paired, but the server would not say who the token belongs to" +
                   (e.empty() ? std::string() : " (" + e + ")");
        return false;
    }
    // ASKED BEFORE THE WRITE, because `add` replaces in place and afterwards
    // there is no way to tell the two cases apart.
    const std::vector<Account> before = all();
    const bool already = find(before, me.id) != nullptr;

    Account a;
    a.id = me.id;
    a.name = me.username;
    a.avatar = me.avatarPath;
    if (!add(a, client.token(), err)) return false;
    if (out) { out->id = me.id; out->name = me.username; out->isNew = !already; }
    return true;
}

// --- RetroAchievements (#74) -----------------------------------------------

std::string raPath(int id) {
    return homeDir() + "/.config/cabinetos/accounts/" + std::to_string(id) +
           ".retroachievements.json";
}

std::string raDir(int id) {
    const std::string d = homeDir() + "/.config/cabinetos/accounts/" + std::to_string(id) +
                          ".retroachievements";
    makeDirs0700(d);
    return d;
}

bool raLogin(int id, std::string* username, std::string* token) {
    json_object* o = json_tokener_parse(readFile(raPath(id)).c_str());
    if (!o) return false;
    const std::string u = jstr(o, "username"), t = jstr(o, "token");
    json_object_put(o);
    if (u.empty() || t.empty()) return false;
    if (username) *username = u;
    if (token) *token = t;
    return true;
}

bool setRaLogin(int id, const std::string& username, const std::string& token) {
    if (id <= 0 || username.empty() || token.empty()) return false;
    // json-c writes it, not concatenation: a RetroAchievements username is
    // the person's own text, and a quote in it must not break the file.
    json_object* o = json_object_new_object();
    json_object_object_add(o, "username", json_object_new_string(username.c_str()));
    json_object_object_add(o, "token", json_object_new_string(token.c_str()));
    std::string body = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
    body += '\n';
    json_object_put(o);
    makeDirs0700(homeDir() + "/.config/cabinetos/accounts");
    return cab::writeBytes(raPath(id), body, 0600);
}

void clearRaLogin(int id) { ::unlink(raPath(id).c_str()); }

int ownerId() {
    const std::vector<Account> list = all();
    return list.empty() ? 0 : list.front().id;
}

bool pinIsSet() { return !trimmed(readFile(pinPath())).empty(); }

bool setPin(const std::string& pin, std::string* err) {
    const std::string value = trimmed(pin);
    if (value.empty()) {
        ::unlink(pinPath().c_str());
        return true;
    }
    makeDirs0700(homeDir() + "/.config/cabinetos");
    if (!cab::writeBytes(pinPath(), value + "\n", 0600)) {
        if (err) *err = "could not write " + pinPath();
        return false;
    }
    return true;
}

bool checkPin(const std::string& pin) {
    const std::string stored = trimmed(readFile(pinPath()));
    if (stored.empty()) return false;
    return trimmed(pin) == stored;
}

bool forgetEveryone() {
    const std::string dir = homeDir() + "/.config/cabinetos";
    bool ok = true;
    for (const std::string& p : {listPath(), dir + "/accounts", pinPath()})
        if (storage::exists(p) && !storage::removeEntry(p)) {
            std::fprintf(stderr, "[sign out] could not remove %s\n", p.c_str());
            ok = false;
        }
    return ok;
}

}  // namespace accounts

// Who this console is acting as, settled before anything writes a save.
//
// Saves, states and the decision to keep a game all live under a person now, so
// this is not a nicety: without an answer there is no directory to write into.
// The answer comes from RomM's `/api/users/me` and is cached on disk, because a
// console with no network still has to know whose saves it is holding.
bool adoptUser(romm::Client& client) {
    std::string err;
    if (storage::resolveCurrentUser(client, &err)) {
        std::fprintf(stderr, "[storage] user %s\n",
                     storage::currentUser().dirName().c_str());
        return true;
    }
    std::fprintf(stderr,
                 "[storage] no RomM user (%s) — saves and keeps have nobody to "
                 "belong to until this console is paired\n", err.c_str());
    return false;
}

// WHO THE CONSOLE IS, WITH NO SERVER TO ASK (#88): the account's own id,
// name and picture, from accounts.json. Switching people works offline
// (MMagTech, 2026-10-05); /api/users/me is only how an online console checks.
bool adoptAccountOffline(int id) {
    const std::vector<accounts::Account> known = accounts::all();
    const accounts::Account* a = accounts::find(known, id);
    if (!a) return false;
    storage::User u;
    u.id = a->id;
    u.name = a->name;
    u.avatar = a->avatar;
    storage::setCurrentUser(u);
    std::fprintf(stderr, "[storage] user %s (offline)\n", u.dirName().c_str());
    return true;
}
