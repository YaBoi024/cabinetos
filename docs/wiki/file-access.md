# File access

Copy files to and from the console over your network: saves, states,
screenshots, BIOS, and games on its drives. Off to start with; nothing listens
until you turn it on.

## Turn it on

Settings, **Storage**, **File access** (the PIN, if one is set). Press the row
again to see the details:

- the console's address, and its name ending in `.local`
- **User name**: `cabinet`
- **Password**: made by the console, shown on the TV. **New password** makes
  another.

## Connect

Use any SFTP client (FileZilla, Cyberduck, WinSCP, or `sftp` in a terminal) on
port 22:

```bash
sftp cabinet@192.168.1.50
```

with the console's own address. You see these folders:

| Folder | Holds |
|---|---|
| `users` | Each person's saves, states and screenshots, and the buttons they moved in arcade games (`controls`) |
| `bios` | The BIOS, firmware and keys the console has fetched |
| `roms` | Downloaded games, and their manuals in each system's `Manuals` folder |
| `cache` | Games fetched by Play, and their manuals the same way |
| `reports` | Diagnostic reports, the last five |
| `External` and others | The `CabinetOS` folder on each extra drive |

Only these folders are reachable, and only by file transfer, not a shell.

Each game starts from the newest save on your RomM server, so a save file
copied in by hand is only used for a game that has no save on the server.

## Developer access

For people working on CabinetOS itself: Settings, **About**, press **Version**
seven times and **Developer access** appears. It opens a full shell over SSH on
port 2222, with the same user name and password. It needs a PIN to be set. Seven
more presses on Version hide it and turn it off.
