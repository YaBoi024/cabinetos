---
name: check-code-and-docs-agree
description: Read the code and the docs of origin/main against each other, read-only, and report every place they disagree, most important first. Use before a release, with the monthly base update, or when asked to audit the comments and docs.
---

# Check the code and the docs agree

CLAUDE.md, rules 4 to 6, say comments and docs change with the code, and
`docs/lessons/README.md`, rule 8, says why a stale one does harm.
`ci/check-code-and-docs-agree.py` holds each pull request to the mechanical half
of that, on the lines it touches. This is the full read: what a script cannot
judge, and what no one pull request touched.

It reads only. It fixes nothing, files nothing and posts nothing, and it never
switches, pulls or commits in the checkout. What to do about a finding is the
maintainer's call.

## The tree

From the checkout, with `S` a folder in your scratchpad:

    git fetch origin main
    rm -rf "$S" && mkdir -p "$S"
    git archive origin/main | tar -x -C "$S"
    git rev-parse --short origin/main

Read every file from `$S`, and name that commit in the report.

## 1. The check, over the whole tree

    python3 -I "$S/ci/check-code-and-docs-agree.py" --whole-tree origin/main

Run it from the checkout: it reads git, not the folder. Its header says what it
counts and what it cannot see. Each line it prints is a finding, unless it names
another project's file or function, which is a false alarm for its `SKIP`.

## 2. docs/ARCHITECTURE.md

**Section 2, "What each part owns, and must never do".** Each sentence is a
claim. Check each against the code: that part owns that job, and a "never" holds
when you grep for the thing it must not do.

**Section 3, "The main flows, step by step", one step at a time.** Open the file
and the function the step names. It is there, it does what the step says, and it
leads to the next step: who calls whom, in that order. A step that names no code:
find the code that does it.

## 3. The other docs

Read `CONTRIBUTING.md`, `docs/SETTINGS.md`, `docs/wiki/` (not
`docs/wiki/changelog.md`, which records what was true then), `docs/lessons/` and
`README.md` the same way. A lesson is history, so check what it says is true
today, not the story.

**The counts**, wherever any doc or comment gives one, in figures or in words:

- Systems, each distinct name in the playable rows:

      sed -n '/^const Entry kTable\[\] = {/,/^};/p' "$S/frontend/src/catalog.cpp" | grep 'Support::Playable' | sed -E 's/^ *\{"([^"]+)".*/\1/' | sort -u | wc -l

- Libretro cores, one more than this for MAME 0.289 (`cores/fetch-mame.sh`):

      grep -cE '^\s*[a-z0-9_]+\)\s*(#.*)?$' "$S/cores/build-core.sh"

- Emulators that run as programs, Steam not among them:

      ls "$S"/frontend/src/standalone_*.cpp | grep -vc standalone_play

- Files in `frontend/src`: `ls "$S/frontend/src" | wc -l`.

## 4. The code nothing calls

The report comes from the newest pull-request run of "Build CabinetOS image".
Its step runs only on pull requests (`.github/workflows/build-frontend.yml`), and
a pull request of docs alone has no such job, so take the next older run if so.

    run=$(gh run list --workflow build.yml --event pull_request --status completed --limit 1 --json databaseId -q '.[0].databaseId')
    job=$(gh run view "$run" --json jobs -q '.jobs[] | select(.name == "Frontend / Compile") | .databaseId')
    gh run view "$run" --job "$job" --log | cut -f3- | sed -n '/## Unused code/,/Post job cleanup/p'

It read that pull request's code, not `origin/main`, so check each name is still
in `$S`.

## 5. Private addresses

The first step lists each one outside the check's documented examples. This asks
whether any address in the tree, an example included, is a real machine's, by
comparing with the maintainer's local notes. It prints a count and file names, and
**no address, here or in the report**. With no notes file, say this was not done.

    re='(10(\.[0-9]{1,3}){3}|172\.(1[6-9]|2[0-9]|3[01])(\.[0-9]{1,3}){2}|192\.168(\.[0-9]{1,3}){2})'
    grep -rhoIE "$re" "$S" | sort -u > "$S.tree"
    grep -oE "$re" ~/.claude/cabinetos.local.md | sort -u > "$S.notes"
    comm -12 "$S.tree" "$S.notes" > "$S.both"
    wc -l < "$S.both"
    [ -s "$S.both" ] && grep -rlIFf "$S.both" "$S"

A count above zero is a real machine's address in a public repository, and the
first finding.

## The report

The commit read, then a count for each step (step 1 by each of its checks). Then
the findings, most important first: a real address, a doc that sends a person
wrong, a stale name last. One each:

    file:line: what it says. What is true. The fix, in one line.

Say what you could not check. A real fault, where the code is wrong and not the
words, goes in issue form as CLAUDE.md, rule 7, says, for the maintainer to decide.
