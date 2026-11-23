// Save states and screenshots of a game in play: made, a state marked owed in
// the game's snapshot (savepush.h) and a screenshot sent, and the newest
// state loaded back off this console or RomM. Moved out of main.cpp in the
// restructure (#398).

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "gamesession.h"
#include "notice.h"
#include "romm.h"
#include "uploader.h"

void saveStateNow(GameSession& sess, MenuNotice& notice);
void screenshotNow(GameSession& sess, Uploader& up, MenuNotice& notice);

// The newest state the game offers that THIS build can actually restore
// (savepush::offeredStates: the console's own name only). A state from
// another emulator is never attempted: loading one does not fail cleanly, it
// boots something that looks like the game and is not. Finding and fetching
// a state can be network work, so it happens on a worker; only applying it
// touches the core, and that waits for the frame thread. Same split as the
// upload, same reason: nothing that talks to a server may stop the picture.
struct StateLoad {
    std::atomic<bool> running{false};
    std::atomic<bool> ready{false};
    // The server had nothing for this core, or could not be asked. The frame
    // thread then tries this machine's own newest state — reading it there
    // rather than here because loading one touches the core.
    bool tryLocal = false;
    // Set when a state went in. The menu closes on it: the person is back in
    // the game at the point they loaded, which is what they pressed for.
    bool loaded = false;
    // The server could not be asked at all, as opposed to having nothing.
    bool serverFailed = false;
    // Home's Resume asked, not a person pressing Load: finding no state is
    // not news (the game just starts), so nothing is said about it.
    bool quiet = false;
    std::vector<uint8_t> data;
    std::string note;
    std::thread worker;
    ~StateLoad() { if (worker.joinable()) worker.join(); }
};

void beginLoadLatestState(StateLoad& load, GameSession& sess, romm::Client& client,
                          MenuNotice& notice, int wantId = 0, bool quiet = false);
void pumpStateLoad(StateLoad& load, GameSession& sess, MenuNotice& notice);
bool loadStateOnItsDisc(GameSession& sess, const std::string& name,
                        const std::vector<uint8_t>& bytes);
std::string stateRefusedText();
