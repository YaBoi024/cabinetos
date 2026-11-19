# Display and Sound

![Display and Sound in Settings](../media/settings-display.webp)

## Resolution

The console runs at the TV's own resolution, 4K included, and turns on variable
refresh rate when the TV offers it. Every game fills the height of the screen at
its own shape, with a soft glow in the bars at the sides.

## Picture level

Settings, Display and Sound, **Picture**: **Performance**, **Balanced** or
**Quality**, for every game on the console. Performance draws each system at its
own resolution, Balanced at about 1080p, Quality at about 4K.

A single game can have its own level: **Picture** in its
[pause menu](pause-menu.md). **Console** there means the game follows Settings.
A game's own level takes effect from its next start and stays when the
console's level changes. With a PIN set, only the owner sees either setting.

The console starts on the level its graphics chip suits, judged from an AMD
chip's size and speed. NVIDIA and Intel chips, and machines without Vulkan,
start on Performance; **Picture** raises them.

| System | Performance | Balanced | Quality |
|---|---|---|---|
| Nintendo 64 | 1x, 320x240 | 2x, 640x480 | 4x, 1280x960 |
| Nintendo DS, each screen | 1x, 256x192 | 4x, 1024x768 | 6x, 1536x1152 |
| Nintendo 3DS, top screen | 1x, 400x240 | 3x, 1200x720 | 6x, 2400x1440 |
| GameCube | 1x, 640x528 | 3x, 1920x1584 | 6x, 3840x3168 |
| Wii | 1x, 640x528 | 3x, 1920x1584 | 5x, 3200x2640 |
| Switch, docked | 1x, 1920x1080 | 1x, 1920x1080 | 2x, 3840x2160 |
| PS2 | 1x, 640x448 | 3x, 1920x1344 | 4x, 2560x1792 |
| PS3 | 1x, 1280x720 | 1.5x, 1920x1080 | 1.5x, 1920x1080 |
| PSP | 1x, 480x272 | 4x, 1920x1088 | 8x, 3840x2176 |
| PS Vita | 1x, 960x544 | 2x, 1920x1088 | 4x, 3840x2176 |
| Dreamcast, Naomi and Atomiswave | 1x, 640x480 | 2.25x, 1440x1080 | 4.5x, 2880x2160 |
| Model 2 | 1x, 496x384 | 3x, 1488x1152 | 6x, 2976x2304 |
| Model 3, at most the TV's | 720p | 1080p | 4K |
| Xbox | 1x, 640x480 | 2x, 1280x960 | 4x, 2560x1920 |

Sizes are for each system's usual picture. A game drawn at another size scales
by the same amount.

These systems look the same at every level, so their pause menu has no
**Picture**: Xbox 360 at 720p, Wii U at the game's own resolution, 3DO at
640x480, Vectrex and the vector arcade games drawn sharp, and PlayStation,
Saturn, the 2D systems and the other arcade boards at their own resolution.

On GameCube, Wii, Switch and PS Vita, Balanced and Quality also build shaders in
the background. At Performance they are built when first needed, which can
stutter for a moment.

On the Nintendo DS, Balanced and Quality use melonDS's OpenGL renderer, which
draws the 3D larger; the 2D stays at the DS's own pixels. A DS game that looks
wrong with it can be set to Performance in its pause menu. A machine without
desktop OpenGL keeps every DS game at Performance, with no **Picture** row.

On the Nintendo 3DS the two screens are side by side, and the levels are sized
for that: Balanced just above a 1080p TV, Quality just above a 4K TV.

## PS2 widescreen

A PS2 game plays in 16:9 when PCSX2's widescreen patch exists for it. Some
patches widen only the 3D view and leave the HUD and menus stretched. **Widescreen**
in the [pause menu](pause-menu.md) turns it off for that game, from the next
start; it shows only on games that have a patch.

## Screen looks

![The Look row in the pause menu](../media/pause-menu-snes.webp)

<video src="../media/looks.mp4" poster="../media/looks-poster.webp" controls muted loop playsinline preload="none"></video>

**Look** in the pause menu sets how a system's picture is drawn. It changes
behind the menu as you choose, and the choice is kept for that system.

| Systems | Looks | Starts on |
|---|---|---|
| NES, SNES, Genesis, Master System, Sega CD, 32X, TurboGrafx-16 and CD, Atari 2600 and 7800, PlayStation, Saturn, and every arcade folder, which share one choice | Plain, Sharp, CRT easymode, CRT easymode halation, CRT lottes, CRT geom, CRT zfast, CRT aperture, CRT guest | CRT easymode |
| Nintendo 64, Dreamcast, 3DO | Plain, Sharp, CRT easymode, CRT lottes, CRT aperture | CRT easymode |
| Game Boy Advance, Game Gear, Neo Geo Pocket Color | Plain, Sharp, LCD 3x, LCD grid, LCD zfast | LCD 3x |
| Game Boy | as above, plus Dot matrix and Dot matrix Pocket | LCD 3x |
| Game Boy Color | as above, plus Dot matrix | LCD 3x |

The looks are RetroArch's own shaders. PS2 and newer, DS, PSP, Virtual Boy and
Vectrex have no Look row.

Three systems have a row of their own in the pause menu:

- **Virtual Boy, Screen:** Red, White, Blue, Cyan, Electric cyan, Green,
  Magenta, Yellow, and five 3D glasses pairs (red/blue, red/cyan, red/electric
  cyan, green/magenta, yellow/blue).
- **Game Boy, Colors:** Off, Auto, Game Boy Color, Super Game Boy.
- **Vectrex, Overlays:** On, Off. Every Vectrex cartridge came with a
  coloured sheet that sat in front of the screen, and the console draws each
  game's over its picture, a little smaller so the edges of the game stay
  inside the sheet's window; Off shows the bare screen at full size. One
  choice for every Vectrex game, On to start with. The prototypes, the 3D
  Imager games and homebrew had no sheet, and show no row.

## Colours

![Six of the sixteen colours](../media/colours-grid.webp)

<video src="../media/colours.mp4" poster="../media/colours-poster.webp" controls muted loop playsinline preload="none"></video>

Settings, Display and Sound, **Color**: Purple, Blue, Teal, Green, Amber, Red,
Wine, Graphite, and the bright ones, Pink, Sky, Lime, Orange, and the
gradients, Sunset, Ocean, Aurora and Fire. Press Left and Right to change it;
the console fades to it as you go.

Each person has their own colour, and the console changes when they sign in.

## Appearance and dark hours

**Appearance**:

- **Standard**: the normal look.
- **Dark**: darker menus for the evening. The games are not changed.
- **Scheduled**: Dark during the **Dark hours** you choose (8 PM to 7 AM to
  start with), by the time zone picked at install (at first run, on a console
  that runs from a USB drive).

## Screen off

**Turn off screen after**: 10, 15 or 30 minutes, 15 to start with. In the
menus, and in a game that is paused, the screen dims after 5 minutes and turns
off at the time chosen; the first press after that only wakes it, and after the
screen was off, buttons do nothing for 3 seconds while the TV brings the
picture back. A game being played dims after 20 minutes and never turns the
screen off. The picture also moves by a pixel or two every few minutes, to be
kind to OLED TVs.

## Sound

The sound plays on the TV the picture is on. Headphones in a jack that senses
the plug take it over while they are in, and give it back when they come out.
Nothing else takes it from the TV: not a speaker built into the PC's case, not
a jack that cannot sense a plug, and not a USB sound card or headset. There is
no setting for it. When the screen turns off or the TV sleeps, the sound comes
back with the picture. If the screen tells the PC it has no sound of its own,
the PC's own speaker or jack plays instead.

The volume is the TV's. The volume keys on a keyboard change it too, and it
goes back to full each time the console starts.

- **Interface sounds**: Off, Quiet, Medium or Loud, for the menu sounds.
- **Achievement sound**: the same, for the chime when you unlock an
  achievement. It shows once you are signed in to
  [RetroAchievements](achievements.md).
