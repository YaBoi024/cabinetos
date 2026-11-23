# 3DS games get no network

- **Decided:** A 3DS game never reaches a server named in the game. From 2026-10-20 a patch to Azahar did it: the emulated 3DS's Wi-Fi said no connection, and a socket, a name lookup, an HTTP request and SOC's lookup of the console's own address all failed, as on a 3DS with no access point. Since 2026-10-26 the block for every emulator does it ([emulators-get-no-network](2026-10-26-emulators-get-no-network.md)) and the patch is gone, so a 3DS's Wi-Fi reads as connected and its online options reach no server.
- **Date:** 2026-10-20
- **Why:** The console connects only to fixed places the code chooses, never to an address found in the data (CONTRIBUTING.md, "The rules"), and a game's servers are in the game. The emulated 3DS reaches the real network for a game: its sockets are the host's, its HTTP service makes real requests, and its Wi-Fi says connected as soon as a game asks. Azahar has no setting for it; the option was asked for upstream.
- **Numbers:** docs/PROJECT.md, open question 50, condition (2); #524, "Nintendo 3DS through Azahar's own libretro core (decrypted dumps)"; #537, "Azahar patch: games get no network (until Azahar has an option)"; azahar-emu/azahar#2695.
- **Replaces:** none.
