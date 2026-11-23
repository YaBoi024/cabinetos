// What revision is this core, and is it a core at all?
//
// `build-core.sh` asserts the *checkout* is at the pinned commit. That proves
// what went in, not what came out — and the two have come apart before. Every
// Makefile-based core in the set compiles its own revision into the string it
// reports through `retro_get_system_info`:
//
//     GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
//
// which is why the builder container carries git (see frontend/Containerfile).
// Build without git and the core silently forgets which revision it is, which
// is precisely the fact the whole core-parity problem is about.
//
// So this reads the answer back out of the finished artifact and asserts it.
// docs/PROJECT.md, open question 13: "asserts at build time that what it
// produced matches. A mismatch becomes a failed build rather than a save state
// that silently will not load."
//
// No ROM is needed. `retro_get_system_info` is documented as callable before
// `retro_init`, so this runs anywhere the .so loads — including CI, which has
// no game to give it.
//
// Deliberately a single C file with no dependencies beyond dlfcn and
// libretro.h, for the same reason as state-probe.c: the same source must
// compile on macOS arm64 and Linux x86-64, or a difference in the output tells
// you nothing.
//
//   core-info <core.so> [expected-commit]
//
// Build:
//   cc -O2 -o core-info tools/core-info.c -ldl        (Linux)
//   cc -O2 -o core-info tools/core-info.c             (macOS)

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "../frontend/src/libretro.h"

// What `git rev-parse --short` gives on a clean clone. It can grow if a prefix
// is ambiguous, so this is a lower bound on what to look for, not an exact
// length: the match below is a substring test.
#define SHORT_SHA 7

// Same idiom as state-probe.c: assigning through a void** keeps this a plain
// object-pointer conversion, which C allows, rather than the object-to-function
// cast it would otherwise be.
#define SYM(name) \
    do { \
        *(void **)(&name) = dlsym(core, #name); \
        if (!name) { fprintf(stderr, "core-info: missing %s\n", #name); return 1; } \
    } while (0)

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: core-info <core.so> [expected-commit]\n");
        return 2;
    }
    const char *path = argv[1];
    const char *expected = (argc == 3) ? argv[2] : NULL;

    // RTLD_LOCAL is what gives one core's symbols namespace isolation from
    // another's on Linux, and it is the reason the entire prefix-and-merge
    // apparatus Cabinet needed for Apple does not exist here. Load the way the
    // frontend loads, so this tests the real thing.
    void *core = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        // Read dlerror ONCE: it clears itself, and the second read is NULL.
        const char *why = dlerror();
        fprintf(stderr, "core-info: %s: %s\n", path, why ? why : "dlopen failed");
        return 1;
    }

    unsigned (*retro_api_version)(void);
    void (*retro_get_system_info)(struct retro_system_info *);
    SYM(retro_api_version);
    SYM(retro_get_system_info);

    // Not a warning. A core built against a different libretro.h than the
    // frontend's is not loadable by the frontend, whatever else is true of it.
    unsigned api = retro_api_version();
    if (api != RETRO_API_VERSION) {
        fprintf(stderr, "core-info: libretro API %u, frontend expects %u\n",
                api, RETRO_API_VERSION);
        return 1;
    }

    struct retro_system_info info;
    memset(&info, 0, sizeof info);
    retro_get_system_info(&info);

    const char *name = info.library_name ? info.library_name : "";
    const char *version = info.library_version ? info.library_version : "";

    printf("name        %s\n", name);
    printf("version     %s\n", version);
    printf("extensions  %s\n", info.valid_extensions ? info.valid_extensions : "");
    printf("api         %u\n", api);
    printf("fullpath    %s\n", info.need_fullpath ? "yes" : "no");

    if (!expected)
        return 0;

    if (strlen(expected) < SHORT_SHA) {
        fprintf(stderr, "core-info: expected-commit %s is shorter than a short SHA\n",
                expected);
        return 2;
    }

    char shortsha[SHORT_SHA + 1];
    memcpy(shortsha, expected, SHORT_SHA);
    shortsha[SHORT_SHA] = '\0';

    if (!strstr(version, shortsha)) {
        fprintf(stderr,
                "core-info: %s reports version \"%s\", which does not carry the\n"
                "           pinned revision %s. Either it was built from a\n"
                "           different commit, or git was missing from the build\n"
                "           container and the core does not know what it is.\n",
                name, version, shortsha);
        return 1;
    }

    printf("revision    %s, as pinned\n", shortsha);
    return 0;
}
