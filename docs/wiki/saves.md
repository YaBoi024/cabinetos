# Saves, states and sync

Two kinds of saving, both kept on your RomM server for you, in RomM's save
channels (RomM 5.5 or newer).

- **Saves** are the game's own: the memory card, the battery save, the save
  slot in the game's menu. Every system has them.
- **Save states** freeze the whole game at one moment. The built-in emulators
  have them (see [What works where](systems.md#what-works-where)).

## Saves

Nothing to do. A game starts from your newest save on RomM, wherever you made
it: on this console, another CabinetOS console, RomM's web player or another
app. When you leave the game, its save goes up once, if it changed.

- When the save came from anywhere but this console, the game starts with
  *Continuing from another device*. Play on one console only and you never see
  it.
- Saves are written to the console first and sent in the background, so a slow
  or missing server never loses one. Anything not yet sent is sent again later:
  at start-up, every five minutes, and as soon as the server is back.
- **Which save wins:** a save on the console that has not reached the server
  yet; otherwise the newest on the server, by when it was made, not when it
  arrived. A blank memory card never replaces one with saves on it.
- Every way out of a game sends the save: **Exit to Home**, the Power menu
  (Sleep, Restart, Power off), and a separate emulator closing, even if it
  crashed.
- If the console crashes or the power goes out during a game, a save the game
  had already made is kept and sent when the console next starts, on the
  systems whose emulator writes the save while you play: DS, Dreamcast, PS2,
  PSP, 3DS, GameCube, Wii, a Saturn game's backup cartridge, and the systems
  run as separate programs (Switch, PS3, Wii U, Xbox, Xbox 360, Vita, Model 2,
  Model 3). The other systems keep the save inside the emulator until you
  leave, so what was saved in that session is lost.
- The game's page shows when it was last saved: *Saved today, 8:17 PM*.
- On RomM's website a save from the console shows a picture of the game from
  the moment it saved. Sega CD, Neo Geo Pocket Color, 3DO, Xbox and arcade
  games keep their save inside the emulator until you leave, so theirs show
  RomM's own save icon instead.
- A Game Boy game with a clock, such as Pokémon Gold and Silver, keeps its
  clock with its save. A Saturn game saved to the backup cartridge takes the
  cartridge with its save; a cartridge with nothing on it stays on the
  console.
- PS2 and Dreamcast use one memory card per game. GameCube keeps each save as
  its own `.gci` file, as Dolphin does.
- Your Miis are copied into every Wii game.

## The same game on two devices

If you play a game somewhere else while the console has it too, nothing is
lost:

- RomM keeps both saves. The one made later is the game's current save, and
  the console starts from it next time. The other stays on RomM, in the game's
  save history, or for 30 days as a side copy.
- To go back to an older save, open the game's save channel on RomM's website
  and choose **Restore as current**. The console starts from it next time.
- Before another device's save replaces this console's own, the console keeps
  its own aside: one copy per game, in the game's `replaced` folder (see
  [File access](file-access.md)).
- A game you play in hardcore mode in another app keeps its hardcore save as
  it is. The console starts from it, and its own progress goes into a save
  channel of its own on RomM, named *softcore*.

## Save states

![Continue from, on a game page](../media/game.webp)

- **Save state** in the [pause menu](pause-menu.md) saves the moment, with a
  picture of the game, on the console, and says *State saved*. It goes up to
  RomM with the game's save when you leave the game.
- **Load latest state** goes back to your newest one. The game waits on *Press
  (A) to continue* so you are ready.
- A game's page shows your three newest states under **Continue from**, each
  with its picture and time. Press one to play from there.
- **Resume**, the first game on Home, starts from the newest state too.
- Your three newest are the newest from any CabinetOS console. Making a fourth
  sets the oldest aside: RomM keeps it in the game's save history, and the
  console deletes no state on RomM.
- States made in other apps stay on RomM as they are and are not shown here: a
  state loads only in the emulator that made it.
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
starts from the same save it would online. New saves and states wait on the
console and go up when the server is back. If you also played the game on
another device meanwhile, the save made later wins, and the other stays on
RomM, as above.

A RomM older than 5.5 has no save channels. On such a server the console plays
and saves exactly as it does offline, and sends everything by itself within a
minute of the server being updated.

## Saves from before save channels

A save the console or Cabinet left on your server before the console used
RomM's save channels moves into a channel the first time you start that game,
with the game's states. RomM copies it in, and the save it copied stays as it
was, so a console that has not been updated yet goes on using it. Until a game
is started, RomM's website shows its save the old way.

Saves use the same names and formats as Cabinet, the retired Apple TV app, so
a save it left on your server carries on here, except GameCube: Cabinet for Mac
kept a whole memory card, and the console keeps Dolphin's single `.gci` files.

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
  Pocket Color, 3DO, Virtual Boy, Atari 2600, Atari 7800 and Vectrex.
- **Only from the same emulator, until RomM converts them:** DS, Game Boy
  Advance, Mega Drive, Master System, Game Gear, 32X, Xbox and Xbox 360. Their
  emulators keep different files.
- **Arcade games, between CabinetOS consoles only:** FinalBurn Neo, MAME
  2003-Plus, Naomi, Atomiswave, Model 2 and Model 3. A board's memory carries
  nothing that says which emulator, or which version of it, wrote it, so a
  save from another app is never used. MAME keeps its saves on the console.

## Each person's own

Saves, states, play time and favourites belong to the person signed in. See
[Players and accounts](players.md).
