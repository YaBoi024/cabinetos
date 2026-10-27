#!/usr/bin/env bash
# FORK (handheld), NETPLAY PHASE 0 (NETPLAY-PLAN.md): is a core deterministic from a
# cold boot? Runs the same game twice as two separate processes with the same
# scripted two-player button list (--netplay-test, coretests.cpp), then compares
# the per-frame picture and sound digests of the two runs.
#
# Usage: tools/netplay-determinism.sh <frontend-binary> <core.so> <rom> [frames]
#
# Needs whatever the other core tests need (--state-test): a window, since a
# hardware-rendered core needs the GL context. Exits 0 when the runs match.
set -euo pipefail

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <frontend-binary> <core.so> <rom> [frames]" >&2
    exit 2
fi
bin=$1
core=$2
rom=$3
frames=${4:-1200}

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

for run in 1 2; do
    CABINETOS_NETPLAY_FRAMES=$frames CABINETOS_DIGEST_OUT="$dir/run$run.txt" \
        "$bin" --core "$core" --rom "$rom" --netplay-test 2>"$dir/run$run.log" || {
        echo "run $run failed:" >&2
        tail -n 20 "$dir/run$run.log" >&2
        exit 1
    }
    grep '^\[netplay\]' "$dir/run$run.log" || true
done

if cmp -s "$dir/run1.txt" "$dir/run2.txt"; then
    echo "PASS: $frames frames match between two cold boots ($(basename "$core"))"
    exit 0
fi

first=$(diff "$dir/run1.txt" "$dir/run2.txt" | grep -m1 '^[<>]' | awk '{print $2}')
echo "FAIL: the runs part at frame $first ($(basename "$core"))"
exit 1
