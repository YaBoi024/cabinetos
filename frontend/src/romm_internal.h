// What the RomM client's own files share, and nothing outside the client may
// use: the two timeouts every request is sent with, the write sinks and the
// path encoder (romm_http.cpp), the JSON field readers (romm_library.cpp),
// and the readers of RomM's answers that the snapshot test feeds recorded
// answers to (romm_pairing.cpp, romm_snapshots.cpp). The client's face is
// romm.h. Split out of romm.cpp in the restructure (#398), where the first
// three sat in one anonymous namespace.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <json-c/json.h>

#include "romm_types.h"

namespace romm {

constexpr long kConnectTimeoutSec = 5;
constexpr long kTimeoutSec = 30;

// Defined in romm_http.cpp, with why each is as it is.
size_t sink(char* p, size_t sz, size_t n, void* user);
size_t byteSink(char* p, size_t sz, size_t n, void* user);
std::string encodeUrl(const std::string& in);

// Defined in romm_library.cpp, with why each is as it is.
std::string jstr(json_object* o, const char* key);
int64_t jint(json_object* o, const char* key);
std::vector<std::string> jstrs(json_object* o, const char* key);

// What `/api/heartbeat` says (romm_pairing.cpp): the version under
// `SYSTEM`, and whether the server has save channels (a `SAVE_SYNC` block).
// False when the body is not JSON.
struct Heartbeat {
    bool hasSystem = false;
    std::string version;
    bool channels = false;
};
bool readHeartbeat(const std::string& body, Heartbeat* out);

// RomM's answers about channels (romm_snapshots.cpp): a list of channels,
// and what a push's status and body say. readPushAnswer fills `*out` from
// any answer; a body it cannot read is Refused, its first words kept.
bool readChannels(const std::string& body, std::vector<Channel>* out, std::string* err);
void readPushAnswer(long status, const std::string& body, PushAnswer* out);
// The manifest part a push sends, as JSON text.
std::string pushManifest(const SnapshotPush& push);

}  // namespace romm
