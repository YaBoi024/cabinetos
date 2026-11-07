# cabinetos-linkup (#654). The one build command, used by CI
# (.github/workflows/build-frontend.yml) and by hand, in the builder container, from
# this folder with the whole repository mounted: it borrows the frontend's fileio.cpp
# and linkup_wire.h, the one file both programs read.
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++20 -Wall -Wextra -I../frontend/src -DGST_USE_UNSTABLE_API

BASE_PKGS := libcrypto json-c libcurl
STREAM_PKGS := glib-2.0 gobject-2.0 gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 \
               gstreamer-sdp-1.0 gstreamer-webrtc-1.0 libpipewire-0.3 gupnp-igd-1.6
BASE_CFLAGS := $(shell pkg-config --cflags $(BASE_PKGS))
BASE_LIBS := $(shell pkg-config --libs $(BASE_PKGS))
STREAM_CFLAGS := $(shell pkg-config --cflags $(STREAM_PKGS))
STREAM_LIBS := $(shell pkg-config --libs $(STREAM_PKGS))

SERVER_SRC := noise.cpp box.cpp keys.cpp server_link.cpp pairing.cpp ../frontend/src/fileio.cpp
SERVER_HDR := noise.h box.h keys.h server_link.h pairing.h jsonutil.h hexutil.h
STREAM_SRC := main.cpp conductor.cpp local_socket.cpp rtc.cpp capture.cpp stream_host.cpp stream_friend.cpp \
              bitrate.cpp portmap.cpp
STREAM_HDR := conductor.h local_socket.h rtc.h capture.h stream_host.h stream_friend.h bitrate.h portmap.h \
              ../frontend/src/linkup_wire.h

cabinetos-linkup: $(STREAM_SRC) $(STREAM_HDR) $(SERVER_SRC) $(SERVER_HDR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) $(STREAM_CFLAGS) -pthread -o $@ $(STREAM_SRC) $(SERVER_SRC) $(BASE_LIBS) $(STREAM_LIBS)

# The Noise test (noise-test.cpp): no network, no hardware.
noise-test: noise-test.cpp noise.cpp noise.h box.cpp box.h
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -o $@ noise-test.cpp noise.cpp box.cpp $(BASE_LIBS)

# A console on the command line, for tools/linkup-interop.sh. Not in the image.
linkup-probe: probe.cpp $(SERVER_SRC) $(SERVER_HDR)
	$(CXX) $(CXXFLAGS) $(BASE_CFLAGS) -pthread -o $@ probe.cpp $(SERVER_SRC) $(BASE_LIBS)

test: noise-test
	./noise-test .

clean:
	rm -f cabinetos-linkup noise-test linkup-probe

.PHONY: clean test
