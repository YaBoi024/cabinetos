#!/usr/bin/env bash
#
# Builds Sunshine, Remote Play's streaming host, as a Fedora 44 package: the
# pinned release, with our change for #304 on it, by LizardByte's own recipe.
#
# WHY WE BUILD IT AT ALL (MMagTech, 2026-10-09, #304). Until then the image
# installed LizardByte's own package, never patched. Sunshine copies one
# display plane to the phone, picks it only at certain moments, and when that
# plane goes empty it waits on it for ever: the phone stays black, with sound,
# until it leaves and joins again (LizardByte/Sunshine#5839). Every change of
# planes we did not foresee did that to a stream: a game in Steam, the way
# back from Steam, Steam switching composing off at its own start. Our change
# (cores/sunshine-patches/) makes it pick again once its plane has been empty
# for a quarter of a second, and is offered upstream. Take it out, and go
# back to LizardByte's package, when a release has it.
#
# HOW: EXACTLY AS LIZARDBYTE BUILDS ITS FEDORA PACKAGE (LizardByte/copr-ci,
# copr-ci.sh): the tag with every submodule, the spec's three values filled
# in, the whole tree as tarball.tar.gz, rpmbuild with its own spec. Its build
# dependencies come from that spec (`dnf builddep`), so they follow the pin.
# What we change besides our patch is only where the spec fetches tools from
# the internet unpinned, and its one check that reaches the internet (below).
#
# WHAT IT ASSERTS: the source is at the tag's commit; each patch applies, and
# is not already in Sunshine (then this says to drop it); the spec still has
# the lines we fill in or pin; Sunshine's own tests pass (the spec's %check).
# WHAT IT CANNOT: that a stream shows a picture. That is seen on the A9.
#
# Usage: cores/build-sunshine.sh
# Output: cores/build/sunshine/{Sunshine-<version>-1.fc44.x86_64.rpm, VERSION}

set -euo pipefail

# LizardByte's release of 2026-09-14, the one the image shipped from
# LizardByte's package before (#286). To move it: the tag and its commit here,
# build, check each patch still applies (or is upstream now), and go through
# build_files/install-sunshine.sh's list on the A9.
VERSION=2026.914.233613
COMMIT=63d35f702ee9e362e43263742981836ec0710384
REPO=https://github.com/LizardByte/Sunshine.git
# The spec installs nvm from its master branch and then whatever Node.js is
# newest that day, for building Sunshine's web page. Pinned here instead, so
# the same pin always builds the same way. Node 24 is the current LTS line;
# vite 8 (Sunshine's web build at this tag) needs 20.19 or later.
NVM_VERSION=v0.40.8
NODE_VERSION=24

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_ROOT="${CABINETOS_CORE_SRC:-$ROOT/.core-src}"
SRC="$SRC_ROOT/sunshine"
WORK="$SRC_ROOT/sunshine-rpm"
BUILDER="${CABINETOS_SUNSHINE_BUILDER:-cabinetos-sunshine-builder}"
OUT="${CABINETOS_SUNSHINE_OUT:-$ROOT/cores/build/sunshine}"
PATCHES="$ROOT/cores/sunshine-patches"

# --- the container ---------------------------------------------------------
if ! podman image exists "$BUILDER"; then
    echo "building $BUILDER"
    podman build -t "$BUILDER" -f "$ROOT/cores/sunshine-builder/Containerfile" \
        "$ROOT/cores/sunshine-builder"
fi

# --- the source, fresh every time ----------------------------------------
# Fresh, because the tarball is the whole tree: nothing a previous run left
# (a build folder, a half-applied patch) may go into the package.
# Through `podman unshare`: rpmbuild unpacks the tarball as the container's
# root, so a previous run's files belong to a user mapped from it, not to us.
podman unshare rm -rf "$SRC" "$WORK"
mkdir -p "$SRC_ROOT" "$WORK"
git clone --quiet --depth 1 --branch "v$VERSION" "$REPO" "$SRC"
HEAD=$(git -C "$SRC" rev-parse HEAD)
if [ "$HEAD" != "$COMMIT" ]; then
    echo "Sunshine v$VERSION is at $HEAD, expected the pinned $COMMIT" >&2
    exit 1
fi
git -C "$SRC" submodule update --quiet --init --recursive --depth 1
# build-deps' tags, which a shallow submodule lacks: its tag names the
# prebuilt FFmpeg this version links with (see the build, below).
git -C "$SRC/third-party/build-deps" fetch --quiet --tags --depth=1
FFMPEG_TAG=$(git -C "$SRC/third-party/build-deps" tag --points-at HEAD | head -1)
[ -n "$FFMPEG_TAG" ] || { echo "Sunshine's build-deps submodule is at no tag" >&2; exit 1; }
echo "sunshine v$VERSION @ $COMMIT, FFmpeg from build-deps $FFMPEG_TAG"

# --- our patches -----------------------------------------------------------
# Each must apply as it is. One that applies in reverse is in Sunshine
# already: the build stops and says so, so it is taken out, not doubled.
shopt -s nullglob
for p in "$PATCHES"/*.patch; do
    if git -C "$SRC" apply --check "$p" 2>/dev/null; then
        git -C "$SRC" apply "$p"
        echo "applied $(basename "$p")"
    elif git -C "$SRC" apply --reverse --check "$p" 2>/dev/null; then
        echo "$(basename "$p") is in Sunshine v$VERSION already: remove it from $PATCHES" >&2
        exit 1
    else
        echo "$(basename "$p") no longer applies to Sunshine v$VERSION: fit it again" >&2
        exit 1
    fi
done
shopt -u nullglob

# --- the spec, as copr-ci.sh fills it in ---------------------------------
SPEC="$WORK/Sunshine.spec"
cp "$SRC/packaging/linux/copr/Sunshine.spec" "$SPEC"
# Every edit checks its anchor (docs/lessons/README.md, rule 10): a spec that
# changed shape stops the build rather than building something else.
edit() {
    local from="$1" to="$2" n
    n=$(grep -cF -- "$from" "$SPEC" || true)
    if [ "$n" -eq 0 ]; then
        echo "Sunshine.spec at v$VERSION has no '$from' to change" >&2
        exit 1
    fi
    FROM="$from" TO="$to" perl -0pi -e 's/\Q$ENV{FROM}\E/$ENV{TO}/g' "$SPEC"
}
edit '%global build_version 0' "%global build_version $VERSION"
edit '%global branch 0' '%global branch master'
edit '%global commit 0' "%global commit $COMMIT"
edit 'nvm-sh/nvm/master/install.sh' "nvm-sh/nvm/$NVM_VERSION/install.sh"
edit 'nvm install node' "nvm install $NODE_VERSION"
edit 'nvm use node' "nvm use $NODE_VERSION"
# THE METAINFO CHECK OFFLINE, as the spec's second one already is
# (appstream-util --nonet). Online it loads the screenshots named in the
# metainfo from LizardByte's website, and one going missing there failed the
# build with nothing changed in Sunshine (2026-11-12: 01-sunshine-welcome-page.png,
# not found). What the file says is still checked.
edit 'appstreamcli validate %{buildroot}' 'appstreamcli validate --no-net %{buildroot}'

# The whole tree, as copr-ci.sh tars it.
tar -czf "$WORK/tarball.tar.gz" -C "$SRC" .

# --- the build, in the container ----------------------------------------
# --nogpgcheck nowhere: builddep reads Fedora's own signed repositories.
#
# GIT MUST TRUST THE TREE (safe.directory), as in build-cemu.sh. Sunshine's
# CMake asks git which build-deps tag its submodule is at, and downloads
# that tag's prebuilt FFmpeg. rpmbuild unpacks the tree owned by another
# user than the container's root, git then refuses to answer, and Sunshine
# falls back to build-deps' NEWEST FFmpeg, which does not link with this
# version (undefined swr_* in libavcodec; A9, 2026-10-09). Checked below.
podman run --rm -v "$WORK":/work:Z -w /work \
    -e GIT_CONFIG_COUNT=1 \
    -e GIT_CONFIG_KEY_0=safe.directory \
    -e GIT_CONFIG_VALUE_0='*' \
    "$BUILDER" bash -c '
    set -euo pipefail
    dnf builddep -y --setopt=install_weak_deps=False /work/Sunshine.spec >/dev/null
    HOME=/work/home rpmbuild -bb \
        --define "_topdir /work/rpmbuild" \
        --define "_sourcedir /work" \
        /work/Sunshine.spec 2>&1 | tee /work/rpmbuild.log
'
if ! grep -qxF -- "-- Using FFmpeg from build-deps tag: $FFMPEG_TAG" "$WORK/rpmbuild.log"; then
    echo "Sunshine's build did not use FFmpeg from build-deps $FFMPEG_TAG, the tag its submodule is at" >&2
    exit 1
fi

# --- what goes into the image ----------------------------------------------
rpm=$(find "$WORK/rpmbuild/RPMS" -name "Sunshine-$VERSION-1.fc44.x86_64.rpm" | head -1)
[ -n "$rpm" ] || { echo "rpmbuild made no Sunshine-$VERSION-1.fc44.x86_64.rpm" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
cp "$rpm" "$OUT/"
# The version and every patch in it, so the image's build log says what it
# installed.
{
    echo "$VERSION $COMMIT"
    for p in "$PATCHES"/*.patch; do
        echo "$(basename "$p") $(sha256sum "$p" | cut -c1-12)"
    done
} > "$OUT/VERSION"
echo "wrote $OUT ($(du -h "$OUT/$(basename "$rpm")" | cut -f1))"
cat "$OUT/VERSION"
