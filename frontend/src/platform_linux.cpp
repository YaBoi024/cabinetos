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
bool nativeFieldOpen(const NativeField&) { return false; }
bool nativeFieldText(std::string*) { return false; }
void nativeFieldSay(const std::string&, bool, bool) {}
void nativeFieldBusy(bool) {}
void nativeFieldClose() {}
float nativeFieldTop() { return 1.0f; }
bool fillsWideScreens() { return false; }
bool fillsNarrowScreens() { return false; }
void safeInsets(int, int, int* left, int* top, int* right, int* bottom) {
    *left = *top = *right = *bottom = 0;
}
bool showsGamesWithoutACore() { return false; }
bool hidesSystemsWithoutACore() { return false; }
bool showsBackButton() { return false; }
bool touchesDsScreen() { return false; }
bool dsScreens(float*, float*) { return false; }
bool dsLook(float*, float*) { return false; }
bool gameLook(float*, float*) { return false; }
bool gameScreen(float*) { return false; }
float textPixels(int) { return 0.0f; }
void gamePlaying(bool, const char*) {}
bool runsInAnotherApp(const char*) { return false; }
bool openInAnotherApp(const char*, const std::string&) { return false; }
std::string emulatorInstallUrl(const char*) { return std::string(); }
std::string otherAppExtensions(const char*) { return std::string(); }
bool hidesSetting(const char*) { return false; }
void* coreGLContextMake() { return nullptr; }
void coreGLContextUse(void*) {}
void coreGLContextFree(void*) {}

}  // namespace platform
