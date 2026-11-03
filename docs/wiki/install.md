# Install

CabinetOS replaces everything on the drive you install it to. It is a whole
operating system, not an app.

## What you need

- **A PC for under the TV.** x86-64, with an AMD Radeon graphics chip. The
  only machine CabinetOS has been tested on is the **GEEKOM A9 Max** (Ryzen AI 9
  HX 370, Radeon 890M). Other AMD machines may work; reports are welcome, with
  the machine named.
- **A USB stick** of 8 GB or more, for the installer.
- **A USB keyboard** for the installer and for first run. After that a
  controller does everything.
- **A controller.** Most Bluetooth and USB pads work. See
  [Controllers and Bluetooth](controllers.md).
- **A [RomM](https://github.com/rommapp/romm) server**, version 5.1 or newer,
  on your network, with your games, BIOS and firmware in it. See
  [Your RomM server](romm.md).
- **A network.** Ethernet or Wi-Fi.

**Handhelds are not supported.** Bazzite keeps a separate image for handhelds
(`bazzite-deck`); CabinetOS builds on Bazzite's desktop image and is made for a
TV, tested on one mini PC by a maintainer who will never own a handheld. It may
run on one, but nothing handheld-specific is tested or planned: the console
uses a classic controller layout only, and a handheld's extra buttons,
trackpads and paddles do nothing on its screens. The one exception is a Steam
Deck inside Steam, which reads the Deck's controls itself. Reports about that
are welcome, but no fix is promised, nor that it keeps working after an update.
Anyone wanting fuller handheld support is welcome to fork CabinetOS.

A graphics chip without Vulkan still runs the console, but PlayStation 2 and
GameCube need Vulkan, and every system starts on the lowest picture level.

## Get the installer

**[Download cabinetos.iso](https://download.mmagtech.com/cabinetos.iso)**
(about 7 GB). No account needed; it is always the newest release. What
has changed in each update: [What's new](changelog.md).

Optional: check it is whole against [its SHA-256](https://download.mmagtech.com/cabinetos.iso.sha256)
(`shasum -a 256 cabinetos.iso`; on Windows `certutil -hashfile cabinetos.iso SHA256`).

The installer only has to be new enough to install. Once installed, the
console updates itself (see [Updates](updates.md)).

## Write it to a USB stick

Any image writer works: Fedora Media Writer, balenaEtcher, Rufus (in DD mode),
or `dd` on Linux:

```bash
sudo dd if=cabinetos.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

`/dev/sdX` is the whole stick, not a partition. Everything on it is erased.

## Firmware settings

Before installing, open the PC's firmware setup (on the A9 Max: **Delete**
during the logo) and change two things:

- **Restore on AC power loss: Power On.** The console comes back by itself
  after a power cut.
- **Secure Boot: Off.**

## Install

1. Plug in the stick and the keyboard. Start the PC and open its boot menu (on
   the A9 Max: **F7** during the logo) and pick the USB stick. The stick
   shows **Install CabinetOS** and starts by itself after a few seconds
   (**Enter** starts it at once).
2. The installer lists the internal drives (USB drives are never offered). With
   more than one, type the number of the one to use.
3. It asks once: *This will erase this machine's 2.0 TB drive (model).
   Continue? (y/n)*. **y** goes on. **n** switches the PC off with nothing
   written.
4. **Time & Date**: pick your time zone (it is what the console's dark hours
   follow), **Done**, then **Begin Installation**. Nothing else is asked, and no
   network is needed.
5. The PC restarts by itself into CabinetOS. Take the stick out.

The whole drive is erased and used. There is no dual boot and no partition
choice.

Next: [First run](first-run.md).
