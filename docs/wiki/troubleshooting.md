# When something is wrong

## Messages a game can show

The system's name in a message is RomM's name for the platform.

| Message | What to do |
|---|---|
| *No Sega Saturn BIOS on the server* (or another system; an arcade game names its folder) | Add the BIOS to RomM as firmware for that platform. File names: [Systems, formats and BIOS](systems.md). |
| *No Nintendo Switch keys on the server* | Add `prod.keys` as Switch firmware in RomM. |
| *Needs newer Nintendo Switch keys on the server* | The game needs keys from a newer firmware. Replace `prod.keys`. |
| *No PlayStation 3 firmware on the server* | Add `PS3UPDAT.PUP` as PS3 firmware. |
| *No PlayStation Vita firmware on the server* | Add Sony's Vita system software, `PSVUPDAT.PUP`, as Vita firmware. |
| *No PlayStation Vita font package on the server* | Add Sony's Vita font package (Sony names it `PSP2UPDAT.PUP`) as Vita firmware too. The Vita needs both files. |
| *Couldn't install the PlayStation Vita firmware* | One of the Vita firmware files on the server is damaged or is not a Vita file. Replace it in RomM. |
| *Encrypted 3DS games can't be played* | The dump is encrypted. Use a decrypted cartridge dump (`.3ds`, `.cci`, `.cxi`). |
| *3DS CIA files can't be played* | A CIA has to be installed, which the console does not do. Use a decrypted cartridge dump instead. |
| *No license for this game on the server* | Put the game's `.rap` file in the same RomM game folder as its PKG. |
| *No Xbox BIOS on the server* | Add the MCPX boot ROM and a flash BIOS as Xbox firmware. |
| *This game's file is a web page, not a game* | The file on the server is a saved download page. Replace it in RomM. |
| *Needs X.X GB more space* | Remove downloads (Settings, Storage, Downloads) or plug in a drive. |
| *Couldn't start this game* | Check the format against [Systems](systems.md#file-formats-in-short). A zipped Dreamcast, GameCube, Wii or Saturn disc will not start: use CHD, RVZ or ISO. |

## A game crashes, freezes or looks wrong

Some games do this inside their emulator, and no setting on the console
changes it. You are brought back to Home and your save is still sent. Every
game plays as its emulator plays it, so a game like this waits for the
emulator to be fixed.

On the PlayStation Vita, Vita3K has a known problem with some games on AMD
graphics under Linux:
[Vita3K/Vita3K#3099](https://github.com/Vita3K/Vita3K/issues/3099).

## The Library

- **A system is missing.** Its RomM platform slug must match the
  [list](systems.md#every-system).
- **A tile says *Emulator not installed*.** Switch and Xbox emulators install
  from Flathub on the first boot with internet (RomM alone is not enough), and
  try again every six hours.
  Leave the console on and connected.
- **A Wii or Wii U game is greyed.** It needs a Wii Remote (pair one) or the Wii
  U GamePad (not supported). *Needs MotionPlus* means it requires MotionPlus,
  which works only on TechKen Remotes here, not on Nintendo's own; see
  [Wii Remotes](controllers.md#wii-remotes).
- **The top bar says *Offline*.** The server cannot be reached. Games on the
  drive still play, and saves wait. See [Offline](library.md#offline).

## Controllers

- **A controller will not pair.** The console pairs without a passkey. Put the
  controller in pairing mode again, and keep it close.
- **A Wii Remote will not pair.** Press the red sync button inside the battery
  cover, not the 1 and 2 buttons.
- **No pause menu with the keyboard.** In PS2 and in Switch, PS3, Xbox, Xbox 360,
  Wii U and PS Vita games, use a pad: both sticks pressed together.

## The screen

- **The TV is black.** The console turned the screen off after the time in
  Settings, Display and Sound. Press any button; that first press only wakes
  it, and buttons do nothing for 3 seconds while the picture comes back.

## Remote Play

- **Steam looks washed out on the device.** Turn on HDR in Moonlight's settings.
- **The device cannot find the console.** Check Streaming is on, and that the
  device is on the same network (or on Tailscale, using the Tailscale address).

## Reporting a problem

1. Settings, **System**, **Diagnostic report**. Scan the code with a
   phone on the same network to download the report, or type the address
   shown under it into a computer. It is also in
   [File access](file-access.md), under `reports`.
2. [Open an issue](https://github.com/MMagTech/cabinetos/issues/new/choose)
   and attach the report. Say what you saw and when.

The report says which machine it is, every version, the settings and the
console's logs, and lists the games that share a name with their file names.
Your server's address, names, Wi-Fi networks, network addresses, serial
numbers and passwords are replaced before it is written, so it is safe to
post. Game titles and those file names stay in.

If the console does not get as far as Settings, a report from the next boot
still has the boot that failed. With Developer access, `cabinetos-report`
makes the same file.

CabinetOS is tested on the GEEKOM A9 Max only, so reports from other machines
are how it gets better on them.
