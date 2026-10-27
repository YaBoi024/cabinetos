# L3 shows a DS or 3DS game's screens, the DS's lid moves to R2

- **Decided:** In a DS or 3DS game, player 1's L3 click moves the view on
  one: the top screen alone, then the bottom screen alone, then both again.
  Every game starts with both. On the DS the lid, which melonDS has on L3,
  moves to R2 (held is closed), where melonDS has its screen swap. Neither
  emulator's own swap is used any more, and nothing is said on screen when
  the view changes. Later, a system whose games use the click (the Wii U)
  would take a half-second hold of L3 instead, as the Vita's touch dots do;
  not decided, only the shape.
- **Date:** 2026-10-26
- **Why:** A player asked for one screen at full size. Both emulators
  already draw one screen alone and change their layout mid-game, so the
  console only sets it; no patch. On a TV each emulator's swap only traded
  the screens' places. The first build kept each emulator's own button (R2
  on the DS, L3 on the 3DS); MMagTech asked for one button on both. L3 is
  free on both because neither console has a stick to click, while R2 is the
  3DS's ZR, which New 3DS games use. The lid stays, on R2, because a few
  games need it to go on (Zelda: Phantom Hourglass's map); the 3DS's HOME,
  also on L3, does nothing unless the Home Menu was started first, which the
  console never does. A pause-menu row was the other option: easier to find,
  slower in play; the button won. Stray clicks were weighed and set aside by
  MMagTech: his pad's stick needs a firm press to click.
- **Numbers:** PR #596, "L3 shows a DS or 3DS game's top screen, bottom
  screen, or both". No issue; decided in a session and at the TV.
- **Replaces:** the 3DS's L3 swapping the screens while held
  (`citra_swap_screen_mode` "Hold", docs/PROJECT.md question 50).
