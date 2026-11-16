# Steam's spare is checked monthly

- **Decided:** The stored spare of Steam's session is checked on the 1st of each month, the same day as the Bazzite base check, instead of every Monday. It still only opens a pull request and never merges.
- **Date:** 2026-11-10
- **Why:** The spare is used only when terra fails during an image build, so a week's freshness bought nothing a player sees while each pull request cost a review and a place in an update. On the same day as the base check, both ride the same monthly update.
- **Numbers:** #270, "Image build: fall back to a stored copy of Steam's session when Terra fails"; #688, "The controller list and Steam's spare are checked monthly, with the Bazzite base".
- **Replaces:** the weekly schedule of the spare's check (#270).
