# Games on several discs

A game on several discs is one game on CabinetOS Handheld: one entry in the library,
one save, and a **Change disc** row in its pause menu. Single-disc games show
none of this.

## On your RomM server

- **Put every disc of a game in one folder**, for example
  `Metal Gear Solid/Metal Gear Solid (Disc 1).chd` and
  `Metal Gear Solid/Metal Gear Solid (Disc 2).chd`. That is RomM's rule: loose
  discs side by side are separate games, each with its own save and no
  Change disc.
- **Discs are numbered from their file names.** `Disc 2`, `Disk 2`, `CD2`,
  `Side B`, `Part 2` and `D2` all work, with or without brackets. With no
  marker at all, the part of the names that differs decides the order
  (`game_a`, `game_b`, `game_c`). An `.m3u` you made yourself sets the order if
  it names every disc.
- **A disc's name in the menu** is *Disc N*, plus any bracketed part only that
  disc's file has: `(Disc 2) (Soviet)` shows as *Disc 2 (Soviet)*. Without one
  you see plain *Disc 1*, *Disc 2*.
- **A disc missing from the folder** shows on the game's page as
  *Disc 2 missing*; the other discs still play.

## Playing

- **Play starts on the disc you were last on** (disc 1 the first time). The
  game's page says which: *Disc 2 of 4*.
- **One save for the whole game.** Every disc reads and writes the same memory
  card, so a save made on disc 1 is there on disc 2.
- **Remove** deletes every disc. Your saves stay.
  On PlayStation and Sega CD the emulator can keep a disc file open after
  you leave the game, so a removed game's space may only come back after the
  console restarts.

## Changing discs

Pause, **Change disc**, pick the disc. The one in the drive is marked
*In the drive*. Then:

| Choice | What it does | When |
|---|---|---|
| **Insert** | Opens the lid, swaps the disc, closes it. The game keeps running. | **The game asks for a disc**: "Insert DISC 2", "Please switch to Disk 2". |
| **Insert and restart** | Swaps the disc and starts the console again on it, like pressing Reset. | **You want a disc from its beginning**: another campaign (Red Alert's Soviet disc, Resident Evil 2's Claire disc), back to disc 1 after the ending, or a save the game says belongs to the other disc. |

Picking the disc already in the drive offers only *insert and restart*: it
takes you back to that disc's title screen.

**Insert only when the game asks.** A disc the game is not expecting can
crash a PlayStation game, as it would hang a real console.

Examples:

- **Metal Gear Solid** says *Insert DISC 2.* at the end of disc 1: Insert,
  Disc 2, then Start as the game says.
- **Resident Evil 2**: Leon is on disc 1, Claire on disc 2. Loading Claire's
  save on disc 1 says *This data is Claire's data*: Insert and restart on
  Disc 2, then load it there.
- **Command & Conquer: Red Alert**: Insert and restart on *Disc 2 (Soviet)*,
  then New Game, for the Soviet campaign.

## By system

| System | Change disc |
|---|---|
| PlayStation, Dreamcast, Sega CD | Insert and Insert and restart. |
| Saturn | Both. Opening the lid always takes a Saturn to its CD player screen, as on the real console: press **Start** there to boot the new disc, then load your save. Insert and restart skips that screen. |
| 3DO | Insert and restart only: its emulator cannot swap a disc during play. |
| GameCube | No menu. The game asks for the disc it needs and it goes in by itself. A GameCube game always starts from disc 1 and shows no *Disc N of M*. |

## Save states

A save state remembers its disc. Loading one puts that disc in first, so a
state made on disc 2 comes back on disc 2 wherever you load it.

On **3DO** a state can only load on its own disc: the console says
*That state is on Disc 3*. Insert and restart on that disc, then Load latest
state. A state picked on the game's page starts the game on its disc by
itself.
