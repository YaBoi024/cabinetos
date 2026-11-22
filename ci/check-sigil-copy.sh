#!/usr/bin/env bash
#
# Is frontend/third_party/sigil Sigil's own files, unchanged, at the commits
# its VERSION names?
#
#   ci/check-sigil-copy.sh
#
# The console compiles Sigil, RomM's save library, into its own program for
# Switch saves: copied unchanged, with the parts of its two submodules its
# build compiles (frontend/third_party/sigil/VERSION;
# docs/decisions/2026-11-22-sigil-pin-follows-romm.md). This fetches the
# trees of Sigil at the commit VERSION names, and of each submodule at the
# commit Sigil's tree points it at, without the files' contents (about a
# second), and holds every file of the copy but VERSION to the file at the
# same path there, by git's hash of its bytes. A file that differs, or that
# is not there, fails, as does a submodule commit VERSION does not name, or
# GitHub not answering. .github/workflows/checks.yml runs it on every pull
# request; on GitHub each finding is also marked on the pull request. REPORT
# ONLY UNTIL A DATE: while today (UTC) is not past REPORT_ONLY_UNTIL
# (YYYY-MM-DD, set by checks.yml), the findings are warnings and it exits 0.
#
# WHY. The pin says the console runs the Sigil RomM runs. A file edited by
# hand, or copied from another commit, would make that untrue with nothing
# on the pull request to show it.

set -euo pipefail

cd "$(dirname "$0")/.."

COPY=frontend/third_party/sigil
SIGIL_URL=https://github.com/rommapp/argosy-sigil.git

level=error
if [[ -n "${REPORT_ONLY_UNTIL:-}" && ! "$(date -u +%F)" > "${REPORT_ONLY_UNTIL}" ]]; then
    level=warning
fi

found=0
finding() {  # <file> <message>
    echo "$1: $2"
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::${level} file=$1::$2"
    fi
    found=1
}

finish() {
    if (( found )); then
        if [[ "${level}" == warning ]]; then
            echo "Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
            exit 0
        fi
        exit 1
    fi
}

commit=$(sed -n '1s/^sigil [^,]*, commit \([0-9a-f]\{40\}\)$/\1/p' "${COPY}/VERSION")
if [[ -z "${commit}" ]]; then
    finding "${COPY}/VERSION" "its first line names no commit (sigil <version>, commit <40 hex>)"
    finish
fi

work=$(mktemp -d)
trap 'rm -rf "${work}"' EXIT

# A commit's trees, without its files' contents, in ${work}/<name>.
fetch() {  # <name> <url> <commit>
    git init -q "${work}/$1"
    if ! git -C "${work}/$1" fetch -q --depth 1 --filter=blob:none "$2" "$3" 2>"${work}/$1.err"; then
        finding "${COPY}/VERSION" "could not fetch $2 at $3: $(tr '\n' ' ' <"${work}/$1.err")"
        finish
    fi
}

fetch sigil "${SIGIL_URL}" "${commit}"

# Each submodule: its path in Sigil's tree, the commit that tree points it
# at, and its URL in Sigil's .gitmodules at the same commit.
git -C "${work}/sigil" show "${commit}:.gitmodules" >"${work}/gitmodules"
subs=()
while IFS=$'\t' read -r meta path; do
    read -r _ type sub <<<"${meta}"
    [[ "${type}" == commit ]] || continue
    name=$(git config -f "${work}/gitmodules" --get-regexp '^submodule\..*\.path$' |
        awk -v p="${path}" '$2 == p { sub(/^submodule\./, "", $1); sub(/\.path$/, "", $1); print $1 }')
    url=$(git config -f "${work}/gitmodules" --get "submodule.${name}.url")
    if ! grep -qF "${sub}" "${COPY}/VERSION"; then
        finding "${COPY}/VERSION" "Sigil ${commit:0:9} has ${path} at ${sub}, a commit this note does not name"
    fi
    fetch "sub$(( ${#subs[@]} / 2 ))" "${url}" "${sub}"
    subs+=("${path}" "${sub}")
done < <(git -C "${work}/sigil" ls-tree -r "${commit}")

# Every file of the copy, tracked or new (not the ignored build outputs),
# against the file at the same path in Sigil or the submodule it is under.
count=0
while IFS= read -r file; do
    rel=${file#"${COPY}/"}
    [[ "${rel}" == VERSION ]] && continue
    repo=sigil where=Sigil at=${commit} inner=${rel}
    for (( i = 0; i < ${#subs[@]}; i += 2 )); do
        if [[ "${rel}" == "${subs[i]}/"* ]]; then
            repo=sub$(( i / 2 )) where=${subs[i]} at=${subs[i + 1]} inner=${rel#"${subs[i]}/"}
        fi
    done
    theirs=$(git -C "${work}/${repo}" rev-parse -q --verify "${at}:${inner}" 2>/dev/null || true)
    ours=$(git hash-object --no-filters "${file}")
    if [[ -z "${theirs}" ]]; then
        finding "${file}" "not in ${where} at ${at:0:9} (${inner})"
    elif [[ "${theirs}" != "${ours}" ]]; then
        finding "${file}" "differs from ${where}'s ${inner} at ${at:0:9}"
    fi
    count=$(( count + 1 ))
done < <(git ls-files --cached --others --exclude-standard -- "${COPY}" | sort)

finish
echo "Sigil's copy is Sigil ${commit:0:9}'s own files and its submodules', unchanged: ${count} files."
