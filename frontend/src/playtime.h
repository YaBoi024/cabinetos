// Time played (#128): how long a person has played a game, reported to RomM as
// play sessions and shown on the game's launch screen. docs/PROJECT.md, "Time
// played".
//
// WHAT COUNTS. Only time the game is running: the pause menu stops the clock,
// and so does a game left alone. Time after the last press counts only while
// it is under twenty minutes, which is when the screen dims in a game
// (idle::kGameDimAfter); a game left running all afternoon counts up to the
// last press. A session under a minute is not recorded at all.
//
// WHERE IT LIVES. Per person, in `users/<id> - <name>/playtime.json`: the
// session in progress (written every minute, so a power cut loses at most
// one), the sessions not yet on RomM, and the last total RomM gave for each
// game (so the number is right with no server).
//
// WHOSE TIME RomM RETURNS. A console's token is tied to a RomM device, and the
// session list is asked for that device by name (romm::Client::fetchPlayedMs,
// `device_id`, which RomM 5.4.0 requires, #401), so it holds that device's
// sessions only. Every
// CabinetOS console pairs under the same device identifier, so for one person
// that is all their play on any CabinetOS console, surviving a reinstall; RomM's
// web player and other apps are not in it. rommapp/romm#4837 asks for the
// whole total.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "storage.h"

namespace playtime {

// Under this, a session is not recorded: a launch that failed, or a look.
constexpr int64_t kShortestMs = 60 * 1000;
// Time since the last press stops counting past this. The in-game dim.
constexpr double kUnwatchedAfter = 20 * 60.0;

struct Session {
    int romId = 0;
    int64_t startMs = 0;    // wall clock, milliseconds since 1970
    int64_t endMs = 0;
    int64_t playedMs = 0;   // what counted, which is at most end - start
};

// The clock for the game being played. Frame thread only.
class Clock {
public:
    void begin(int romId, int64_t nowMs);
    // Once a frame. `running` is a game on screen with no menu over it.
    void tick(float dt, bool running);
    // A press, anywhere.
    void touch();
    bool active() const { return romId_ > 0; }
    Session now(int64_t nowMs) const;
    // Ends it and hands it back; the clock is idle afterwards.
    Session finish(int64_t nowMs);

private:
    double counted() const;
    int romId_ = 0;
    int64_t startMs_ = 0;
    double kept_ = 0.0;         // seconds up to the last press
    double sinceTouch_ = 0.0;   // running seconds since it
};

int64_t wallMs();
// "2026-09-27T12:14:03Z", which is what RomM takes.
std::string iso(int64_t ms);
// "40 minutes played", "12 hours played", or empty under a minute. Whole
// hours, rounded down.
std::string describe(int64_t ms);

// The record on disk. Safe from any thread.
void checkpoint(const storage::User& u, const Session& s);
// Ends the session in progress: owed if it is long enough, dropped if not.
void close(const storage::User& u, const Session& s);
// A session a crash or a power cut left in progress becomes owed, ending at
// its last checkpoint.
void recover(const storage::User& u);
std::vector<Session> owed(const storage::User& u);
// These reached RomM (or RomM refused them for good): no longer owed, and
// added to the remembered total.
void sent(const storage::User& u, const std::vector<Session>& done);
int64_t owedMs(const storage::User& u, int romId);
// The last total RomM gave for a game.
void remember(const storage::User& u, int romId, int64_t ms);
bool known(const storage::User& u, int romId, int64_t* ms);

// `--playtime-test`: the counting rules and the record, walked in a scratch
// directory. Prints each check; returns the number that failed.
int test();

}  // namespace playtime
