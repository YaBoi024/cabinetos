# A new LinkUp server's first page opens only by an address or a home name

- **Decided:** Until a LinkUp server has a password, its admin page opens only by an address, a name with no dots in it, or a name ending in `.local`, `.lan`, `.home`, `.home.arpa`, `.internal`, `.localdomain` or `.localhost`. Any other name gets one line: open it by its address. Once a password is set, any name works, because signing in needs it.
- **Date:** 2026-11-09
- **Why:** Whoever opens a new server's page first sets its password. A web page open in a browser at home could point a public name of its own at the server (DNS rebinding) and, to that browser, be the server's own page, sending from a home address; the audit proved it with a test that set the password under a stranger's name. Public DNS never gives out the names allowed here, so a stray page cannot reach the first-run form. The cost: an owner who gave the server a public name before setting the password types its address once instead.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet"; MMagTech/cabinet-linkup#6, "Security audit before v0.1.1: six fixes, and what was checked".
- **Replaces:** none.
