// A person's favourites on this console: their list in Home's order
// (favorites.json, kept for the offline Home and a game page's heart), and
// the presses not yet on the server (#267): rom id to wanted state, in
// favorites-owed.json. favorites.cpp is the only reader and writer of both,
// under one lock of its own (#405); the frame thread, the upload worker and
// the online reload all take it. Moved out of main.cpp in the restructure
// (#398).

#pragma once

#include <map>
#include <string>
#include <vector>

#include "romm.h"
#include "storage.h"

std::map<int, bool> favOwed(const storage::User& u);
// The person's list, newest first as this console knows it.
std::vector<int> favoriteList(const storage::User& u);
// A heart pressed: the list and what is owed change together.
void pressFavorite(const storage::User& u, int romId, bool on);
// The list made from the server's (library.h, putLibrary), with every press
// not yet sent applied on top, newest first as this console knows it; written
// when it changed, and returned.
std::vector<int> keepFavorites(const storage::User& u, const std::vector<int>& onServer);
// The same list, with nothing written: for asking RomM about the games Home's
// shelf will need (library.h, readLibrary).
std::vector<int> favoriteOrder(const storage::User& u, const std::vector<int>& onServer);
bool sendFavoritesAs(romm::Client& c, const storage::User& who, std::string* err);
