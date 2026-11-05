# Run from a USB drive

Try CabinetOS on a PC without installing it: it runs from a USB drive, and the
PC's own drives stay as they are.

**This is an alpha.** It is built to leave your PC alone, but it has only been
tried on the one PC below. Back up anything you can't lose before you start.

## What has been tried

**Tried:**

- One PC with Windows, a Beelink SER mini PC (Ryzen 5 5560U), with Secure Boot
  off, from a 125 GB SanDisk USB 3.2 stick: setup, a game, an update, a restart.
  With the stick out, Windows started as normal.
- Secure Boot on and off, in a virtual machine, on every build of the file.

**Not tried yet:**

- A real PC with Secure Boot on. The blue screen below has only run in a virtual
  machine, and some PCs may never show it.
- A PC with BitLocker on.
- Rufus and Etcher. Both say they take this kind of file.
- An SSD or NVMe drive in a USB case.

## What it does to your PC

- **Your PC's own drives:** the console never opens them, and neither the console
  nor Steam lists them.
- **Starting your PC:** it adds no boot entry, and with the drive out your PC
  starts as it did before. A rare kind of system update could add one entry later.
- **Windows's clock:** it stays right.
- **The PC's firmware:** like any Linux USB drive, it stores one small setting of
  its own there. With Secure Boot on, you also add a key, and that key stays (see
  [the blue screen](#the-blue-screen-secure-boot-on)).

## What you need

Everything on the [Install](install.md) page's list except the stick for the
installer, plus:

- **A USB drive of 16 GB or more.** Everything on it is erased, and CabinetOS
  uses all of it. An SSD or NVMe drive in a USB case should run best, though none
  has been tried yet. A USB stick works, but it is slow
  ([see below](#on-a-usb-stick)).
- **A USB keyboard.** The blue screen and first run both need one.
- **[balenaEtcher](https://etcher.balena.io) or
  [Rufus](https://rufus.ie)** to write the file.

## Before you start

**If Windows on this PC uses BitLocker, save your recovery key first.** You can
find it in [your Microsoft account](https://account.microsoft.com/devices/recoverykey).
Starting from a USB drive can make Windows ask for it the next time it starts.

Leave your PC's Secure Boot setting as it is. Changing it is one of the things
that makes Windows ask.

## Get the file

**[Download cabinetos-portable.img.xz](https://download.mmagtech.com/cabinetos-portable.img.xz)**
(about 5.6 GB). No account needed. Don't unpack it: both tools read it as it is.

Optional: check it is whole against
[its SHA-256](https://download.mmagtech.com/cabinetos-portable.img.xz.sha256)
(`shasum -a 256 cabinetos-portable.img.xz`; on Windows
`certutil -hashfile cabinetos-portable.img.xz SHA256`).

## Write it to the drive

**Everything on the drive you pick is erased, so check you have the right one.**

- **Etcher:** **Flash from file** and pick the file, **Select target** and pick
  the drive, then **Flash!**
- **Rufus:** pick the drive under **Device**, **SELECT** and pick the file, then
  **START**. An SSD in a USB case only shows if you open **Show advanced drive
  properties** and tick **List USB Hard Drives**.

## Start from it

1. With the PC off, plug in the drive and the keyboard.
2. Start the PC and open its boot menu, then pick the USB drive. The key depends
   on the PC, and is often **F7**, **F8**, **F11**, **F12** or **Esc** during the
   logo.
3. If Secure Boot is on, the blue screen comes next: see below. If it is off, you
   won't see it.
4. Leave it alone until the time zone screen. On a USB stick that takes about 4
   minutes, and about 40 seconds of it is a black screen.

Next: [First run](first-run.md).

## The blue screen (Secure Boot on)

This comes once per PC. Your PC won't start CabinetOS until it trusts the key
CabinetOS is signed with, and this adds it. Press these on the keyboard:

| The screen says | Press |
|---|---|
| *Press any key to perform MOK management*, within 10 seconds | any key |
| *Enroll key from disk* | **Down**, **Enter** |
| *EFI-SYSTEM*, the drive it lists (with others beside it, move to this one first) | **Enter** |
| *cabinetos-key.cer*, below *EFI/* | **Down**, **Enter** |
| *Continue* | **Down**, **Enter** |
| *Yes* | **Down**, **Enter** |
| *Reboot* | **Enter** |

The PC then restarts by its own boot order, so it may start Windows. Restart it
and pick the USB drive from the boot menu again.

- **Missed the 10 seconds?** A menu comes up with **Set up this PC to start
  CabinetOS**. Pick it and the blue screen comes back.
- **The key stays on your PC** after the drive is gone. From then on the PC also
  trusts other software signed with that key, which is Universal Blue's.
- **No blue screen, and it won't start?** The only way left is to turn Secure Boot
  off in the PC's firmware. Have your BitLocker key saved first, and turn it back
  on when you are done.

## Every start after that

- **Starting:** pick the USB drive from the boot menu each time. Otherwise the PC
  starts Windows as usual.
- **Restarting:** a restart from the console's menu follows your PC's own boot
  order, so it may land in Windows. Restart and pick the drive again.
- **Stopping:** shut down from the console's menu, then take the drive out. Don't
  pull it while the console is running.
- **Updates:** the same as on any console: [Updates](updates.md).

## On a USB stick

A stick is far slower than a PC's own drive at the small writes a system makes.
These have been seen on one, and nobody knows yet whether a faster drive avoids
them:

- **Starting takes 3 to 5 minutes**, every time.
- **The first game can sit for minutes** before it shows, then play normally.
- **Sound can be missing** after a start. Restart the console from its menu.
- **Updates take longer:** about 4 minutes for one that took 34 seconds on a PC's
  own drive.

## Tell us how it went

Reports are what this alpha is for, whether it worked or not. Say which PC, which
drive and which tool you used, and whether Secure Boot and BitLocker were on.
[When something is wrong](troubleshooting.md) says how to make a report.
