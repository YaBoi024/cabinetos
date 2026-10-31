# Arcade

How to set out arcade games in RomM for CabinetOS, and what each one gets.

- **One folder for each system**, named the way RomM knows it. All of them are one Library tile, **Arcade**, in RomM's title order. The line beside its heading names the focused game's folder: *Metal Slug · NeoGeoMVS*. Everywhere else an arcade game's system is *Arcade*. A game in two folders shows twice. For one board on its own, make a RomM collection of it.

  | Folder | Games |
  |---|---|
  | `cps1`, `cps2`, `cps3` | Capcom CPS1, CPS2 and CPS3 |
  | `neogeomvs` | Neo Geo |
  | `naomi`, `naomi2`, `naomigd`, `atomiswave` | Sega Naomi, Naomi 2 and Naomi GD-ROM, and Sammy Atomiswave |
  | `stv` | Sega ST-V |
  | `model2`, `model3` | Sega Model 2 and Model 3 |
  | `fbneo` | FinalBurn Neo sets |
  | `gaelco` | Gaelco |
  | `arcade` or `mame` | everything else |

- **Sets:** non-merged, for MAME 0.289 or MAME 2003-Plus (0.78), as zip or 7z. MAME 0.289 sets also play in FinalBurn Neo, Flycast, sm2-emu and Supermodel. MAME 2003-Plus and Supermodel play only zip, so keep Model 3 games as zip.
- **Keep each set's own name**, for example `sf2.zip`. The name and the files inside it pick the emulator.
- **A game with a disc is one folder** with the zip and its CHD side by side, for example `cps3/sfiii3/sfiii3.zip` and `cps3/sfiii3/cap-33s-2.chd`. Use the CHD that MAME 0.289 lists. A CHD in a subfolder is not found.
- **CPS3:** a CD set, a small zip and a CHD, installs the game into the board the first time each person plays it, about 22 minutes: choose *Rewrite the game* with button 1. The No CD sets, whose names end in `n` (`sfiii3n`), start at once.
- **BIOS:** Naomi, Naomi 2, Naomi GD-ROM and Atomiswave need their BIOS zips as RomM firmware for that folder: `naomi.zip`, `naomi2.zip`, `naomigd.zip` and `awbios.zip`. Other arcade games carry theirs inside each zip.
- **Greyed:** a game no emulator here plays, one its emulator marks as not working, or one made for anything but a controller (a touchscreen, a mahjong or hanafuda panel, a gambling, medal or electromechanical machine) shows *Not supported*. Light-gun games show *Needs a light gun*. A zip whose files do not match its set says so when you press Play.
- **Hold Start for a second for the game's own settings.** In a MAME game this opens MAME's menu, where **Dip Switches** has the difficulty, the lives and the rest. Up and down move, left and right change a setting. If the menu ends with **Return to Game**, **B** chooses and **A** goes back; if it ends with **Close Menu**, **A** chooses and **B** goes back. Hold Start again, or choose that last row, to play on. In a FinalBurn Neo, Naomi or Atomiswave game it opens the board's own test menu, where the board has one. Some games have none, Final Fight among them: holding Start does nothing there. What you change is kept on this console, for you.
- **Buttons:** the 8BitDo Arcade Stick plays arcade games with the cabinet's layout ([Fight sticks](controllers.md#fight-sticks)), and **Controls** in the pause menu moves a game's buttons for any controller ([Controls](pause-menu.md#controls)).
- **Model 2 and Model 3 games** run in emulators of their own, so they have less: their pause menu is Resume, Screenshot and Exit to Home, with no save states and no Controls row, and holding Start does nothing.
- High scores and arcade settings are kept on this console, for you. Model 2 and Model 3 games are the exception: their high scores are part of the game's save, so they go to RomM with it.
