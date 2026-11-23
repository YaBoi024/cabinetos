#!/usr/bin/env bash
#
# Check whether the pinned Bazzite base has moved, and work out whether the move
# matters to CabinetOS.
#
# Bazzite rebuilds daily. A dependency bot pointed at it would open a pull
# request every day, and a pull request that arrives every day stops being read.
# So this does five things a bot cannot:
#
#   1. Compares the package manifest of the new base against the one committed
#      at the last bump, so the PR says what actually changed rather than just
#      "digest moved".
#   2. Classifies that change against ci/base-watch.txt — the packages CabinetOS
#      relies on, in areas — and labels the result RELEVANT or ROUTINE. Since
#      2026-10-09 ci/base-report.py does this part, and writes what a person
#      reads: update notes that also serve as the public ones, the checks for
#      the changed areas only (the A9's log, and the TV), and the kernel's
#      fixes for the parts the console uses.
#   3. Flags strip-list drift: removal targets in build_files/ that no longer
#      exist in the base, which means a strip script has silently become a
#      no-op and a desktop application may be creeping back in.
#   4. Checks the new gamescope still takes every flag cabinetos-session
#      and Steam's first download (cabinetos-steam-download) pass it
#      (ci/check-gamescope-flags.sh, #227). One gone makes the update
#      RELEVANT and says which, before it can reach a television; a check
#      that could not tell says why (no --help, or the reading broke), in
#      the pull request and in the run's summary.
#   5. Checks the builders still start from the base's Fedora release: the
#      frontend's and the PCSX2, Cemu and Sunshine builders' `FROM
#      fedora:NN`, and Sunshine's fc44 package name. A base on a newer Fedora
#      makes the update RELEVANT and names the files that must move with it.
#
# The pull request ends with the steps after the merge: the downloads on R2
# are cut again from the new image
# (docs/decisions/2026-11-12-when-the-downloads-are-cut.md), and a GitHub
# release page goes up for the version they install
# (docs/decisions/2026-11-13-a-release-page-per-download.md).
#
# Writes to the working directory:
#   base-manifest.txt   the new base's package list (committed with the bump)
#   /tmp/pr-body.md     the pull request body
#   /tmp/base-notes.md  the update notes alone (the bump's commit message)
#
# Sets these outputs when running under GitHub Actions:
#   changed, relevant, new_version, summary, gamescope (ok, missing; or not
#   checked: no-help, its --help did not run; too-few, the reading broke;
#   failed, the check stopped)

set -euo pipefail

# Byte ordering, so `sort`, `comm` and `join` agree with each other.
export LC_ALL=C

BASE_REPO="${BASE_REPO:-ghcr.io/ublue-os/bazzite}"
BASE_CHANNEL="${BASE_CHANNEL:-stable}"
MANIFEST="base-manifest.txt"
PR_BODY="/tmp/pr-body.md"

say() { echo "[base-update] $*" >&2; }

# wc pads its output on some platforms, and these numbers go into the PR body.
count() { wc -l < "$1" | tr -d '[:space:]'; }

emit() {
    # Set a GitHub Actions output, when there is one to set.
    [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "$1=$2" >> "${GITHUB_OUTPUT}"
    say "$1=$2"
}

# ---------------------------------------------------------------------------
# 1. What are we pinned to now?
# ---------------------------------------------------------------------------

pin_line="$(grep -oE '^FROM[[:space:]]+[^[:space:]]+@sha256:[0-9a-f]{64}' Containerfile | head -1)"
if [[ -z "${pin_line}" ]]; then
    say "ERROR: could not find a digest-pinned FROM line in the Containerfile."
    say "The pin must look like: FROM repo:tag@sha256:<64 hex chars>"
    exit 1
fi

old_ref="${pin_line#FROM}"
old_ref="${old_ref// /}"
old_digest="sha256:${old_ref##*@sha256:}"
old_tag="${old_ref%@*}"
old_tag="${old_tag##*:}"

say "pinned: ${old_tag} (${old_digest})"

# ---------------------------------------------------------------------------
# 2. What is upstream now?
# ---------------------------------------------------------------------------
#
# skopeo reads the manifest and config blob only — a few hundred kilobytes
# rather than the ten gigabytes a pull would cost. Nothing below this point runs
# unless the digest has actually moved.

inspect="$(skopeo inspect "docker://${BASE_REPO}:${BASE_CHANNEL}")"
new_digest="$(jq -r '.Digest' <<<"${inspect}")"
new_version="$(jq -r '.Labels["org.opencontainers.image.version"] // "unknown"' <<<"${inspect}")"

say "upstream ${BASE_CHANNEL}: ${new_version} (${new_digest})"

if [[ "${new_digest}" == "${old_digest}" ]]; then
    say "no change"
    emit changed false
    exit 0
fi

emit changed true
emit new_version "${new_version}"

# ---------------------------------------------------------------------------
# 3. What changed inside it?
# ---------------------------------------------------------------------------

say "pulling the new base to read its package list (this is the slow part)"
podman pull "${BASE_REPO}@${new_digest}" >/dev/null

# gpg-pubkey is dropped, and it is not a detail.
#
# It is not a package — it is an entry in RPM's keyring, and a dozen or so of
# them share the single NAME "gpg-pubkey" with different versions. The version
# comparison below joins the two manifests on NAME, so thirteen entries on each
# side produce a hundred and sixty-nine pairs, and every pair whose versions
# differ is reported as a changed package.
#
# On the 44.20260914 -> 44.20260916 bump that was 156 of 193 reported changes.
# Eighty-one per cent of the diff was keyring churn, and the five firmware
# packages that had genuinely gone BACKWARDS a month were buried in it.
#
# base-watch.txt's own header says a stream of noise stops being read, which is
# worse than not having it at all. This is that, arriving through a door nobody
# was watching.
podman run --rm "${BASE_REPO}@${new_digest}" \
    rpm -qa --queryformat '%{NAME} %{VERSION}-%{RELEASE}\n' \
    | grep -v '^gpg-pubkey ' \
    | sort -u > /tmp/new-manifest.txt

say "new base has $(count /tmp/new-manifest.txt) packages"

# gamescope's flags (#227). Copied out before it runs: the binary carries a
# file capability (cap_sys_nice), and a container refuses to execute it as
# shipped ("Operation not permitted", found on the A9). The copy has none, and
# --help needs none.
podman run --rm "${BASE_REPO}@${new_digest}" \
    sh -c 'cp "$(command -v gamescope)" /tmp/gamescope && /tmp/gamescope --help' \
    > /tmp/gamescope-help.txt 2>&1 || true
# Its exit says which: 1 a flag gone, 2 no --help from the new base, 3 too
# few flags read from a script (the reading broke). Kept apart, here and in
# the run's summary, because the fix is in a different place.
gamescope_check=ok
if ci/check-gamescope-flags.sh /tmp/gamescope-help.txt \
    > /tmp/gamescope-missing.txt 2> /tmp/gamescope-check.txt; then
    say "gamescope takes every flag the session and Steam's download pass"
else
    rc=$?
    # A 1 with nothing named is the check stopping on an error, not a flag.
    if (( rc == 1 )) && [[ -s /tmp/gamescope-missing.txt ]]; then
        gamescope_check=missing
        say "gamescope no longer takes: $(tr '\n' ' ' < /tmp/gamescope-missing.txt)"
    elif (( rc == 2 )); then
        gamescope_check=no-help
        say "could not check gamescope's flags: the new base's gamescope --help did not run"
    elif (( rc == 3 )); then
        gamescope_check=too-few
        say "could not check gamescope's flags: too few read from a script"
    else
        gamescope_check=failed
        say "could not check gamescope's flags: ci/check-gamescope-flags.sh stopped (${rc})"
    fi
fi
cat /tmp/gamescope-check.txt >&2
emit gamescope "${gamescope_check}"

# The builders' Fedora (#487, SCRIPTS-20). They are pinned to a Fedora
# release, not to the base, and a cache hit restores a build made against
# that release, so nothing else says they are behind the day Bazzite moves.
new_fedora="$(podman run --rm "${BASE_REPO}@${new_digest}" rpm -E %fedora)"
: > /tmp/builders-behind.txt
for f in frontend/Containerfile cores/*-builder/Containerfile; do
    if grep -qE '^FROM[[:space:]]+\S*fedora:[0-9]+' "${f}" &&
        ! grep -qE "^FROM[[:space:]]+\S*fedora:${new_fedora}([[:space:]]|$)" "${f}"; then
        echo "${f}: $(grep -oE '^FROM[[:space:]]+\S*fedora:[0-9]+' "${f}" | head -1)" \
            >> /tmp/builders-behind.txt
    fi
done
if grep -qE 'fc[0-9]+' cores/build-sunshine.sh && ! grep -q "fc${new_fedora}" cores/build-sunshine.sh; then
    echo "cores/build-sunshine.sh: the package name's $(grep -oE 'fc[0-9]+' cores/build-sunshine.sh | head -1)" \
        >> /tmp/builders-behind.txt
fi
builders_behind=$(count /tmp/builders-behind.txt)
say "new base is Fedora ${new_fedora}; ${builders_behind} builder file(s) on another release"

first_run=false
if [[ ! -s "${MANIFEST}" ]]; then
    say "no committed manifest — establishing a baseline, no comparison possible"
    first_run=true
    : > /tmp/old-manifest.txt
else
    cp "${MANIFEST}" /tmp/old-manifest.txt
fi

# Package names on each side, for add/remove detection.
awk '{print $1}' /tmp/old-manifest.txt | sort -u > /tmp/old-names.txt
awk '{print $1}' /tmp/new-manifest.txt | sort -u > /tmp/new-names.txt

comm -13 /tmp/old-names.txt /tmp/new-names.txt > /tmp/added.txt
comm -23 /tmp/old-names.txt /tmp/new-names.txt > /tmp/removed.txt

# Version changes: packages present on both sides whose version differs.
: > /tmp/changed.txt
join /tmp/old-manifest.txt /tmp/new-manifest.txt \
    | awk 'NF==3 && $2 != $3 { print $1, $2, "->", $3 }' \
    >> /tmp/changed.txt

say "added=$(count /tmp/added.txt) removed=$(count /tmp/removed.txt) updated=$(count /tmp/changed.txt)"

# ---------------------------------------------------------------------------
# 4. Does any of it matter to us?
# ---------------------------------------------------------------------------

# ci/base-report.py sorts every watched change into its area, writes the
# notes, the checks and the kernel's fixes, and counts what is relevant. The
# watch list is no longer one glob per line (it carries each area's checks),
# so nothing here reads it any more.
: > /tmp/base-report.md
: > /tmp/base-notes.md
echo '{"relevant_count": 0, "areas": []}' > /tmp/base-report.json
if [[ "${first_run}" != true ]]; then
    python3 ci/base-report.py report /tmp/old-manifest.txt /tmp/new-manifest.txt \
        --md /tmp/base-report.md --json /tmp/base-report.json \
        --notes /tmp/base-notes.md \
        --old-version "${old_tag}" --new-version "${new_version}"
fi
relevant_count=$(jq -r '.relevant_count' /tmp/base-report.json)
areas=$(jq -r '.areas | join(", ")' /tmp/base-report.json)

# ---------------------------------------------------------------------------
# 5. Strip-list drift.
# ---------------------------------------------------------------------------
#
# A removal target that existed in the OLD base and does not exist in the new one
# means upstream dropped or renamed it during this update. If it was renamed, the
# real package is still in the image and our removal is now doing nothing —
# a desktop application quietly coming back.
#
# Deliberately scoped to packages that were present before. Removal lists also
# contain speculative entries for packages this base has never shipped;
# remove_pkgs skips those by design and logs them at build time, and reporting
# them here every single update would bury the signal that matters.
# Continuation lines in the remove_pkgs calls are one package name each. The
# grep can also catch a bare shell keyword on its own indented line, so those
# are filtered out — a false positive here is only noise in the report, but
# noise is what this whole script exists to avoid.
grep -hoE '^[[:space:]]+[a-zA-Z0-9][a-zA-Z0-9._+-]*[[:space:]]*\\?$' build_files/strip-*.sh \
    | awk '{print $1}' \
    | grep -vxE 'fi|done|else|esac|then|do|exit|true|false|return|group_end|group_start' \
    | sort -u > /tmp/strip-targets.txt || true

: > /tmp/drift.txt
while read -r target; do
    [[ -z "${target}" ]] && continue
    # Systemd unit names appear in the disable_units calls; they are not packages.
    case "${target}" in *.service|*.timer|*.socket|*.target) continue ;; esac
    if grep -qxF "${target}" /tmp/old-names.txt && ! grep -qxF "${target}" /tmp/new-names.txt; then
        echo "${target}" >> /tmp/drift.txt
    fi
done < /tmp/strip-targets.txt

drift_count=$(count /tmp/drift.txt)

# ---------------------------------------------------------------------------
# 6. Verdict.
# ---------------------------------------------------------------------------

if [[ "${first_run}" == true ]]; then
    verdict="BASELINE"
    summary="Baseline manifest recorded — no comparison was possible."
elif [[ "${gamescope_check}" == missing ]]; then
    verdict="RELEVANT"
    summary="gamescope no longer takes a flag we pass it; the console would lose 4K and VRR, or Install Steam would fail."
elif (( builders_behind > 0 )); then
    verdict="RELEVANT"
    summary="The base moved to Fedora ${new_fedora}; ${builders_behind} builder file(s) still build for another release."
elif (( relevant_count > 0 )); then
    verdict="RELEVANT"
    summary="${relevant_count} change(s) to packages CabinetOS depends on, in: ${areas}."
else
    verdict="ROUTINE"
    summary="No changes to packages CabinetOS depends on."
fi

emit relevant "$([[ "${verdict}" == RELEVANT ]] && echo true || echo false)"
emit summary "${summary}"

# ---------------------------------------------------------------------------
# 7. Write the pull request body.
# ---------------------------------------------------------------------------

{
    echo "## Bazzite base update: \`${old_tag}\` → \`${new_version}\`"
    echo
    echo "**${verdict}** — ${summary}"
    echo
    echo "| | |"
    echo "|---|---|"
    echo "| Channel | \`${BASE_CHANNEL}\` |"
    echo "| Old digest | \`${old_digest}\` |"
    echo "| New digest | \`${new_digest}\` |"
    echo "| Packages | $(count /tmp/new-manifest.txt) |"
    echo

    if [[ "${verdict}" == "BASELINE" ]]; then
        echo "There was no committed \`base-manifest.txt\` to compare against, so"
        echo "this run only records one. The next base update will produce a real"
        echo "diff."
        echo
    fi

    if [[ "${gamescope_check}" == missing ]]; then
        echo "### ⛔ gamescope no longer takes what we pass it"
        echo
        echo "The scripts named below start gamescope with these, and the new"
        echo "gamescope's \`--help\` no longer lists them. gamescope would refuse to"
        echo "start: from \`cabinetos-session\`, the console would fall to a lower"
        echo "rung (no 4K, no VRR, software drawing); from"
        echo "\`cabinetos-steam-download\`, Install Steam would fail at once."
        echo "**Fix the script before merging.**"
        echo
        echo '```'; cat /tmp/gamescope-missing.txt; echo '```'
        echo
    elif [[ "${gamescope_check}" == no-help ]]; then
        echo "### gamescope's flags could not be checked: no \`--help\`"
        echo
        echo "\`gamescope --help\` did not run in the new base, or printed nothing"
        echo "that names \`--backend\`, so nobody has confirmed gamescope still"
        echo "takes what \`cabinetos-session\` and \`cabinetos-steam-download\`"
        echo "pass it. Check by hand before merging. What it printed:"
        echo
        echo '```'; head -20 /tmp/gamescope-help.txt; echo '```'
        echo
    elif [[ "${gamescope_check}" == too-few || "${gamescope_check}" == failed ]]; then
        echo "### gamescope's flags could not be checked: the reading broke"
        echo
        echo "\`ci/check-gamescope-flags.sh\` read fewer flags from a script than"
        echo "it passes today (or stopped), so a flag passed in a way it cannot"
        echo "follow went unchecked. Fix the reader, or its floor if a flag was"
        echo "taken out on purpose, and check gamescope by hand before merging:"
        echo
        echo '```'; cat /tmp/gamescope-check.txt; echo '```'
        echo
    fi

    if (( builders_behind > 0 )); then
        echo "### ⛔ The builders are on another Fedora"
        echo
        echo "The new base is Fedora ${new_fedora}. These still build for another"
        echo "release, so the frontend, PCSX2, Cemu or Sunshine would be built"
        echo "against libraries the image no longer has. Move them in this pull"
        echo "request:"
        echo
        echo '```'; cat /tmp/builders-behind.txt; echo '```'
        echo
    fi

    cat /tmp/base-report.md

    if (( drift_count > 0 )); then
        echo "### ⚠️ Strip-list drift"
        echo
        echo "These packages are named in \`build_files/strip-*.sh\`, existed in the"
        echo "previous base, and are gone from this one. \`remove_pkgs\` skips missing"
        echo "packages silently, so nothing breaks — but if one was **renamed** rather"
        echo "than dropped, the real package is still in the image and the removal is"
        echo "now doing nothing."
        echo
        echo '```'; cat /tmp/drift.txt; echo '```'
        echo
    fi

    echo "### Full package diff"
    echo
    for f in changed added removed; do
        n=$(count "/tmp/${f}.txt")
        (( n == 0 )) && continue
        # Capitalised without ${f^}, which needs bash 4 and is not portable.
        label="$(tr '[:lower:]' '[:upper:]' <<<"${f:0:1}")${f:1}"
        echo "<details><summary>${label} (${n})</summary>"
        echo; echo '```'
        head -500 "/tmp/${f}.txt"
        (( n > 500 )) && echo "... $(( n - 500 )) more"
        echo '```'; echo "</details>"; echo
    done

    echo "---"
    echo
    echo "**Do not merge on a green build alone.** A CabinetOS image that builds"
    echo "is not the same as one that boots. Push this branch to \`testing\`, put"
    echo "that image on the A9, run \`tools/base-checks.sh base-update/${new_version}\`"
    echo "and do the TV checks above, per \`docs/PROJECT.md\` constraint 2."
    echo
    echo "- [ ] **After merging:** cut both downloads from the image the merge puts"
    echo "  on \`latest\`. Build the ISO and the portable from it, install the ISO in"
    echo "  the VM, then publish both from those runs"
    echo "  (\`docs/decisions/2026-11-12-when-the-downloads-are-cut.md\`)."
    echo "- [ ] **After publishing:** put up a GitHub release page for the version"
    echo "  the ISO installs, tagged at the commit in its image's"
    echo "  \`org.opencontainers.image.revision\` label, with the last page's text and"
    echo "  what is new since it, linking the SHA-256 file, never typing it"
    echo "  (\`docs/decisions/2026-11-13-a-release-page-per-download.md\`)."
} > "${PR_BODY}"

# ---------------------------------------------------------------------------
# 8. Apply the bump.
# ---------------------------------------------------------------------------

cp /tmp/new-manifest.txt "${MANIFEST}"

python3 - "${BASE_REPO}" "${new_version}" "${new_digest}" "${BASE_CHANNEL}" <<'PY'
import re, sys, pathlib
repo, version, digest, channel = sys.argv[1:5]
p = pathlib.Path("Containerfile")
s = p.read_text()
new_from = f"FROM {repo}:{version}@{digest}"
s2, n = re.subn(r'^FROM\s+\S+@sha256:[0-9a-f]{64}.*$', new_from, s, count=1, flags=re.M)
assert n == 1, "failed to rewrite the FROM line"

# Keep the "resolved on" comment honest, on the channel this run read.
# Asserted like the FROM line: a reworded comment stops the run rather than
# going stale.
import datetime
today = datetime.date.today().isoformat()
s2, n = re.subn(r'^# Resolved \d{4}-\d{2}-\d{2}:$', f"# Resolved {today}:", s2, count=1, flags=re.M)
assert n == 1, "failed to rewrite the '# Resolved <date>:' comment"
s2, n = re.subn(r'^#   \S+:\S+ == .*$', f"#   {repo}:{channel} == {version}", s2, count=1, flags=re.M)
assert n == 1, "failed to rewrite the '#   <repo>:<channel> == <version>' comment"
p.write_text(s2)
print(f"Containerfile pinned to {new_from}")
PY

say "done: ${verdict}"
