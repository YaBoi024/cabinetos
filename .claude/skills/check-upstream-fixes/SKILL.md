---
name: check-upstream-fixes
description: List every fix CabinetOS is waiting for from another project (a patch, a workaround, a RomM feature) and say which have landed upstream, in a stable release, or in our pin. Use when asked to check patches or upstream fixes.
model: sonnet
---

# Check upstream fixes

CabinetOS carries patches and workarounds until another project fixes
something, and leaves features out until RomM ships them. This skill finds each
of those waits and says which have moved. It reads GitHub only. It changes,
posts, closes and files nothing: what to do about a moved wait is the
maintainer's call. It keeps no list of its own; the waits are found fresh each
time.

## Find the waits

1. **Patches:** the rows of `docs/ARCHITECTURE.md`, "Moved by a pin of ours",
   that name a patch, with the issue and upstream report each names. A row
   that says the patch is not an upstream fault is not a wait. A patch row
   that names no upstream report goes on the list as cannot be checked.
2. **Issues:** open issues that name another project's report:

       gh issue list --state open --limit 200 --json number,title,body

   Look in each body for `owner/repo#N`, or a `github.com/owner/repo/issues/N`
   or `/pull/N` link, to a repository other than `MMagTech/cabinetos`. An issue
   that says it waits on another project but names no report goes on the list
   as cannot be checked.
3. **Code:** comments that name another project's report:

       grep -rnE '[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+#[0-9]+|github\.com/[^/ ]+/[^/ ]+/(issues|pull)/[0-9]+' frontend/src cores build_files system_files wiibridge

   Read the comment around each. A report named as the reason for a
   workaround, a patch or a missing feature is a wait; one cited only as
   history is not.
4. **`docs/NEXT-SESSION.md`, "Waiting on others"**, for waits kept on closed
   issues.

Merge the four into one list: one line per upstream report, with every place
in CabinetOS that waits on it.

## Check each one

1. **Its state** (this works for a pull request too):

       gh api repos/<owner>/<repo>/issues/<n> --jq '.state, .state_reason, .pull_request.merged_at'

2. **The fix.** For a merged pull request, its merge commit. For a report
   closed as completed, the merged pull request in the same project that
   refers to it:

       gh api --paginate "repos/<owner>/<repo>/issues/<n>/timeline?per_page=100" --jq '.[] | select(.event=="cross-referenced" and .source.issue.repository.full_name=="<owner>/<repo>" and .source.issue.pull_request.merged_at != null) | .source.issue.number'

   then `gh api repos/<owner>/<repo>/pulls/<pr> --jq .merge_commit_sha`. With
   no such pull request, the `closed` event's `commit_id` in the same timeline.
   With neither, say the fix could not be found.
3. **Where the fix is.** Against our pin, when we pin that project (the pins
   are listed in `.claude/skills/check-emulator-update/SKILL.md`, "The list,
   when none is named"), and against the project's newest stable release
   (`gh api repos/<owner>/<repo>/releases/latest --jq .tag_name`, which skips
   pre-releases):

       gh api repos/<owner>/<repo>/compare/<fix>...<pin or tag> --jq .status

   `ahead` or `identical` means it holds the fix; `behind` means the fix came
   after. A project that makes no releases is judged on its default branch. A
   project whose code is not on GitHub gets its report's state only.

## Sort and report

Each wait is one of:

- **In our pin already:** the workaround can go without moving anything.
- **Released:** in a stable release, not in our pin. For something we pin, the
  next step is `check-emulator-update <name>` (reason D, for a patch). For
  RomM, which we do not pin, say from which version the console could use
  it; whether it should is the maintainer's call, judged on RomM's
  documentation.
- **Fixed, not released:** merged, but in no stable release yet, or only in
  pre-releases. Nothing to do yet.
- **Closed without a fix:** closed as not planned, or no fix was found. The
  workaround stays unless the maintainer says otherwise.
- **Still waiting:** the report is open.
- **Cannot be checked:** no upstream report is named, or the project's code is
  not on GitHub. Whether to file a report upstream is the maintainer's call.

Report them in that order, so what moved comes first. Give one line each: the
upstream report, our issue, every place in CabinetOS that waits on it, and the
evidence (the fix's pull request, and the release or pin that holds it). Write
it so it can be pasted on our issue as it is, and give it to whoever asked.
