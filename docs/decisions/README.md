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
- [3ds-games-get-no-network](2026-10-20-3ds-games-get-no-network.md): a 3DS game never reaches a server named in the game; by a patch to Azahar until 2026-10-26, by the block for every emulator since.
- [emulators-get-no-network](2026-10-26-emulators-get-no-network.md): one block keeps every emulator off the network, inside the console's program by who asked and outside it by starting each with no network.

### Controls

- [wii-remote-outside-wii-games](2026-10-24-wii-remote-outside-wii-games.md): a Wii Remote starts only Wii games, holding HOME on one opens the pause menu in every game, and a game's line says *Needs a controller* or *Needs a Wii Remote* when the controller it needs is off.
- [fight-stick-layout](2026-10-24-fight-stick-layout.md): the 8BitDo Arcade Stick over Bluetooth plays arcade games with a fixed fight-stick layout, and every other pad as before.
- [hold-start-opens-arcade-settings](2026-10-25-hold-start-opens-arcade-settings.md): holding Start in an arcade game opens the emulator's own menu, never one of the console's.
- [no-remapping](2026-09-26-no-remapping.md): no button remapping on any system but arcade games, which have Controls.
- [arcade-controls](2026-10-25-arcade-controls.md): an arcade game's pause menu has a Controls row that moves its buttons, per game, kind of controller and person.
- [ds-3ds-screen-view](2026-10-26-ds-3ds-screen-view.md): L3 shows a DS or 3DS game's top screen alone, the bottom alone, then both, and the DS's lid moves to R2.
- [controls-guide-is-a-pdf](2026-10-27-controls-guide-is-a-pdf.md): the printable one-page controls guide is a PDF on the wiki, made from an HTML file beside it.

### Sound and display

- [sound-backup-output](2026-10-26-sound-backup-output.md): the image starts pipewire-pulse at login, so a backup output catches the console's sound while the TV's is gone.

### RomM and the library

### Screens and settings

- [focused-game-names-its-system](2026-10-25-focused-game-names-its-system.md): the focused game's line names its system on Home, in a collection and in Search, and not in a system's own grid.

### The image and updates

### Building and checking

### How we work
