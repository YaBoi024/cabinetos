---
name: check-emulator-update
description: Decide whether an emulator, core or pinned Flatpak should be updated. Returns No, Look closer or Yes against the five reasons in the decision file. Use when asked whether to update one.
---

# Check an emulator update

The rule is `docs/decisions/2026-10-23-when-an-emulator-pin-moves.md`. Read it
first; this skill is only how to apply it. It changes nothing: a Yes is a
proposal, and the maintainer decides.

## Steps

1. **Find the pin and when it was set.** A core: the `COMMIT` in its `case`
   arm in `cores/build-core.sh`. Others: `cores/build-pcsx2.sh`,
   `cores/build-cemu.sh`, `cores/build-sunshine.sh`, `build_files/install-*.sh`
   and `system_files/usr/share/cabinetos/flatpaks.list`. Date it with
   `git log -S<value>` on that file.
2. **Only a session working with MMagTech asks him one question:** has
   anything been wrong with this emulator on the television? (B) A
   contributor's AI, or a run with nobody to ask, judges B from step 3 alone
   and says so.
3. **B, from the record:** open issues (the `emulator` label, and a search for
   the emulator's name), `docs/lessons/emulators.md`, and the index in
   `docs/PROJECT.md`. The changelog says what was fixed, not what is wrong, so
   it is not evidence for B.
4. **D:** the patches table in `docs/ARCHITECTURE.md`, "Moved by a pin of
   ours". For each patch we carry for this emulator: is its upstream report
   closed or released since our pin, and does the patch still apply to
   upstream's current code?
5. **A and C, from upstream.** Do this web reading in a subagent on a cheaper
   model, so the pages stay out of the main session: release notes or progress
   reports since the pin date, and the emulator's own compatibility list
   changes since that date. A counts only what changes what a player can do or
   how a game plays. C counts only a meaningful set of games, judged on the
   emulator's compatibility data. Never cross-reference anyone's library. If it
   has no public compatibility list, say C could not be judged.
6. **E:** a runtime reaching end of life, a security fix in something facing
   the network, or a fix needed for hardware we support.
7. **Cost:** this emulator's row in "Moved by a pin of ours": what we lean on
   and what is checked when the pin moves. Choose the target build after a quiet
   stretch of upstream history, not the newest commit, and note any regression
   and its fix inside the range.

## The verdict

One of **No**, **Look closer**, **Yes**. Then: which reasons (A to E) are met,
each with its evidence (a link, an issue, a count of games); the cost; and what
would change the answer. A reason without evidence is not met. With no reason
met, the verdict is No. Say it in plain words, never as a list of upstream
changes.
