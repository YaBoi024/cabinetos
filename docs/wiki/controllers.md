# Controllers and Bluetooth

![Controllers in Settings](../media/settings-controllers.webp)

## Which controllers work

**Officially supported: Xbox, PlayStation (PS4 and PS5) and Switch
controllers**, over Bluetooth or USB, and the **8BitDo Arcade Stick** (see
[Fight sticks](#fight-sticks)). These are the ones CabinetOS is built and
tested around.

Other pads in the [SDL controller list](https://github.com/mdqinc/SDL_GameControllerDB)
usually work too, with no setup; the list holds over two thousand pads and
arrives with each update. The original Steam Controller is expected to work; the
2026 Steam Controller needs a system update that has not arrived yet. If a pad
misbehaves, [report it](troubleshooting.md). Buttons are matched by
position, so the bottom face button is always **A** (Cross on PlayStation, B
on Nintendo). There is no button-mapping screen, except in arcade games:
their pause menu's [Controls](pause-menu.md#controls) moves the game's
buttons.

Wii Remotes have their own pairing, below.

## Pair a Bluetooth controller

1. Settings, Controllers, **Controllers**, **Add a controller**.
2. Put the controller into pairing mode (see its manual).
3. Its name appears in the list. Press **A** on it. *Paired successfully. Press
   a button on it.*
4. Press any button on the new controller. The console says which player it is.

A wired pad needs nothing: plug it in.

## Players

Up to **four players**. Each controller takes the next free number as it
connects. In a game, a controller that switches off keeps its place (and the
game pauses until any pad resumes it).

Settings, Controllers, **Controllers** lists each controller with its player.
Press one for:

- **Make player N**: swap numbers (not during a game).
- **Forget**: unpair a Bluetooth controller.

Every controller plays as the person signed in. See
[Players and accounts](players.md).

## Batteries

The top bar on Home, the Library, Search and Settings shows the battery of
each controller that reports one (**P1** to **P4**, Wii Remotes **W1** to
**W4**), with a bolt while charging. A notice says when one runs low. Some
controllers do not report their battery, such as an 8BitDo pad in one of its
modes; those show their player number alone, so you still see they are
connected.

## Rumble

Settings, Controllers, **Rumble**: on or off for every controller and every
system. It is on to start with.

## Fight sticks

The **8BitDo Arcade Stick**, paired over Bluetooth in **X mode**, plays arcade
games with a fight-stick layout: the game's buttons in the game's own order
along the top row, then the bottom. A Street Fighter-style game, which numbers
its punches first, gets its punches on top and its kicks below; other games
follow their own numbering, so Mortal Kombat II's top row is Low Punch, Low
Kick, High Punch. A game with four buttons, such as a Neo Geo game, uses the
bottom row, as the cabinet's one row did; one with one to three uses the
bottom row too. Moves that need two buttons at once take two buttons, as on
the cabinet: the combination buttons a pad gets in these games are not on the
stick. To put a game's buttons elsewhere, use [Controls](pause-menu.md#controls)
in its pause menu. Tap **Home** for the pause menu in any game. Other
games play it as an Xbox pad. Wired, or on its 2.4G receiver, it is treated as
an ordinary pad everywhere.

## Wii Remotes

Real Wii Remotes play Wii games, pointer included. **TechKen's Wii Remote Plus
copies are the only officially supported Wii Remote**, MotionPlus included.
Other Wii Remotes, Nintendo's own included, may work, but no fixes or promises
are made for them. On Nintendo's own Remotes MotionPlus cannot work: Linux
turns it off whenever a game turns it on.

1. Settings, Controllers, **Wii Remotes**, **Pair a Wii Remote**.
2. Press the red **sync** button on the Remote (under the battery cover).
3. *Wii Remote paired.* After the first one, the console asks where your
   **sensor bar** is: above or below the TV.

In the menus a Wii Remote held upright works like a pad: the d-pad moves, **A**
selects, **B** (the trigger) goes back, **+** is Start. It switches itself off
after five minutes untouched outside a Wii game.

Wii games that need a Remote are greyed (*Needs a Wii Remote*) until one is
paired. A game that requires MotionPlus, such as Wii Sports Resort, Red Steel 2
or Skyward Sword, is greyed (*Needs MotionPlus*) until a TechKen Remote is
paired; a Nintendo Remote alone does not un-grey it. A game where MotionPlus
is optional plays on either. In any game, hold the Remote's **HOME** button for
a second for the console's pause menu. In a Wii game a short press is the
game's own HOME menu. A Wii Remote starts only Wii games: Play on any other
game says *Needs a controller*. While only Wii Remotes are on,
any other game says *Needs a controller* after its name, until another
controller is on. While no Remote is on, a Wii game that needs one says
*Needs a Wii Remote*, and Play on it says the same:
turn a Remote on, then press Play.

## In-game shortcuts

Off to start with. Turn on **In-game shortcuts** in Settings, Controllers, and
the Home (Guide) button gains shortcuts for save states, rewind, fast forward
and screenshots. See [Buttons and shortcuts](navigation.md#in-game-shortcuts).
