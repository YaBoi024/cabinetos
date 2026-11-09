# LinkUp's server is a container anyone runs, in a repository of its own

- **Decided:** The server is a small Docker container, `cabinet-linkup`, that each owner runs for their own people. It lives in a repository of its own, MIT licensed and maintained by the project, with its image on ghcr.io under the project's owner. The project runs no service.
- **Date:** 2026-11-03
- **Why:** The project runs no service and asks nobody for money. Each owner runs their own copy, and ghcr.io is already where the console's image lives.
- **Numbers:** #654, "LinkUp: play a game with a friend on another CabinetOS console over the internet".
- **Replaces:** the first plan, a service run on Cloudflare with a relay paid for by sponsors, which was never built.
