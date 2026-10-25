# Hold Start opens an arcade game's own settings

- **Decided:** In an arcade game, player 1 holding Start for about a second
  (60 frames, FinalBurn Neo's own default) opens the emulator's own menu:
  MAME 2003-Plus's menu, the DIP switches among the rest, and on FinalBurn
  Neo the board's test menu where the board has one. The console draws no
  settings screen of its own and adds no pause-menu row, and a game with no
  menu of its own gets nothing. MAME's menu no longer opens on player 1's
  left stick click. A changed setting stays on the console where it was set,
  for that player, until RomM's save channels.
- **Date:** 2026-10-24, the rest settled 2026-10-25.
- **Why:** Start is on every pad and on the stick, and in arcade games it is
  only ever tapped. A held game button such as R1 is normal play, a shortcut
  works only with In-game shortcuts on, and the pause menu should not keep
  growing; FinalBurn Neo already does it this way. On 2026-10-25 a screen of
  the console's own for FinalBurn Neo's switches, which it keeps as emulator
  settings with no menu (Final Fight's among them), was put to MMagTech and
  refused: the game's own menu is the rule. MAME's settings file waits with
  the other arcade files for save channels, as decided on #353.
- **Numbers:** #577, "Hold Start in an arcade game to open its own settings
  (DIP switches), on both cores"; #353, "Three kinds of save never reach
  RomM: Saturn cartridge, arcade high scores, cartridge clocks"; PR #580,
  "Hold Start in an arcade game to open its own settings (#577)".
- **Replaces:** none.
