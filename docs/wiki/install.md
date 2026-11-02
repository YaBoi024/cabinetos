# Install

CabinetOS replaces everything on the drive you install it to. It is a whole
operating system, not an app. Back up anything you want to keep first, such as
your Steam games' saves.

## What you need
- **A Steam Deck.** The OLED is tested. The LCD is not tested yet.
- **A [RomM](https://github.com/rommapp/romm) server**, version 5.1 or newer,
  on your network, with your games, BIOS and firmware in it. See
  [Your RomM server](romm.md).
- **A network.** Ethernet or Wi-Fi.
- **A USB stick, 16 GB or larger**, and a computer to write it from.
- **A USB keyboard** for the installer's questions, and a USB-C hub or dock to
  plug the stick and the keyboard into.

## Get the installer

**https://pub-e4881344a12f4bdfa25b269cea81a741.r2.dev/cabinetos.iso**

It is about 8 GB. No account is needed. The link never changes, and it always
gives the newest installer.

To check the download is complete, compare its checksum with the one in
[cabinetos.iso.sha256](https://pub-e4881344a12f4bdfa25b269cea81a741.r2.dev/cabinetos.iso.sha256).
The two should be identical:

- Mac: `shasum -a 256 cabinetos.iso`
- Linux: `sha256sum cabinetos.iso`
- Windows: `certutil -hashfile cabinetos.iso SHA256`

## Install

1. Plug the USB stick and the keyboard into the Deck (through the hub/dock). Switch
   the Deck off. Hold **Volume Down** and press **Power**, and keep holding
   Volume Down until the boot menu appears. Choose your USB stick, then
   **Install CabinetOS**.
2. The installer lists the internal drives (USB drives are never offered). With
   more than one, type the number of the one to use.
3. It asks once: *This will erase this machine's drive (model). Continue?
   (y/n)*. **y** goes on. **n** switches the Deck off with nothing written. The
   whole drive is erased and used. There is no dual boot and no partition
   choice.
4. **Time & Date**: pick your time zone (it is what the console's dark hours
   follow), **Done**, then **Begin Installation**. Nothing else is asked, and no
   network is needed.
5. The Deck restarts by itself into CabinetOS. Take the stick out.
6. On the first start the console installs Eden and xemu, which needs the
   internet for about two minutes. If there is no sound the first time, restart
   the Deck once.
7. Go to Settings > System > System Update and update your system if necessary.

## Already running CabinetOS? No stick needed

On a console that already runs CabinetOS (from the original project's main
image), you can switch to this version without writing a USB stick. Open a
terminal and run:

```bash
sudo bootc switch ghcr.io/yaboi024/cabinetos
sudo systemctl reboot
```

Only the system image changes, so your saves and settings should be kept. This is known to work from a CabinetOS
main install. From plain Bazzite it is not tested.

Next: [First run](first-run.md).
