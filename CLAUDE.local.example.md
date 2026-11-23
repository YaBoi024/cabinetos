# Your machines (example)

Copy this file to `~/.claude/cabinetos.local.md` and fill it in. CLAUDE.md
loads it from there, so every session on your computer sees it, in every
worktree, and git never does. The first session after you create it asks you
once to allow the import.

It lives in your home folder, not in the repository, for two reasons: each
session works in its own copy of the repository, and a line added to
`.gitignore` counts as a change to the console image (build.yml's `paths`), so
it would make `main` build an update with nothing in it.

## The test console

| | |
|---|---|
| Address | `<console address>`, SSH port 2222 (Developer access on) |
| Key | `<path to your SSH key>` |
| Password | shown on the television when Developer access is on |
| Channel | `testing` or `latest` |

## Other test machines

| | |
|---|---|
| `<name>` | `<address>`: what it is for, and what it cannot answer |

## Your RomM server

`<address:port>`. `tools/romm-readonly-relay.py` and
`tools/offline-relay.py` read it from `CABINETOS_RELAY_UP`.

## A RomM server for write tests

`<address:port>`, if you have one: a RomM of your own for tests that write
(CONTRIBUTING.md, "Running and testing"), in `CABINETOS_TEST_ROMM`. Its
admin login, and the rule that writes are allowed there and only there.

## For the tools

The tools in `tools/` take your machines from the environment
(CONTRIBUTING.md, "Running and testing"). Paste these into `~/.zshrc` (or
your shell's start-up file), filled in:

```
export CABINETOS_A9=cabinet@<console address>
export CABINETOS_KEY=<path to your SSH key>        # if not ~/.ssh/cabinetos
export CABINETOS_VM=cabinet@<build VM address>      # only for ui-loop.sh --via-vm
export CABINETOS_RELAY_UP=http://<address:port>     # your RomM server
export CABINETOS_TEST_ROMM=http://<address:port>    # your RomM for write tests
```

The relays and the disc rig run on the console, so there the server goes
on their command line: `CABINETOS_RELAY_UP=http://<address:port> python3
relay.py 16006`.

## Hand-made state on your machines, not in git

Folders and scripts you made by hand on the test console, and what each is
for.
