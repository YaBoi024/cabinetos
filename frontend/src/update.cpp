#include "update.h"

#include "unit.h"

#include <json-c/json.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace update {

namespace {

std::string g_dir = "/run/cabinetos-update";

std::string slurp(const std::string& path) {
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

std::string firstLine(const std::string& s) {
    const size_t nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

State stateOf(const std::string& s) {
    if (s == "checking") return State::Checking;
    if (s == "uptodate") return State::UpToDate;
    if (s == "available") return State::Available;
    if (s == "downloading") return State::Downloading;
    if (s == "installing") return State::Installing;
    if (s == "ready") return State::Ready;
    if (s == "failed") return State::Failed;
    return State::None;
}

}  // namespace

void setDir(const std::string& dir) { g_dir = dir; }

Status read() {
    Status st;
    const std::string body = slurp(g_dir + "/status");
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = body.size();
        const std::string line = body.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int64_t n = std::strtoll(v.c_str(), nullptr, 10);
        if (k == "state") st.state = stateOf(v);
        else if (k == "op") st.download = (v == "download");
        else if (k == "version") st.version = v;
        else if (k == "size") st.size = n;
        else if (k == "romm") st.romm = v;
        else if (k == "done") st.done = n;
        else if (k == "total") st.total = n;
        else if (k == "since") st.since = n;
        else if (k == "written") st.written = n;
        else if (k == "reason") st.reason = v;
        else if (k == "at") st.at = n;
    }
    return st;
}

bool start(bool download, std::string* why) {
    return unit::start(download ? "cabinetos-update-download.service"
                                : "cabinetos-update-check.service",
                       why);
}

std::string bootedVersion() {
    // --update-dir's stand-in first, so "Updated to" can be shown by the TV
    // loop on an image that is not the one being described.
    const std::string standIn = firstLine(slurp(g_dir + "/version"));
    if (!standIn.empty()) return standIn;
    return firstLine(slurp("/usr/share/cabinetos/version"));
}

std::string bootId() { return firstLine(slurp("/proc/sys/kernel/random/boot_id")); }

std::string channel() {
    // The kernel command line names the booted deployment (ostree=/ostree/
    // boot.N/...), a chain of symlinks ending in /ostree/deploy/<os>/deploy/
    // <hash>.0, and its .origin file holds the image reference bootc follows.
    // All of it is world-readable, so this needs no root.
    const std::string cmdline = slurp("/proc/cmdline");
    const size_t at = cmdline.find("ostree=");
    if (at == std::string::npos) return "";
    size_t end = cmdline.find_first_of(" \n", at);
    if (end == std::string::npos) end = cmdline.size();
    const std::string link = "/sysroot" + cmdline.substr(at + 7, end - at - 7);
    char real[PATH_MAX];
    if (!realpath(link.c_str(), real)) return "";
    const std::string origin = slurp(std::string(real) + ".origin");
    const std::string key = "container-image-reference=";
    const size_t k = origin.find(key);
    if (k == std::string::npos) return "";
    const std::string ref = firstLine(origin.substr(k + key.size()));
    const size_t colon = ref.rfind(':');
    const size_t slash = ref.rfind('/');
    if (colon == std::string::npos || (slash != std::string::npos && colon < slash)) return "";
    return ref.substr(colon + 1);
}

std::string baseVersion() {
    const std::string body = slurp("/usr/share/ublue-os/image-info.json");
    json_object* o = body.empty() ? nullptr : json_tokener_parse(body.c_str());
    std::string out;
    json_object* v = nullptr;
    if (o && json_object_object_get_ex(o, "version", &v) &&
        json_object_get_type(v) == json_type_string)
        out = json_object_get_string(v);
    if (o) json_object_put(o);
    return out;
}

namespace {

// "5.5.0-alpha.2" as 5, 5, 0. False for anything that is not numbers joined
// by dots before an optional "-" or "+": "", "development", "5.", "v5.5".
bool versionParts(const std::string& v, std::vector<long>* parts) {
    parts->clear();
    const std::string head = v.substr(0, v.find_first_of("-+"));
    size_t pos = 0;
    while (true) {
        size_t end = pos;
        while (end < head.size() && head[end] >= '0' && head[end] <= '9') ++end;
        if (end == pos || end - pos > 9) return false;
        parts->push_back(std::strtol(head.substr(pos, end - pos).c_str(), nullptr, 10));
        if (end == head.size()) return true;
        if (head[end] != '.') return false;
        pos = end + 1;
    }
}

}  // namespace

Romm rommFor(const std::string& server, const std::string& needed) {
    std::vector<long> have, need;
    if (needed.empty()) return Romm::NewEnough;
    // A label this program cannot read is a build's mistake, never a reason
    // to download: the Justfile refuses to build one.
    if (!versionParts(needed, &need)) return Romm::Unknown;
    if (!versionParts(server, &have)) return Romm::Unknown;
    const size_t n = std::max(have.size(), need.size());
    for (size_t i = 0; i < n; ++i) {
        const long h = i < have.size() ? have[i] : 0;
        const long w = i < need.size() ? need[i] : 0;
        if (h != w) return h > w ? Romm::NewEnough : Romm::TooOld;
    }
    return Romm::NewEnough;
}

int test() {
    int failures = 0;
    auto check = [&](const char* server, const char* needed, Romm want) {
        const Romm got = rommFor(server, needed);
        auto name = [](Romm r) {
            return r == Romm::NewEnough ? "new enough" : r == Romm::TooOld ? "too old" : "unknown";
        };
        const bool ok = got == want;
        std::printf("  %s  RomM \"%s\" for an image needing \"%s\": %s\n", ok ? "ok  " : "FAIL",
                    server, needed, name(got));
        if (!ok) ++failures;
    };
    // The cases the save channels build meets (#346): the newest stable
    // RomM before channels, RomM's alpha of 5.5, its release, and a server
    // whose version the console has not heard.
    check("5.4.1", "5.5", Romm::TooOld);
    check("5.5.0-alpha.2", "5.5", Romm::NewEnough);
    check("5.5.0", "5.5", Romm::NewEnough);
    check("", "5.5", Romm::Unknown);
    check("development", "5.5", Romm::Unknown);
    // Every part counts, as a number and not as text, and a missing part
    // is 0.
    check("5.10.0", "5.9", Romm::NewEnough);
    check("5.4.9", "5.5", Romm::TooOld);
    check("6.0.0", "5.5", Romm::NewEnough);
    check("5.5", "5.5.1", Romm::TooOld);
    check("5.5.1+build.3", "5.5.1", Romm::NewEnough);
    check("5.1.0", "5.1", Romm::NewEnough);
    check("5.3.1", "5.1", Romm::NewEnough);
    check("5.0.9", "5.1", Romm::TooOld);
    // Not a version: unknown, never new enough by accident.
    check("v5.5.0", "5.5", Romm::Unknown);
    check("5.", "5.5", Romm::Unknown);
    check("5..5", "5.5", Romm::Unknown);
    check("<version>", "5.5", Romm::Unknown);
    // An image that names none holds nothing back, whatever the server.
    check("", "", Romm::NewEnough);
    check("4.0.0", "", Romm::NewEnough);
    // A label this program cannot read holds it back: unknown.
    check("5.5.0", "5.5x", Romm::Unknown);

    // The status file's line, as cabinetos-update writes it.
    char dir[] = "/tmp/cabinetos-update-test-XXXXXX";
    if (!mkdtemp(dir)) {
        std::fprintf(stderr, "[update] could not make a scratch folder\n");
        return 1;
    }
    const std::string was = g_dir;
    setDir(dir);
    auto statusSays = [&](const char* body, const char* romm, const char* what) {
        const std::string path = std::string(dir) + "/status";
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fputs(body, f);
            std::fclose(f);
        }
        const bool ok = read().romm == romm;
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
        std::remove(path.c_str());
    };
    statusSays("op=check\nstate=available\nversion=2026.11.20\nsize=1\nromm=5.5\nat=1\n", "5.5",
               "an update that needs RomM 5.5 says so");
    statusSays("op=check\nstate=available\nversion=2026.11.20\nsize=1\nromm=\nat=1\n", "",
               "an update that names none says nothing");
    statusSays("op=check\nstate=available\nversion=2026.11.20\nsize=1\nat=1\n", "",
               "a status with no romm line says nothing");
    setDir(was);
    rmdir(dir);

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace update
