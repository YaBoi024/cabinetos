// FORK (handheld): WHERE THE CERTIFICATE AUTHORITIES ARE, when the machine
// says so.
//
// The console's curl finds them where its distribution keeps them. A phone's
// are in a folder this program may not read (Android keeps them behind its own
// API), so the Android app writes the list Android trusts into a file of its
// own and names it in CABINETOS_CA_BUNDLE. Every connection the program makes
// to a server that is checked (RomM, RetroAchievements) calls this once on its
// new handle. With the variable unset, which is always the case on the
// console, it does nothing and curl uses what it always used.

#pragma once

#include <curl/curl.h>

#include <cstdlib>

namespace tlsca {

inline void apply(CURL* c) {
    const char* path = std::getenv("CABINETOS_CA_BUNDLE");
    if (path && *path) curl_easy_setopt(c, CURLOPT_CAINFO, path);
}

}  // namespace tlsca
