# The pause menu

![The pause menu over Zero Gunner 2](../media/pause-menu.webp)

The same menu in every game, on every system.

## Open it

- **Press both sticks in together (L3 + R3).** Always, on any pad.
- **Escape** on a USB keyboard. In PS2, Switch, PS3, Xbox, Xbox 360, Wii U
  and PS Vita games, and Model 2 and Model 3 arcade games, the keyboard does
  not reach the menu: use a pad.
- A tap of the **shortcut button** (Home, unless you chose another), when
  [In-game shortcuts](navigation.md#in-game-shortcuts) are on.
- **Hold HOME** for a second on a Wii Remote, in any game.
- **Hold Select** on a phone or controller streaming over
  [Remote Play](remote-play.md).

The game also pauses by itself when a controller switches off, and when a
Remote Play stream starts or ends.

**B** closes the menu and goes back to the game.

## What is in it

| Item | What it does |
|---|---|
| **Resume** | Back to the game. |
| **Save state** | Saves this exact moment, with a picture, to the console and to RomM. |
| **Load latest state** | Goes back to your newest save state for this game. The game waits on *Press (A) to continue*. |
| **Screenshot** | Saves a picture of the game (without the menu) to your RomM gallery. |
| **Change disc** | Lists the game's discs, the one in the drive marked. **Insert** puts that disc in and the game carries on; **Insert and restart** starts the game again on that disc. Play then carries on from the last disc you used. See [Games on several discs](discs.md). |
| **Picture** | This game's picture level: **Console** (the console's setting), Performance, Balanced or Quality. Takes effect from the next start. |
| **Widescreen** | PS2: **On** plays the game in 16:9 with PCSX2's widescreen patch; **Off** plays it in 4:3 as made. Kept for this game. Takes effect from the next start. |
| **Look** | The screen look for this system: CRT, LCD, sharp or plain. Changes behind the menu straight away. |
| **Screen** / **Colors** | Virtual Boy's screen colours and 3D glasses, Game Boy's colour palettes. |
| **Controls** | Arcade games: puts the game's buttons on the buttons you choose, for this game. See [Controls](#controls). |
| **Exit to Home** | Saves your game to RomM and goes back to its page. |

Rows appear only where they apply:

- **Save state** and **Load latest state**: not on PS2, GameCube, Wii,
  Switch, PS3, Xbox, Xbox 360, Wii U or PS Vita, or in Model 2 and Model 3
  arcade games, which have no save states. A 3DS
  state may not load after an update changes the emulator.
- **Picture**: only on systems with picture levels, and only for the owner
  when a PIN is set.
- **Widescreen**: only on PS2 games that have a widescreen patch.
- **Look**: only on systems that have looks. See [Display and Sound](picture.md#screen-looks).
- **Change disc**: only on a game with several discs. Not on GameCube, which
  changes discs by itself; 3DO has Insert and restart only. On Saturn Insert
  goes through the console's CD player screen: press Start there.
- **Controls**: only in arcade games that name their buttons; not yet in
  Model 2 and Model 3 games.

## Controls

In an arcade game, **Controls** lists the game's buttons by the game's own
names, each with the button on your controller that presses it, such as
*Button 1: A* or *Jab Punch: X*.

1. Highlight one and press **A**. It reads *Button 1: Press a button…*.
2. Press the button you want it on: a face button, a shoulder or a trigger.
   If another of the game's buttons was there, the two swap.

To leave it as it was, press the button it is already on. **B** on the list
goes back to the pause menu. The change works as soon as you go back to the game, and is kept
for this game, for you, and for this kind of controller: every controller of
the same kind uses it. The stick clicks, Start, Select, the d-pad and the
shortcut button can't take a game's button. **Reset to default** puts every
button back.

With two kinds of controller on (a pad and a fight stick, say), the first row
says whose they are, **Player 1** or **Player 2**: press Left or Right to
change it.

Your changes are kept on the console, in `users/<you>/controls/`, one small
file per game, which [File access](file-access.md) shows. They are not sent to
your RomM server.

## The Power menu

Press the PC's **power button** (or **Start** on Home) for **Sleep**,
**Restart** and **Power off**, plus **Switch to Steam** outside a game. In a
game it opens with **Resume** first. Anything other than Resume leaves the game
properly first, saves included. Restart and Power off fade the picture to
black over a second first, without a sound.
