# Your RomM server

Everything CabinetOS plays comes from [RomM](https://github.com/rommapp/romm):
games, covers, BIOS, firmware and keys. Your saves, states, screenshots and play
time go back to it. CabinetOS ships none of them.

- **RomM 5.1 or newer.** Tested with 5.1.0 and 5.3.1.
- One server per console. Every person on the console is a user on that server.

## Setting RomM up for the console

Follow [RomM's own documentation](https://docs.romm.app) to install it and add
your library. These things matter to CabinetOS:

1. **Platform folders.** The console finds a system by RomM's platform slug.
   The slugs it plays are listed in [Systems, formats and BIOS](systems.md).
   Anything RomM recognises with the right slug appears as a Library tile.
2. **Arcade.** One folder for each arcade system, named the way RomM knows
   it. The console shows them all as one Arcade tile, and picks each game's
   emulator from the files in its zip. See [Arcade](arcade.md).
3. **Firmware.** Put BIOS files, firmware and keys in RomM as firmware for the
   platform they belong to. The console downloads them when you first play that
   system. The exact files are in [Systems, formats and BIOS](systems.md).
4. **Games on several discs.** Every disc of a game in one folder, so RomM
   lists them as one game. See [Games on several discs](discs.md).
5. **Manuals.** A game's manual in RomM, found by the ScreenScraper scraper or
   uploaded on the game's page, shows as a book on the console. PDF only;
   other files in a game's `manual` folder are not read.
6. **File names.** Tags in brackets, named as in
   [RomM's folder structure](https://docs.romm.app/latest/getting-started/folder-structure/)
   (`(USA)`, `(Rev 1)`, `(Beta)`), are what tell two games of the same name
   apart on the console.

Covers come from RomM too. A game with no cover shows a coloured tile.

## What the console reads and writes

| Reads | Writes |
|---|---|
| Your platforms, games, covers, manuals and collections | Saves, when you leave a game |
| Firmware for the system you are playing | Save states, each with a picture |
| Your saves and states | Screenshots, to your own RomM gallery |
| Your favourites and play time | Play time |
| | Favourites, when you press the heart |

It never changes or deletes your games, platforms, firmware or users.

## Saves made elsewhere

- Saves made in **Cabinet**, the retired Apple TV app, carry on here: they
  use the same names.
- Saves made in RomM's web player or in other apps are not read, and the
  console leaves them alone on the server.

## When the server is away

The console keeps working with what is on its drive. See
[Offline](library.md#offline).

## Changing or leaving the server

Settings, Network, **RomM server** (behind the PIN if one is set):

- **Change address**: for when the same server moves. Pointing at a different
  server warns first: *Removes every game and account from this console.*
- **Sign out**: removes every game and account from the console and goes back
  to [first run](first-run.md). Saves still waiting to upload are lost, and the
  console says so before you confirm.
