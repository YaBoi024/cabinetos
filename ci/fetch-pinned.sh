#!/usr/bin/env bash
#
# The files the image build would otherwise download from outside, fetched
# once and kept in CI's Actions cache (decided with MMagTech, 2026-10-12, after
# a GitHub 500 on RPCS3's release failed a build).
#
# THE PINS ARE THE INSTALL SCRIPTS' OWN, read out of them here, so there is no
# second copy to drift: each script names its file by a `readonly *_URL=` and
# a `readonly *_SHA256=` line. Moving a pin changes the cache key, and the new
# file is fetched.
#
#   ci/fetch-pinned.sh key                    the cache key: a hash of every
#                                             URL and sha256
#   ci/fetch-pinned.sh fetch <cache> <out>    each file into <cache>/<sha256>
#                                             (from the cache, or downloaded
#                                             and checked), anything else in
#                                             <cache> dropped, and a copy at
#                                             <out>/<sha256> for the image
#
# The install scripts take <out> as /ctx/payload/downloads (lib.sh,
# fetch_pinned) and check the sha256 again themselves, cached or not. Without
# it (an A9 dry run) they download as before.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPTS=(install-rpcs3.sh install-xenia.sh install-vita3k.sh install-xemu-drive.sh)

# "<sha256> <url> <script>" per pin. Only the `readonly NAME=...` lines are
# evaluated, in a subshell, since a URL is built from a version line above it.
pins() {
    local s
    for s in "${SCRIPTS[@]}"; do
        (
            lines="$(grep -E '^readonly [A-Z][A-Z0-9_]*=' "${ROOT}/build_files/${s}")"
            eval "${lines}"
            url="" sha=""
            # The script's own names only: the runner's environment has
            # GITHUB_SERVER_URL and others that end in _URL too.
            while read -r v; do
                case "$v" in
                    *_URL) url="${!v}" ;;
                    *_SHA256) sha="${!v}" ;;
                esac
            done < <(sed -E 's/^readonly ([A-Z][A-Z0-9_]*)=.*/\1/' <<<"${lines}")
            if [[ -z "${url}" || ! "${sha}" =~ ^[0-9a-f]{64}$ ]]; then
                echo "${s}: no readonly *_URL and *_SHA256 pin found" >&2
                exit 1
            fi
            echo "${sha} ${url} ${s}"
        ) || return 1
    done
}

case "${1:-}" in
    key)
        pins | sha256sum | cut -c1-16
        ;;
    fetch)
        cache="${2:?cache directory}"
        out="${3:?output directory}"
        mkdir -p "${cache}" "${out}"
        # Read whole first, so a pin that does not parse stops the script
        # here; a loop fed by `< <(pins)` never sees its status, and would
        # skip that pin and every later one, then drop their cached files.
        pinlist="$(pins)"
        keep=()
        while read -r sha url script; do
            keep+=("${sha}")
            file="${cache}/${sha}"
            if [[ -f "${file}" && "$(sha256sum "${file}" | cut -d' ' -f1)" == "${sha}" ]]; then
                echo "${script}: from the cache"
            else
                rm -f "${file}"
                ok=0
                for attempt in 1 2 3; do
                    if curl -fsSL --retry 3 -o "${file}.part" "${url}"; then ok=1; break; fi
                    echo "${script}: download attempt ${attempt} failed" >&2
                    sleep 10
                done
                if [[ ${ok} -ne 1 ]]; then
                    # Not fatal: the image build downloads it itself, as it
                    # always has, and fails there if it really cannot.
                    echo "${script}: could not download ${url}; the image build will try" >&2
                    rm -f "${file}.part"
                    continue
                fi
                got="$(sha256sum "${file}.part" | cut -d' ' -f1)"
                if [[ "${got}" != "${sha}" ]]; then
                    echo "${script}: ${url} has sha256 ${got}, the pin says ${sha}" >&2
                    rm -f "${file}.part"
                    exit 1
                fi
                mv "${file}.part" "${file}"
                echo "${script}: downloaded, $(stat -c %s "${file}") bytes"
            fi
            cp "${file}" "${out}/${sha}"
        done <<<"${pinlist}"
        # A pin that moved leaves its old file behind; the cache keeps only
        # what the pins name.
        for f in "${cache}"/*; do
            [[ -e "${f}" ]] || continue
            name="$(basename "${f}")"
            if [[ ! " ${keep[*]} " == *" ${name} "* ]]; then
                rm -f "${f}"
                echo "dropped ${name} from the cache: no pin names it now"
            fi
        done
        ;;
    *)
        echo "usage: ci/fetch-pinned.sh key | fetch <cache> <out>" >&2
        exit 2
        ;;
esac
