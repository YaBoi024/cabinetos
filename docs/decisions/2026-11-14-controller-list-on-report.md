# The controller list is updated when a player reports a pad

- **Decided:** The controller list (SDL_GameControllerDB) is no longer checked on a schedule. When a player reports a pad the console does not know or maps wrong, `pad-db-update.yml` is run by hand; it opens a pull request that updates the whole list to the community's newest and names the pads it adds, changes and removes.
- **Date:** 2026-11-14
- **Why:** In the year to 2026-11-14 the list's Linux entries gained 57 pads, nearly all little known, and changed the buttons of 4; the pads the console supports officially need none of it, because SDL, which moves with every base update, knows them itself. No player had reported a pad the console did not know. Each scheduled pull request still needed a description written by hand and a place on a testing image. The cost: a pad added to the community's list after the console's copy works only once someone reports it and the update ships.
- **Numbers:** #66, "Settings: button mapping"; #644, "Controller list → 07af1d50187e", the last scheduled update, which this change carries; #688, "The controller list and Steam's spare are checked monthly, with the Bazzite base".
- **Replaces:** the controller list's monthly check in [scheduled-checks-monthly](2026-11-10-scheduled-checks-monthly.md) (2026-11-10). Steam's spare stays monthly.
