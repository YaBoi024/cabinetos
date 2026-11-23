// See netblock.h.

#include "netblock.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <dlfcn.h>
#include <link.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unwind.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>

// The two sections of code the walk must tell from the rest of the program,
// whose ends the linker gives: the start of every thread an emulator starts
// (threadStart), and the self-test's stand-ins for an emulator.
extern "C" const char __start_netblock_thread[], __stop_netblock_thread[];
extern "C" const char __start_netblock_test[], __stop_netblock_test[];

namespace {

struct Span {
    uintptr_t lo = 0, hi = 0;
};
constexpr int kSpans = 8;

// An emulator's library: its code, and enough to tell whether it is still
// loaded after a dlclose. Plain arrays, so nothing here is built at start or
// torn down at exit while a thread may still be asking.
struct Emulator {
    bool used = false;
    uintptr_t base = 0;   // its load address, as dl_iterate_phdr says it
    char path[256] = {};  // and its file, as the loader names it
    char name[64] = {};   // the file's name alone, for the log
    Span spans[kSpans];
    int spanCount = 0;
};
constexpr int kEmulators = 8;

std::mutex gLock;
Emulator gEmulators[kEmulators];

// 1 + the slot of the emulator that started this thread, or 0.
thread_local int tOwner = 0;

// For the self-test: refusals, and those the thread's mark decided.
std::atomic<int> gRefused{0}, gRefusedByMark{0};

// What one loaded object is, by an address inside it or by its load address
// and file, and every part of it that holds code.
struct Find {
    uintptr_t addr = 0;
    uintptr_t base = 0;
    const char* path = nullptr;
    bool found = false;
    Span spans[kSpans];
    int n = 0;
};

int visit(dl_phdr_info* info, size_t, void* data) {
    Find* f = static_cast<Find*>(data);
    bool match = false;
    if (f->path) {
        match = info->dlpi_addr == f->base && std::strcmp(info->dlpi_name, f->path) == 0;
    } else {
        for (int i = 0; i < info->dlpi_phnum && !match; ++i) {
            const ElfW(Phdr)& p = info->dlpi_phdr[i];
            const uintptr_t lo = info->dlpi_addr + p.p_vaddr;
            match = p.p_type == PT_LOAD && f->addr >= lo && f->addr < lo + p.p_memsz;
        }
    }
    if (!match) return 0;
    f->found = true;
    for (int i = 0; i < info->dlpi_phnum && f->n < kSpans; ++i) {
        const ElfW(Phdr)& p = info->dlpi_phdr[i];
        if (p.p_type != PT_LOAD || !(p.p_flags & PF_X)) continue;
        f->spans[f->n++] = {info->dlpi_addr + p.p_vaddr, info->dlpi_addr + p.p_vaddr + p.p_memsz};
    }
    return 1;
}

// The console's own program: the object this function is in.
const Find& console() {
    static const Find self = [] {
        Find f;
        f.addr = reinterpret_cast<uintptr_t>(&console);
        dl_iterate_phdr(visit, &f);
        return f;
    }();
    return self;
}

bool inside(uintptr_t ip, const Span* s, int n) {
    for (int i = 0; i < n; ++i)
        if (ip >= s[i].lo && ip < s[i].hi) return true;
    return false;
}

bool inside(uintptr_t ip, const char* lo, const char* hi) {
    return ip >= reinterpret_cast<uintptr_t>(lo) && ip < reinterpret_cast<uintptr_t>(hi);
}

// Deep enough for curl: from a socket up to the call that asked for it is
// about thirty frames of its own.
constexpr int kFrames = 128;
struct Walk {
    uintptr_t ip[kFrames];
    int n = 0;
};

_Unwind_Reason_Code step(_Unwind_Context* c, void* data) {
    Walk* w = static_cast<Walk*>(data);
    int signalFrame = 0;
    const uintptr_t ip = _Unwind_GetIPInfo(c, &signalFrame);
    if (ip == 0) return _URC_END_OF_STACK;
    // A return address is the instruction after the call, which can be past
    // the end of the function that called.
    w->ip[w->n++] = signalFrame ? ip : ip - 1;
    return w->n < kFrames ? _URC_NO_REASON : _URC_END_OF_STACK;
}

struct Asker {
    int who = 0;          // 1 + the emulator's slot, or 0 for the console
    bool byMark = false;  // decided by the thread's mark, no frame deciding
};

// Who asked, from the frame that called the function being asked (`caller`
// is its return address) up the stack. The frames below it are this file's.
__attribute__((noinline)) Asker whoAsked(uintptr_t caller) {
    const Find& self = console();
    Walk w;
    _Unwind_Backtrace(step, &w);
    int i = 0;
    while (i < w.n && w.ip[i] != caller - 1) ++i;
    if (i == w.n) i = 0;
    std::lock_guard<std::mutex> lock(gLock);
    for (; i < w.n; ++i) {
        const uintptr_t ip = w.ip[i];
        // The start of a thread an emulator started: its mark says.
        if (inside(ip, __start_netblock_thread, __stop_netblock_thread)) break;
        for (int e = 0; e < kEmulators; ++e)
            if (gEmulators[e].used && inside(ip, gEmulators[e].spans, gEmulators[e].spanCount))
                return {e + 1, false};
        if (inside(ip, self.spans, self.n)) return {0, false};
    }
    return {tOwner, tOwner != 0};
}

#define WHO_ASKED() whoAsked(reinterpret_cast<uintptr_t>(__builtin_return_address(0)))

// What has been logged this game, so each refusal is said once.
std::set<std::string>& said() {
    static std::set<std::string>* s = new std::set<std::string>;
    return *s;
}

void refuse(const Asker& a, const std::string& what) {
    ++gRefused;
    if (a.byMark) ++gRefusedByMark;
    char name[64] = "an emulator";
    {
        std::lock_guard<std::mutex> lock(gLock);
        const Emulator& e = gEmulators[a.who - 1];
        if (e.used) std::snprintf(name, sizeof name, "%s", e.name);
        if (!said().insert(std::string(name) + '\n' + what).second) return;
    }
    std::fprintf(stderr, "[netblock] %s asked for %s: refused\n", name, what.c_str());
}

bool internet(int domain) {
    return domain == AF_INET || domain == AF_INET6 || domain == AF_PACKET;
}

std::string addressText(const void* addr, int family) {
    char text[INET6_ADDRSTRLEN] = "an address";
    if (addr && (family == AF_INET || family == AF_INET6))
        inet_ntop(family, addr, text, sizeof text);
    return std::string("the name of ") + text;
}

std::string addressText(const sockaddr* sa) {
    if (sa && sa->sa_family == AF_INET)
        return addressText(&reinterpret_cast<const sockaddr_in*>(sa)->sin_addr, AF_INET);
    if (sa && sa->sa_family == AF_INET6)
        return addressText(&reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr, AF_INET6);
    return addressText(nullptr, 0);
}

template <typename F>
F next(const char* name) {
    return reinterpret_cast<F>(dlsym(RTLD_NEXT, name));
}

struct Start {
    void* (*start)(void*);
    void* arg;
    int owner;
};

// EVERY THREAD AN EMULATOR STARTS STARTS HERE, so it carries the mark for
// its whole life. In a section of its own because it is the console's code:
// the walk must stop at it, not take it as the console asking.
__attribute__((noipa, section("netblock_thread"))) void* threadStart(void* p) {
    const Start s = *static_cast<Start*>(p);
    std::free(p);
    tOwner = s.owner;
    return s.start(s.arg);
}

}  // namespace

// THE FUNCTIONS EVERY LIBRARY REACHES. Each is the C library's own for the
// console, and refuses at once for an emulator: a socket with ENETDOWN, as
// Azahar's own patch did, and a lookup as a permanent failure (EAI_FAIL,
// NO_RECOVERY), so nothing waits to try again.

extern "C" __attribute__((noinline)) int socket(int domain, int type, int protocol) noexcept {
    static const auto real = next<int (*)(int, int, int)>("socket");
    if (internet(domain)) {
        if (const Asker a = WHO_ASKED(); a.who) {
            refuse(a, domain == AF_PACKET ? "a raw network socket" : "a network socket");
            errno = ENETDOWN;
            return -1;
        }
    }
    return real(domain, type, protocol);
}

extern "C" __attribute__((noinline)) int getaddrinfo(const char* node, const char* service,
                                                     const addrinfo* hints, addrinfo** res) {
    static const auto real =
        next<int (*)(const char*, const char*, const addrinfo*, addrinfo**)>("getaddrinfo");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, node ? node : "an address to listen on");
        if (res) *res = nullptr;
        return EAI_FAIL;
    }
    return real(node, service, hints, res);
}

extern "C" __attribute__((noinline)) hostent* gethostbyname(const char* name) {
    static const auto real = next<hostent* (*)(const char*)>("gethostbyname");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, name ? name : "a name");
        h_errno = NO_RECOVERY;
        return nullptr;
    }
    return real(name);
}

extern "C" __attribute__((noinline)) hostent* gethostbyname2(const char* name, int af) {
    static const auto real = next<hostent* (*)(const char*, int)>("gethostbyname2");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, name ? name : "a name");
        h_errno = NO_RECOVERY;
        return nullptr;
    }
    return real(name, af);
}

extern "C" __attribute__((noinline)) int gethostbyname_r(const char* name, hostent* ret, char* buf,
                                                         size_t len, hostent** result,
                                                         int* herr) {
    static const auto real =
        next<int (*)(const char*, hostent*, char*, size_t, hostent**, int*)>("gethostbyname_r");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, name ? name : "a name");
        if (result) *result = nullptr;
        if (herr) *herr = NO_RECOVERY;
        return ENETDOWN;
    }
    return real(name, ret, buf, len, result, herr);
}

extern "C" __attribute__((noinline)) int gethostbyname2_r(const char* name, int af, hostent* ret,
                                                          char* buf, size_t len,
                                                          hostent** result, int* herr) {
    static const auto real = next<int (*)(const char*, int, hostent*, char*, size_t, hostent**,
                                          int*)>("gethostbyname2_r");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, name ? name : "a name");
        if (result) *result = nullptr;
        if (herr) *herr = NO_RECOVERY;
        return ENETDOWN;
    }
    return real(name, af, ret, buf, len, result, herr);
}

extern "C" __attribute__((noinline)) hostent* gethostbyaddr(const void* addr, socklen_t len,
                                                            int type) {
    static const auto real = next<hostent* (*)(const void*, socklen_t, int)>("gethostbyaddr");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, addressText(addr, type));
        h_errno = NO_RECOVERY;
        return nullptr;
    }
    return real(addr, len, type);
}

extern "C" __attribute__((noinline)) int gethostbyaddr_r(const void* addr, socklen_t len, int type,
                                                         hostent* ret, char* buf, size_t buflen,
                                                         hostent** result, int* herr) {
    static const auto real = next<int (*)(const void*, socklen_t, int, hostent*, char*, size_t,
                                          hostent**, int*)>("gethostbyaddr_r");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, addressText(addr, type));
        if (result) *result = nullptr;
        if (herr) *herr = NO_RECOVERY;
        return ENETDOWN;
    }
    return real(addr, len, type, ret, buf, buflen, result, herr);
}

extern "C" __attribute__((noinline)) int getnameinfo(const sockaddr* sa, socklen_t salen, char* host,
                                                     socklen_t hostlen, char* serv,
                                                     socklen_t servlen, int flags) {
    static const auto real = next<int (*)(const sockaddr*, socklen_t, char*, socklen_t, char*,
                                          socklen_t, int)>("getnameinfo");
    if (const Asker a = WHO_ASKED(); a.who) {
        refuse(a, addressText(sa));
        return EAI_FAIL;
    }
    return real(sa, salen, host, hostlen, serv, servlen, flags);
}

// A thread the console starts is started as it always was. One an emulator
// starts (or a library inside its call: curl's name lookup) starts in
// threadStart, with the mark.
extern "C" __attribute__((noinline)) int pthread_create(pthread_t* thread,
                                                        const pthread_attr_t* attr,
                                                        void* (*start)(void*),
                                                        void* arg) noexcept {
    static const auto real =
        next<int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*)>(
            "pthread_create");
    const Asker a = WHO_ASKED();
    if (!a.who) return real(thread, attr, start, arg);
    Start* s = static_cast<Start*>(std::malloc(sizeof(Start)));
    if (!s) return real(thread, attr, start, arg);
    *s = {start, arg, a.who};
    const int rc = real(thread, attr, threadStart, s);
    if (rc != 0) std::free(s);
    return rc;
}

namespace {

// Whether the program exports the functions above, so a library reaches them
// (frontend/Makefile, NETBLOCK). Said in the log when one does not.
bool exported() {
    const struct {
        const char* name;
        void* ours;
    } names[] = {
        {"socket", reinterpret_cast<void*>(&socket)},
        {"getaddrinfo", reinterpret_cast<void*>(&getaddrinfo)},
        {"gethostbyname", reinterpret_cast<void*>(&gethostbyname)},
        {"gethostbyname2", reinterpret_cast<void*>(&gethostbyname2)},
        {"gethostbyname_r", reinterpret_cast<void*>(&gethostbyname_r)},
        {"gethostbyname2_r", reinterpret_cast<void*>(&gethostbyname2_r)},
        {"gethostbyaddr", reinterpret_cast<void*>(&gethostbyaddr)},
        {"gethostbyaddr_r", reinterpret_cast<void*>(&gethostbyaddr_r)},
        {"getnameinfo", reinterpret_cast<void*>(&getnameinfo)},
        {"pthread_create", reinterpret_cast<void*>(&pthread_create)},
    };
    bool all = true;
    for (const auto& n : names) {
        if (dlsym(RTLD_DEFAULT, n.name) == n.ours) continue;
        std::fprintf(stderr, "[netblock] %s is not exported: emulators reach the C library's\n",
                     n.name);
        all = false;
    }
    return all;
}

// A slot for the object, or the one it already has. Under gLock.
Emulator* slotFor(uintptr_t base, const char* path) {
    for (Emulator& e : gEmulators)
        if (e.used && e.base == base && std::strcmp(e.path, path) == 0) return &e;
    for (Emulator& e : gEmulators)
        if (!e.used) return &e;
    return nullptr;
}

void add(uintptr_t base, const char* path, const char* name, const Span* spans, int n) {
    std::lock_guard<std::mutex> lock(gLock);
    said().clear();
    Emulator* e = slotFor(base, path);
    if (!e) {
        std::fprintf(stderr, "[netblock] no room for %s: it reaches the network\n", name);
        return;
    }
    e->used = true;
    e->base = base;
    std::snprintf(e->path, sizeof e->path, "%s", path);
    std::snprintf(e->name, sizeof e->name, "%s", name);
    e->spanCount = n;
    for (int i = 0; i < n; ++i) e->spans[i] = spans[i];
}

}  // namespace

void netblock::addEmulator(void* handle, const std::string& path) {
    static const bool ok = exported();
    (void)ok;
    const std::string name = path.substr(path.find_last_of('/') + 1);
    link_map* map = nullptr;
    Find f;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) == 0 && map) {
        f.base = map->l_addr;
        f.path = map->l_name;
        dl_iterate_phdr(visit, &f);
    }
    if (!f.found || f.n == 0) {
        std::fprintf(stderr, "[netblock] could not find %s's code: it reaches the network\n",
                     name.c_str());
        return;
    }
    add(f.base, f.path, name.c_str(), f.spans, f.n);
    std::fprintf(stderr, "[netblock] %s gets no network\n", name.c_str());
}

void netblock::dropUnloaded() {
    Emulator was[kEmulators];
    {
        std::lock_guard<std::mutex> lock(gLock);
        for (int i = 0; i < kEmulators; ++i) was[i] = gEmulators[i];
    }
    bool gone[kEmulators] = {};
    for (int i = 0; i < kEmulators; ++i) {
        if (!was[i].used) continue;
        Find f;
        f.base = was[i].base;
        f.path = was[i].path;
        dl_iterate_phdr(visit, &f);
        gone[i] = !f.found;
    }
    std::lock_guard<std::mutex> lock(gLock);
    for (int i = 0; i < kEmulators; ++i) {
        Emulator& e = gEmulators[i];
        if (gone[i] && e.used && e.base == was[i].base && std::strcmp(e.path, was[i].path) == 0)
            e = Emulator{};
    }
}

// THE SELF-TEST'S EMULATOR: code in a section of its own, which the test
// adds as an emulator's library while it runs. Each keeps its frame on the
// stack (the empty asm after the call), so no call here is a tail call that
// would leave the console's caller asking instead.
namespace {

__attribute__((noipa, section("netblock_test"))) int emuSocket(int domain) {
    const int fd = ::socket(domain, SOCK_STREAM, 0);
    asm volatile("" ::: "memory");
    return fd;
}

__attribute__((noipa, section("netblock_test"))) int emuLookup(const char* numeric) {
    addrinfo hints{};
    hints.ai_flags = AI_NUMERICHOST;
    addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(numeric, nullptr, &hints, &res);
    if (rc == 0) ::freeaddrinfo(res);
    asm volatile("" ::: "memory");
    return rc;
}

__attribute__((noipa, section("netblock_test"))) hostent* emuByName(const char* name) {
    hostent* h = ::gethostbyname(name);
    asm volatile("" ::: "memory");
    return h;
}

// The emulator calls into the console, which asks for itself.
__attribute__((noipa, section("netblock_test"))) int emuCallsConsole(int (*console)()) {
    const int r = console();
    asm volatile("" ::: "memory");
    return r;
}

// curl inside the emulator's call: curl looks the name up on a thread of
// its own, which only the thread's mark can refuse.
__attribute__((noipa, section("netblock_test"))) int emuCurl(const char* url) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    const int rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    asm volatile("" ::: "memory");
    return rc;
}

int consoleSocket() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) ::close(fd);
    return fd;
}

}  // namespace

int netblock::test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto closed = [](int fd) {
        if (fd >= 0) ::close(fd);
        return fd;
    };

    check(exported(), "every function is the console's, exported for every library");

    const Span code{reinterpret_cast<uintptr_t>(__start_netblock_test),
                    reinterpret_cast<uintptr_t>(__stop_netblock_test)};
    add(0, "netblock-test", "the self-test", &code, 1);

    errno = 0;
    check(closed(emuSocket(AF_INET)) < 0 && errno == ENETDOWN,
          "an emulator's IPv4 socket is refused, with ENETDOWN");
    check(closed(emuSocket(AF_INET6)) < 0, "and its IPv6 socket");
    errno = 0;
    check(closed(emuSocket(AF_PACKET)) < 0 && errno == ENETDOWN, "and a raw one");
    check(closed(emuSocket(AF_UNIX)) >= 0, "a Unix socket is left alone");
    check(emuLookup("127.0.0.1") == EAI_FAIL, "an emulator's lookup is refused");
    check(emuByName("localhost") == nullptr, "and gethostbyname");

    const int before = gRefusedByMark;
    check(emuCurl("http://netblock-test.invalid/") == CURLE_COULDNT_RESOLVE_HOST,
          "curl inside an emulator cannot look a name up");
    check(gRefusedByMark > before, "refused on curl's own thread, by the mark");

    check(consoleSocket() >= 0, "the console's own socket is left alone");
    check(emuCallsConsole(consoleSocket) >= 0,
          "and so is one the console asks for inside an emulator's call");
    addrinfo hints{};
    hints.ai_flags = AI_NUMERICHOST;
    addrinfo* res = nullptr;
    const int rc = ::getaddrinfo("127.0.0.1", nullptr, &hints, &res);
    if (rc == 0) ::freeaddrinfo(res);
    check(rc == 0, "and the console's own lookup");

    {
        std::lock_guard<std::mutex> lock(gLock);
        for (Emulator& e : gEmulators)
            if (e.used && std::strcmp(e.path, "netblock-test") == 0) e = Emulator{};
    }
    check(closed(emuSocket(AF_INET)) >= 0, "a library no longer an emulator's is left alone");

    std::printf("%s\n", failures ? "netblock test: FAILED" : "netblock test: passed");
    return failures ? 1 : 0;
}
