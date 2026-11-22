#!/usr/bin/env bash
#
# Is each library copied into frontend/third_party its own files, unchanged,
# at the commits its VERSION names?
#
#   ci/check-vendored-copies.sh
#
# The console compiles two libraries into its own program from copies of
# their files: rcheevos, RetroAchievements' code, for achievements, and
# Sigil, RomM's save library, for Switch saves, with the parts of its two
# submodules its build compiles. Each folder's VERSION names, on its first
# line, the commit (`<name> <version>, commit <40 hex>`) and, on its second,
# the repository it was copied from; each says what was copied and what left
# out. For each folder this fetches the trees of that commit, and of each
# submodule at the commit the tree points it at, without the files' contents
# (about a second each), and holds every file of the copy but VERSION to the
# file at the same path there, by git's hash of its bytes. A file that
# differs, or that is not there, fails, as does a submodule commit VERSION
# does not name, or GitHub not answering. .github/workflows/checks.yml runs
# it on every pull request; on GitHub each finding is also marked on the pull
# request. REPORT ONLY UNTIL A DATE: while today (UTC) is not past
# REPORT_ONLY_UNTIL (YYYY-MM-DD, set by checks.yml), the findings are
# warnings and it exits 0.
#
# WHY. Each copy is taken unchanged, so that the console runs what its
# makers ship: rcheevos's rules for unlocking, and the Sigil RomM runs
# (docs/decisions/2026-11-22-sigil-pin-follows-romm.md). A file edited by
# hand, or copied from another commit, would make that untrue with nothing on
# the pull request to show it.

set -euo pipefail

cd "$(dirname "$0")/.."

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

work=$(mktemp -d)
trap 'rm -rf "${work}"' EXIT

# A commit's trees, without its files' contents, in ${work}/<name>. False,
# with a finding, when GitHub does not answer.
fetch() {  # <name> <url> <commit> <the VERSION file>
    git init -q "${work}/$1"
    if ! git -C "${work}/$1" fetch -q --depth 1 --filter=blob:none "$2" "$3" 2>"${work}/$1.err"; then
        finding "$4" "could not fetch $2 at $3: $(tr '\n' ' ' <"${work}/$1.err")"
        return 1
    fi
}

# One copy: frontend/third_party/<name>, against its VERSION's commit.
check_copy() {  # <folder>
    local copy=$1 version=$1/VERSION
    local name commit url
    name=$(basename "${copy}")
    commit=$(sed -n '1s/^[^ ]* [^,]*, commit \([0-9a-f]\{40\}\)$/\1/p' "${version}")
    url=$(sed -n '2s/^\(https:\/\/github\.com\/[^ ]*\).*$/\1/p' "${version}")
    if [[ -z "${commit}" || -z "${url}" ]]; then
        finding "${version}" "its first line names no commit (<name> <version>, commit <40 hex>), or its second no GitHub repository"
        return
    fi
    fetch "${name}" "${url}.git" "${commit}" "${version}" || return 0

    # Each submodule: its path in the tree, the commit the tree points it at,
    # and its URL in the tree's .gitmodules at the same commit.
    local subs=() meta path type sub mod modurl
    while IFS=$'\t' read -r meta path; do
        read -r _ type sub <<<"${meta}"
        [[ "${type}" == commit ]] || continue
        git -C "${work}/${name}" show "${commit}:.gitmodules" >"${work}/${name}.gitmodules"
        mod=$(git config -f "${work}/${name}.gitmodules" --get-regexp '^submodule\..*\.path$' |
            awk -v p="${path}" '$2 == p { sub(/^submodule\./, "", $1); sub(/\.path$/, "", $1); print $1 }')
        modurl=$(git config -f "${work}/${name}.gitmodules" --get "submodule.${mod}.url")
        if ! grep -qF "${sub}" "${version}"; then
            finding "${version}" "${name} ${commit:0:9} has ${path} at ${sub}, a commit this note does not name"
        fi
        fetch "${name}.sub$(( ${#subs[@]} / 2 ))" "${modurl}" "${sub}" "${version}" || return 0
        subs+=("${path}" "${sub}")
    done < <(git -C "${work}/${name}" ls-tree -r "${commit}")

    # Every file of the copy, tracked or new (not the ignored build outputs),
    # against the file at the same path in the library or the submodule it is
    # under.
    local count=0 file rel repo where at inner theirs ours i
    while IFS= read -r file; do
        rel=${file#"${copy}/"}
        [[ "${rel}" == VERSION ]] && continue
        repo=${name} where=${name} at=${commit} inner=${rel}
        for (( i = 0; i < ${#subs[@]}; i += 2 )); do
            if [[ "${rel}" == "${subs[i]}/"* ]]; then
                repo=${name}.sub$(( i / 2 )) where=${subs[i]} at=${subs[i + 1]} inner=${rel#"${subs[i]}/"}
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
    done < <(git ls-files --cached --others --exclude-standard -- "${copy}" | sort)
    echo "${copy}: ${count} files read against ${name} ${commit:0:9}$( (( ${#subs[@]} )) && echo " and its submodules")."
}

for version in frontend/third_party/*/VERSION; do
    check_copy "$(dirname "${version}")"
done

if (( found )); then
    if [[ "${level}" == warning ]]; then
        echo "Report only until ${REPORT_ONLY_UNTIL} (UTC): passing. From the next day this fails."
        exit 0
    fi
    exit 1
fi
echo "Every copy in frontend/third_party is its own files, unchanged."
