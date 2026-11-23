#!/usr/bin/env bash
#
# Make the image that was TESTED the one that ships, instead of building it
# again. MMagTech, 2026-09-25: the point of `testing` is that a feature is
# built once, tried on his console, and when it is good, that goes to main.
#
# On a push to main: if main's files are exactly the files of the commit the
# current `testing` image was built from, that image is tagged `latest`
# (and latest-<sha>, and the date tags) with no build. Same digest, so the
# same bytes, the same version number and the same signature. If they are
# not the same files (merged without testing, or main moved on since), this
# says so and the workflow builds, as it always did.
#
# "The same files" is the git TREE, not the commit: a merge commit has a new
# hash but, from a branch that was up to date with main, the same tree.
#
# DOCUMENTATION DOES NOT COUNT, EXCEPT docs/LICENCES.md (#156). Every `.md`
# file and everything under docs/ is ignored, the same files build.yml's
# trigger ignores, because none of them is in the image; so a handover or a
# roadmap line written after the testing push no longer costs a rebuild.
# docs/LICENCES.md IS in the image (the Containerfile copies it to
# /licences/), so a change to it still builds. Keep this rule and build.yml's
# `paths` list the same.
#
# Writes promoted=true|false to $GITHUB_OUTPUT.
#
# Usage: ci/promote-tested.sh <registry/image>     needs REGISTRY_USER and
#                                                   REGISTRY_TOKEN to push tags

set -euo pipefail

IMAGE="${1:?usage: ci/promote-tested.sh <registry/image>}"
out() { echo "promoted=$1" >> "${GITHUB_OUTPUT:-/dev/stdout}"; }

if ! info=$(skopeo inspect "docker://${IMAGE}:testing" 2>/dev/null); then
    echo "no testing image to promote; building"
    out false; exit 0
fi
rev=$(jq -r '.Labels["org.opencontainers.image.revision"] // empty' <<<"${info}")
version=$(jq -r '.Labels["org.opencontainers.image.version"] // empty' <<<"${info}")
digest=$(jq -r '.Digest' <<<"${info}")

# AN UNSIGNED IMAGE IS NEVER PROMOTED: every console made by the installer
# refuses it (#135). build.yml moves `testing` only to a digest it signed and
# checked (#398, SCRIPTS-02); this covers a `testing` published before that.
# cosign keeps the signature beside the image under sha256-<hex>.sig, the tag
# the consoles read, whatever tags name the image.
if ! skopeo inspect --raw "docker://${IMAGE}:${digest/:/-}.sig" >/dev/null 2>&1; then
    echo "testing (${digest}) has no signature; building"
    out false; exit 0
fi

if [[ -z "${rev}" ]] || ! git cat-file -e "${rev}^{commit}" 2>/dev/null; then
    echo "testing was built from '${rev:-nothing recorded}', which this checkout does not have; building"
    out false; exit 0
fi

# A file the image is built from: anything but documentation, and the
# licence list, which is documentation the image carries.
in_image() {
    case "$1" in
        docs/LICENCES.md) return 0 ;;
        docs/*|*.md) return 1 ;;
        *) return 0 ;;
    esac
}

differ=()
while IFS= read -r -d '' path; do
    in_image "${path}" && differ+=("${path}")
done < <(git diff -z --no-renames --name-only "${rev}" HEAD)
if [[ ${#differ[@]} -gt 0 ]]; then
    echo "main is not what testing was built from (${rev}); building"
    echo "the files the image is built from that differ (${#differ[@]}):"
    printf '  %s\n' "${differ[@]:0:20}"
    out false; exit 0
fi
docs=$(git diff --name-only "${rev}" HEAD | wc -l | tr -d " ")
if [[ ${docs} -gt 0 ]]; then
    echo "main differs from ${rev} only in documentation (${docs} file(s)), which is not in the image:"
       git diff --name-only "${rev}" HEAD | sed 's/^/  /' | head -20 || true
fi

sha=$(git rev-parse --short HEAD)
date=$(date +%Y%m%d)
echo "main is ${rev} for every file in the image, tested as ${version} (${digest}): promoting it"
for tag in latest "latest-${sha}" "${date}-${sha}" "${date}"; do
    skopeo copy --preserve-digests \
        --dest-creds "${REGISTRY_USER}:${REGISTRY_TOKEN}" \
        "docker://${IMAGE}@${digest}" "docker://${IMAGE}:${tag}"
    echo "  tagged ${tag}"
done

# Read back, not assumed.
now=$(skopeo inspect "docker://${IMAGE}:latest" | jq -r '.Digest')
if [[ "${now}" != "${digest}" ]]; then
    echo "latest is ${now}, not the tested ${digest}" >&2
    exit 1
fi
{
    echo "## Promoted, not built"
    echo
    echo "- \`latest\` is the tested image: version \`${version}\`, built from \`${rev}\`"
    echo "- Digest: \`${digest}\`"
} >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
out true
