// What offline will need, fetched while online, and the server tried in the
// background while it is away (#88); the fetch first removes the games RomM
// no longer has (gonegames.h, #625). Both run on threads of their own with
// clients of their own, stopped and joined at exit by stopOfflineSync (#406).
// Moved out of main.cpp in the restructure (#398); the rest of offline comes
// with the frame loop in batch (g).

#pragma once

#include <string>

void backfillDrive(const std::string& address);
void startReconnect(const std::string& address);
// At exit (App::shutDown): both threads stop at their next step and are
// joined; nothing starts them again.
void stopOfflineSync();
