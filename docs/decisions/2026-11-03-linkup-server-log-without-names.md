# A LinkUp server keeps a capped log in its data folder, free of names and addresses

- **Decided:** The server keeps all its state in one data folder outside the container, so updates leave it alone, and writes nothing per packet. Its log is a file in that folder, capped at about a megabyte, written only when something happens, and shown on the admin page with a download button. It never records codes, passwords, keys, internet addresses, usernames, game titles or traffic: a console shows as a short number, such as "console 3".
- **Date:** 2026-11-03
- **Why:** Docker's own log is lost whenever the container is recreated, which every update does, so the evidence for a fault after an update would be gone. Keeping the log free of names and addresses means an owner can paste it into a public GitHub issue. Nothing per packet means relaying costs an owner's connection, never their drive, and a small device such as a Raspberry Pi can run the server. The cost: the log shows how many times someone tried to pair, not who.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet".
- **Replaces:** none.
