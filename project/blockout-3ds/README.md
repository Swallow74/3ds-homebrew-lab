# BlockOut 3DS (unofficial port)

Unofficial port of *BlockOut II 2.5* (GPL, Jean-Luc Pons,
https://www.blockout.net/blockout2/) for the Nintendo 3DS (devkitARM / libctru /
citro2d), loosely inspired by the original rather than a 1:1 conversion.
"BlockOut" is a registered trademark of Kadon Enterprises, used here
only to identify the original game. Port license: GNU GPL v2 or
later (see `COPYING`). The rules, polycube tables, scoring formulas, game timings,
bot AI and setup/save files follow the original: only what the 3DS
hardware requires was adapted (input source, graphics backend, audio,
filesystem).

The original BlockOut II 2.5 source is not included in this repo: download it from
https://www.blockout.net/blockout2/.

## Build

```bash
cd project/blockout-3ds
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$PATH
make            # → output/blockout-3ds.{elf,3dsx,smdh}
```

The required libraries must be installed: **libctru** (separate services: `hid`, `apt`,
`ndsp`), **citro2d/citro3d**. The old `gfx3d` no longer exists: the renderer
uses `C3D/C2D` with left/right frame buffers and `gfxScreenSwapBuffers`.

## Layout

| File | Role |
|------|------|
| `main.cpp`     | hardware init, `aptMainLoop` loop, pad->key mapping, menu/game flow, per-state music |
| `screens.cpp`  | intro (3D voxel logo), menu, setup, hall of fame, pause, game over + name entry |
| `ui.cpp`       | DOS-style UI primitives: EGA palette, double-line frames, bars, menus |
| `render.cpp`   | 3D software renderer (pit, cubes, piece, GAME OVER orbit), DOS HUD, 8x8 bitmap font |
| `audio.c`      | NDSP: 4 synthesized effect channels (DOS square-wave style or BlockOut II) |
| `music.c`      | real-time soundtrack (sequencer + synth in a thread, channel 4): menu, game, game over |
| `Game.cpp`, `Pit.*`, `PolyCube.*`, `BotPlayer*`, `BotMatrix*` | game logic from BlockOut II 2.5 |
| `SetupManager.*` | setup + high scores on the SD card (`/3ds/blockout/`) |
| `autotest.h`   | `make AT=1`: the game plays itself (graphics check in the emulator) |

## Controls

### Menu
`up/down` select · `A`/`START` confirm · touch the items · 40 s of inactivity = demo

### Game
| Key | Action |
|-----|--------|
| D-Pad | moves the piece (`L`+D-Pad: diagonals, like 7/9/1/3 on the keypad) |
| `A`   | fast drop |
| `B` / `X` / `Y` | rotate on Z / X / Y (`R` held: opposite direction) |
| `ZL`  | AI hint (practice) |
| `ZR`  | 3D depth (OFF, 1..4; also follows the 3D slider) |
| `START` | pause (menu: resume / restart / quit); stops the demo |
| `SELECT` | ends the game |

### Setup
Pit (3..7 x 3..7 x 6..18), block set, starting level, animation speed,
ghost faces, piece fill, 3D depth, effects on/off, effects style
(MS-DOS / BlockOut II), music on/off. `B` saves.
The port's options are appended to `setup.dat` (old files stay compatible).

## "MS-DOS" look

- black background, 1-pixel green grid, layers colored by depth
  with black edges, falling piece as a white wireframe only (red when blocked)
- square pit viewport (like the original: the projection has aspect 1)
- level column on the left, LEVEL / SCORE / CUBES PLAYED /
  HIGH SCORE / PIT / BLOCK SET column on the right, 8x8 CP437 font, EGA palette
- GAME OVER orbit with faces sorted by depth (the original used
  a z-buffer)
- grid flash when layers are completed

## Fixes (2026-09 revision)

- **Demo/practice AI**: an earlier version had swapped the
  `BotMatrix` matrices and changed the coefficients: the bot never completed
  a layer. The original sources were restored (the `Game`
  and `GLMatrix` matrices are identical to the original).
- **Inverted stereoscopy**: the sign of the disparity was wrong
  (pit mouth "behind", bottom "in front"). Now zero disparity at the
  mouth and the pit behind the screen, HUD on the screen plane.
- **Missing polygons with a full pit**: `C2D_Init(4096)` limits objects
  per FRAME (two eyes + bottom screen): raised to 24000.
- **Unreadable text**: the system font scaled to 0.25 was blurry;
  now an 8x8 bitmap font with nearest filtering.
- ghost faces (`GHOST FACES`) with wrong neighbors on the X/Y axes.

## Adaptations (hardware only)

- Input: SDL keyboard → 3DS pad (`hid`); the key codes are renamed
  `BO_KEY_*` because libctru uses `KEY_A/B/L/...`
- Graphics: OpenGL/GDI → citro2d. The original **textures** (marble, glass,
  crystal, numbers, backgrounds) are not in the source package, so the
  renderer uses the **CLASSIC** style with the color palettes and materials
  (diffuse/ambient) identical to `Pit::Create`/`Game::Create`
- Audio: SDL_mixer -> synthesized NDSP (4 effect channels + streamed music)
- Filesystem: `%APPDATA%`/`HOME` -> `/3ds/blockout/` (`setup.dat`, `hscore.dat`)
- Top screen: pit + HUD (400x240, stereoscopic); bottom: statistics,
  controls, touchable menus

## Status

- [x] Builds clean: `output/blockout-3ds.3dsx`
- [x] Verified in Azahar (autotest): intro, menu, setup, hall of fame,
      game, pause, game over + name entry, practice with hint, demo that
      completes layers
- [ ] To try on a console: real 3D effect, audio/music, performance
      on the Old 3DS
- [ ] Style textures (MARBLE/ARCADE): missing from the source, CLASSIC remains
