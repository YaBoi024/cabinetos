# Run from a USB drive

Try CabinetOS on a PC before you commit to it. It runs from a USB drive, the PC's
own drives stay as they are, and with the drive out your PC starts as it always
did. When you want to keep it, [install it](install.md) on the PC's own drive,
where it runs at its best.

## What you need

Everything on the [Install](install.md) page's list except the stick for the
installer, plus:

- **A USB drive of 64 GB or more, ideally an SSD or NVMe drive in a USB case.**
  We strongly recommend one: a USB stick works, but it is much slower. Everything
  on the drive is erased, and CabinetOS uses all of it, for the system and the
  games you play, so more room means more games kept ready.
- **A USB keyboard.**
- **[balenaEtcher](https://etcher.balena.io) or
  [Rufus](https://rufus.ie)** to write the file.

**If Windows on this PC uses BitLocker,** have your
[recovery key](https://account.microsoft.com/devices/recoverykey) handy and leave
the PC's Secure Boot setting as it is. Windows can ask for the key after a start
from a USB drive.

## Get the file

**[Download cabinetos-portable.img.xz](https://download.mmagtech.com/cabinetos-portable.img.xz)**
(about 5.6 GB). No account needed. Don't unpack it: both tools read it as it is.

Optional: check it is whole against
[its SHA-256](https://download.mmagtech.com/cabinetos-portable.img.xz.sha256)
(`shasum -a 256 cabinetos-portable.img.xz`; on Windows
`certutil -hashfile cabinetos-portable.img.xz SHA256`).

## Write it to the drive

Everything on the drive you pick is erased, so check you have the right one.

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

This comes once per PC, so that your PC trusts the key CabinetOS is signed with.
The key stays on the PC, and it is Universal Blue's, so the PC also trusts other
software signed with it. Press these on the keyboard:

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
- **No blue screen, and it won't start?** Turn Secure Boot off in the PC's
  firmware, with your BitLocker key to hand, and turn it back on when you are done.

## Every start after that

- **Starting:** pick the USB drive from the boot menu each time. Otherwise the PC
  starts Windows as usual.
- **Restarting:** a restart from the console's menu follows your PC's own boot
  order, so it may land in Windows. Restart and pick the drive again.
- **Stopping:** shut down from the console's menu, then take the drive out.
- **Updates:** the same as on any console: [Updates](updates.md).

## On a USB stick

A stick is slower than other drives, so expect:

- Starting to take 3 to 5 minutes.
- Your first game to take a few minutes to appear.
- Updates to take a few minutes.

If a start comes up with no sound, restart the console from its menu.

## Tell us how it went

Say which PC, which drive and which tool you used, and whether Secure Boot and
BitLocker were on. [When something is wrong](troubleshooting.md) says how to make
a report.
