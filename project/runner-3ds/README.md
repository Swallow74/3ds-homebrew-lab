# Neon Rush 3DS (.3dsx)

"Into the screen" runner for the Nintendo 3DS: 3 lanes, low barriers to
jump over, tall walls to dodge, increasing speed, stereoscopic 3D.

```bash
source ../../tools/env.sh
make        # output/runner-3ds.3dsx + .smdh
make clean
```

On a 3DS: copy `output/*` to `sd:/3ds/runner-3ds/`, start it from hbmenu
(the best score is saved in `sd:/3ds/runner-3ds/best.txt`).
On a Mac: open the `.3dsx` with Azahar.

- **Graphics**: polygons with procedural textures mapped onto the faces
  (256x256 atlas with mipmaps generated at runtime, `texgen.c`), directional
  light, ground occlusion and per-vertex fog, additive glows.
  Vertices go straight into the citro2d batch (`rx.c`, written for
  citro2d 1.7.0; with a different layout it falls back to solid-color triangles).
- **Synthwave scenery**: striped sun, wireframe mountains, buildings with
  windows, neon signs and antennas, street lamps with pools of light.
- **Coins**: lines in the free lanes and arcs above the barriers; a rising-pitch
  sound chain. Total coins saved to the SD card.
- **Power-ups**: MAGNET (8 s), SHIELD (absorbs one hit), 2X (10 s).
- **Score** (x difficulty x multiplier): 1/m, coin 10, clean jump
  25, dodge 2, power-up 50. Multiplier +1 every 20 coins
  (max x5), doubled by 2X.
- **UI**: list menu, countdown, pause (START or touch),
  results screen (A retry, B title), bottom screen with statistics.
- **Comfortable stereoscopic 3D** (everything behind the glass, HUD on the glass).
- **Synthesized DSP audio**: looping music + effects. SELECT = music.

Controls: Left/Right lane, A/B/Up jump, Down in the air = dive,
START pause, SELECT music.

## License

GNU GPL v2 or later (see `COPYING`). Copyright (C) 2026 Alessandro Del Rosso.
