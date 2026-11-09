# First run

The first time CabinetOS Handheld starts, it walks through four steps. Keep the USB
keyboard plugged in until a controller is paired.

**On the keyboard:** arrows move, Return or Space selects, Escape or Backspace
goes back. **On a pad:** the d-pad moves, **A** selects, **B** goes back. A
mouse works too.

## Time zone (running from a USB drive)

Only on a console with no time zone yet, which is one running from a USB
drive: the installer's Time & Date screen sets it on every other. Pick your
region, then a city in your time zone; **Continue** stays greyed until you
have. The lists are long, so a letter on the keyboard jumps to the first city
that starts with it, and **L1** and **R1** (Page Up and Page Down) move a page.
There is no time zone in Settings: to change it, press **Back** on the next
screen.

## 1. Connect to a network

The console needs a network to reach your RomM server, so **Continue** stays
greyed until it is online.

- **Ethernet:** plug in the cable and it connects by itself.
- **Wi-Fi:** pick your network from the list. A secured network opens the
  on-screen keyboard for the password (the character just typed stays visible;
  **show** reveals the rest). *Wrong password* opens the keyboard again.
- Networks that need a user name as well as a password (802.1X, as used by
  many offices and schools) show *not supported*.
- Hidden networks cannot be joined.

## 2. RomM server

Type the address you open RomM at in a browser, without `http://`, for
example `192.168.0.20:8080` or `romm.local:8080`. The keyboard has `.local`
and `:8080` keys to save typing.

- Type the port if your server uses one. None is added for you.
- Addresses on your own network try `http` first, others try `https` first.
  Type `https://` or `http://` in front to choose.
- *Found RomM 5.3.1 at ...* means it worked. *Nothing answered at that
  address* means the console could not reach it: check the address in a
  browser on the same network.

## 3. Pair with RomM

The console never asks for your RomM password. It shows a QR code and a link
instead.

1. Scan the code with a phone (or open the link) in a browser that is signed
   in to RomM, as the person who will play.
2. Approve **CabinetOS** in RomM.
3. The TV says *Approved* and moves on.

A code that expires or is refused in RomM is replaced by a new one by itself.
**Start again** makes a new one at any time.

The console asks RomM only for what it needs: to read your games, platforms,
covers, collections and firmware, and to write your saves, states, screenshots,
play time and favourites. It never asks to change or delete games, platforms,
firmware or users.

The first person paired becomes the console's **owner**. Add more people
later: see [Players and accounts](players.md).

## 4. Pair a controller

Optional, and **Skip** moves on. Put a Bluetooth controller into pairing mode
and press its name in the list when it appears. A wired pad, or one that is
already paired, completes the step by itself.

## Ready

*You can unplug the keyboard.* Press **Start playing** and you are on Home.

Switch and Xbox games show greyed, *Emulator not installed*, for a few
minutes: the console downloads those two emulators once it has internet.

If the console was set up before and signed out (Settings, Network, RomM
server, Sign out), it starts again at the server step.

Next: [Your RomM server](romm.md).
