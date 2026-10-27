# Install

CabinetOS replaces everything on the drive you install it to. It is a whole
operating system, not an app.

## What you need
- **A Steam Deck
- **A [RomM](https://github.com/rommapp/romm) server**, version 5.1 or newer,
  on your network, with your games, BIOS and firmware in it. See
  [Your RomM server](romm.md).
- **A network.** Ethernet or Wi-Fi.

A graphics chip without Vulkan still runs the console, but PlayStation 2 and
GameCube need Vulkan, and every system starts on the lowest picture level.

## Get the installer

**https://github.com/YaBoi024/cabinetos**
(about 7-8 GB). No account needed; it is always the newest release.

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

1. Plug in the USB stick and a keyboard. Hold the volume down button and press the power button, keep holding the volume down until the Dock boots up. Use the keyboard to select your USB stick. 
   **Install CabinetOS**.
2. The installer lists the internal drives (USB drives are never offered). With
   more than one, type the number of the one to use.
3. It asks once: *This will erase this machine's 2.0 TB drive (model).
   Continue? (y/n)*. **y** goes on. **n** switches the Deck off with nothing
   written. The whole drive is erased and used. There is no dual boot and no partition
   choice.
4. **Time & Date**: pick your time zone (it is what the console's dark hours
   follow), **Done**, then **Begin Installation**. Nothing else is asked, and no
   network is needed.
5. The Deck restarts by itself into CabinetOS. Take the stick out.
6. Go to Settings > System > System Update and update your system if necessary.

Next: [First run](first-run.md).
