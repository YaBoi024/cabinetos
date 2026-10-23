// SPDX-License-Identifier: GPL-2.0-or-later
//
// What cabinetos-wii-bridge's two files share (bridge.cpp, mii.cpp): a real
// Remote and its stand-in, and the calls each makes of the other. Moved out
// of bridge.cpp in the restructure (#398).

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "encryption.h"

namespace wiibridge {

struct PendingRead {
    bool reg;        // register space (0x04), not EEPROM
    unsigned addr;   // 24-bit start
    unsigned size;
    unsigned done = 0;
};

// REPORTS THE BRIDGE SENDS A SET TIME APART, one per pass of its loop when
// it is due, never by sleeping: the loop serves every Remote, and a sleep
// stopped them all (#480). While some are left, or the last one's gap runs,
// their Remote is held: see held, in bridge.cpp.
struct Paced {
    std::deque<std::vector<uint8_t>> left;
    std::chrono::steady_clock::time_point due{};   // the next, or the end of the last one's gap
    bool busy() const { return !left.empty() || std::chrono::steady_clock::now() < due; }
};

struct Remote {
    std::string node, name, uniq;
    int real = -1;
    int uhid = -1;
    uint8_t btn1 = 0, btn2 = 0;
    bool ext = false;
    bool plain = false;      // the real extension has had 0x55/0x00 since it connected
    bool enc = false;        // the game believes encryption is on
    std::array<uint8_t, 16> keyData{};
    bool keyDirty = true;
    WiimoteEmu::EncryptionKey key;
    std::deque<PendingRead> reads;
    // ANSWERS TO THE BRIDGE'S OWN COMMANDS, kept from the game, but only for a
    // second after the last of them. A count that outlived its commands hid the
    // game's own answers instead, and a Wii game that misses one says
    // "Communication with the Wii Remote has been interrupted": measured on the
    // A9 2026-10-03 20:29, Geometry Wars right after a replay in Wild West Guns.
    // The count can be high because these copies drop commands sent too fast,
    // and a dropped command is never answered.
    int swallowAcks = 0;
    int swallowed = 0;
    std::chrono::steady_clock::time_point swallowUntil{};
    // WHEN THE GAME LETS THE REMOTE GO (a Wii game's idle disconnect, about
    // five minutes untouched). See checkDropped.
    std::chrono::steady_clock::time_point lastOut{};
    bool lastOutWasReset = false;   // the last thing sent was Dolphin's 12 00 30
    bool driven = false;            // a game has asked for more than buttons
    // The last light and report mode asked for, put back when the Remote
    // comes back (see reattach).
    std::vector<uint8_t> lastLed, lastMode;
    // The Remote's last status report (0x20), answered from while it is off.
    std::vector<uint8_t> lastStatus;
    // EVERY OTHER SETUP COMMAND THE GAME SENT, in order: the camera (0x13, 0x1a,
    // registers 0xB0..), the extension and MotionPlus (0xA4.., 0xA6..). A
    // Remote switched off forgets all of it; when it is back it is sent
    // again. The encryption the bridge answers itself never reached the
    // Remote and is not here. Nor is the speaker: see isSetup.
    std::vector<std::vector<uint8_t>> setup;
    // That setup going out again, and whether the light and report mode are
    // still to follow it (see reattach and replayNext).
    Paced replay;
    bool replaying = false;
    // A Mii read's answers still to go to the game (see miiRead).
    Paced miiAnswers;
    // LINUX'S WII REMOTE DRIVER TAKES THIS REMOTE TOO (Nintendo's own id; never
    // a TechKen), and undoes the game's report mode and rumble: see putBack.
    bool linuxDriver = false;
    bool rumble = false;   // the game's rumble, as its last report said
    // When to put them back, and whether the report mode too; zero: nothing.
    std::chrono::steady_clock::time_point putBackAt{};
    bool putBackMode = false;
    bool putBackSaid = false;   // logged once per connection
};

// bridge.cpp: a report to the stand-in, and the acknowledgement of one.
void toGame(Remote& r, const uint8_t* d, size_t n);
void ackToGame(Remote& r, uint8_t reportId);

// mii.cpp: where the Miis are kept, a read or a write of the Remote's Mii
// memory answered from there (false when it is not one), and the next of a
// read's answers, sent when it is due.
extern std::string gMiiDir;
bool miiRead(Remote& r, const uint8_t* d, size_t n);
bool miiWrite(Remote& r, const uint8_t* d, size_t n);
void miiAnswer(Remote& r);

}  // namespace wiibridge
