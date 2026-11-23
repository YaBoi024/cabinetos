# LinkUp works over any network and is built around none

- **Decided:** The server's address can be a public IP, a name or a VPN address, and the console does not care. Nothing is built or promised for Tailscale, WireGuard or any other VPN; if two people already share a network, LinkUp treats it as any other. Remote Play's Tailscale stays as it is, and WireGuard on the console itself is not supported. The setup guide recommends a dynamic DNS name for home hosts.
- **Date:** 2026-11-03
- **Why:** A VPN asks every friend for an account and setup, where everything else asks only the owner, and joining someone's tailnet gives access to their other machines unless they restrict it. The relay reaches the same people with no work for players. Remote Play's Tailscale firewall zone allows only Sunshine's ports, so it cannot be counted on to carry a game, and WireGuard on the console itself would take Developer access. A name for home hosts because most home addresses change now and then, and without one everyone on the server, and any linked owner, has to retype the address each time.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet".
- **Replaces:** none.
