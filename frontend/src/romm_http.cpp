// The RomM client's transport: every request goes out from here (get,
// postJson and sendJson, postMultipart, fetchToFile, fetchBytes, fetchRange),
// each on its own CURL handle, with the console-wide flag that says whether
// the server is away. One does not: setFavorite makes a person's first
// Favorites collection on a handle of its own (romm_playdata.cpp). The client's other files (romm_pairing.cpp,
// romm_library.cpp, romm_playdata.cpp, romm_codes.cpp) ask through these.
// Moved out of romm.cpp in the restructure (#398).

#include "romm.h"

#include "romm_internal.h"

#include <curl/curl.h>

#include "tlsca.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>

namespace romm {

size_t sink(char* p, size_t sz, size_t n, void* user) {
    static_cast<std::string*>(user)->append(p, sz * n);
    return sz * n;
}

size_t byteSink(char* p, size_t sz, size_t n, void* user) {
    auto* v = static_cast<std::vector<uint8_t>*>(user);
    v->insert(v->end(), p, p + sz * n);
    return sz * n;
}

// RomM hands back cover paths with a cache-busting query appended:
//
//   /assets/.../cover/small.png?ts=2026-02-08 21:13:30
//                                              ^ a space, inside a URL
//
// curl rejects that outright — "Malformed input to a URL function" — so
// without this EVERY cover fails, fetchBytes returns empty, ImageCache reads
// empty as failure, and the library renders with no art and no error anywhere.
// A silent total failure is worth more care than a loud partial one.
//
// Encoding conservatively: anything already legal in a URL is left alone,
// including a % that begins a valid escape, so a path that is already encoded
// is not encoded twice. Everything else — the space, and any non-ASCII byte —
// becomes %XX.
std::string encodeUrl(const std::string& in) {
    static const char* kHex = "0123456789ABCDEF";
    auto isHex = [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    };
    std::string out;
    out.reserve(in.size() + 16);
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') ||
                                c == '-' || c == '.' || c == '_' || c == '~';
        // Delimiters that are meaningful where they sit and must survive.
        const bool delimiter = strchr("/?=&:@+$,;#!*'()[]", c) != nullptr;
        const bool liveEscape = c == '%' && i + 2 < in.size() &&
                                isHex(static_cast<unsigned char>(in[i + 1])) &&
                                isHex(static_cast<unsigned char>(in[i + 2]));
        if (unreserved || delimiter || liveEscape) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string encodeSegment(const std::string& name) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(name.size() + 16);
    for (const unsigned char c : name) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// WHETHER THE SERVER IS AWAY (#88). One fact for the whole console, set by
// what every request actually met rather than by a separate check: a request
// that could not reach the server at all says so, and any answer at all says
// it is back. "Could not reach" is narrow on purpose: no connection, no name,
// or a deadline that passed before a connection was ever made. A slow
// download, an HTTP error and a refused token are a server that is there.
namespace {
std::atomic<bool> gAway{false};
std::atomic<long long> gAwaySince{0};
}  // namespace

bool serverAway() { return gAway.load(); }

void setServerAway(bool away) {
    const bool was = gAway.exchange(away);
    if (away && !was) gAwaySince = static_cast<long long>(std::time(nullptr));
}

long long serverAwaySince() { return gAwaySince.load(); }

void noteTransport(void* handle, int code) {
    CURL* c = static_cast<CURL*>(handle);
    const CURLcode rc = static_cast<CURLcode>(code);
    if (rc == CURLE_OK) { setServerAway(false); return; }
    bool unreachable = rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST;
    if (rc == CURLE_OPERATION_TIMEDOUT) {
        double connected = 0.0;
        curl_easy_getinfo(c, CURLINFO_CONNECT_TIME, &connected);
        unreachable = connected <= 0.0;
    }
    if (unreachable) setServerAway(true);
}

// THE CONSOLE CONNECTS ONLY TO ITS RomM SERVER (#380). An address that comes
// out of RomM's data, or a redirect in an answer, is followed only while it
// names the server the console was set up with: the same scheme, host and
// port. Anywhere else would let whoever controls the data decide where every
// console connects, with the person's sign-in attached. Refusals are logged
// by path only, never the host (as [covers] lines are), since a report reads
// this log and knows no host but the server's to hide.
namespace {
// "https://host:port", lower case: everything before the path.
std::string originOf(const std::string& url) {
    const size_t s = url.find("://");
    if (s == std::string::npos) return {};
    std::string o = url.substr(0, url.find_first_of("/?#", s + 3));
    for (char& ch : o) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return o;
}

std::string pathOf(const std::string& url) {
    const size_t s = url.find("://");
    const size_t p = url.find('/', s == std::string::npos ? 0 : s + 3);
    if (p == std::string::npos) return "/";
    return url.substr(p, url.find_first_of("?#", p) - p);
}

// Performs the request on `c`, following a redirect only while it stays on
// the server `base` names; `restart` empties what a redirect's own body put
// in the sink. A redirect elsewhere is left as the answer (a 3xx, which
// every caller counts as a failure) and its address in `*movedTo`.
CURLcode performOnServer(CURL* c, const std::string& base, const std::function<void()>& restart,
                         std::string* movedTo = nullptr) {
    for (int hops = 0;; ++hops) {
        const CURLcode rc = curl_easy_perform(c);
        if (rc != CURLE_OK) return rc;
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        char* next = nullptr;
        curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &next);
        if (status < 300 || status >= 400 || !next) return rc;
        const std::string to = next;   // the handle owns `next`
        if (originOf(to) != originOf(base)) {
            if (movedTo) *movedTo = to;
            else std::fprintf(stderr, "[romm] not followed: a redirect off the RomM server, "
                                      "to %s\n", pathOf(to).c_str());
            return rc;
        }
        if (hops >= 5) return rc;
        if (restart) restart();
        curl_easy_setopt(c, CURLOPT_URL, to.c_str());
    }
}
}  // namespace

Client::Client() { curl_global_init(CURL_GLOBAL_DEFAULT); }
Client::~Client() = default;

Client::Where Client::where() const {
    std::lock_guard<std::mutex> lock(mu_);
    return {base_, token_};
}

bool Client::get(const std::string& path, std::string* body, std::string* err,
                 long* status) const {
    return getAt(where(), path, body, err, true, nullptr, status);
}

bool Client::getAt(const Where& at, const std::string& path, std::string* body,
                   std::string* err, bool report, std::string* movedTo,
                   long* statusOut) const {
    if (statusOut) *statusOut = 0;
    CURL* c = curl_easy_init();
    if (c) tlsca::apply(c);
    if (!c) { if (err) *err = "curl init failed"; return false; }
    const std::string url = at.base + path;
    curl_slist* hdrs = nullptr;
    if (!at.token.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + at.token).c_str());

    body->clear();
    // The words curl keeps for a failure (a certificate's fault, the step that
    // failed), which its code alone does not carry. Read only on a failure.
    char detail[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = performOnServer(c, at.base, [body] { body->clear(); }, movedTo);
    if (report || rc == CURLE_OK) noteTransport(c, rc);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        if (err) {
            *err = curl_easy_strerror(rc);
            if (detail[0] != '\0' && *err != detail) *err += std::string(": ") + detail;
        }
        return false;
    }
    if (statusOut) *statusOut = status;
    if (status == 401 || status == 403) {
        if (err) *err = "not authorised: the token is missing, expired or lacks the scope";
        return false;
    }
    if (status >= 300) {
        if (err) *err = "HTTP " + std::to_string(status) + " for " + path;
        return false;
    }
    return true;
}

bool Client::postJson(const std::string& path, const std::string& json,
                      std::string* body, long* status, std::string* err) const {
    return sendJson("POST", path, json, body, status, err);
}

bool Client::sendJson(const char* method, const std::string& path, const std::string& json,
                      std::string* body, long* status, std::string* err) const {
    CURL* c = curl_easy_init();
    if (c) tlsca::apply(c);
    if (!c) { if (err) *err = "curl init failed"; return false; }
    const Where at = where();
    const std::string url = at.base + path;
    curl_slist* hdrs = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!at.token.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + at.token).c_str());

    body->clear();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(json.size()));
    if (std::strcmp(method, "POST") != 0) curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(c);
    noteTransport(c, rc);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (status) *status = code;
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) { if (err) *err = curl_easy_strerror(rc); return false; }
    return true;
}

namespace {
// One multipart/form-data body: the file part, and a state's picture as a
// second part when there is one. Hand-built because the whole client is: no
// HTTP library, and this is thirty lines. The picture part is Cabinet's to the
// byte: `screenshotFile`, image/png (RommClient.swift, uploadState).
std::string multipartBody(const std::string& boundary, const char* partName,
                          const std::string& fileName, const std::vector<uint8_t>& data,
                          const std::string& shotName, const std::vector<uint8_t>& shot) {
    std::string b;
    b.reserve(data.size() + shot.size() + fileName.size() + shotName.size() + 512);
    b += "--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"";
    b += partName;
    b += "\"; filename=\"" + fileName + "\"\r\n";
    b += "Content-Type: application/octet-stream\r\n\r\n";
    b.append(reinterpret_cast<const char*>(data.data()), data.size());
    b += "\r\n";
    if (!shotName.empty() && !shot.empty()) {
        b += "--" + boundary + "\r\n";
        b += "Content-Disposition: form-data; name=\"screenshotFile\"; filename=\"" +
             shotName + "\"\r\n";
        b += "Content-Type: image/png\r\n\r\n";
        b.append(reinterpret_cast<const char*>(shot.data()), shot.size());
        b += "\r\n";
    }
    b += "--" + boundary + "--\r\n";
    return b;
}
}  // namespace

bool Client::postMultipart(const std::string& path, const char* partName,
                           const std::string& fileName, const std::vector<uint8_t>& data,
                           std::string* err, const std::string& shotName,
                           const std::vector<uint8_t>& shot) const {
    CURL* c = curl_easy_init();
    if (c) tlsca::apply(c);
    if (!c) { if (err) *err = "curl init failed"; return false; }

    const std::string boundary = "CabinetOSBoundary7f3a91c4";
    const std::string body = multipartBody(boundary, partName, fileName, data, shotName, shot);

    curl_slist* hdrs = curl_slist_append(
        nullptr, ("Content-Type: multipart/form-data; boundary=" + boundary).c_str());
    const Where at = where();
    if (!at.token.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + at.token).c_str());

    std::string reply;
    const std::string url = encodeUrl(at.base + path);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    const CURLcode rc = curl_easy_perform(c);
    noteTransport(c, rc);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) { if (err) *err = curl_easy_strerror(rc); return false; }
    // ONLY A 2xx IS AN UPLOAD. This request does not follow redirects, so an
    // address that answers a POST with 301, 302, 307 or 308 (http behind an
    // https proxy, a path prefix, a sign-in page) received nothing; counting
    // it as sent cleared the owed marker and rotated local states away (#478).
    if (status < 200 || status >= 300) {
        if (err) *err = "HTTP " + std::to_string(status) + ": " + reply.substr(0, 180);
        return false;
    }
    return true;
}

namespace {
struct FileSink {
    FILE* f = nullptr;
    const Client::ProgressFn* progress = nullptr;
    int64_t got = 0;
    int64_t total = 0;
    bool aborted = false;
};

size_t writeToFile(char* p, size_t sz, size_t n, void* user) {
    auto* s = static_cast<FileSink*>(user);
    const size_t bytes = sz * n;
    if (std::fwrite(p, 1, bytes, s->f) != bytes) return 0;   // short write aborts curl
    s->got += static_cast<int64_t>(bytes);
    return bytes;
}

int reportProgress(void* user, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t, curl_off_t) {
    auto* s = static_cast<FileSink*>(user);
    s->total = dlTotal;
    if (s->progress && *s->progress) {
        // A non-zero return aborts the transfer, which is what makes cancelling
        // a three gigabyte download possible at all.
        if (!(*s->progress)(dlNow, dlTotal)) { s->aborted = true; return 1; }
    }
    return 0;
}
}  // namespace

bool Client::fetchToFile(const std::string& path, const std::string& destPath,
                         const ProgressFn& onProgress, std::string* err) const {
    CURL* c = curl_easy_init();
    if (c) tlsca::apply(c);
    if (!c) { if (err) *err = "curl init failed"; return false; }

    // Written to a neighbouring .part and renamed only on success, so an
    // interrupted download can never be mistaken for a complete ROM by
    // whatever looks in this directory next.
    const std::string partPath = destPath + ".part";
    FileSink sink;
    sink.f = std::fopen(partPath.c_str(), "wb");
    if (!sink.f) {
        if (err) *err = "cannot write " + partPath;
        curl_easy_cleanup(c);
        return false;
    }
    sink.progress = &onProgress;

    const Where at = where();
    const std::string url = encodeUrl(at.base + path);
    curl_slist* hdrs = nullptr;
    if (!at.token.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + at.token).c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, reportProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    // NO CURLOPT_TIMEOUT. A whole-transfer deadline is wrong for a file that
    // can legitimately take twenty minutes on a slow link; a stall is caught by
    // the low-speed limit below instead.
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    const CURLcode rc = performOnServer(c, at.base, [&sink] {
        std::fflush(sink.f);
        if (::ftruncate(fileno(sink.f), 0) == 0) std::rewind(sink.f);
        sink.got = 0;
    });
    noteTransport(c, rc);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    std::fclose(sink.f);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (sink.aborted) {
        std::remove(partPath.c_str());
        if (err) *err = "cancelled";
        return false;
    }
    if (rc != CURLE_OK || status >= 300) {
        std::remove(partPath.c_str());
        if (err) {
            *err = rc != CURLE_OK ? curl_easy_strerror(rc)
                                  : "HTTP " + std::to_string(status);
        }
        return false;
    }
    if (std::rename(partPath.c_str(), destPath.c_str()) != 0) {
        std::remove(partPath.c_str());
        if (err) *err = "cannot rename into place: " + destPath;
        return false;
    }
    return true;
}

std::vector<uint8_t> Client::fetchBytes(const std::string& path) const {
    std::vector<uint8_t> data;
    CURL* c = curl_easy_init();
    if (c) tlsca::apply(c);
    if (!c) return data;
    // The path comes from the server and may carry anything; see encodeUrl.
    // A whole address is taken only when it is on the RomM server, and a
    // path must be one: "@host/x" after the address would name another host
    // (#380).
    const Where at = where();
    const bool whole = path.find("://") != std::string::npos;
    if (whole ? originOf(path) != originOf(at.base) : path.empty() || path[0] != '/') {
        std::fprintf(stderr, "[covers] not asked: an address off the RomM server, %s\n",
                     pathOf(path).c_str());
        curl_easy_cleanup(c);
        return data;
    }
    const std::string url = encodeUrl(whole ? path : at.base + path);
    curl_slist* hdrs = nullptr;
    if (!at.token.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + at.token).c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, byteSink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = performOnServer(c, at.base, [&data] { data.clear(); });
    noteTransport(c, rc);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK || status >= 300) {
        // SAID, so a report shows why a picture is missing instead of a blank
        // tile and nothing in the log (#376). The path only, never the host or
        // the query; RomM's paths carry game numbers, not names.
        const std::string shown = path.substr(0, path.find('?'));
        if (rc != CURLE_OK)
            std::fprintf(stderr, "[covers] %s: %s\n", shown.c_str(), curl_easy_strerror(rc));
        else
            std::fprintf(stderr, "[covers] %s: the server answered %ld\n", shown.c_str(), status);
        data.clear();
    }
    return data;
}

namespace {
// Keeps at most `cap` bytes and then stops the transfer, so a server that
// ignores the range cannot stream a 4 GB disc into memory.
struct CappedSink {
    std::vector<uint8_t>* data;
    size_t cap;
};
size_t cappedSink(char* p, size_t sz, size_t n, void* user) {
    auto* s = static_cast<CappedSink*>(user);
    const size_t room = s->cap - std::min(s->cap, s->data->size());
    const size_t take = std::min(room, sz * n);
    s->data->insert(s->data->end(), p, p + take);
    return s->data->size() >= s->cap ? 0 : sz * n;   // 0 ends the transfer
}
}  // namespace

std::vector<uint8_t> Client::fetchHead(const Game& g, size_t bytes) const {
    return fetchRange(g, 0, bytes);
}

std::vector<uint8_t> Client::fetchRange(const Game& g, uint64_t offset, size_t bytes) const {
    std::vector<uint8_t> data;
    if (bytes == 0 || g.id == 0) return data;
    // The name in the path is RomM's and is escaped whole (encodeSegment):
    // `Sonic & Sega All-Stars Racing` must not become a query.
    const std::string name = encodeSegment(g.fsName);
    CURL* c = curl_easy_init();
    if (c) tlsca::apply(c);
    if (!c) return data;
    const Where at = where();
    const std::string url = at.base + "/api/roms/" + std::to_string(g.id) + "/content/" + name;
    const std::string range = std::to_string(offset) + "-" + std::to_string(offset + bytes - 1);
    curl_slist* hdrs = nullptr;
    if (!at.token.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + at.token).c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_RANGE, range.c_str());
    CappedSink sink{&data, bytes};
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cappedSink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    const CURLcode rc = performOnServer(c, at.base, [&data] { data.clear(); });
    noteTransport(c, rc);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    // 206 is the answer asked for. A 200 means the server ignored the range;
    // the sink stopped it at `bytes`, which are the right bytes only when they
    // were asked for from the start. A write error is that stop, not a failure.
    const bool stopped = rc == CURLE_WRITE_ERROR && data.size() >= bytes;
    if ((rc != CURLE_OK && !stopped) || (status != 206 && !(status == 200 && offset == 0)))
        data.clear();
    return data;
}

}  // namespace romm
