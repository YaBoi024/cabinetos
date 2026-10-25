# What's new

Every update that reached consoles, newest first. To update: Settings,
**System**, **System update**. An update you skipped is included in the next
one you install, so you only ever install the newest.

## 2026.10.25.4 {#v2026-10-25-4}

- **New:** in an arcade game (FinalBurn Neo or MAME 2003-Plus), the pause
  menu has **Controls**: the game's buttons by their own names, each with the
  button that presses it. Highlight one, press **A**, then press the button
  you want it on; if another of the game's buttons was there, the two swap.
  Press the button it is already on to leave it, and **Reset to default**
  puts them all back. It is kept for
  that game, for you and for that kind of controller, and works as soon as
  you go back to the game. See [Controls](pause-menu.md#controls).
  ([#582](https://github.com/MMagTech/cabinetos/pull/582))

## 2026.10.25.2 {#v2026-10-25-2}

- **New:** hold **Start** for a second in an arcade game to open the game's
  own settings: in a MAME 2003-Plus game, MAME's menu, whose **Dip Switches**
  set the difficulty, the lives and the rest; in a FinalBurn Neo game, the
  board's own test menu, where the board has one. Hold Start again to close
  MAME's menu. See [Arcade](systems.md#arcade).
  ([#580](https://github.com/MMagTech/cabinetos/pull/580))
- **Fixed:** leaving a MAME 2003-Plus game with both stick clicks could
  open MAME's DIP switch menu. It no longer does.
  ([#580](https://github.com/MMagTech/cabinetos/pull/580))
- **Changed:** MAME 2003-Plus games start without MAME's copyright notice
  over their first seconds.
  ([#580](https://github.com/MMagTech/cabinetos/pull/580))

## 2026.10.25 {#v2026-10-25}

- **New:** the 8BitDo Arcade Stick plays arcade games with a fight-stick
  layout. Pair it over Bluetooth in **X mode**: the game's buttons follow the
  game's own order along the top row, then the bottom, so a Street
  Fighter-style game has its punches on top and its kicks below; a four-button game, such as a Neo Geo
  game, uses the bottom row, as does a game with one to three. Press two
  buttons together for moves that need them, as on the cabinet. Tap **Home** on the stick
  for the pause menu in any game. Wired, or on its 2.4G receiver, it is
  treated as an ordinary pad, as before, and nothing changes for any other
  controller. See
  [Fight sticks](controllers.md#fight-sticks).
  ([#576](https://github.com/MMagTech/cabinetos/pull/576))

## 2026.10.24 {#v2026-10-24}

- **Fixed:** with only a Wii Remote on, you could start a game it can't play
  and be stuck in it until you turned the console off. A Wii Remote now
  starts only Wii games: on any other game, Play says *This Remote only plays
  Wii games*. And holding **HOME** on a Remote for a second opens the pause
  menu in every game, not only Wii games. See
  [Wii Remotes](controllers.md#wii-remotes).
  ([#571](https://github.com/MMagTech/cabinetos/pull/571))

## 2026.10.23.2 {#v2026-10-23-2}

- **Fixed:** a game kept on your server as a *.zip* or *.7z* showed
  *Unpacking…* at every start, not only the first: for a 3DS game, up to 37
  seconds every time. Only the first start unpacks now. After this update,
  each such game unpacks one last time at its next start, then goes
  straight to the game. A nearly full drive also no longer removes other
  downloads, or says *Not enough space*, to start a game already unpacked.
  ([#530](https://github.com/MMagTech/cabinetos/issues/530))

## 2026.10.23 {#v2026-10-23}

- **Fixed:** in a Wii U game, a screen that reads only the GamePad ignored
  every button, such as a first screen asking you to set the screen size.
  Player 1's controller now counts as the Wii U GamePad, as on a real Wii U,
  and players 2 and up are Pro Controllers as before. A game that shows a
  map or items on the GamePad's own screen may now put them there, and the
  console cannot show that screen yet.
  ([#555](https://github.com/MMagTech/cabinetos/issues/555))

## 2026.10.22.4 {#v2026-10-22-4}

- **Changed:** Wii games that require MotionPlus, such as Wii Sports Resort,
  Red Steel 2 and Skyward Sword, are greyed with *Needs MotionPlus* until a
  TechKen Wii Remote is paired: on Nintendo's own Remotes, Linux turns
  MotionPlus off whenever a game turns it on. TechKen's copies are the only
  officially supported Wii Remote. See
  [Wii Remotes](controllers.md#wii-remotes).
- **Fixed:** a Wii U game that needs the GamePad said *Needs a Wii Remote* on
  its tile. It says *Needs a Wii U GamePad* now.

## 2026.10.22.3 {#v2026-10-22-3}

- **Fixed:** on a computer with a speaker built into its case, every sound
  played from that speaker instead of the TV. The TV plays it now, and
  headphones still take over while they are plugged in. While the TV is
  asleep or off, the built-in speaker stays silent.
  ([#547](https://github.com/MMagTech/cabinetos/issues/547))
- **Changed, for players on a handheld PC or a laptop:** sorry, the built-in
  speakers on a handheld PC or a laptop may no longer play. This came with a
  fix for PCs on a TV that played their sound through a small speaker in the
  case. CabinetOS is made to be a home console: a PC attached to a TV, so
  handheld PCs and laptops are not supported.

## 2026.10.22.2 {#v2026-10-22-2}

- **Fixed:** a Wii Remote made by Nintendo, or a copy that reports itself as
  one, showed as two controllers, a player and a Wii Remote (P2 and W1), and
  every press counted twice. It is one Wii Remote now.
  ([#548](https://github.com/MMagTech/cabinetos/issues/548))

## 2026.10.21.4 {#v2026-10-21-4}

- **New:** the PlayStation Vita, played as a PlayStation TV. A game is a zip
  with its licence inside; the first Play installs it, and both of Sony's
  firmware files (the system software and the font package) must be on your
  server. Each controller is its own player. To touch the screen, hold L3
  for half a second: two dots show. The left stick moves the white one and
  L2 touches (hold to drag); the right stick brings a red one that R2
  touches. Hold L3 again to hide them. Games that need the rear touch pad,
  tilting or the camera may stop where they need them, as on a PS TV. Saves
  sync with your server; no save states. Updates and DLC do not play yet.
  See [Systems](systems.md#playstation-vita).
  ([#198](https://github.com/MMagTech/cabinetos/issues/198))

## 2026.10.21.2 {#v2026-10-21-2}

- **New:** the Nintendo 3DS. Decrypted cartridge dumps play, the two
  screens side by side on the TV; the right stick moves a pointer on the
  touch screen, R3 taps, and holding L3 swaps the screens. Encrypted dumps
  do not play, and say so on the game's page; `.cia` files do not show on
  the console. Updates and DLC do not play yet. Games get no internet. Saves
  sync with your server; save states and fast forward work, rewind does not,
  and a 3DS state may not load after an update changes the emulator.
- **Fixed:** pressing both sticks for the pause menu no longer sends a stray
  stick click to the game.
- **Fixed:** in the Library, moving up from the third or fourth system to
  Systems and Collections showed no focus, and opening a system or
  collection after moving up from the fourth closed the console's screen and
  started it again. Focus lands on Collections now, and the grid opens.

## 2026.10.20.4 {#v2026-10-20-4}

- **New:** Vectrex games show the coloured sheet each cartridge came with,
  over the screen as it sat on the real console: the colours, the artwork
  and the controls printed around the play area. The pause menu has
  **Overlays: On / Off** to show the bare screen instead; the choice holds
  for every Vectrex game. See [Picture and colours](picture.md).
  ([#224](https://github.com/MMagTech/cabinetos/issues/224))

## 2026.10.20.3 {#v2026-10-20-3}

- **Fixed:** streaming an arcade game to a phone, a tap of Select could open
  the pause menu instead of putting a coin in. Select, Start and the stick
  buttons can no longer be chosen for shortcuts (games use them, and L3+R3
  is the pause menu), and Home opens the pause menu even when a spare
  button was chosen for that kind of controller. The row now reads
  "Shortcuts use: Home" (#313).
- **Fixed:** on Search, the covers grow and shrink with the keyboard as it
  slides in and out, instead of jumping to their new size at once. Done
  slides the keyboard away rather than dropping it.

## 2026.10.20 {#v2026-10-20}

- **Fixed:** opening Steam, and some restarts, could sit on a blank screen
  for about 25 seconds while the picture changed over. The change-over
  now takes a few seconds at most, even when the compositor stalls.
  ([#305](https://github.com/MMagTech/cabinetos/issues/305))
- **Fixed:** after leaving a game through the pause menu, the menu and its
  dimming lingered over Home for a moment. They go with the game now.
- **Fixed:** a game this console cannot play, opened from Home's Resume,
  now says why on its page in plain words, such as "No core for this
  system". ([#484](https://github.com/MMagTech/cabinetos/issues/484))
- **Fixed:** pressing Play from a save state on a second game while
  another was still downloading lost the first game's pick. Each press now
  keeps its own. ([#484](https://github.com/MMagTech/cabinetos/issues/484))
- **Fixed:** on a keyboard, a held [ or ] kept walking along the top bar,
  and a held F5 wrote a save state at every repeat. Keys now act once per
  press. On Search, Start puts the keyboard away; the next Start opens the
  Power menu. ([#484](https://github.com/MMagTech/cabinetos/issues/484))
- **Fixed:** a game your server gives no size for could fail to download
  onto a nearly full drive. Room is now made as the download reports its
  length. ([#484](https://github.com/MMagTech/cabinetos/issues/484))
- **Fixed:** a save or screenshot upload the server kept refusing could be
  sent again without end and hold up the console on its way out. It is now
  tried once, noted, and tried again at the next start.

## 2026.10.19.3 {#v2026-10-19-3}

- **New:** this page, on the console. Settings, **About**, **What's new**
  shows a QR code that opens What's new; scan it with your phone, B goes
  back. ([#329](https://github.com/MMagTech/cabinetos/issues/329))
- **Fixed:** Wii Remotes: when one was switched back on during a game, the
  others froze for up to a second and a half, and loading Miis from a
  Remote briefly froze the others too. The other Remotes now keep working
  throughout. ([#480](https://github.com/MMagTech/cabinetos/issues/480))
- **Fixed:** pressing Play while your server was slow or not answering held
  a black screen until it answered. The game's saves are now fetched while
  the download is still on screen, and the picture stays alive. A drive the
  console could not make its folder on added ten seconds to every start; it
  no longer does. Formatting a drive full of files no longer pauses the
  picture after the PIN. ([#481](https://github.com/MMagTech/cabinetos/issues/481))
- **Fixed:** a game whose archive keeps its files in folders (a cue sheet
  with its tracks in a folder beside it, or two discs whose tracks share a
  name) could fail to start or start the wrong disc. Its folders are kept
  now. ([#482](https://github.com/MMagTech/cabinetos/issues/482))
- **Fixed:** an original-Xbox game that another person's game left its saves
  on said "Couldn't start this game" until that person played it again.
  Their saves are now kept for them and sent as them the next time the
  console acts as them, and the game starts for you on your own saves.
  ([#484](https://github.com/MMagTech/cabinetos/issues/484))
- **Fixed:** opening Steam no longer waits on the console's background
  tasks: once your saves are sent, the switch goes ahead within two
  seconds. A separate pause of about 25 seconds that sometimes happens
  while the screen changes over is not fixed yet.
  ([#305](https://github.com/MMagTech/cabinetos/issues/305))

## 2026.10.19.2 {#v2026-10-19-2}

- **New:** save states keep your RetroAchievements progress. Loading a state
  no longer starts every achievement's count over, here or on another
  console, as RetroArch does. ([#279](https://github.com/MMagTech/cabinetos/issues/279))
- **New:** Restart and Power off fade the screen to black over a second,
  silently, instead of cutting the picture off. ([#393](https://github.com/MMagTech/cabinetos/issues/393))
- **Fixed:** updates downloaded about 320 MB that had not changed. An update
  now downloads only the parts that changed. ([#511](https://github.com/MMagTech/cabinetos/issues/511))
- **Fixed:** Genesis games were drawn too wide, and a game that changes its
  picture shape mid-game kept its first one. Games now use the shape they
  ask for. ([#220](https://github.com/MMagTech/cabinetos/issues/220))
- **Fixed:** N64 games stuttered about twice a second. They play smoothly
  now, with rewind still there. ([#254](https://github.com/MMagTech/cabinetos/issues/254))
- **Fixed:** PS3 games could drop pictures, because the emulator ignored the
  console's VSync setting. VSync is on now.
- **Fixed:** each N64, PSP, Dreamcast, GameCube or Wii game left some
  graphics memory behind when it ended. It is all given back now. ([#489](https://github.com/MMagTech/cabinetos/issues/489))
- **Fixed:** a PS2 disc the emulator cannot open started with nothing on the
  screen. The game's page now says "Disc can't be read". ([#506](https://github.com/MMagTech/cabinetos/issues/506))
- **Fixed:** a save state that failed said "No save states for this system".
  It now says "Couldn't save the state". ([#222](https://github.com/MMagTech/cabinetos/issues/222))
- **Fixed:** pressing Play again just as a game started, or cancelling a
  download, could leave the screen black or carry on downloading. The game
  starts, or the download stops. ([#472](https://github.com/MMagTech/cabinetos/issues/472))
- **Fixed:** Sleep or Power off chosen while a game was starting could
  restart the console instead, and over a PS3, Switch, Xbox or Wii U game
  the Power menu slept before the game had closed. It now waits for the
  game and closes it first. ([#473](https://github.com/MMagTech/cabinetos/issues/473))
- **Fixed:** a system or collection of more than 500 games could come back
  short, show games twice, or freeze the screen while it filled. It fills to
  the end, once, while you move around. ([#471](https://github.com/MMagTech/cabinetos/issues/471))
- **Fixed:** the picture froze while Search, Remote Play, Tailscale, Add a
  controller or Wii Remote pairing waited, or as the console came back
  online. These waits now happen behind the screen. ([#481](https://github.com/MMagTech/cabinetos/issues/481))
- **Fixed:** with a server address typed without http or https, Offline
  could flicker about once a minute. It now shows only when the server is
  really gone. ([#479](https://github.com/MMagTech/cabinetos/issues/479))
- **Fixed:** covers could be fetched from any web address in RomM's data,
  with your RomM sign-in attached. The console now only connects to your
  RomM server. ([#380](https://github.com/MMagTech/cabinetos/issues/380))
- **Fixed:** Home's Favorites were in alphabetical order, stopped at the
  first 40, and could lose a heart made offline or while Home reloaded. The
  newest come first, all of them count, and an unsent heart stays. ([#476](https://github.com/MMagTech/cabinetos/issues/476))
  ([#405](https://github.com/MMagTech/cabinetos/issues/405))
- **Fixed:** with a RomM collection of your own named Favorites, the console
  retried every five minutes to make its own. It no longer does. ([#474](https://github.com/MMagTech/cabinetos/issues/474))
- **Fixed:** Wii Remotes could not pair when Linux named the Bluetooth radio
  something other than hci0. They pair now. ([#303](https://github.com/MMagTech/cabinetos/issues/303))
- **Fixed:** GameCube games RomM had no game code for kept their saves on
  the console only. The console reads the code off the game and the saves
  travel. ([#484](https://github.com/MMagTech/cabinetos/issues/484))
- **Fixed:** small faults with games: states piling up for games with `:` or
  `+` in their file name, Resume doing nothing on a game this console can't
  play, a failed screenshot stopping every later one, and a download
  starting with no room for it. ([#484](https://github.com/MMagTech/cabinetos/issues/484))
- **Fixed:** after a full disk or a power cut, a sign-in, the PIN, the
  server address or a save could be lost or cut. Files are now written
  whole or not at all. ([#482](https://github.com/MMagTech/cabinetos/issues/482))
- **Fixed:** small faults in Settings, first run and the screen shown while
  the server doesn't answer: a console with no server showed six made-up
  games, a failed account switch showed an internal error, held keys acted
  again and again. Each now behaves as Settings does. ([#485](https://github.com/MMagTech/cabinetos/issues/485))
- **Fixed:** the console could crash on its way out, and rarely a working
  drive was listed as unusable or Eject named one drive and ejected
  another. ([#406](https://github.com/MMagTech/cabinetos/issues/406)) ([#483](https://github.com/MMagTech/cabinetos/issues/483)) ([#404](https://github.com/MMagTech/cabinetos/issues/404))
- **Fixed:** the diagnostic report could say Steam was set up after it was
  removed, miss a server reached only over https, or never arrive on a slow
  console. It matches the console now. ([#486](https://github.com/MMagTech/cabinetos/issues/486))

## 2026.10.18.7 {#v2026-10-18-7}

- **Fixed:** ready for RomM 5.4. After your server moves to RomM 5.4, a
  game's page still shows how long you have played it.
  ([#401](https://github.com/MMagTech/cabinetos/issues/401))
- **Fixed:** a game whose file name on your server begins with # or ? would
  not download. It downloads now.
  ([#402](https://github.com/MMagTech/cabinetos/issues/402))
- **Fixed:** on a server address that redirects (for example to https), saves
  and save states could be marked as sent when RomM never got them. They now
  stay waiting until RomM takes them.
  ([#478](https://github.com/MMagTech/cabinetos/issues/478))
- **Fixed:** when RomM refused a save state, for example because your account
  cannot upload, the console said it would upload when RomM was back. It now
  says RomM refused it. The state is kept on the console.
  ([#477](https://github.com/MMagTech/cabinetos/issues/477))
- **Fixed:** a diagnostic report bigger than about 2.5 MB never finished
  downloading to your phone. It downloads whole now.
  ([#417](https://github.com/MMagTech/cabinetos/issues/417))
- **Fixed:** touching a PS5 controller's touchpad showed a mouse arrow over
  Home and games. There is no arrow now.
  ([#509](https://github.com/MMagTech/cabinetos/issues/509))
- **Fixed:** a cover fixed on your server showed only after the console
  restarted. Open the system again a minute later and it shows.
  ([#387](https://github.com/MMagTech/cabinetos/issues/387))

## 2026.10.18.6 {#v2026-10-18-6}

- **New:** PS1 games get a DualShock. The sticks work in the games made for
  them, and the controller rumbles. Games made for the original pad play as
  they always did. A game that needs the DualShock's ANALOG button pressed
  takes **L1 + R1 + Select**.
  ([#345](https://github.com/MMagTech/cabinetos/issues/345))
- **Fixed:** the Nintendo DS touch screen did nothing. The right stick now
  moves a pointer on it, and pressing the stick (**R3**) touches.
  ([#374](https://github.com/MMagTech/cabinetos/issues/374))
- **New:** Nintendo DS games draw their 3D sharper at the Balanced and
  Quality picture levels, 4 and 6 times the DS's own size. Performance keeps
  the DS's own size, as before.
  ([#219](https://github.com/MMagTech/cabinetos/issues/219))

## 2026.10.18.5 {#v2026-10-18-5}

- **Fixed:** with more than one PS2 BIOS on your server, every game started
  on whichever BIOS the emulator found first. Each game now starts on a BIOS
  for its own region (USA, Europe or Japan), going by the disc code RomM
  reads from the game, whatever the files are called. A game with no BIOS
  for its region uses your USA one.
  ([#416](https://github.com/MMagTech/cabinetos/issues/416))

## 2026.10.18.4 {#v2026-10-18-4}

- **Changed:** the system CabinetOS is built on moves to Bazzite
  44.20261012.1, with Linux 7.2.9. The new kernel has fixes for Radeon
  graphics, Bluetooth and sound, and the libraries that draw the screen,
  download games from RomM and run the Original Xbox and Switch emulators
  are newer versions.
  ([#503](https://github.com/MMagTech/cabinetos/pull/503))

## 2026.10.18.3 {#v2026-10-18-3}

- **Fixed:** a game kept on your RomM server as one file inside its own
  folder now starts. RomM makes such a folder when you upload a manual, a
  soundtrack or screenshots to a game. The console used to hand the
  emulator the soundtrack or the manual instead of the game, and a PS2 game
  alone in its folder showed the red "not a PlayStation or PlayStation 2
  format disc" screen with every BIOS. Now only the game file downloads.
  ([#400](https://github.com/MMagTech/cabinetos/issues/400),
  [#416](https://github.com/MMagTech/cabinetos/issues/416))

## 2026.10.18.2 {#v2026-10-18-2}

- **Fixed:** a console set up over Wi-Fi now gets its Xbox and Switch
  emulators without needing a restart.
- **Fixed:** a game no longer opens on an old save state, or on the wrong
  disc, after a game before it failed to start, or after resuming a Switch,
  PS3 or Xbox game from Home.
- **Fixed:** Resume on Home now says why a game cannot start (for example
  "Not enough space") instead of doing nothing.
- **Fixed:** on a nearly full console, Play no longer deletes the game it is
  about to download and then fail with "Couldn't download this game".
- **Fixed:** in PS3, Switch, Xbox, Xbox 360 and Wii U games, a controller
  switching off now pauses the game and the other players keep their
  numbers; a button held when the game started no longer stays pressed on
  Home afterwards; a message showing when you close the game's pause menu
  no longer stays frozen on the game; and a stray "Paused" box no longer
  shows over a game while A is held.
- **Fixed:** an Xbox game's saves can no longer be lost when they could not
  be copied off its drive the last time it was played.
- **Fixed:** with a USB keyboard, holding an arrow now speeds up on every
  screen, and holding Return or Escape acts once (holding Return on Exit to
  Home no longer starts the game again).
- **Fixed:** downloaded Wii U games now show on Home, the Library and Search
  while the console is offline.
- **Fixed:** a switch to another person that fails now goes back to whoever
  was signed in, instead of mixing the two people's games, saves and RomM
  sign-in.
- **Fixed:** signing out now also forgets each game's picture choices and
  what the console read off its files, so a new server's games never take
  over another server's.
- **Fixed:** in Settings, B on a question goes back to the panel it came
  from, as Cancel does; coming back to Add an account while its code is
  still valid shows that code again; the cursor stays on the controller you
  just gave a new player number; and the Wiki's QR code in How to no longer
  jumps smaller as you move away from it.
- **Fixed:** Home keeps saying "Nothing played yet" until you play
  something, instead of filling Recent with every game you browsed; a Home
  with only favourites starts on them; and games that cannot be played here
  are greyed on Home as they are in a system's games.
- **Fixed:** the Wii games turn bright or greyed the next time you open them
  after you pair or remove a Wii Remote, or a stream starts or ends.
- **Fixed:** on Search, Down from the results brings the keyboard back with
  what you typed, after opening a game or pressing Done.
- **Fixed:** first setup no longer freezes when you press a Wi-Fi network or
  a controller again while it is still joining or pairing, and a pairing
  code that expires or is refused on the phone is replaced by a new one by
  itself.
- **Fixed:** a console whose RomM sign-in has been refused, or whose RomM
  has no systems it plays, now starts to an empty Home and says so, instead
  of restarting over and over with a black screen.
- **Changed:** the grey example in the RomM server address field is now a
  made-up address.
  ([#398](https://github.com/MMagTech/cabinetos/issues/398))

## 2026.10.17.3 {#v2026-10-17-3}

- **Changed:** nothing you should see. The console's code has been
  reorganised into smaller files named for what they do, so others can find
  their way around it and help. Every screen, game and setting works as
  before.
  ([#398](https://github.com/MMagTech/cabinetos/issues/398))
- **Fixed:** a rare fault where, while the console reconnected to your RomM
  server or changed accounts, a cover, a save upload or another request
  could go out with a half-changed server address or sign-in and fail. The
  console now changes them in one step.

## 2026.10.14.5 {#v2026-10-14-5}

- **Added:** when two games on one system have the same name, the heading
  now shows what tells them apart, such as the region, language or revision
  in brackets in their file names on your RomM server: "Metroid Prime
  Hunters (USA) (Rev 1)". It shows in a system's games, collections, search,
  and on Home when both copies are on the same shelf. Games with plain file
  names show their name alone, as before.
- **Added:** the diagnostic report lists the games that share a name, with
  their file names, to help sort out a library where two games look the same.

## 2026.10.14.4 {#v2026-10-14-4}

- **Changed:** the interface sounds are softer and lower, to suit the
  console's quiet look.
- **Fixed:** some places made no sound when you moved or chose something:
  a game's page, opening a game, the Resume card, the power menu on Home,
  the account switcher, the Library's tabs, the on-screen keyboard and first
  setup. They now sound like everywhere else, at the level set in
  Settings. The pause menu stays silent.
  ([#389](https://github.com/MMagTech/cabinetos/issues/389))
- **Changed:** Search no longer shows "Search" twice above the keyboard, and
  its box says "Search…".

## 2026.10.14.3 {#v2026-10-14-3}

- **Fixed:** on some machines the console made no sound at all, because it
  played into a headphone jack or USB sound device with nothing plugged in.
  The sound now always plays on your TV. USB sound devices no longer take it
  from the TV.
  ([#341](https://github.com/MMagTech/cabinetos/issues/341))
- **Changed:** on machines whose headphone jack can tell when headphones are
  plugged in, plugging them in moves the sound to them, and unplugging brings
  it back to the TV. After a Remote Play session this needs a restart of the
  console first ([#390](https://github.com/MMagTech/cabinetos/issues/390)).

## 2026.10.14.2 {#v2026-10-14-2}

- **Changed:** in a platform's game grid, the heading (the platform, how
  many games, and the name of the game you are on) now stays at the top
  while the covers scroll underneath, so you can always see which game is
  selected. The grid moves a whole row at a time, so no row is left half
  cut off under the heading.

## 2026.10.14 {#v2026-10-14}

- **Fixed:** some games showed a blank tile on Home, the shelves and the
  grid, and some showed no cover even on their own page, although your RomM
  server had one. Covers now show in every picture format RomM takes (WebP,
  AVIF, GIF, BMP and TIFF as well as PNG and JPEG), and when a game's small
  cover is missing or broken, its large one is used instead. If a cover
  still does not show, a diagnostic report now says why.
  ([#376](https://github.com/MMagTech/cabinetos/issues/376))

## 2026.10.13.5 {#v2026-10-13-5}

- **Fixed:** a console switched on while the TV was off, on another input or
  unplugged could come up at a lower size and stay blurry on a 4K TV until it
  was restarted. It now comes up at your TV's own size straight away, and
  picks the TV up, picture and sound, as soon as it is on.
  ([#268](https://github.com/MMagTech/cabinetos/issues/268))
- **Changed:** File access, Developer access and Tailscale now start when the
  console starts, if they are on in Settings, so the console can be reached
  even before the TV is on. Remote Play starts as soon as the TV is on; it
  needs a screen to stream.

## 2026.10.12.9 {#v2026-10-12-9}

- **Fixed:** a game kept as a folder on your RomM server (for example a
  Switch game with `update` and `dlc` folders beside it, or a PS1 game as a
  `.cue` and its tracks) downloaded again every time you started it. It now
  downloads once. The first time you start such a game after updating,
  nothing downloads, and the extra copies left on your drive by the old
  downloads are cleared away.
- Updates and DLC kept beside a game are not used yet; only the game itself
  is downloaded. ([#181](https://github.com/MMagTech/cabinetos/issues/181))

## 2026.10.12.8 {#v2026-10-12-8}

- **New:** a **Widescreen** row in the pause menu for PS2 games that play in
  16:9. Some of those games stretch their menus and on-screen display to fit;
  turn it **Off** to play that game in 4:3 as it was made. It is kept for that
  game, and shows only on games that have a widescreen option.
  ([#368](https://github.com/MMagTech/cabinetos/issues/368))
- **Changed:** when you change **Picture** or **Widescreen** in the pause
  menu, the notice now says **Exit and relaunch to apply**.

## 2026.10.12.7 {#v2026-10-12-7}

- **New:** read a game's manual on the TV. When your RomM server has a
  manual for a game, a small book shows on its cover. On the game's page,
  **R1** opens it. **Left** and **Right** turn the page; **A** zooms in, the
  d-pad then moves around the page, and **A** again zooms out. **L1** or **B**
  goes back. Manuals are kept with the game, so they can be read offline too.
  See [Buttons and shortcuts](navigation.md).
  ([#364](https://github.com/MMagTech/cabinetos/issues/364))

## 2026.10.12.5 {#v2026-10-12-5}

- **Fixed:** the console made no sound at all, in menus or games, after
  updating to 2026.10.12.3 or 2026.10.12.4. Sound works again.
  ([#351](https://github.com/MMagTech/cabinetos/issues/351))

## 2026.10.12.4 {#v2026-10-12-4}

- **New:** the diagnostic report (Settings, **System**) now includes your
  sound setup, so a "no sound" report can show where it goes wrong.
- Known problem in this version: no sound. Fixed in 2026.10.12.5.

## 2026.10.12.3 {#v2026-10-12-3}

- **New:** the volume keys on a keyboard (or a Steam Deck's volume buttons)
  change the console's volume, in menus and every game. The console starts
  at full volume each time, so it can never be left silent with no way back
  from a controller. ([#351](https://github.com/MMagTech/cabinetos/issues/351))
- **New:** RomM's smart collections show in **Library**, **Collections**.
  Your own collections come first, then each other person's public ones under
  their name; **L2** and **R2** jump between people. Another person's
  Favorites stays private. ([#352](https://github.com/MMagTech/cabinetos/issues/352))
- Known problem in this version: no sound. Fixed in 2026.10.12.5.

## 2026.10.12 {#v2026-10-12}

- **Fixed:** a GameCube save kept only on your RomM server (after a fresh
  install, or on a second console) loaded as an empty memory card. It loads
  now, and saves made from now on reach your server.
  ([#344](https://github.com/MMagTech/cabinetos/issues/344))

## 2026.10.11.4 {#v2026-10-11-4}

- **New:** games on several discs play as one game, with one save that
  carries on from disc to disc. When the game asks for the next disc, open
  the pause menu and choose **Change disc**: it lists the game's discs with
  the one in the drive marked; pick one and choose **Insert**. The game page
  shows which disc you're on. For PS1, Saturn, Dreamcast, Sega CD, 3DO and
  GameCube; put a game's discs in one folder on your RomM server. See
  [Games on several discs](discs.md).
  ([#162](https://github.com/MMagTech/cabinetos/issues/162))

## 2026.10.11.3 {#v2026-10-11-3}

- **Fixed:** in a system's game list, the letter column showed only near the
  top of the list, and some letters were out of order. It now shows wherever
  you are, in your server's order, and holding **L2** or **R2** keeps jumping
  letter by letter.

## 2026.10.10.10 {#v2026-10-10-10}

- **Fixed:** an Xbox controller connected for the first time after a restart
  could count as two players, so every press happened twice (the pause menu
  opened and closed at once). ([#318](https://github.com/MMagTech/cabinetos/issues/318))
- **Fixed:** newer Xbox controllers (Series X|S) would not pair over
  Bluetooth. They pair now.

## 2026.10.10 {#v2026-10-10}

The first release. See its
[release notes](https://github.com/MMagTech/cabinetos/releases/tag/2026.10.10).
