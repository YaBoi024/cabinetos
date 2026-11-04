// The time zone a console is in: the list first run's Time zone screen
// offers, and setting one (setup.cpp draws the screen; firstrun.h has the
// rule that it is asked only when no zone is set).
//
// ONLY A CONSOLE WITH NO ZONE IS ASKED, which is a portable install (a disk
// image on a USB drive): an install from the ISO has the one its Time & Date
// screen set. There is no time zone in Settings.
//
// THE LIST IS THE TZ DATABASE'S OWN, /usr/share/zoneinfo/zone.tab: one zone
// for each country it covers, so a person finds a city in their own country
// (Amsterdam, which the newer zone1970.tab folds into Brussels), with the
// country's name from iso3166.tab beside it. Regions are the first part of a
// zone's name ("America"), cities the rest.

#pragma once

#include <string>
#include <vector>

namespace tz {

struct City {
    std::string zone;      // "America/Argentina/Buenos_Aires"
    std::string name;      // "Buenos Aires"; "Knox, Indiana" when the middle is not the country
    std::string country;   // "Argentina"
};

struct Region {
    std::string key;       // "America", the first part of every zone in it
    std::string name;      // "Americas", as a person says it
    std::vector<City> cities;   // by name
};

// Every region and its cities, by name, read from the database. Empty when
// zone.tab cannot be read.
std::vector<Region> regions();

// The zone set on this machine ("America/New_York"), from /etc/localtime's
// link into the database; "" when none is set, or it is not a link.
std::string current();

// Whether any zone is set: /etc/localtime is there.
bool isSet();

// The region a zone is in: its first part, "America".
std::string regionKey(const std::string& zone);

// SETS THE ZONE, through cabinetos-timezone@<zone>.service (the zone as its
// instance, escaped as systemd escapes one), and waits up to ten seconds for
// /etc/localtime to name it. The root helper sets a zone only on a console
// that has none, or replaces the one it set at this start. Blocks: call it on
// a worker. `why` says what went wrong, for the screen.
bool choose(const std::string& zone, std::string* why);

// This program's own clock follows a zone just chosen: glibc reads
// /etc/localtime once, so without this the console would keep UTC's hours
// (Appearance's dark hours, every time it shows) until it restarts. On the
// frame thread.
void follow(const std::string& zone);

}  // namespace tz
