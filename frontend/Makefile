# The CabinetOS frontend.
#
# Built inside the container in Containerfile, never on the host: the OS image
# carries no compiler by design, and the development machine is a Mac.
# See CONTRIBUTING.md, "Building" and "Running and testing".

CXX      ?= g++
# -MMD -MP writes a .d file per object listing the headers it included, so
# editing a header rebuilds everything that uses it. Without this, changing a
# signature in ui.h left stale objects linking against the old one, which
# surfaces as an undefined reference rather than as a compile error.
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS  ?=

PKGS     := sdl3 sdl3-image egl glesv2 freetype2 libjpeg libpng libcurl json-c libarchive libsystemd zlib
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS     := $(shell pkg-config --libs $(PKGS)) -lpthread -ldl

# NO EMULATOR REACHES THE NETWORK (src/netblock.cpp, #557). The program's own
# versions of these are exported, so every library's call reaches them before
# the C library's. Without this line they serve only the console's own code
# and every emulator reaches the network: the log says so at a game's start,
# and --netblock-test fails.
NETBLOCK := socket getaddrinfo gethostbyname gethostbyname_r gethostbyname2 \
            gethostbyname2_r gethostbyaddr gethostbyaddr_r getnameinfo pthread_create
LIBS     += $(foreach f,$(NETBLOCK),-Wl,--export-dynamic-symbol=$(f))

SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:.cpp=.o)

# RETROACHIEVEMENTS: rcheevos, vendored at the tag in its VERSION file. It is
# C, so it gets its own rule and flags; nothing in it is changed. Softcore
# needs no encrypted-ROM hashing (3DS only), so that part is compiled out.
RC       := third_party/rcheevos
RC_SRC   := $(wildcard $(RC)/src/*.c) $(wildcard $(RC)/src/rapi/*.c) \
            $(wildcard $(RC)/src/rcheevos/*.c) $(wildcard $(RC)/src/rhash/*.c)
RC_OBJ   := $(RC_SRC:.c=.o)
CC       ?= gcc
CFLAGS   ?= -O2 -MMD -MP
CFLAGS   += -I$(RC)/include -Isrc -DRC_HASH_NO_ENCRYPTED
CXXFLAGS += -I$(RC)/include -I$(RC)/src -Isrc
OBJ      += $(RC_OBJ)

DEP      := $(OBJ:.o=.d)
BIN      := build/cabinetos-frontend

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p build
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)
	@echo "built $@"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RC)/%.o: $(RC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP)
	rm -rf build

-include $(DEP)

.PHONY: all clean
