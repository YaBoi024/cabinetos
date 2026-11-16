# Wii U and Vita: only player 1's controller tilts the game

- **Decided:** In Wii U and PS Vita games only player 1's controller carries its tilt to the game. Every other player's stand-in shows no motion sensors, whatever their controller has. In Switch and PS3 games every player's controller still moves their own part of the game.
- **Date:** 2026-11-11
- **Why:** Each of those machines has one gyro, held by one person: the Wii U's GamePad, which on a Wii U is always player 1, and the Vita itself. It is the rule the Game Boy Advance and 3DS already follow (#670). Vita3K reads every controller's motion into the one Vita, so without this a second controller with a gyro, picked up during a one-player game, would move it too. The cost: a player 2 holding the only controller with sensors gets no tilt in those games until the controllers are swapped.
- **Numbers:** #669, "Gyro for Switch, Wii U and Vita: a virtual pad that carries the player's tilt"; pull request #696, "Gyro for Switch, Wii U and Vita: a stand-in DualSense carries the player's tilt (#669)"; #670, "Tilt for the Game Boy Advance and 3DS cores: answer libretro's sensor interface", for the handheld rule.
- **Replaces:** none.
