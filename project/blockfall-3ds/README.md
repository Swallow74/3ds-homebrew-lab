# Blockfall 3DS (.3dsx)

Falling-blocks puzzle homebrew for the Nintendo 3DS with GPU graphics (citro2d):
colored blocks with highlights, ghost piece, next-piece preview,
score/level on the top screen, help on the bottom screen.

```bash
source ../../tools/env.sh
make        # output/blockfall-3ds.3dsx + .smdh
make clean
```

Installing on a 3DS: copy `output/blockfall-3ds.3dsx` and `output/blockfall-3ds.smdh`
to `sd:/3ds/blockfall-3ds/`, then start it from the Homebrew Launcher (hbmenu).

Controls: D-Pad move / Down soft drop, A/Up rotate, B hard drop, START exit.
A on GAME OVER = restart. Guideline-style scoring
(100/300/500/800 × level, level up every 10 lines), 7-piece bag.

## License

GNU GPL v2 or later (see `COPYING`). Copyright (C) 2026 Alessandro Del Rosso.
