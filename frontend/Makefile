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
# What every vendored C library starts from, before each adds its own.
C_BASE   := $(CFLAGS)
CFLAGS   += -I$(RC)/include -Isrc -DRC_HASH_NO_ENCRYPTED
CXXFLAGS += -I$(RC)/include -I$(RC)/src -Isrc
OBJ      += $(RC_OBJ)

# SWITCH SAVES: Sigil, the save library RomM builds in, vendored at the commit
# in its VERSION file with the parts of libchdr and tiny-AES-c its own build
# compiles (docs/decisions/2026-11-20-romm-converts-saves.md, switchsave.h).
# Each part gets the definitions and include folders Sigil's CMakeLists gives
# it, and its Release build's NDEBUG; nothing in it is changed. The rest are
# ours, so that it reaches nothing outside itself: no name in it is exported
# from the program (-fvisibility=hidden, and zstd's own switches, which
# otherwise mark its names public), so libarchive, or a core, that asks for
# zstd or libchdr still gets its own; and its zlib is built under the names
# zlib's own Z_PREFIX gives it (`z_inflate`), so the console's own calls to
# zlib (rewind.cpp, arcadematch.cpp, xboxhdd.cpp) still reach the system's.
SG       := third_party/sigil
SG_CHDR  := $(SG)/third_party/libchdr
SG_ZSTD  := $(SG_CHDR)/deps/zstd-1.5.6/lib
SG_ZLIB  := $(SG_CHDR)/deps/zlib-1.3.1
SG_LZMA  := $(SG_CHDR)/deps/lzma-24.05
SG_AES   := $(SG)/third_party/tiny-AES-c
SG_SRC   := $(wildcard $(SG)/src/*.c) $(wildcard $(SG_CHDR)/src/*.c) \
            $(wildcard $(SG_ZSTD)/common/*.c) $(wildcard $(SG_ZSTD)/decompress/*.c) \
            $(wildcard $(SG_ZLIB)/*.c) $(wildcard $(SG_LZMA)/src/*.c) $(SG_AES)/aes.c
SG_OBJ   := $(SG_SRC:.c=.o)
SG_CFLAGS := $(C_BASE) -std=c99 -DNDEBUG -fvisibility=hidden -DZ_PREFIX \
             -DZSTDLIB_VISIBLE= -DZSTDERRORLIB_VISIBLE= -DZDICTLIB_VISIBLE= \
             -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 \
             -DAES128=1 -DCBC=0 -DCTR=1 -DECB=1
SG_ZSTD_I := -I$(SG_ZSTD) -I$(SG_ZSTD)/common
$(SG)/src/%.o: SG_PART := -I$(SG)/include -I$(SG_CHDR)/include -I$(SG_ZLIB) $(SG_ZSTD_I) \
                          -I$(SG_LZMA)/include -I$(SG_AES) -DSIGIL_VERSION_STRING=\"1.0.0\"
$(SG_CHDR)/src/%.o: SG_PART := -I$(SG_CHDR)/include -I$(SG_ZLIB) $(SG_ZSTD_I) \
                               -I$(SG_LZMA)/include -DFLAC__NO_DLL -DFLAC__HAS_OGG=0
$(SG_ZSTD)/%.o: SG_PART := $(SG_ZSTD_I) -DZSTD_DISABLE_ASM=1
$(SG_ZLIB)/%.o: SG_PART := -I$(SG_ZLIB)
$(SG_LZMA)/%.o: SG_PART := -I$(SG_LZMA)/include -D_7ZIP_ST
$(SG_AES)/%.o: SG_PART := -I$(SG_AES)
CXXFLAGS += -I$(SG)/include
OBJ      += $(SG_OBJ)

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

$(SG)/%.o: $(SG)/%.c
	$(CC) $(SG_CFLAGS) $(SG_PART) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP)
	rm -rf build

-include $(DEP)

.PHONY: all clean
