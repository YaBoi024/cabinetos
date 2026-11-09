#!/usr/bin/env bash
#
# Does the main program's LinkUp code hold any internet? (#654)
#
#   ci/check-linkup-no-network.sh
#
# LinkUp's promise is that off means off: everything on the network is its own program, linkup/, so a
# firewall can tell it from RomM. The main program's LinkUp files reach it through a local Unix socket
# and nothing else. A word on this list in one of them fails, and exits 1. Comments are not read.

set -euo pipefail

cd "$(dirname "$0")/.."

WORDS='AF_INET|AF_INET6|SOCK_DGRAM|SOCK_STREAM|getaddrinfo|gethostbyname|curl_|CURL|romm_http|http://|https://|stun:|turn:'
status=0
for file in frontend/src/linkup*.cpp frontend/src/linkup*.h; do
    # The repository's address on Run your own is a QR code the console shows, and reaches nothing.
    found=$(sed -e 's://.*$::' "${file}" | grep -nE "${WORDS}" | grep -v 'kRepositoryUrl' || true)
    if [[ -n "${found}" ]]; then
        printf '%s holds internet code:\n%s\n' "${file}" "${found}" >&2
        status=1
    fi
done
exit "${status}"
