---
name: check-emulator-update
description: Decide whether an emulator, core or pinned Flatpak should be updated, or list which ones are worth checking. Returns No, Look closer or Yes against the five reasons in the decision file. Use when asked whether to update one or all of them.
argument-hint: [emulator or core]
---

# Check an emulator update

The rule is `docs/decisions/2026-10-23-when-an-emulator-pin-moves.md`. Read it
first; this skill is only how to apply it. It changes nothing and posts
nothing: a Yes is a proposal, and the maintainer decides.

With one emulator or core named, do the full check. With none named, make the
list.

## The list, when none is named

From the repository and GitHub only, with no web reading. The pins:

    grep -nE '^\s*(REPO|COMMIT)=' cores/build-*.sh
    grep -nE '^readonly [A-Z0-9_]+_(VERSION|RELEASE|URL)=' build_files/install-*.sh
    grep -vE '^\s*(#|$)' system_files/usr/share/cabinetos/flatpaks.list

For each one: the date it was set (step 1), the commits upstream since
(`gh api repos/<owner>/<repo>/compare/<pin>...<branch> --jq .total_commits`),
upstream's newest release and its date, the open issues that name it (step 3),
and whether a report behind one of its patches has closed (step 4).

Suggest a full check only where the list shows a possible reason: an open issue
(B), a closed report behind a patch (D), a runtime at end of life (E), or a
release or progress report since the pin that could hold A or C. Never because
a pin is old or far behind. Each full check reads upstream's pages, so run none
unasked.

## The full check, for one

1. **Find the pin and when it was set.** A core: `REPO` and `COMMIT` in its
   `case` arm in `cores/build-core.sh`. Others: the other `cores/build-*.sh`
   (PCSX2, Cemu, sm2-emu, Supermodel, Sunshine), `build_files/install-*.sh`
   and `system_files/usr/share/cabinetos/flatpaks.list`. Date it with
   `git log -S<value>` on that file. A downloaded build names its upstream
   commit in its version or on its release page. A Flathub pin's version comes
   from `flatpak remote-info --system --log flathub <app>` on the reference
   console (flatpaks.list, "How to move a version"); a session without the
   console says the version could not be read.
2. **Only a session working with MMagTech asks him one question:** has
   anything been wrong with this emulator on the television? (B) A
   contributor's AI, or a run with nobody to ask, judges B from step 3 alone
   and says so.
3. **B, from the record:** open issues (the `emulator` label, and a search for
   the emulator's name), `docs/lessons/emulators.md`, and the index in
   `docs/PROJECT.md`. The changelog says what was fixed, not what is wrong, so
   it is not evidence for B.
4. **D:** the patches table in `docs/ARCHITECTURE.md`, "Moved by a pin of
   ours". For each patch we carry for this emulator, find the upstream commit
   that fixed its report, then run
   `gh api repos/<owner>/<repo>/compare/<fix>...<our pin> --jq .status`:
   - `ahead` or `identical`: our pin already has the fix, so the patch can be
     dropped without moving the pin;
   - `behind`: the fix came after our pin, so D is met;
   - `diverged`: the fix is on another branch; say so.

   A report closed with no fix merged is not D. A patch whose row names no
   upstream report cannot meet D; say so. Also say whether the patch still
   applies to upstream's current code.
5. **A and C, from upstream.** Do this web reading in a subagent on Sonnet, so
   the pages stay out of the main session.
   - **A:** release notes or progress reports since the pin date. Where there
     are none, or a release note is only a list of pull requests, read the
     commit titles since the pin instead:
     `gh api --paginate "repos/<owner>/<repo>/compare/<pin>...<branch>?per_page=100" --jq '.commits[].commit.message | split("\n")[0]'`.
     A counts only what changes what a player can do or how a game plays, not
     new options.
   - **C:** games whose status rose on the emulator's own compatibility list
     since the pin date. A report dated after the pin is a re-test, not a
     change. If the list shows only each game's current status, with no
     history to compare, or there is no public list, C is not met; say which.
     Never cross-reference anyone's library.
6. **E:** a runtime reaching end of life, a security fix in something facing
   the network, or a fix needed for hardware we support.
7. **Cost:** this emulator's row in "Moved by a pin of ours": what we lean on
   and what is checked when the pin moves. Choose the target build after a quiet
   stretch of upstream history, not the newest commit. Search upstream's open
   issues for regressions reported since the pin, and note each with its cause
   and any fix inside the range. A Yes also names the work the move starts:
   CONTRIBUTING.md, "Duties that come with some changes".

## The verdict

One of:
- **No:** no reason is met.
- **Look closer:** a reason looks met, but its evidence is not in hand yet.
  Name the one thing that would settle it, and who can get it.
- **Yes:** at least one reason is met, with its evidence.

Then: which reasons (A to E) are met, each with its evidence (a link, an issue,
a count of games); the cost; and what would change the answer. A reason without
evidence is not met. Say it in plain words, never as a list of upstream
changes. Write it so it can be pasted as it is on the emulator's issue or pull
request, where the decision file says the answer is recorded, and give it to
whoever asked.
