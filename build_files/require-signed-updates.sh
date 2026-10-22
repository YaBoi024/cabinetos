#!/usr/bin/env bash
#
# Only CabinetOS images signed with our key may be installed as updates (#135).
#
# WHAT SIGNS: every image build signs the image by its digest with the key in
# the SIGNING_SECRET repository secret (build.yml, "Sign image"). A promotion
# moves a tag to an already-signed digest, so promoted images stay signed.
#
# WHAT CHECKS: this. The image carries the public half
# (/etc/pki/containers/cabinetos.pub, from system_files/), the place
# signatures are kept (registries.d/cabinetos.yaml), and a rule in
# /etc/containers/policy.json that an image from ghcr.io/mmagtech/cabinetos
# must carry a signature made with that key. Bazzite's own rule for
# ghcr.io/ublue-os is the same shape, and is left as it is.
#
# THE RULE ONLY BITES ON A CONSOLE SET TO CHECK: one installed by the
# installer (disk_config/iso.toml switches with --enforce-container-sigpolicy),
# or switched once by hand. A console still on "ostree-unverified-registry"
# keeps updating unchecked. MMagTech is the only user (2026-10-09): his A9 is
# switched once by hand, so there is no automatic migration here.
#
# The key itself is MMagTech's: the secret half is in GitHub and his password
# manager, never in this repository (.gitignore lists cosign.key).

set -euo pipefail
source /ctx/lib.sh

readonly PUB=/etc/pki/containers/cabinetos.pub
readonly POLICY=/etc/containers/policy.json
readonly REPO=ghcr.io/yaboi024/cabinetos

group_start "Updates must be signed by CabinetOS (#135)"

grep -q -- "-----BEGIN PUBLIC KEY-----" "${PUB}" || { log "ERROR: no public key at ${PUB}"; exit 1; }
grep -q "${REPO}:" /etc/containers/registries.d/cabinetos.yaml ||
    { log "ERROR: registries.d/cabinetos.yaml does not name ${REPO}"; exit 1; }
[[ -s "${POLICY}" ]] || { log "ERROR: no ${POLICY} in the base image"; exit 1; }

python3 - "${POLICY}" "${REPO}" "${PUB}" <<'PY'
import json, sys
path, repo, pub = sys.argv[1:]
policy = json.load(open(path))
docker = policy.setdefault("transports", {}).setdefault("docker", {})
docker[repo] = [{
    "type": "sigstoreSigned",
    "keyPath": pub,
    "signedIdentity": {"type": "matchRepository"},
}]
json.dump(policy, open(path, "w"), indent=2)
open(path, "a").write("\n")
PY

# CHECKED, NOT ASSUMED: the rule is there, and Bazzite's own is untouched.
python3 - "${POLICY}" "${REPO}" "${PUB}" <<'PY'
import json, sys
path, repo, pub = sys.argv[1:]
docker = json.load(open(path))["transports"]["docker"]
assert docker[repo][0]["keyPath"] == pub, "our rule is missing"
assert "ghcr.io/ublue-os" in docker, "Bazzite's own rule went missing"
PY
log "  ${REPO} must be signed with ${PUB}"
group_end
