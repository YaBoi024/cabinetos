# Saves, states and sync

Two kinds of saving, both kept on your RomM server for you.

- **Saves** are the game's own: the memory card, the battery save, the save
  slot in the game's menu. Every system has them.
- **Save states** freeze the whole game at one moment. The built-in emulators
  have them (see [What works where](systems.md#what-works-where)).

## Saves

Nothing to do. When a game starts, the console fetches your newest save for it
from RomM. When you leave the game, it sends the save back if it changed.

- Saves are written to the console first and sent in the background, so a slow
  or missing server never loses one. Anything not yet sent is sent again later:
  at start-up, every five minutes, and as soon as the server is back.
- The game's page shows when it was last saved: *Saved today, 8:17 PM*.
- On RomM's website a save from the console shows a picture of the game from
  the moment it saved. Sega CD, Neo Geo Pocket Color, 3DO, Xbox and arcade
  games keep their save inside the emulator until you leave, so theirs show
  RomM's own save icon instead.
- Every way out of a game sends the save: **Exit to Home**, the Power menu
  (Sleep, Restart, Power off), and a separate emulator closing, even if it
  crashed.
- **Which copy wins:** a save on the console that has not reached the server
  yet; otherwise the newest one on the server. A blank memory card never
  replaces one with saves on it.
- PS2 and Dreamcast use one memory card per game. GameCube keeps each save as
  its own `.gci` file, as Dolphin does.
- Your Miis are copied into every Wii game.

## Save states

![Continue from, on a game page](../media/game.webp)

- **Save state** in the [pause menu](pause-menu.md) saves the moment, with a
  picture of the game, on the console, and says *State saved*. It goes up to
  RomM with the game's save when you leave the game, so your other consoles
  have it the next time they start the game.
- **Load latest state** goes back to your newest one. The game waits on *Press
  (A) to continue* so you are ready.
- A game's page shows your three newest states under **Continue from**, each
  with its picture and time. Press one to play from there.
- **Resume**, the first game on Home, starts from the newest state too.
- The console offers **your three newest states per game**. Saving a fourth
  sets the oldest aside: RomM keeps it in the game's save history, and the
  console deletes no state on RomM.
- There is no autosave: leaving a game keeps its save, not a state.
- A 3DS state may not load after an update changes the emulator; the console
  says *Couldn't load that state*.

With [In-game shortcuts](navigation.md#in-game-shortcuts) on, the shortcut
button with **R1** saves a state and with **L1** loads the newest.

## Rewind and fast forward

On systems with save states, with In-game shortcuts on: hold the shortcut
button and **L2** to rewind up to about 15 seconds, or **R2** to play at about
four times speed. Rewind is kept in memory only. The 3DS has fast forward but
no rewind.

## Offline

Offline, the console uses the saves it last saw on the server, so a game
starts from the same save it would online. New saves wait on the console and go
up when the server is back.

## Saves from Cabinet

Saves use the same names and formats as Cabinet, the retired Apple TV app, so
a save it left on your server carries on here, except GameCube: Cabinet for Mac
kept a whole memory card, and the console keeps Dolphin's single `.gci` files.

A save the console or Cabinet left on your server before the console used
RomM's save channels moves into a channel the first time you start that game.
RomM copies it in, and the save it copied stays as it was, so a console that
has not been updated yet goes on using it. Until a game is started, RomM's
website shows its save the old way.

## Saves from other apps

A save made in RomM's web player or another app that keeps its saves on RomM,
such as Argosy, carries on here when it is the newest. RomM turns it into the
file the console's emulator reads, the console checks it, and the game starts
with *Continuing from another device*. When it can't be used, the game starts
from the console's own save and says *Couldn't use the newest save*; the other
save stays on RomM. Your saves from the console carry on in those apps the
same way. An app that does not use RomM's save channels can't see them, but
its saves still carry on here.

What crosses between apps depends on the system, by what RomM's save library
(Sigil) does for it:

- **RomM converts the save:** Game Boy, Game Boy Color, Nintendo 64,
  PlayStation, PlayStation 2, PSP, PS Vita, PlayStation 3, Saturn, Sega CD,
  Dreamcast, GameCube, Wii, Wii U, 3DS and Switch. A 3DS or Switch save
  converts only when RomM knows the game's title ID.
- **The save travels as it is,** and the console's check decides whether its
  emulator reads it: NES, Super Nintendo, TurboGrafx-16, TurboGrafx-CD, Neo Geo
  Pocket Color, 3DO, Virtual Boy, Atari 2600, Atari 7800, Vectrex, and arcade
  games in FinalBurn Neo and MAME 2003-Plus.
- **Only from the same emulator, until RomM converts them:** DS, Game Boy
  Advance, Mega Drive, Master System, Game Gear and 32X. Their emulators keep
  different files.
- **Between CabinetOS consoles only, for now:** Xbox, Xbox 360, and arcade
  games in MAME, Naomi, Atomiswave, Model 2 and Model 3.

## Each person's own

Saves, states, play time and favourites belong to the person signed in. See
[Players and accounts](players.md).
