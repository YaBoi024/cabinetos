// FORK (handheld): the console's answers to platform.h. They are the behaviour
// the program has always had: nothing is the machine's to do, so every call does
// nothing. A build for another machine leaves this file out and provides its
// own.

#include "platform.h"

namespace platform {

bool ownsNetwork() { return false; }
bool skipsControllerStep() { return false; }

bool canOpenUrl() { return false; }
bool canOpenNetworkSettings() { return false; }
bool openUrl(const std::string&) { return false; }
bool openNetworkSettings() { return false; }

bool textInputOnDemand() { return false; }
void textFieldOpened() {}
void textFieldClosed() {}
bool ownsKeyboard() { return false; }
bool fillsWideScreens() { return false; }
bool fillsNarrowScreens() { return false; }
void safeInsets(int, int, int* left, int* top, int* right, int* bottom) {
    *left = *top = *right = *bottom = 0;
}
bool showsGamesWithoutACore() { return false; }
void gamePlaying(bool, const char*) {}

}  // namespace platform
