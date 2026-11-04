# A CI check and a skill keep comments and docs true, not a new rule

- **Decided:** Comments and docs agreeing with the code is held by `ci/check-code-and-docs-agree.py`, which fails a pull request on what it touches, and by the read-only skill `check-code-and-docs-agree`, run before a release or with the monthly base update. No new rule is added.
- **Date:** 2026-11-03
- **Why:** The rules already say it (CLAUDE.md, rules 4 to 6). An audit found 33 places where comments or docs had drifted across the 695 commits since the restructure, and the rules that held were the ones CI checks; the ones only a session remembers slipped. Another rule would be one more to remember. A script cannot judge a sentence or see a stale comment on a line nobody touched, so the skill reads what the check cannot, and the check counts only what is clearly ours, so that another project's name in a comment never fails a contributor.
- **Numbers:** #398, "Audit the code and restructure it for contributors (main.cpp is 20,000 lines)"; #650, "PS3 and Vita installs get no network; comments and docs say what is true", which fixed the 33 first so the check starts from none; #652, "CI checks that comments name what is there, and a skill for the full read".
- **Replaces:** none.
