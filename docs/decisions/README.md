# Decisions

What the project decided and why, one small file for each decision. The older ones are in [docs/PROJECT.md](../PROJECT.md), frozen on 2026-10-23: look there when nothing here covers the question.

## When to write one

When you or MMagTech chose between real options and a later reader would ask why it is this way. Not for a bug fix, a choice with no alternative, research, a fault or a build log. Settings decisions stay in [docs/SETTINGS.md](../SETTINGS.md) and "Decided against" stays in [docs/ROADMAP.md](../ROADMAP.md); this folder is for the rest.

## The file

Named `YYYY-MM-DD-short-name.md`, at most 60 lines, in this form:

```
# The decision in a few words

- **Decided:** what, in one to three sentences.
- **Date:** YYYY-MM-DD
- **Why:** the reason a later reader needs.
- **Numbers:** the issues and pull requests, each with its title, or "none" for a decision made at the TV.
- **Replaces:** the decision it replaces, or "none".
```

A decision moved from docs/PROJECT.md names its old question number under Numbers, and the one-line pointer left in PROJECT.md names the new file.

A decision that no longer holds is rewritten or deleted in the same pull request (CLAUDE.md, rule 5), and its line in the index with it.

## Index

One line per file, under the area it belongs to, in the form `- [name](file.md): the decision in a few words.`

### Emulators and cores

- [when-an-emulator-pin-moves](2026-10-23-when-an-emulator-pin-moves.md): a pin moves only for a major feature, a bug or slowness we have seen, more games playing, a patch fixed upstream, or a move we do not choose.

### Controls

### Sound and display

### RomM and the library

### Screens and settings

### The image and updates

### Building and checking

### How we work
