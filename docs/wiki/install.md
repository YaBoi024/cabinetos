# Install

CabinetOS Handheld replaces everything on the drive you install it to. It is a
whole operating system, not an app.

There is no installer ISO for CabinetOS Handheld yet (the one tried so far froze
while installing Bazzite). For now it is installed in two steps: install the
original CabinetOS, then switch it to this image.

## What you need
- **A Steam Deck.** Tested on the OLED model; the LCD model is next.
- **A [RomM](https://github.com/rommapp/romm) server**, version 5.1 or newer,
  on your network, with your games, BIOS and firmware in it. See
  [Your RomM server](romm.md).
- **A network.** Ethernet or Wi-Fi.

A graphics chip without Vulkan still runs the console, but PlayStation 2 and
GameCube need Vulkan, and every system starts on the lowest picture level.

## 1. Install the original CabinetOS

Install [CabinetOS](https://github.com/MMagTech/cabinetos) by MMagTech on the
Steam Deck, following its install guide (get the installer, write it to a USB
stick, and install). **This erases the whole drive.**

## 2. Switch to CabinetOS Handheld

Open a terminal on the Deck (Developer access in Settings gives you SSH on
port 2222) and run:

```bash
sudo bootc switch ghcr.io/yaboi024/cabinetos:latest
```

Then restart the Deck. It boots into CabinetOS Handheld.

From then on the console updates itself from this repository's image: Settings >
System > System Update (see [Updates](updates.md)).

Next: [First run](first-run.md).
