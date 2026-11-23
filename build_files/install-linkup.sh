#!/usr/bin/env bash
#
# What LinkUp's program needs from the image (#654; the program is linkup/, and install-frontend.sh puts
# it in place). GStreamer's `webrtcbin` is already in the base, but it gathers no connections without
# libnice's elements, which it reports only as "libnice elements are not available" when it runs, so
# this adds that one package, and fails the build if anything the program builds its pipelines from is
# not here. The hardware's encoders and decoders are not checked: they register only on a machine with
# the hardware, and a console without them says so in a session.

set -euo pipefail

# shellcheck source=build_files/lib.sh
source /ctx/lib.sh

group_start "Installing what LinkUp needs"

dnf5 -y install --setopt=install_weak_deps=False libnice-gstreamer1

missing=0
for element in webrtcbin nicesrc nicesink sctpenc appsrc valve capssetter queue \
               rtph265pay rtph265depay h265parse rtph264pay rtph264depay h264parse \
               opusenc opusdec rtpopuspay rtpopusdepay audioconvert audioresample \
               pulsesrc appsink glimagesink; do
    if ! gst-inspect-1.0 --exists "${element}"; then
        log "ERROR: LinkUp needs the GStreamer element ${element}, and it is not in the image"
        missing=1
    fi
done
[[ "${missing}" -eq 0 ]] || exit 1
log "LinkUp's GStreamer elements are all here"

group_end
