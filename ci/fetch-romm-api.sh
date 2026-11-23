#!/usr/bin/env bash
#
# Take RomM's description of its API at one release, and keep the parts the
# console's client uses (#729).
#
#   ci/fetch-romm-api.sh <release>              writes ci/romm-api.json, the copy
#                                               ci/check-romm-api.py holds the
#                                               client's uses to
#   ci/fetch-romm-api.sh --full <release> <out> writes the whole description to a file
#                                               (romm-api.yml, RomM's newest release)
#
# RomM makes its description itself, from its own code: this runs RomM's own
# dump_openapi.py (in its backend's tools, what RomM's own CI runs) inside RomM's published image
# at that release, with no network and no database. Checked on 2026-11-22 against
# a running 5.5.0-beta.1 server: the same document. Needs podman and python3.
# The release's own name is read from the description, so a tag that names
# another release stops this.
#
# The copy is taken again when the release the console is tested against moves,
# when ROMM_NEEDED moves, or when the uses list gains a call
# (ci/romm-api-uses.txt). Its diff is what RomM changed in the parts the client
# uses: read it with the "A RomM update" duty (CONTRIBUTING.md).

set -euo pipefail

cd "$(dirname "$0")/.."

full=""
if [[ "${1:-}" == "--full" ]]; then
    full=1
    shift
fi
release="${1:?usage: ci/fetch-romm-api.sh [--full] <release> [<out>]}"
[[ -z "${full}" || $# -eq 2 ]] || { echo "ci/fetch-romm-api.sh --full needs an output file" >&2; exit 2; }
out="${2:-}"

image="docker.io/rommapp/romm:${release}"
podman pull -q "${image}" >/dev/null
digest=$(podman image inspect --format '{{index .RepoDigests 0}}' "${image}")

tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT
mkdir "${tmp}/out"
podman run --rm --network none -v "${tmp}/out":/out:Z --entrypoint sh \
    -e ROMM_AUTH_SECRET_KEY=0000000000000000000000000000000000000000000000000000000000000000 \
    -e DB_HOST=127.0.0.1 -e DB_USER=unused -e DB_PASSWD=unused -e DB_NAME=unused \
    -e ROMM_BASE_PATH=/tmp/romm \
    "${image}" -c 'mkdir -p /tmp/romm && cd /backend && python -m tools.dump_openapi /out/openapi.json' \
    >"${tmp}/dump.log" 2>&1 || { cat "${tmp}/dump.log" >&2; exit 1; }

named=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["info"]["version"])' "${tmp}/out/openapi.json")
if [[ "${named}" != "${release}" ]]; then
    echo "${image} describes itself as RomM ${named}, not ${release}." >&2
    exit 1
fi

if [[ -n "${full}" ]]; then
    cp "${tmp}/out/openapi.json" "${out}"
    echo "${out}: RomM ${release}'s whole description, from ${digest}"
else
    python3 ci/check-romm-api.py --trim "${tmp}/out/openapi.json" ci/romm-api.json "${release}" "${digest}"
fi
