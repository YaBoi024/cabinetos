#include "favorites.h"

#include <json-c/json.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>

#include "fileio.h"

#include "app.h"

// FAVOURITES NOT YET ON THE SERVER (#267). The heart on a game's page answers
// at once; what the server still has to hear is kept per person in
// `favorites-owed.json`, rom id to wanted state, and sent by the uploader
// whenever it sends anything owed. The last press wins: pressing twice while
// the server is away leaves nothing to send.
//
// THE PERSON'S LIST, `favorites.json` (#88): their favourites in Home's
// order, rom ids, kept for the offline Home and a game page's heart. This
// file is its only reader and writer (#405).
//
// ONE LOCK FOR BOTH FILES (#405): the frame thread presses, the uploader's
// worker sends, and loadLibrary rewrites the list on the online reload's
// worker as well as on the frame thread, where a refresh does too (#730).
// Each change is made whole under the lock, so two writers never share the
// `.part` file, and a press during a reload is not written over by the list
// the reload read before it.
static std::mutex gFavMutex;

// PEOPLE RomM WILL NOT MAKE A FAVOURITES COLLECTION FOR (#474), by folder,
// under the lock. Someone who already owns an ordinary collection named
// Favorites has none flagged, and RomM refuses the one the first heart asks
// for. Asking every five minutes changes nothing, so it is said once and
// not asked again until their next heart or the next start; the hearts stay
// on this console and owed meanwhile.
static std::set<std::string> gFavParked;

static std::string listPath(const storage::User& u) { return storage::userDir(u) + "/favorites.json"; }

static std::vector<int> listLocked(const storage::User& u) {
    std::vector<int> ids;
    if (json_object* arr = json_object_from_file(listPath(u).c_str())) {
        if (json_object_is_type(arr, json_type_array))
            for (size_t i = 0; i < json_object_array_length(arr); ++i)
                ids.push_back(json_object_get_int(json_object_array_get_idx(arr, i)));
        json_object_put(arr);
    }
    return ids;
}

static void putListLocked(const storage::User& u, const std::vector<int>& ids) {
    json_object* arr = json_object_new_array();
    for (int id : ids) json_object_array_add(arr, json_object_new_int(id));
    const std::string path = listPath(u);
    const std::string tmp = path + ".part";
    if (json_object_to_file(tmp.c_str(), arr) == 0) ::rename(tmp.c_str(), path.c_str());
    json_object_put(arr);
}

static std::map<int, bool> favOwedLocked(const storage::User& u) {
    std::map<int, bool> m;
    if (json_object* o = json_object_from_file((storage::userDir(u) + "/favorites-owed.json").c_str())) {
        if (json_object_is_type(o, json_type_object)) {
            json_object_object_foreach(o, k, v) m[std::atoi(k)] = json_object_get_boolean(v);
        }
        json_object_put(o);
    }
    return m;
}

static void putFavOwedLocked(const storage::User& u, const std::map<int, bool>& m) {
    const std::string path = storage::userDir(u) + "/favorites-owed.json";
    if (m.empty()) { cab::removeFile(path); return; }
    json_object* o = json_object_new_object();
    for (const auto& [id, on] : m)
        json_object_object_add(o, std::to_string(id).c_str(), json_object_new_boolean(on));
    const std::string tmp = path + ".part";
    if (json_object_to_file(tmp.c_str(), o) == 0) ::rename(tmp.c_str(), path.c_str());
    json_object_put(o);
}

std::map<int, bool> favOwed(const storage::User& u) {
    std::lock_guard<std::mutex> lock(gFavMutex);
    return favOwedLocked(u);
}

std::vector<int> favoriteList(const storage::User& u) {
    std::lock_guard<std::mutex> lock(gFavMutex);
    return listLocked(u);
}

void pressFavorite(const storage::User& u, int romId, bool on) {
    std::lock_guard<std::mutex> lock(gFavMutex);
    gFavParked.erase(storage::userDir(u));
    std::vector<int> ids = listLocked(u);
    ids.erase(std::remove(ids.begin(), ids.end(), romId), ids.end());
    if (on) ids.insert(ids.begin(), romId);
    putListLocked(u, ids);
    std::map<int, bool> m = favOwedLocked(u);
    m[romId] = on;
    putFavOwedLocked(u, m);
}

// NEWEST FIRST, AS FAR AS THIS CONSOLE CAN KNOW (#476). RomM keeps no date
// for a favourite and lists them by title, so the order is the console's
// own: a heart pressed here goes first (pressFavorite), a favourite RomM has
// that this list has never held was made elsewhere since it was last read
// and comes next, in RomM's order, and the rest keep their places. One
// RomM no longer has goes, unless a press here not yet sent says otherwise.
//
// What is owed is read here, under the lock, rather than when the caller
// fetched: a press made since then wins (#405).
static std::vector<int> orderLocked(const storage::User& u, const std::vector<int>& old,
                                    const std::vector<int>& onServer) {
    const std::map<int, bool> owed = favOwedLocked(u);
    auto has = [](const std::vector<int>& v, int id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    };
    auto wanted = [&](int id) {
        if (auto it = owed.find(id); it != owed.end()) return it->second;
        return has(onServer, id);
    };
    std::vector<int> ids;
    // Owed but missing from the list: only if the list was lost.
    for (const auto& [id, on] : owed)
        if (on && !has(old, id)) ids.push_back(id);
    for (int id : onServer)
        if (!has(old, id) && wanted(id) && !has(ids, id)) ids.push_back(id);
    for (int id : old)
        if (wanted(id) && !has(ids, id)) ids.push_back(id);
    return ids;
}

std::vector<int> keepFavorites(const storage::User& u, const std::vector<int>& onServer) {
    std::lock_guard<std::mutex> lock(gFavMutex);
    const std::vector<int> old = listLocked(u);
    const std::vector<int> ids = orderLocked(u, old, onServer);
    if (ids != old) putListLocked(u, ids);
    return ids;
}

std::vector<int> favoriteOrder(const storage::User& u, const std::vector<int>& onServer) {
    std::lock_guard<std::mutex> lock(gFavMutex);
    return orderLocked(u, listLocked(u), onServer);
}

// Every favourite `who` owes, sent; each stops being owed once the server has
// it, unless it was pressed again meanwhile. True also when RomM will not make
// their Favorites collection (gFavParked): nothing more is worth sending now.
bool sendFavoritesAs(romm::Client& c, const storage::User& who, std::string* err) {
    {
        std::lock_guard<std::mutex> lock(gFavMutex);
        if (gFavParked.count(storage::userDir(who))) return true;
    }
    for (const auto& [romId, on] : favOwed(who)) {
        bool forbidden = false, cannotMake = false;
        if (!c.setFavorite(romId, on, &forbidden, err, &cannotMake)) {
            if (forbidden)
                std::fprintf(stderr, "[favorite] RomM refused: this console was paired "
                                     "before favourites needed collections.write\n");
            if (cannotMake) {
                std::lock_guard<std::mutex> lock(gFavMutex);
                gFavParked.insert(storage::userDir(who));
                std::fprintf(stderr, "[favorite] RomM would not make a Favorites collection "
                                     "(%s): a collection by that name may already be "
                                     "theirs; kept on this console, tried again at the "
                                     "next heart or start\n",
                             err ? err->c_str() : "");
                return true;
            }
            return false;
        }
        std::lock_guard<std::mutex> lock(gFavMutex);
        std::map<int, bool> m = favOwedLocked(who);
        if (auto it = m.find(romId); it != m.end() && it->second == on) m.erase(it);
        putFavOwedLocked(who, m);
        std::fprintf(stderr, "[favorite] %d %s on RomM\n", romId, on ? "added" : "removed");
    }
    return true;
}

// ---- The heart on a game's page (#267) -----------------------------------
// A member of the App (app.h), moved out of apply in batch (e).

void App::toggleFavorite(const screens::Result& res) {
    // THE HEART ANSWERS AT ONCE (#267): the page, Home's shelf and
    // the person's list on the drive change now; the server hears
    // it from the uploader, now or when it is back.
    const int romId = res.value;
    const bool on = !detailScreen.game().favorite;
    detailScreen.setFavorite(on);
    sound::play(sound::Cue::Activate);
    const int card = detailScreen.game().cardIndex;
    auto it = std::find(favorites.begin(), favorites.end(), card);
    if (on && it == favorites.end() && card >= 0) favorites.insert(favorites.begin(), card);
    if (!on && it != favorites.end()) favorites.erase(it);
    // Home's place on the Favorites shelf, which just changed
    // under it: kept in range, or the top row if its shelf is gone
    // or Favorites is now the only one (nothing played yet).
    slotFocus[RowFavorites].clear();
    slotPress[RowFavorites].clear();
    shelfGlide[RowFavorites].stop();
    if (focusRow == RowFavorites || !rowExists(focusRow)) {
        if (!rowExists(focusRow)) { focusRow = homeTopRow(); focusSlot = 0; }
        else focusSlot = std::clamp(focusSlot, 0, static_cast<int>(favorites.size()) - 1);
        if (cardAt(focusRow, focusSlot)) slotAnim(slotFocus, focusRow, focusSlot).settle(1.0f);
    }
    if (const storage::User& who = storage::currentUser(); who.valid()) {
        pressFavorite(who, romId, on);
        uploader.sendFavorites();
    }
    libraryRefresh.hearts[romId] = on;   // it wins over a read already out
    std::fprintf(stderr, "[favorite] %d %s\n", romId, on ? "on" : "off");
}
