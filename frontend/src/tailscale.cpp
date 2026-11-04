#include "tailscale.h"

#include "proc.h"

#include <json-c/json.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

namespace tailscale {

namespace {

std::mutex gM;
Status gLast;
int gGeneration = 0;

std::string str(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v)) return {};
    const char* s = json_object_get_string(v);
    return s ? s : "";
}

// `tailscale status --json`: the backend's state, the waiting sign-in link,
// and this console's own entry. Its peers are left out (--peers=false): the
// row needs none of them.
Status ask() {
    Status s;
    const proc::Result r = proc::run({"tailscale", "status", "--json", "--peers=false"}, 5);
    json_object* o = r.out.empty() ? nullptr : json_tokener_parse(r.out.c_str());
    if (!o) return s;
    s.answered = true;
    s.state = str(o, "BackendState");
    s.authUrl = str(o, "AuthURL");
    json_object* self = nullptr;
    if (json_object_object_get_ex(o, "Self", &self) && self) {
        s.name = str(self, "HostName");
        // "2027-04-06T03:44:07Z", or absent when the owner turned expiry off.
        const std::string exp = str(self, "KeyExpiry");
        std::tm tm{};
        if (!exp.empty() && strptime(exp.c_str(), "%Y-%m-%dT%H:%M:%S", &tm))
            s.expires = static_cast<int64_t>(timegm(&tm));
        json_object* ips = nullptr;
        if (json_object_object_get_ex(self, "TailscaleIPs", &ips) &&
            json_object_is_type(ips, json_type_array))
            for (size_t i = 0; i < json_object_array_length(ips); ++i) {
                const char* ip = json_object_get_string(json_object_array_get_idx(ips, i));
                // The IPv4 one: Moonlight on an iPhone needs it, and it is the
                // short one to type.
                if (ip && std::string(ip).find(':') == std::string::npos) {
                    s.address = ip;
                    break;
                }
            }
    }
    json_object_put(o);
    return s;
}

std::mutex gRunM;
std::condition_variable gRunCv;
std::thread gThread;
bool gWatching = false;

void refresh() {
    Status s = ask();
    std::lock_guard<std::mutex> lk(gM);
    if (s.answered == gLast.answered && s.state == gLast.state && s.authUrl == gLast.authUrl &&
        s.name == gLast.name && s.address == gLast.address && s.expires == gLast.expires)
        return;
    if (s.state != gLast.state)
        std::fprintf(stderr, "[tailscale] %s\n", s.answered ? s.state.c_str() : "not running");
    gLast = std::move(s);
    ++gGeneration;
}

void loop() {
    std::unique_lock<std::mutex> lk(gRunM);
    while (gWatching) {
        lk.unlock();
        refresh();
        lk.lock();
        gRunCv.wait_for(lk, std::chrono::seconds(2), [] { return !gWatching; });
    }
}

// The sign-in: one at a time, cancellable.
std::thread gLogin;
std::atomic<bool> gLoginCancel{false};
std::atomic<bool> gLoginRunning{false};
std::mutex gLoginM;
std::string gLoginWhy;   // set when the command ended without signing in

}  // namespace

void watch(bool on) {
    {
        std::lock_guard<std::mutex> lk(gRunM);
        if (gWatching == on) return;
        if (!on) gWatching = false;
    }
    gRunCv.notify_all();
    if (!on) return;
    if (gThread.joinable()) gThread.join();   // the last watch's, maybe still asking
    {
        std::lock_guard<std::mutex> lk(gRunM);
        gWatching = true;
    }
    gThread = std::thread(loop);
}

void join() {
    if (gThread.joinable()) gThread.join();
    if (gLogin.joinable()) gLogin.join();
}

Status last() {
    std::lock_guard<std::mutex> lk(gM);
    return gLast;
}

int generation() {
    std::lock_guard<std::mutex> lk(gM);
    return gGeneration;
}

void startLogin(bool again) {
    cancelLogin();
    if (gLogin.joinable()) gLogin.join();
    {
        std::lock_guard<std::mutex> lk(gLoginM);
        gLoginWhy.clear();
    }
    gLoginCancel = false;
    gLoginRunning = true;
    gLogin = std::thread([again] {
        // tailscaled was only just started: give it a few seconds to answer
        // before asking it to sign in, or the command says it is not running.
        for (int i = 0; i < 20 && !gLoginCancel; ++i) {
            if (ask().answered) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        // THE SAME SETTINGS THE UNIT SETS (tailscaled.service.d), named
        // because `up` insists on it (tailscale.h).
        std::vector<std::string> args = {"tailscale", "up", "--accept-dns=false",
                                         "--accept-routes=false"};
        if (again) args.push_back("--force-reauth");
        const proc::Result r =
            gLoginCancel ? proc::Result{} : proc::run(args, 15 * 60, &gLoginCancel);
        if (!gLoginCancel && !r.ok()) {
            std::string why = proc::trimmed(r.err.empty() ? r.out : r.err);
            std::fprintf(stderr, "[tailscale] sign-in ended: %s\n",
                         r.timedOut ? "timed out" : why.c_str());
            std::lock_guard<std::mutex> lk(gLoginM);
            gLoginWhy = r.timedOut ? "The link expired" : "Couldn't reach Tailscale";
        }
        gLoginRunning = false;
    });
}

void cancelLogin() {
    gLoginCancel = true;
    gLoginRunning = false;
}

bool loginRunning() { return gLoginRunning; }

bool loginFailed(std::string* why) {
    if (gLoginRunning) return false;
    std::lock_guard<std::mutex> lk(gLoginM);
    if (gLoginWhy.empty()) return false;
    if (why) *why = gLoginWhy;
    return true;
}

bool answers() { return ask().answered; }

bool logout(std::string* why) {
    const proc::Result r = proc::run({"tailscale", "logout"}, 15);
    if (!r.ok() && why) *why = proc::trimmed(r.err.empty() ? r.out : r.err);
    return r.ok();
}

}  // namespace tailscale
