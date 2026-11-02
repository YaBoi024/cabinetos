# Seeing first run as a console with no cable sees it

The network step is first run's one hard gate, and its two halves behave
differently:

| | With a cable | Without |
|---|---|---|
| The panel | the Wi-Fi list, as a fallback (with no radio, the link and its address) | the Wi-Fi list, the only way forward |
| Continue | enabled: it is the skip | **greyed out until connected** |
| Skip | no separate button | not offered, and never will be |

Wi-Fi is part of the network step (`firstrun.h`, `Step::Network`).

**Neither machine here can show the second column.** The reference console
is always online and the test VM has no radio at all, and taking either
offline is how you lose the machine you are measuring. So this stands in for
`nmcli`, answering its terse queries the way a disconnected machine with
working Wi-Fi hardware would.

From `frontend/`:

```
PATH="$PWD/tools/fake-nmcli:$PATH" SDL_VIDEODRIVER=offscreen \
  ./build/cabinetos-frontend --setup-step network \
  --screenshot /tmp/offline.bmp --render-size 1920x1080 --frames 200
```

## What is real and what is not

**Only the external command is substituted.** `net.cpp`'s parsing, the escape
handling, the deduplication, the sort, `firstrun::Machine`'s rules and every
pixel of the screen are the shipping ones. What this cannot tell you is whether
`nmcli` itself behaves as assumed — for that there is no substitute for a
machine with no cable in it.

**It refuses nothing and joins nothing.** Every write is a no-op, so pressing a
network does not hang: it returns success and the screen moves on as though the
join worked. That makes the *post-join* state easy to photograph and means this
**cannot** be used to test a failed join.

**Except a network with a password.** Its profile, password and all, goes to
NetworkManager itself over D-Bus, not through `nmcli` (#629, `net.cpp`), and a
script on `PATH` cannot stand in for that. From an SSH shell polkit refuses it,
so the join fails with the "not permitted to save a network" message; in a
console's own session it really saves the network, which Settings, Wi-Fi,
Forget removes. Press the open network, `BTWiFi-with-FON`, for the post-join
state.

## Why a script and not a flag in the binary

A `--pretend-offline` would have to be a second source of truth for the facts,
sitting inside the program next to the real one, and the first time the two
disagreed it would be the fake that got believed. This substitutes the thing
`net.cpp` actually talks to and leaves the program alone — the same reasoning as
`--core-options-off`, which is a control rather than a simulation.

It also only exists on `PATH` when somebody puts it there.
