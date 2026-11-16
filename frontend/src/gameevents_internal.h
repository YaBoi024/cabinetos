// What Game events' own files share, and nothing else uses: one message on
// the wire and the home-address rule (gameevents_wire.cpp), the parts of a
// message, and the sender started and stopped without touching the saved
// address (gameevents.cpp), for each other and for the self-test
// (gameevents_test.cpp). Each is defined with why it is as it is.

#pragma once

#include <sys/socket.h>

#include <atomic>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "gameevents.h"

namespace gameevents {

// gameevents_wire.cpp. Connect 2 s, the whole message 5 s, its name's
// lookup included.
constexpr long kConnectMs = 2000;
constexpr long kMessageMs = 5000;
bool isHome(const sockaddr* sa);
bool allHome(const std::vector<sockaddr_storage>& addrs);
std::string ipText(const sockaddr* sa);
// The POST, by every rule; `stop` cuts one on the wire short, or nullptr.
Tried post(const std::string& url, const std::string& body, const std::atomic<bool>* stop);

// gameevents.cpp.
std::string base64(const std::vector<uint8_t>& in);
std::string imageType(const std::vector<uint8_t>& bytes);
std::string localTime(std::time_t t);

// The sender for `url`, started if it is not running. `knocked`: Done's test
// just answered on this network, which is this network's knock.
void startSending(const std::string& url, bool knocked);
// The sender told to stop and the game forgotten; the saved address is left.
void stopRunning();
// The sender's thread has started and is not joined yet.
bool senderJoinable();

// The test's stand-ins for the network the console is on and the cover
// cache; nullptr puts back the real ones.
void standIns(std::function<std::string()> network,
              std::function<std::vector<uint8_t>(const std::string&)> cover);

}  // namespace gameevents
