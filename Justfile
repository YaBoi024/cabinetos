# CabinetOS build recipes.
#
# Derived from the Universal Blue image-template (Apache-2.0):
#   https://github.com/ublue-os/image-template
#
# None of this runs on macOS. `build` needs podman on Linux. These are called
# by .github/workflows/build.yml (check, image_name, generate-default-tag,
# build, generate-build-tags, tag-images); `lint` is for running the lint by
# hand. Disk images are build-disk.yml's, which runs bootc-image-builder itself.

set dotenv-filename := "cabinetos.env"
set dotenv-load

export image_name := env_var("IMAGE_NAME")
export repo_organization := env_var("REPO_ORGANIZATION")
export image_desc := env_var("IMAGE_DESC")
export default_tag := env_var("DEFAULT_TAG")
export romm_needed := env_var("ROMM_NEEDED")

[private]
default:
    @just --list

# Check that the Justfile parses. Run by CI before anything else.
#
# This is a syntax check, not a format check. Upstream's template runs
# `just --fmt --check`, which fails on formatting differences — a needless way
# to break a build.
[group('Just')]
check:
    #!/usr/bin/env bash
    set -eou pipefail
    just --summary > /dev/null
    echo "Justfile parses"

[group('Utility')]
[private]
image_name:
    @echo "${image_name}"

[group('Utility')]
generate-default-tag $tag=default_tag:
    @echo "${tag}"

# Build the container image.
[group('Build')]
build $target_image=image_name $tag=default_tag:
    #!/usr/bin/env bash
    set -euox pipefail

    # The frontend, the cores and PPSSPP's system files. Not in this
    # repository and not built here — collected by ci/stage-image-payload.sh,
    # which is what the workflow runs before this. Checked here so the failure
    # is one sentence rather than a podman COPY error thirty lines into a
    # build log.
    if [[ ! -d image_payload ]]; then
        set +x
        echo "image_payload/ is missing." >&2
        echo >&2
        echo "The image carries the frontend, the cores and the other emulators, and" >&2
        echo "neither is in this repository. Build them and collect them:" >&2
        echo >&2
        echo "    ci/stage-image-payload.sh              from this tree" >&2
        echo "    ci/stage-image-payload.sh <artifacts>  from a CI download" >&2
        exit 1
    fi

    LABELS=()
    if [[ -z "$(git status -s)" ]]; then
        GIT_SHA=$(git rev-parse --short HEAD)
        LABELS+=("--label" "org.opencontainers.image.documentation=https://raw.githubusercontent.com/{{ repo_organization }}/{{ image_name }}/${GIT_SHA}/README.md")
        LABELS+=("--label" "org.opencontainers.image.source=https://github.com/{{ repo_organization }}/{{ image_name }}/blob/${GIT_SHA}/Containerfile")
        LABELS+=("--label" "org.opencontainers.image.url=https://github.com/{{ repo_organization }}/{{ image_name }}/tree/${GIT_SHA}")
        LABELS+=("--label" "org.opencontainers.image.revision=${GIT_SHA}")
    fi

    # THE VERSION IS THE DATE, 2026.09.28, numbered by ci/next-version.sh
    # (docs/SETTINGS.md, System). It goes in twice: the label, which is what
    # `bootc upgrade --check` and `bootc status` read, and a file in the image,
    # /usr/share/cabinetos/version, which is what the console reads after
    # booting to say "Updated to ...". A build with no version is `dev`.
    VERSION="${CABINETOS_VERSION:-dev}"
    LABELS+=("--label" "org.opencontainers.image.version=${VERSION}")

    # THE RomM VERSION THE CONSOLE NEEDS, or newer (cabinetos.env). System
    # update reads this label before it offers the image: a console whose
    # server is older is held back (cabinetos-update, updateflow.cpp). It is
    # also the version docs/wiki/romm.md names, so one number is in two
    # places, and the build stops here when they differ.
    if [[ ! "${romm_needed}" =~ ^[0-9]+(\.[0-9]+){1,2}$ ]] ||
       ! grep -qF "**RomM ${romm_needed} or newer.**" docs/wiki/romm.md; then
        set +x
        echo "cabinetos.env says the console needs RomM ${romm_needed}." >&2
        echo "docs/wiki/romm.md must say the same: **RomM ${romm_needed} or newer.**" >&2
        exit 1
    fi
    LABELS+=("--label" "cabinetos.romm-needed=${romm_needed}")

    LABELS+=("--label" "org.opencontainers.image.created=$(date -u +%Y\-%m\-%d\T%H\:%M\:%S\Z)")
    LABELS+=("--label" "org.opencontainers.image.description={{ image_desc }}")
    LABELS+=("--label" "org.opencontainers.image.title={{ image_name }}")
    LABELS+=("--label" "org.opencontainers.image.vendor={{ repo_organization }}")
    LABELS+=("--label" "org.opencontainers.image.licenses=MIT")

    # --timestamp 0 MAKES AN UNCHANGED LAYER COME OUT AS THE SAME BYTES.
    #
    # CI builds from nothing every time. Without this, every file a RUN writes
    # carries the minute it was built, so every layer gets a new digest on every
    # build and a console downloads all of them again, whether their contents
    # moved or not. With it, every file in every layer is stamped 1970-01-01,
    # and a layer whose inputs did not change is byte-identical to last time,
    # so a console already holding it downloads nothing for it. See the layer
    # order in the Containerfile and docs/PROJECT.md open question 27.
    #
    # Nothing is lost by it: ostree stores every file in /usr with a timestamp
    # of zero anyway. It also stamps the image's own creation time as 1970;
    # the real build date is in the org.opencontainers.image.created label.
    podman build \
      "${LABELS[@]}" \
      --timestamp 0 \
      --build-arg "CABINETOS_VERSION=${VERSION}" \
      --pull=newer \
      --tag "${target_image}:${tag}" \
      --file Containerfile \
      .

# Generate the set of tags to publish.
[group('Utility')]
generate-build-tags $target_image=image_name $tag=default_tag:
    #!/usr/bin/env bash
    set -eou pipefail

    DATE=$(date +%Y%m%d)
    BUILD_TAGS=()
    if [[ -z "$(git status -s)" ]]; then
        GIT_SHA=$(git rev-parse --short HEAD)
        BUILD_TAGS+=("${tag}-${GIT_SHA}")
        # The date tags belong to `latest`. A `testing` build is not what
        # shipped that day and must not take the name.
        if [[ "${tag}" == latest ]]; then BUILD_TAGS+=("${DATE}-${GIT_SHA}"); fi
    fi
    if [[ "${tag}" == latest ]]; then BUILD_TAGS+=("${DATE}"); fi
    # The version, 2026.09.28: the registry's record of which numbers a day
    # has used. See ci/next-version.sh.
    if [[ -n "${CABINETOS_VERSION:-}" ]]; then BUILD_TAGS+=("${CABINETOS_VERSION}"); fi
    BUILD_TAGS+=("${tag}")

    echo "${BUILD_TAGS[@]}"

[group('Utility')]
tag-images $target_image=image_name $tag=default_tag tags="":
    #!/usr/bin/env bash
    set -eoux pipefail

    IMAGE=$(podman inspect ${target_image}:${tag} | jq -r .[].Id)
    podman untag ${IMAGE}

    for tag in {{ tags }}; do
        podman tag $IMAGE "${target_image}:${tag}"
    done

    podman images

# Shellcheck the scripts CI lints: the same two shellcheck commands as
# build.yml's lint job, so a script that fails there fails here.
[group('Lint')]
lint:
    #!/usr/bin/env bash
    set -eou pipefail
    # SC1091: shellcheck cannot follow `source /ctx/lib.sh`, because /ctx only
    # exists inside the container build.
    find build_files ci cores -name '*.sh' -print0 \
        | xargs -0 shellcheck -e SC1091
    # The scripts that ship in the image, picked by their first line.
    find system_files -type f -exec \
        sh -c 'head -1 "$1" | grep -q "^#!.*sh" && echo "$1"' _ {} \; \
        | xargs -r shellcheck -e SC1091
