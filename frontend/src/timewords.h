// Times as RomM writes them and as the game's page says them ("Today,
// 8:13 PM"), and a game's year, maker and players kept for the page offline.
// Used on the game page's worker. Moved out of main.cpp in the restructure
// (#398).

#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#include "romm.h"

std::string isoUtc(time_t t);
// A time as RomM writes one ("2026-11-18T22:42:48.649000+00:00", with or
// without the fraction), in microseconds since 1970. RomM's times are UTC, so
// what follows the seconds is read only for the fraction. 0 when it is not a
// time.
int64_t isoMicros(const std::string& iso);
// A time in milliseconds since 1970 as RomM writes one, with its fraction.
std::string isoOfMs(int64_t ms);
// THE TIME A STATE'S NAME CARRIES: Save state names each one after the game
// and the UTC time to the millisecond, "<game> [2026-09-26 17-45-12-345]"
// (states.cpp, Cabinet's name), in milliseconds since 1970, whatever RomM
// added after it. RomM's snapshot lists a state with no time of its own, so
// this is how the console orders the states it is handed and says when each
// was made. 0 when the name carries none.
int64_t stateNameMs(const std::string& fileName);
void rememberFacts(int romId, const romm::Client::Facts& f);
bool knownFacts(int romId, romm::Client::Facts* f);
std::string stateWhen(const std::string& iso);
