// FORK (handheld): WHAT THE MACHINE UNDER THE PROGRAM DECIDES.
//
// The console owns its whole machine: its network, its pads, its keyboard
// handling. A build for a phone does not, and the phone's own way of doing each
// of these is better than copying the console's. This file is the short list of
// places where the program asks.
//
// Every answer here is the console's, and every call does nothing, in
// platform_linux.cpp. A build for another machine leaves that file out and
// provides its own (the Android app's is platform_android.cpp). So nothing in
// the program branches on an operating system: it asks one of these, and on the
// console the answer is the one it always was.
//
// Keep it short. A hook is added when a screen has no other way to do a job the
// machine already does, and each one is a line in FORK-CHANGES.md.

#pragma once

#include <string>

namespace platform {

// --- First run -----------------------------------------------------------------

// The machine has its own network screens (a phone's Settings), so first run's
// network step lists no networks. It says whether the machine is online, and
// when it is not, offers openNetworkSettings(). It moves on by itself once the
// machine is online.
bool ownsNetwork();

// The machine pairs its own controllers (a phone's Bluetooth settings), so first
// run has no step for pad one.
bool skipsControllerStep();

// --- Opening something outside the program ------------------------------------

// Whether openUrl() / openNetworkSettings() can do anything here. A screen shows
// the button only when it can.
bool canOpenUrl();
bool canOpenNetworkSettings();

// Hands a link to the machine's browser. The program never shows a page itself.
// Returns false when nothing could be opened.
bool openUrl(const std::string& url);

// Opens the machine's own network settings. False when nothing could be opened.
bool openNetworkSettings();

// --- Text entry ----------------------------------------------------------------

// The machine's own keyboard is used, and only while a text field is open:
// text input is not started when the program starts (startup.cpp, setup.cpp),
// textFieldOpened() starts it and textFieldClosed() stops it. The program's own
// on-screen keyboard still draws the field. On the console text input is on
// from the start, as it always was, and these two do nothing.
bool textInputOnDemand();
void textFieldOpened();
void textFieldClosed();

}  // namespace platform
