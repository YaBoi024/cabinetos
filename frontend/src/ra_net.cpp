// RetroAchievements' network: one worker, in order, for what rc_client sends,
// with the answers run on the frame thread (runAnswers, from pump); the
// User-Agent every request carries; and a badge's bytes for the image cache's
// workers (fetchBytes). Moved out of achievements.cpp in the restructure
// (#398).

#include "ra_state.h"

#include <curl/curl.h>

#include "tlsca.h"

#include "rc_api_request.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace ra {
namespace {

// --- The network ----------------------------------------------------------
//
// ONE WORKER, IN ORDER. rc_client sends little (a sign-in, a game's data, a
// session, an unlock now and then), and RetroAchievements asks clients not to
// fire requests in parallel. Answers come back to the frame thread through
// gDone, so rc_client only ever hears from the network there.

std::mutex gUaMutex;
std::string gUaBase;   // "CabinetOS/2026.10.06 (Linux)"
std::string gUaCore;   // " mGBA/0.10.5", the game's core, or empty
std::string gUaRc;     // " rcheevos/12.5.0"

std::string userAgent() {
    std::lock_guard<std::mutex> lk(gUaMutex);
    return gUaBase + gUaCore + gUaRc;
}

std::atomic<bool> gStopping{false};

// Shutting down: a request in flight stops at once rather than waiting out
// curl's timeouts, and comes back as "no answer" (shutdown queues it).
int abortTransfer(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return gStopping ? 1 : 0;
}

size_t appendBody(char* p, size_t size, size_t n, void* out) {
    static_cast<std::string*>(out)->append(p, size * n);
    return size * n;
}

// The HTTP status, or RetroAchievements' "could not reach it, try again" code
// when nothing came back at all: rc_client keeps an unlock and retries it on
// that one, which is what makes a dropped connection mid-game lose nothing.
int request(const std::string& url, const char* post, const char* contentType,
            std::string* body, long timeoutSeconds) {
    // CABINETOS_RA_OFFLINE=1: every request fails as with no network, for
    // trying the offline path without taking the console off its network.
    static const bool offline = std::getenv("CABINETOS_RA_OFFLINE") != nullptr;
    if (offline) return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    CURL* c = curl_easy_init();
    if (c) tlsca::apply(c);
    if (!c) return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    const std::string ua = userAgent();
    curl_slist* headers = nullptr;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, ua.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, abortTransfer);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    if (post) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, post);
        if (contentType) {
            headers = curl_slist_append(headers,
                                        (std::string("Content-Type: ") + contentType).c_str());
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        }
    }
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || status == 0) {
        std::fprintf(stderr, "[ra] no answer from %s: %s\n",
                     url.substr(0, url.find('?')).c_str(), curl_easy_strerror(rc));
        return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    }
    return static_cast<int>(status);
}

std::mutex gJobMutex;
std::condition_variable gJobCv;
std::deque<Job> gJobs;
std::thread gWorker;

std::mutex gDoneMutex;
std::deque<std::function<void()>> gDone;

void workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(gJobMutex);
            gJobCv.wait(lk, [] { return gStopping || !gJobs.empty(); });
            if (gStopping) return;
            job = std::move(gJobs.front());
            gJobs.pop_front();
        }
        auto body = std::make_shared<std::string>();
        const int status = request(job.url, job.isPost ? job.post.c_str() : nullptr,
                                   job.contentType.empty() ? nullptr : job.contentType.c_str(),
                                   body.get(), 30L);
        std::lock_guard<std::mutex> lk(gDoneMutex);
        auto done = std::move(job.done);
        gDone.push_back([done, status, body] { done(status, *body); });
    }
}

}  // namespace

void send(const rc_api_request_t& r, std::function<void(int, const std::string&)> done) {
    Job job;
    job.url = r.url ? r.url : "";
    job.isPost = r.post_data != nullptr;
    if (r.post_data) job.post = r.post_data;
    if (r.content_type) job.contentType = r.content_type;
    job.done = std::move(done);
    {
        std::lock_guard<std::mutex> lk(gJobMutex);
        gJobs.push_back(std::move(job));
    }
    gJobCv.notify_one();
}

// The User-Agent's two parts that last (init): the console's version and
// rcheevos' own clause.
void setUserAgent(const std::string& consoleVersion, const char* clause) {
    std::lock_guard<std::mutex> lk(gUaMutex);
    gUaBase = "CabinetOS/" + (consoleVersion.empty() ? std::string("dev") : consoleVersion) +
              " (Linux)";
    gUaRc = std::string(" ") + clause;
}

// The game's core in the User-Agent, " mGBA/0.10.5" (beginGame), and none once
// the game ends (endGame).
void setUserAgentCore(const std::string& core) {
    std::lock_guard<std::mutex> lk(gUaMutex);
    gUaCore = " " + core;
    std::replace(gUaCore.begin() + 1, gUaCore.end(), ' ', '_');
}

void clearUserAgentCore() {
    std::lock_guard<std::mutex> lk(gUaMutex);
    gUaCore.clear();
}

// The worker, started once (init).
void startWorker() {
    gStopping = false;
    gWorker = std::thread(workerLoop);
}

// Stops the worker (shutdown): the queue is swapped out as it stands and handed
// back, the worker is woken and joined, and the request on the wire is cut
// short (abortTransfer) and comes back as no answer.
std::deque<Job> stopWorker() {
    std::deque<Job> left;
    {
        std::lock_guard<std::mutex> lk(gJobMutex);
        gStopping = true;
        left.swap(gJobs);
    }
    gJobCv.notify_all();
    if (gWorker.joinable()) gWorker.join();
    return left;
}

// JOINED AT EXIT HOWEVER THE PROGRAM ENDS (#406). shutdown stops the worker
// on the way out of App::run, but a start-up that ends before that, after
// init (the library did not load), left gWorker joinable, and its destructor
// among the exit handlers ended the program in std::terminate (exit 134,
// measured on the A9 with a server that fails /api/platforms). Defined after
// the worker's statics, so it is destroyed before them.
namespace {
struct JoinAtExit {
    ~JoinAtExit() { stopWorker(); }
} gJoinAtExit;
}  // namespace

// The answers that have come back, run on the frame thread (pump).
void runAnswers() {
    std::deque<std::function<void()>> ready;
    {
        std::lock_guard<std::mutex> lk(gDoneMutex);
        ready.swap(gDone);
    }
    for (auto& f : ready) f();
}

// The answers not run, thrown away (shutdown, after its last pump).
void dropAnswers() {
    std::lock_guard<std::mutex> lk(gDoneMutex);
    gDone.clear();
}

std::vector<uint8_t> fetchBytes(const std::string& url) {
    std::string body;
    const int status = request(url, nullptr, nullptr, &body, 20L);
    if (status != 200) return {};
    return std::vector<uint8_t>(body.begin(), body.end());
}

}  // namespace ra
