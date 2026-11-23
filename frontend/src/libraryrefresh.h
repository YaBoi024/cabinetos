// HOME AND THE LIBRARY KEEP UP WITH RomM (#730): what its owner adds, renames,
// removes or favorites there, and what is played in RomM's web player or
// another app, shows here without a restart. When RomM may be asked again, the
// rules a person at the TV would notice, and where Home's highlight goes when
// what is under it changes. The read itself and what it changes are library.h's
// (readLibrary, putLibrary); the App's side is in libraryrefresh.cpp.
//
// WHEN, AND NEVER MORE (When): as Home or the Library comes into view, at a
// press on one of them, or as RomM answers again after being away. One read at
// a time, none sooner than a minute after the last ended, a failed one too,
// but not one that never reached RomM. Nothing on a timer, nothing while
// nobody presses, nothing in a game, nothing offline. A start, an account
// switch and the server coming back read everything as they did, and start the
// minute over.
//
// NOBODY EVER WAITS FOR A READ. It has a client of its own that stops at once
// (romm::Client::stopWhen): an account switch, a sign-out, a new server
// address, a game or the console going off drop it, and its answer is thrown
// away. So is one read for somebody else.

#pragma once

#include <vector>

namespace libraryrefresh {

// From the end of one read to the start of the next, and how lately somebody
// pressed for there to be somebody there, in seconds.
constexpr double kBetweenSeconds = 60.0;
constexpr double kThereSeconds = 60.0;
// How long A does nothing on Home's first cover after another game took its
// place: A there starts a game, and nobody chose that one.
constexpr double kResumeHoldSeconds = 1.0;

// Which of the two screens is up and the person's: neither under a game, nor
// while LinkUp has the Library for choosing one.
enum class View { Neither, Home, Library };

// One frame of the console, as this rule sees it.
struct Moment {
    // Seconds, the time the machine slept included (power::upSeconds), so a
    // console back from Rest is not told its last read was a moment ago.
    double at = 0.0;
    View view = View::Neither;
    bool pressed = false;   // somebody pressed something since the last frame
    bool away = false;      // RomM is not there to ask (romm::serverAway)
};

class When {
public:
    // Once a frame. Whether this frame is one to read on, if a read may start.
    bool asks(const Moment& m);
    // Whether a minute has gone since the last one ended.
    bool rested(double at) const { return at - endedAt_ >= kBetweenSeconds; }
    void ended(double at) { endedAt_ = at; }

private:
    bool started_ = false;
    View view_ = View::Neither;
    bool away_ = false;
    double pressedAt_ = 0.0;
    double endedAt_ = -kBetweenSeconds;
};

// Where Home's highlight is: a row (App::Row, Recent 0 and Favorites 1) and a
// place on it.
struct Place {
    int row = 0;
    int slot = 0;
    bool operator==(const Place&) const = default;
};

// Where it goes when the rows change under it, the rows as indices into the
// library's cards. On the first Recent cover, Resume, it stays first; on any
// other cover it follows that game along its row. One that is gone gives it to
// the nearest, an empty row to the other, and a Home that was empty to its top
// row's first. `*newResume` says A would now start another game than the one
// it was on.
Place homePlace(Place at, const std::vector<int>& recentWas, const std::vector<int>& favoritesWas,
                const std::vector<int>& recentNow, const std::vector<int>& favoritesNow,
                bool* newResume);

// `--library-refresh-test`: when a read may start, where the highlight goes,
// and what building the library changes, part by part. Needs no server and no
// screen. 0 when all pass.
int test();

}  // namespace libraryrefresh
