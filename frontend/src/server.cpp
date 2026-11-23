#include "server.h"

#include <cstdio>
#include <cstdlib>

#include "accounts.h"
#include "covercache.h"
#include "firstrun.h"
#include "pending.h"
#include "romm.h"
#include "storage.h"

namespace server {

Check check(const std::string& address, std::string* detail) {
    romm::Client c;
    std::string err;
    if (!c.setAddress(address, &err)) {
        if (detail) *detail = err;
        return Check::NoServer;
    }
    if (!accounts::loadActiveToken(c)) {
        if (detail) *detail = "no account is signed in";
        return Check::Failed;
    }
    romm::User me;
    if (!c.fetchCurrentUser(&me, &err)) {
        if (detail) *detail = err;
        // A 401 or 403: it speaks RomM and does not know this token.
        return err.rfind("not authorised", 0) == 0 ? Check::Different : Check::Failed;
    }
    // Accepted, as somebody else. Only possible if two servers share a
    // signing secret; still not the same server as far as this console's
    // saves are concerned.
    if (me.id != accounts::activeId()) {
        if (detail) *detail = "the token belongs to user " + std::to_string(me.id) + " there";
        return Check::Different;
    }
    return Check::Same;
}

bool addressIsOurs() {
    const std::string src = firstrun::serverAddressSource();
    return src == "config/server.json" || src == "nowhere";
}

bool changeAddress(const std::string& from, const std::string& to, std::string* err) {
    if (!firstrun::setServerAddress(to, err)) return false;
    if (firstrun::serverAddress() != to) {
        if (err) *err = "the address is set in " + firstrun::serverAddressSource();
        return false;
    }
    // THE COVERS ARE FILED BY ADDRESS (covercache.h), so a new address would
    // otherwise fetch every one again. Same server, same art: move them.
    const std::string oldDir = covercache::dir() + "/" + storage::safeSegment(from);
    const std::string newDir = covercache::dir() + "/" + storage::safeSegment(to);
    if (oldDir != newDir && storage::exists(oldDir) && !storage::exists(newDir)) {
        if (::rename(oldDir.c_str(), newDir.c_str()) != 0)
            std::fprintf(stderr, "[server] could not move the covers to %s\n", newDir.c_str());
    }
    std::fprintf(stderr, "[server] address %s -> %s\n", from.c_str(), to.c_str());
    return true;
}

int unsentSaves() {
    int n = 0;
    for (const storage::User& u : storage::knownUsers()) n += cache::pendingCount(u);
    return n;
}

bool signOut(std::string* err) {
    std::fprintf(stderr, "[sign out] signing out of %s; %d unsent save(s)\n",
                 firstrun::serverAddress().c_str(), unsentSaves());
    return firstrun::markSignedOut(/*cleared=*/false, err);
}

void finishSignOut() {
    int64_t bytes = 0;
    auto remove = [&](const std::string& path) {
        if (!storage::exists(path)) return;
        const int64_t b = storage::treeBytes(path);
        if (storage::removeEntry(path)) bytes += b;
        else std::fprintf(stderr, "[sign out] could not remove all of %s\n", path.c_str());
    };
    // Kept and cached games, on every drive plugged in now.
    for (const std::string& loc : storage::locations()) {
        remove(storage::romsDir(loc));
        remove(storage::cacheDir(loc));
    }
    // Everyone's saves, states, screenshots, keeps and pending markers.
    remove(storage::root() + "/users");
    remove(covercache::dir());
    remove(storage::configDir() + "/user.json");
    // What the console learned about that server's games, filed by rom id:
    // their records, the codes read off their files (wii.cpp, wiiu.cpp) and
    // the per-game picture and widescreen choices (quality.cpp, ps2.cpp).
    // RomM numbers every server's games from 1, so the next server's games
    // would take them over (#398, FILES-12).
    remove(storage::gamesDir());
    for (const char* f : {"wii-codes.txt", "wiiu-codes.txt", "picture-quality.json",
                          "ps2-widescreen.json"})
        remove(storage::configDir() + "/" + f);
    accounts::forgetEveryone();
    firstrun::forgetServerAddress();
    // The empty folders back, so the tree is what a new console has.
    std::string err;
    if (!storage::ensureTree(&err))
        std::fprintf(stderr, "[sign out] could not rebuild the folders: %s\n", err.c_str());
    if (!firstrun::markSignedOut(/*cleared=*/true, &err))
        std::fprintf(stderr, "[sign out] could not record it: %s\n", err.c_str());
    std::fprintf(stderr, "[sign out] cleared %.2f GB; first run from the server step\n",
                 bytes / 1e9);
}

}  // namespace server
